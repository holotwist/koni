#ifndef M4A_CODEC_H
#define M4A_CODEC_H

#include "codec.h"

extern const KoniCodecImpl m4a_codec_impl;
bool m4a_read_metadata(const char *filepath, KoniMetadata *meta, uint32_t *duration_sec);

#endif // M4A_CODEC_H