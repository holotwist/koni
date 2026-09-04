#ifndef PXTONE_SYNTH_H
#define PXTONE_SYNTH_H

#include "pxtn_types.h"

uint32_t pxtn_synth_render(PxtnTiny *p, int32_t *out_pcm, uint32_t max_frames);
bool     pxtn_synth_seek(PxtnTiny *p, uint64_t target_sample);
void     pxtn_synth_free(PxtnTiny *p);

#endif // PXTONE_SYNTH_H