#ifndef OPUS_CODEC_H
#define OPUS_CODEC_H

#include "codec.h"

extern const KoniCodecImpl opus_codec_impl;
bool opus_read_metadata(const char *filepath, KoniMetadata *meta, uint32_t *duration_sec);

#endif // OPUS_CODEC_H