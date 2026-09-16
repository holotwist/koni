#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include "codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define FOURCC(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

static inline uint32_t read_u32(FILE *f) {
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return 0;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

static char* save_temp_cover(const uint8_t *data, size_t size, bool is_png) {
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

    const char *ext = is_png ? ".png" : ".jpg";
    char new_path[256];
    snprintf(new_path, sizeof(new_path), "%s%s", tmpl, ext);
    rename(tmpl, new_path);

    char *url = malloc(256);
    if (url) snprintf(url, 256, "file://%s", new_path);
    return url;
}

static char* extract_data_atom_text(FILE *f, uint64_t item_end) {
    while ((uint64_t)ftell(f) + 8 <= item_end) {
        uint64_t box_start = (uint64_t)ftell(f);
        uint32_t sz = read_u32(f);
        uint32_t type = read_u32(f);
        if (sz < 8) break;
        uint64_t box_end = box_start + sz;

        if (type == FOURCC('d', 'a', 't', 'a') && sz > 16) {
            fseek(f, 8, SEEK_CUR); // type flags (4) + locale (4)
            size_t txt_len = sz - 16;
            char *buf = malloc(txt_len + 1);
            if (buf) {
                if (fread(buf, 1, txt_len, f) == txt_len) {
                    buf[txt_len] = '\0';
                    return buf;
                }
                free(buf);
            }
        }
        fseek(f, (long)box_end, SEEK_SET);
    }
    return NULL;
}

static void parse_freeform_item(FILE *f, uint64_t item_end, KoniMetadata *meta) {
    char *name = NULL;
    char *val = NULL;

    while ((uint64_t)ftell(f) + 8 <= item_end) {
        uint64_t start = (uint64_t)ftell(f);
        uint32_t sz = read_u32(f);
        uint32_t type = read_u32(f);
        if (sz < 8) break;
        uint64_t end = start + sz;

        if (type == FOURCC('n', 'a', 'm', 'e') && sz > 12) {
            fseek(f, 4, SEEK_CUR); // version + flags
            size_t len = sz - 12;
            name = malloc(len + 1);
            if (name && fread(name, 1, len, f) == len) name[len] = '\0';
        } else if (type == FOURCC('d', 'a', 't', 'a') && sz > 16) {
            fseek(f, 8, SEEK_CUR); // type flags + locale
            size_t len = sz - 16;
            val = malloc(len + 1);
            if (val && fread(val, 1, len, f) == len) val[len] = '\0';
        }
        fseek(f, (long)end, SEEK_SET);
    }

    if (name && val) {
        if (strcasecmp(name, "replaygain_track_gain") == 0) {
            meta->has_track_gain = true;
            meta->track_gain = (float)atof(val);
        }
    }
    if (name) free(name);
    if (val) free(val);
}

static void parse_ilst(FILE *f, uint64_t ilst_end, KoniMetadata *meta) {
    while ((uint64_t)ftell(f) + 8 <= ilst_end) {
        uint64_t item_start = (uint64_t)ftell(f);
        uint32_t sz = read_u32(f);
        uint32_t type = read_u32(f);
        if (sz < 8) break;
        uint64_t item_end = item_start + sz;

        if (type == FOURCC(0xa9, 'n', 'a', 'm') && !meta->title) {
            meta->title = extract_data_atom_text(f, item_end);
        } else if (type == FOURCC(0xa9, 'A', 'R', 'T') && !meta->artist) {
            meta->artist = extract_data_atom_text(f, item_end);
        } else if (type == FOURCC('a', 'A', 'R', 'T') && !meta->artist) {
            meta->artist = extract_data_atom_text(f, item_end);
        } else if (type == FOURCC(0xa9, 'a', 'l', 'b') && !meta->album) {
            meta->album = extract_data_atom_text(f, item_end);
        } else if (type == FOURCC(0xa9, 'l', 'y', 'r') && !meta->lyrics) {
            meta->lyrics = extract_data_atom_text(f, item_end);
        } else if (type == FOURCC('-', '-', '-', '-')) {
            parse_freeform_item(f, item_end, meta);
        } else if (type == FOURCC('c', 'o', 'v', 'r') && !meta->art_url) {
            while ((uint64_t)ftell(f) + 8 <= item_end) {
                uint64_t data_start = (uint64_t)ftell(f);
                uint32_t dsz = read_u32(f);
                uint32_t dtype = read_u32(f);
                if (dsz < 8) break;
                uint64_t data_end = data_start + dsz;

                if (dtype == FOURCC('d', 'a', 't', 'a') && dsz > 16) {
                    uint32_t flags = read_u32(f);
                    fseek(f, 4, SEEK_CUR); // locale
                    size_t img_sz = dsz - 16;
                    uint8_t *img_buf = malloc(img_sz);
                    if (img_buf && fread(img_buf, 1, img_sz, f) == img_sz) {
                        bool is_png = ((flags & 0xFF) == 14) || (img_sz >= 4 && img_buf[0] == 0x89 && img_buf[1] == 'P');
                        meta->art_url = save_temp_cover(img_buf, img_sz, is_png);
                    }
                    if (img_buf) free(img_buf);
                    break;
                }
                fseek(f, (long)data_end, SEEK_SET);
            }
        }
        fseek(f, (long)item_end, SEEK_SET);
    }
}

bool m4a_read_metadata(const char *filepath, KoniMetadata *meta, uint32_t *duration_sec) {
    if (!filepath || !meta) return false;
    memset(meta, 0, sizeof(KoniMetadata));
    if (duration_sec) *duration_sec = 0;

    FILE *fp = fopen(filepath, "rb");
    if (!fp) return false;

    uint32_t timescale = 0;
    uint64_t duration_ticks = 0;

    // Scan top-level atoms
    uint8_t hdr[8];
    while (fread(hdr, 1, 8, fp) == 8) {
        uint32_t sz32 = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16) | ((uint32_t)hdr[2] << 8) | (uint32_t)hdr[3];
        uint32_t type = ((uint32_t)hdr[4] << 24) | ((uint32_t)hdr[5] << 16) | ((uint32_t)hdr[6] << 8) | (uint32_t)hdr[7];
        uint64_t start = (uint64_t)ftell(fp) - 8;
        uint64_t sz = (sz32 == 1) ? read_u32(fp) : sz32;
        if (sz < 8) break;
        uint64_t end = start + sz;

        if (type == FOURCC('m', 'o', 'o', 'v')) {
            while ((uint64_t)ftell(fp) + 8 <= end) {
                uint64_t s_start = (uint64_t)ftell(fp);
                uint32_t s_sz = read_u32(fp);
                uint32_t s_type = read_u32(fp);
                if (s_sz < 8) break;
                uint64_t s_end = s_start + s_sz;

                if (s_type == FOURCC('m', 'v', 'h', 'd')) {
                    uint8_t ver = (uint8_t)fgetc(fp);
                    fseek(fp, 3, SEEK_CUR); // flags
                    if (ver == 1) {
                        fseek(fp, 16, SEEK_CUR);
                        timescale = read_u32(fp);
                        duration_ticks = ((uint64_t)read_u32(fp) << 32) | read_u32(fp);
                    } else {
                        fseek(fp, 8, SEEK_CUR);
                        timescale = read_u32(fp);
                        duration_ticks = read_u32(fp);
                    }
                } else if (s_type == FOURCC('u', 'd', 't', 'a')) {
                    while ((uint64_t)ftell(fp) + 8 <= s_end) {
                        uint64_t u_start = (uint64_t)ftell(fp);
                        uint32_t u_sz = read_u32(fp);
                        uint32_t u_type = read_u32(fp);
                        if (u_sz < 8) break;
                        uint64_t u_end = u_start + u_sz;

                        if (u_type == FOURCC('m', 'e', 't', 'a')) {
                            fseek(fp, 4, SEEK_CUR); // version + flags
                            while ((uint64_t)ftell(fp) + 8 <= u_end) {
                                uint64_t m_start = (uint64_t)ftell(fp);
                                uint32_t m_sz = read_u32(fp);
                                uint32_t m_type = read_u32(fp);
                                if (m_sz < 8) break;
                                uint64_t m_end = m_start + m_sz;

                                if (m_type == FOURCC('i', 'l', 's', 't')) {
                                    parse_ilst(fp, m_end, meta);
                                }
                                fseek(fp, (long)m_end, SEEK_SET);
                            }
                        }
                        fseek(fp, (long)u_end, SEEK_SET);
                    }
                }
                fseek(fp, (long)s_end, SEEK_SET);
            }
        }
        fseek(fp, (long)end, SEEK_SET);
    }

    fclose(fp);

    if (duration_sec && timescale > 0) {
        *duration_sec = (uint32_t)(duration_ticks / timescale);
    }

    return (meta->title || meta->artist || meta->album || meta->lyrics || meta->art_url || meta->has_track_gain);
}