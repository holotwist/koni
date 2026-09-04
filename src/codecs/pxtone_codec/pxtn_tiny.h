#ifndef PXTONE_TINY_H
#define PXTONE_TINY_H

#include "pxtn_types.h"
#include "pxtn_loader.h"
#include "pxtn_synth.h"

static inline PxtnTiny* pxtn_tiny_open(const char *filepath, uint32_t target_sps) {
    return pxtn_load_file(filepath, target_sps);
}

static inline void pxtn_tiny_close(PxtnTiny *pxtn) {
    pxtn_synth_free(pxtn);
}

static inline bool pxtn_tiny_get_info(const PxtnTiny *pxtn, PxtnSongInfo *out_info) {
    if (!pxtn || !out_info) return false;
    *out_info = pxtn->info;
    return true;
}

static inline uint32_t pxtn_tiny_render(PxtnTiny *pxtn, int32_t *out_pcm, uint32_t max_frames) {
    return pxtn_synth_render(pxtn, out_pcm, max_frames);
}

static inline bool pxtn_tiny_seek(PxtnTiny *pxtn, uint64_t target_sample) {
    return pxtn_synth_seek(pxtn, target_sample);
}

#endif // PXTONE_TINY_H