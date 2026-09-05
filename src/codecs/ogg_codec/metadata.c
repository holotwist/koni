#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600

#include "codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

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

static void parse_flac_picture(const uint8_t* blk, size_t size, KoniMetadata* meta) {
    if (size >= 32 && !meta->art_url) {
        size_t p = 4;
        if (p + 4 > size) return;
        uint32_t mlen = read_u32_be(blk + p);
        p += 4 + mlen;
        if (p + 4 > size) return;
        uint32_t dlen = read_u32_be(blk + p);
        p += 4 + dlen;
        if (p + 20 > size) return;
        p += 16;
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

static void parse_ogg_file(FILE* fp, KoniMetadata* meta) {
    fseek(fp, 0, SEEK_SET);

    uint32_t target_serial = 0;
    bool has_target_serial = false;
    int pages_scanned = 0;
    uint8_t* pkt_buf = NULL;
    size_t pkt_len = 0;
    size_t pkt_cap = 0;
    bool comments_parsed = false;

    while (pages_scanned < 200 && !comments_parsed) {
        uint8_t page_hdr[27];
        if (fread(page_hdr, 1, 27, fp) != 27) break;
        if (memcmp(page_hdr, "OggS", 4) != 0) break;

        pages_scanned++;
        uint8_t header_type = page_hdr[5];
        uint32_t serial_no = read_u32_le(page_hdr + 14);
        uint8_t num_segments = page_hdr[26];

        uint8_t seg_table[256];
        if (fread(seg_table, 1, num_segments, fp) != num_segments) break;

        size_t body_size = 0;
        for (int i = 0; i < num_segments; i++) {
            body_size += seg_table[i];
        }

        if (body_size > 0 && (!has_target_serial || serial_no == target_serial)) {
            uint8_t* page_body = malloc(body_size);
            if (!page_body) break;
            if (fread(page_body, 1, body_size, fp) != body_size) {
                free(page_body);
                break;
            }

            size_t seg_offset = 0;
            for (int i = 0; i < num_segments; i++) {
                uint8_t seg_len = seg_table[i];
                if (seg_len > 0) {
                    if (pkt_len + seg_len > pkt_cap) {
                        size_t new_cap = (pkt_cap == 0) ? 4096 : pkt_cap * 2;
                        while (new_cap < pkt_len + seg_len) new_cap *= 2;
                        uint8_t* new_buf = realloc(pkt_buf, new_cap);
                        if (!new_buf) break;
                        pkt_buf = new_buf;
                        pkt_cap = new_cap;
                    }
                    memcpy(pkt_buf + pkt_len, page_body + seg_offset, seg_len);
                    pkt_len += seg_len;
                    seg_offset += seg_len;
                }

                if (seg_len < 255) {
                    if (!has_target_serial) {
                        if ((pkt_len >= 7 && memcmp(pkt_buf, "\x01vorbis", 7) == 0) ||
                            (pkt_len >= 8 && memcmp(pkt_buf, "OpusHead", 8) == 0) ||
                            (pkt_len >= 5 && memcmp(pkt_buf, "\x7f" "FLAC", 5) == 0) ||
                            (pkt_len >= 8 && memcmp(pkt_buf, "Speex   ", 8) == 0) ||
                            (header_type & 0x02)) {
                            target_serial = serial_no;
                            has_target_serial = true;
                        }
                    }

                    if (has_target_serial && serial_no == target_serial) {
                        if (pkt_len >= 7 && memcmp(pkt_buf, "\x03vorbis", 7) == 0) {
                            parse_vorbis_comments(pkt_buf + 7, pkt_len - 7, meta);
                            comments_parsed = true;
                        } else if (pkt_len >= 8 && memcmp(pkt_buf, "OpusTags", 8) == 0) {
                            parse_vorbis_comments(pkt_buf + 8, pkt_len - 8, meta);
                            comments_parsed = true;
                        }
                    }

                    pkt_len = 0;
                    if (comments_parsed) break;
                }
            }
            free(page_body);
        } else if (body_size > 0) {
            if (fseek(fp, (long)body_size, SEEK_CUR) != 0) break;
        }
    }

    if (pkt_buf) free(pkt_buf);
}

bool ogg_read_metadata(const char* filepath, KoniMetadata* meta, uint32_t* duration_sec) {
    memset(meta, 0, sizeof(KoniMetadata));
    if (duration_sec) *duration_sec = 0;

    FILE* fp = fopen(filepath, "rb");
    if (fp) {
        uint8_t magic[4];
        if (fread(magic, 1, 4, fp) == 4 && memcmp(magic, "OggS", 4) == 0) {
            parse_ogg_file(fp, meta);
        }
        fclose(fp);
    }

    int error = 0;
    stb_vorbis* vb = stb_vorbis_open_filename(filepath, &error, NULL);
    if (vb) {
        stb_vorbis_info info = stb_vorbis_get_info(vb);
        uint32_t total = stb_vorbis_stream_length_in_samples(vb);
        if (duration_sec && info.sample_rate > 0) {
            *duration_sec = total / info.sample_rate;
        }
        stb_vorbis_close(vb);
    }

    return (meta->title || meta->artist || meta->album || meta->lyrics || meta->art_url || meta->has_track_gain);
}