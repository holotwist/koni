#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include "codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#if defined(__has_include)
  #if __has_include(<opus/opusfile.h>)
    #include <opus/opusfile.h>
  #elif __has_include(<opusfile.h>)
    #include <opusfile.h>
  #else
    #include <opusfile.h>
  #endif
#else
  #include <opusfile.h>
#endif

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
    int buf = 0, bits = 0;
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

static uint32_t read_u32_be(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
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

bool opus_read_metadata(const char *filepath, KoniMetadata *meta, uint32_t *duration_sec) {
    if (!filepath || !meta) return false;
    memset(meta, 0, sizeof(KoniMetadata));
    if (duration_sec) *duration_sec = 0;

    int err = 0;
    OggOpusFile *of = op_open_file(filepath, &err);
    if (!of) return false;

    ogg_int64_t total_samples = op_pcm_total(of, -1);
    if (duration_sec && total_samples > 0) {
        *duration_sec = (uint32_t)(total_samples / 48000);
    }

    const OpusTags *tags = op_tags(of, -1);
    if (tags) {
        const char *t = opus_tags_query(tags, "TITLE", 0);
        if (t) meta->title = strdup(t);

        const char *a = opus_tags_query(tags, "ARTIST", 0);
        if (!a) a = opus_tags_query(tags, "ALBUMARTIST", 0);
        if (a) meta->artist = strdup(a);

        const char *al = opus_tags_query(tags, "ALBUM", 0);
        if (al) meta->album = strdup(al);

        const char *lyr = opus_tags_query(tags, "LYRICS", 0);
        if (!lyr) lyr = opus_tags_query(tags, "UNSYNCEDLYRICS", 0);
        if (lyr) meta->lyrics = strdup(lyr);

        const char *rg = opus_tags_query(tags, "REPLAYGAIN_TRACK_GAIN", 0);
        if (rg) {
            meta->has_track_gain = true;
            meta->track_gain = (float)atof(rg);
        }

        const char *pic_b64 = opus_tags_query(tags, "METADATA_BLOCK_PICTURE", 0);
        if (!pic_b64) pic_b64 = opus_tags_query(tags, "COVERART", 0);
        if (pic_b64) {
            size_t dec_sz = 0;
            uint8_t *pic = base64_decode(pic_b64, strlen(pic_b64), &dec_sz);
            if (pic) {
                parse_flac_picture(pic, dec_sz, meta);
                if (!meta->art_url && dec_sz > 4) {
                    meta->art_url = save_temp_cover(pic, dec_sz);
                }
                free(pic);
            }
        }
    }

    op_free(of);
    return (meta->title || meta->artist || meta->album || meta->lyrics || meta->art_url || meta->has_track_gain);
}