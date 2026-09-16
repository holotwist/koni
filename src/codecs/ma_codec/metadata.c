#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include "codec.h"
#include "miniaudio.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <unistd.h>

static uint32_t read_u32_be(const uint8_t* p) {
    return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

static uint32_t read_u32_le(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

static int b64_char_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static uint8_t* base64_decode(const char* src, size_t len, size_t* out_len) {
    if (!src || len == 0) return NULL;
    size_t alloc_sz = (len * 3) / 4 + 4;
    uint8_t* out = malloc(alloc_sz);
    if (!out) return NULL;

    size_t o = 0;
    int buf = 0;
    int bits = 0;
    for (size_t i = 0; i < len; i++) {
        char c = src[i];
        if (c == '=' || c == '\r' || c == '\n' || c == ' ' || c == '\t') {
            if (c == '=') break;
            continue;
        }
        int val = b64_char_value(c);
        if (val < 0) continue;
        buf = (buf << 6) | val;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = (uint8_t)((buf >> bits) & 0xFF);
        }
    }
    *out_len = o;
    return out;
}

static char* decode_id3_string(const uint8_t* data, size_t size, uint8_t encoding) {
    if (size == 0 || !data) return strdup("");
    if (encoding == 3) { // UTF-8
        size_t actual_len = 0;
        while (actual_len < size && data[actual_len] != '\0') actual_len++;
        char* str = calloc(1, actual_len + 1);
        if (str) memcpy(str, data, actual_len);
        return str ? str : strdup("");
    } else if (encoding == 0) { // ISO-8859-1 (Latin-1) -> UTF-8
        char* str = calloc(1, size * 2 + 1);
        if (!str) return strdup("");
        size_t out_idx = 0;
        for (size_t i = 0; i < size; i++) {
            uint8_t b = data[i];
            if (b == 0) break;
            if (b < 0x80) {
                str[out_idx++] = (char)b;
            } else {
                str[out_idx++] = (char)(0xC0 | (b >> 6));
                str[out_idx++] = (char)(0x80 | (b & 0x3F));
            }
        }
        str[out_idx] = '\0';
        return str;
    } else if (encoding == 1 || encoding == 2) {
        char* str = calloc(1, size * 2 + 1);
        size_t out_idx = 0;
        size_t i = 0;
        bool le = false;
        if (encoding == 1 && size >= 2) {
            if (data[0] == 0xFF && data[1] == 0xFE) { le = true; i = 2; }
            else if (data[0] == 0xFE && data[1] == 0xFF) { le = false; i = 2; }
        }
        for (; i + 1 < size; i += 2) {
            uint16_t wc = le ? (data[i] | (data[i+1] << 8)) : ((data[i] << 8) | data[i+1]);
            if (wc == 0) break;
            if (wc < 0x80) {
                str[out_idx++] = (char)wc;
            } else if (wc < 0x800) {
                str[out_idx++] = 0xC0 | (wc >> 6);
                str[out_idx++] = 0x80 | (wc & 0x3F);
            } else {
                str[out_idx++] = 0xE0 | (wc >> 12);
                str[out_idx++] = 0x80 | ((wc >> 6) & 0x3F);
                str[out_idx++] = 0x80 | (wc & 0x3F);
            }
        }
        return str;
    }
    return strdup("");
}

static char* save_temp_cover(const uint8_t* data, size_t size) {
    char tmpl[] = "/tmp/koni_cover_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) return NULL;
    
    size_t written = 0;
    while (written < size) {
        ssize_t res = write(fd, data + written, size - written);
        if (res < 0) break;
        written += res;
    }
    close(fd);
    
    const char* ext = ".jpg";
    if (size >= 4 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        ext = ".png";
    }
    
    char* url = malloc(256);
    char new_path[256];
    snprintf(new_path, sizeof(new_path), "%s%s", tmpl, ext);
    rename(tmpl, new_path);
    
    snprintf(url, 256, "file://%s", new_path);
    return url;
}

static void parse_id3v2(FILE* fp, uint8_t magic[4], KoniMetadata* meta) {
    uint8_t header[6];
    if (fread(header, 1, 6, fp) != 6) return;
    
    uint8_t version = magic[3];
    if (version < 3 || version > 4) return;
    
    uint32_t total_size = (header[2] << 21) | (header[3] << 14) | (header[4] << 7) | header[5];
    
    uint8_t* tag_data = malloc(total_size);
    if (!tag_data) return;
    
    if (fread(tag_data, 1, total_size, fp) != total_size) {
        free(tag_data);
        return;
    }
    
    if (header[1] & 0x80) {
        size_t write_pos = 0;
        for (size_t read_pos = 0; read_pos < total_size; read_pos++) {
            tag_data[write_pos++] = tag_data[read_pos];
            if (read_pos + 1 < total_size && tag_data[read_pos] == 0xFF && tag_data[read_pos + 1] == 0x00) {
                read_pos++; 
            }
        }
        total_size = write_pos;
    }
    
    size_t pos = 0;
    if (header[1] & 0x40) {
        if (version == 3 && total_size >= 4) {
            uint32_t ext_size = read_u32_be(tag_data + pos);
            pos += 4 + ext_size;
        } else if (version == 4 && total_size >= 4) {
            uint32_t ext_size = (tag_data[pos] << 21) | (tag_data[pos+1] << 14) | (tag_data[pos+2] << 7) | tag_data[pos+3];
            pos += ext_size;
        }
    }
    
    while (pos + 10 <= total_size) {
        uint32_t id = read_u32_be(tag_data + pos);
        if (id == 0) break;
        
        uint32_t frame_size;
        if (version == 4) {
            frame_size = (tag_data[pos+4] << 21) | (tag_data[pos+5] << 14) | (tag_data[pos+6] << 7) | tag_data[pos+7];
        } else {
            frame_size = read_u32_be(tag_data + pos + 4);
        }
        
        pos += 10;
        if (pos + frame_size > total_size) break;
        
        if (frame_size > 0) {
            uint8_t* frame_data = tag_data + pos;
            if (id == 0x54495432) { // TIT2
                if (meta->title) free(meta->title);
                meta->title = decode_id3_string(frame_data + 1, frame_size - 1, frame_data[0]);
            } else if (id == 0x54504531) { // TPE1
                if (meta->artist) free(meta->artist);
                meta->artist = decode_id3_string(frame_data + 1, frame_size - 1, frame_data[0]);
            } else if (id == 0x54414C42) { // TALB
                if (meta->album) free(meta->album);
                meta->album = decode_id3_string(frame_data + 1, frame_size - 1, frame_data[0]);
            } else if (id == 0x55534C54 && !meta->lyrics) { // USLT (Unsynced lyrics)
                uint8_t enc = frame_data[0];
                size_t p = 4; // skip encoding (1) + language (3)
                if (p < frame_size) {
                    if (enc == 1 || enc == 2) {
                        while (p + 1 < frame_size && (frame_data[p] != 0 || frame_data[p + 1] != 0)) p += 2;
                        p += 2;
                    } else {
                        while (p < frame_size && frame_data[p] != 0) p++;
                        p += 1;
                    }
                    if (p < frame_size) {
                        meta->lyrics = decode_id3_string(frame_data + p, frame_size - p, enc);
                    }
                }
            } else if (id == 0x54585858) { // TXXX
                uint8_t enc = frame_data[0];
                size_t d_len = 0;
                if (enc == 1 || enc == 2) {
                    while (1 + d_len + 1 < frame_size && (frame_data[1+d_len] != 0 || frame_data[1+d_len+1] != 0)) d_len += 2;
                } else {
                    while (1 + d_len < frame_size && frame_data[1+d_len] != 0) d_len++;
                }
                char* desc = decode_id3_string(frame_data + 1, d_len, enc);
                size_t val_pos = 1 + d_len + (enc == 1 || enc == 2 ? 2 : 1);
                char* val = (val_pos < frame_size) ? decode_id3_string(frame_data + val_pos, frame_size - val_pos, enc) : NULL;
                
                if (desc && val && strcasecmp(desc, "replaygain_track_gain") == 0) {
                    meta->has_track_gain = true;
                    meta->track_gain = (float)atof(val);
                }
                if (desc) free(desc);
                if (val) free(val);
            } else if (id == 0x41504943 && !meta->art_url) { // APIC
                // Directly locate JPEG or PNG magic header bytes to guarantee valid image alignment
                size_t img_offset = 0;
                for (size_t k = 1; k + 3 < frame_size; k++) {
                    if (frame_data[k] == 0xFF && frame_data[k+1] == 0xD8 && frame_data[k+2] == 0xFF) {
                        img_offset = k;
                        break;
                    }
                    if (frame_data[k] == 0x89 && frame_data[k+1] == 'P' && frame_data[k+2] == 'N' && frame_data[k+3] == 'G') {
                        img_offset = k;
                        break;
                    }
                }
                if (img_offset > 0 && img_offset < frame_size) {
                    meta->art_url = save_temp_cover(frame_data + img_offset, frame_size - img_offset);
                }
            }
        }
        pos += frame_size;
    }
    
    free(tag_data);
}

static void parse_flac_picture(const uint8_t* blk, size_t size, KoniMetadata* meta) {
    if (size >= 32 && !meta->art_url) {
        size_t p = 4; // Picture type (4 bytes)
        if (p + 4 > size) return;
        uint32_t mlen = read_u32_be(blk + p);
        p += 4 + mlen;
        if (p + 4 > size) return;
        uint32_t dlen = read_u32_be(blk + p);
        p += 4 + dlen;
        if (p + 20 > size) return;
        p += 16; // width (4), height (4), depth (4), colors (4)
        uint32_t plen = read_u32_be(blk + p);
        p += 4;
        if (p + plen <= size && plen > 0) {
            meta->art_url = save_temp_cover(blk + p, plen);
        }
    }
}

static void parse_vorbis_comments(const uint8_t* blk, size_t size, KoniMetadata* meta) {
    if (!blk || size < 4) return;
    uint32_t vlen = read_u32_le(blk);
    size_t p = 4 + vlen;
    if (p + 4 > size) return;

    uint32_t list_len = read_u32_le(blk + p);
    p += 4;

    for (uint32_t i = 0; i < list_len && p + 4 <= size; i++) {
        uint32_t len = read_u32_le(blk + p);
        p += 4;
        if (p + len > size) break;

        char* comment = malloc(len + 1);
        if (!comment) {
            p += len;
            continue;
        }
        memcpy(comment, blk + p, len);
        comment[len] = '\0';

        char* eq = strchr(comment, '=');
        if (eq) {
            *eq = '\0';
            char* key = comment;
            char* val = eq + 1;
            if (strcasecmp(key, "TITLE") == 0 && !meta->title) {
                meta->title = strdup(val);
            } else if (strcasecmp(key, "ARTIST") == 0) {
                if (meta->artist) free(meta->artist);
                meta->artist = strdup(val);
            } else if ((strcasecmp(key, "ALBUMARTIST") == 0 || strcasecmp(key, "ALBUM_ARTIST") == 0) && !meta->artist) {
                meta->artist = strdup(val);
            } else if (strcasecmp(key, "ALBUM") == 0 && !meta->album) {
                meta->album = strdup(val);
            } else if ((strcasecmp(key, "LYRICS") == 0 || strcasecmp(key, "UNSYNCEDLYRICS") == 0) && !meta->lyrics) {
                meta->lyrics = strdup(val);
            } else if (strcasecmp(key, "REPLAYGAIN_TRACK_GAIN") == 0) {
                meta->has_track_gain = true;
                meta->track_gain = (float)atof(val);
            } else if (strcasecmp(key, "METADATA_BLOCK_PICTURE") == 0 && !meta->art_url) {
                size_t dec_sz = 0;
                uint8_t* pic = base64_decode(val, strlen(val), &dec_sz);
                if (pic) {
                    parse_flac_picture(pic, dec_sz, meta);
                    free(pic);
                }
            } else if (strcasecmp(key, "COVERART") == 0 && !meta->art_url) {
                size_t dec_sz = 0;
                uint8_t* pic = base64_decode(val, strlen(val), &dec_sz);
                if (pic) {
                    if (dec_sz > 4) {
                        meta->art_url = save_temp_cover(pic, dec_sz);
                    }
                    free(pic);
                }
            }
        }
        free(comment);
        p += len;
    }
}

static void parse_flac(FILE* fp, KoniMetadata* meta) {
    uint8_t header[4];
    while (fread(header, 1, 4, fp) == 4) {
        bool is_last = (header[0] & 0x80) != 0;
        uint8_t type = header[0] & 0x7F;
        uint32_t size = (header[1] << 16) | (header[2] << 8) | header[3];
        
        if (size > 1024 * 1024 * 30) break;
        
        uint8_t* blk = malloc(size);
        if (!blk) break;
        
        if (fread(blk, 1, size, fp) != size) {
            free(blk);
            break;
        }
        
        if (type == 4) { // VORBIS_COMMENT
            parse_vorbis_comments(blk, size, meta);
        } else if (type == 6) { // PICTURE
            parse_flac_picture(blk, size, meta);
        }
        
        free(blk);
        if (is_last) break;
    }
}

static char* clean_riff_string(const uint8_t* data, size_t size) {
    if (!data || size == 0) return NULL;
    while (size > 0 && (data[size - 1] == '\0' || data[size - 1] == ' ' || data[size - 1] == '\r' || data[size - 1] == '\n')) {
        size--;
    }
    if (size == 0) return NULL;

    bool is_utf8 = true;
    for (size_t i = 0; i < size; i++) {
        uint8_t c = data[i];
        if (c < 0x80) continue;
        if ((c >= 0xC2 && c <= 0xDF) && (i + 1 < size) && (data[i+1] >= 0x80 && data[i+1] <= 0xBF)) {
            i += 1;
        } else if ((c >= 0xE0 && c <= 0xEF) && (i + 2 < size) &&
                   (data[i+1] >= 0x80 && data[i+1] <= 0xBF) && (data[i+2] >= 0x80 && data[i+2] <= 0xBF)) {
            i += 2;
        } else if ((c >= 0xF0 && c <= 0xF4) && (i + 3 < size) &&
                   (data[i+1] >= 0x80 && data[i+1] <= 0xBF) &&
                   (data[i+2] >= 0x80 && data[i+2] <= 0xBF) && (data[i+3] >= 0x80 && data[i+3] <= 0xBF)) {
            i += 3;
        } else {
            is_utf8 = false;
            break;
        }
    }

    if (is_utf8) {
        char *s = malloc(size + 1);
        if (s) {
            memcpy(s, data, size);
            s[size] = '\0';
        }
        return s;
    }
    return decode_id3_string(data, size, 0);
}

static void parse_riff_info_list(FILE* fp, off_t list_end, KoniMetadata* meta) {
    while ((off_t)ftello(fp) + 8 <= list_end) {
        uint8_t sub_hdr[8];
        if (fread(sub_hdr, 1, 8, fp) != 8) break;
        uint32_t sub_size = read_u32_le(sub_hdr + 4);
        off_t sub_data_pos = ftello(fp);
        if (sub_size > 0 && sub_size < (1024 * 1024)) {
            uint8_t *data = malloc(sub_size + 1);
            if (data) {
                if (fread(data, 1, sub_size, fp) == sub_size) {
                    data[sub_size] = '\0';
                    char *text = clean_riff_string(data, sub_size);
                    if (text && text[0]) {
                        if (memcmp(sub_hdr, "INAM", 4) == 0 && !meta->title) {
                            meta->title = text;
                            text = NULL;
                        } else if (memcmp(sub_hdr, "IART", 4) == 0 && !meta->artist) {
                            meta->artist = text;
                            text = NULL;
                        } else if (memcmp(sub_hdr, "IPRD", 4) == 0 && !meta->album) {
                            meta->album = text;
                            text = NULL;
                        } else if ((memcmp(sub_hdr, "ILRC", 4) == 0 || memcmp(sub_hdr, "ICMT", 4) == 0) && !meta->lyrics) {
                            meta->lyrics = text;
                            text = NULL;
                        }
                    }
                    if (text) free(text);
                }
                free(data);
            }
        }
        off_t next_sub = sub_data_pos + (off_t)((sub_size + 1ULL) & ~1ULL);
        if (next_sub < sub_data_pos || fseeko(fp, next_sub, SEEK_SET) != 0) break;
    }
}

static void parse_riff_wave(FILE* fp, KoniMetadata* meta) {
    off_t riff_start = ftello(fp) - 4;
    if (fseeko(fp, riff_start + 8, SEEK_SET) != 0) return;
    uint8_t format[4];
    if (fread(format, 1, 4, fp) != 4) return;
    if (memcmp(format, "WAVE", 4) != 0) return;

    while (1) {
        uint8_t chunk_hdr[8];
        if (fread(chunk_hdr, 1, 8, fp) != 8) break;
        uint32_t chunk_size = read_u32_le(chunk_hdr + 4);
        off_t chunk_data_pos = ftello(fp);

        if (memcmp(chunk_hdr, "LIST", 4) == 0 && chunk_size >= 4) {
            uint8_t list_type[4];
            if (fread(list_type, 1, 4, fp) == 4 && memcmp(list_type, "INFO", 4) == 0) {
                parse_riff_info_list(fp, chunk_data_pos + chunk_size, meta);
            }
        } else if ((memcmp(chunk_hdr, "id3 ", 4) == 0 || memcmp(chunk_hdr, "ID3 ", 4) == 0) && chunk_size >= 10) {
            uint8_t id3_magic[4];
            if (fread(id3_magic, 1, 4, fp) == 4 && memcmp(id3_magic, "ID3", 3) == 0) {
                parse_id3v2(fp, id3_magic, meta);
            }
        }

        off_t next_chunk = chunk_data_pos + (off_t)((chunk_size + 1ULL) & ~1ULL);
        if (next_chunk < chunk_data_pos || fseeko(fp, next_chunk, SEEK_SET) != 0) break;
    }
}

bool ma_read_metadata(const char* filepath, KoniMetadata* meta, uint32_t* duration_sec) {
    memset(meta, 0, sizeof(KoniMetadata));
    if (duration_sec) *duration_sec = 0;

    FILE* fp = fopen(filepath, "rb");
    if (fp) {
        uint8_t magic[4];
        if (fread(magic, 1, 4, fp) == 4) {
            if (memcmp(magic, "ID3", 3) == 0) {
                parse_id3v2(fp, magic, meta);
                uint8_t next_magic[4];
                if (fread(next_magic, 1, 4, fp) == 4 &&
                    (memcmp(next_magic, "RIFF", 4) == 0 || memcmp(next_magic, "RF64", 4) == 0)) {
                    parse_riff_wave(fp, meta);
                }
            } else if (memcmp(magic, "fLaC", 4) == 0) {
                parse_flac(fp, meta);
            } else if (memcmp(magic, "RIFF", 4) == 0 || memcmp(magic, "RF64", 4) == 0) {
                parse_riff_wave(fp, meta);
            }
        }
        fclose(fp);
    }

    if (duration_sec) {
        ma_decoder_config config = ma_decoder_config_init_default();
        ma_decoder decoder;
        if (ma_decoder_init_file(filepath, &config, &decoder) == MA_SUCCESS) {
            ma_uint64 length = 0;
            if (ma_decoder_get_length_in_pcm_frames(&decoder, &length) == MA_SUCCESS && decoder.outputSampleRate > 0) {
                *duration_sec = length / decoder.outputSampleRate;
            }
            ma_decoder_uninit(&decoder);
        }
    }

    return (meta->title || meta->artist || meta->album || meta->lyrics || meta->art_url || meta->has_track_gain);
}