#include "codec.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>

extern const KoniCodecImpl dana_codec_impl;
extern const KoniCodecImpl ma_codec_impl;

static const KoniCodecImpl* available_codecs[] = {
    &dana_codec_impl,
    &ma_codec_impl,
    NULL
};

void koni_codecs_init(void) {
    // Optional init logic for specific codecs
}

const KoniCodecImpl* koni_find_codec_by_ext(const char* filepath) {
    if (!filepath) return NULL;
    const char* ext = strrchr(filepath, '.');
    if (!ext) return NULL;

    for (int i = 0; available_codecs[i] != NULL; i++) {
        const char** exts = available_codecs[i]->supported_extensions;
        if (!exts) continue;
        for (int j = 0; exts[j] != NULL; j++) {
            if (strcasecmp(ext, exts[j]) == 0) {
                return available_codecs[i];
            }
        }
    }
    return NULL;
}

bool koni_is_supported_extension(const char* ext) {
    if (!ext) return false;
    for (int i = 0; available_codecs[i] != NULL; i++) {
        const char** exts = available_codecs[i]->supported_extensions;
        if (!exts) continue;
        for (int j = 0; exts[j] != NULL; j++) {
            if (strcasecmp(ext, exts[j]) == 0) return true;
        }
    }
    return false;
}

void koni_metadata_free(KoniMetadata* meta) {
    if (!meta) return;
    if (meta->title) free(meta->title);
    if (meta->artist) free(meta->artist);
    if (meta->album) free(meta->album);
    if (meta->lyrics) free(meta->lyrics);
    if (meta->art_url) {
        if (strncmp(meta->art_url, "file://", 7) == 0) {
            remove(meta->art_url + 7); // Clean up the temp image
        }
        free(meta->art_url);
    }
    memset(meta, 0, sizeof(KoniMetadata));
}