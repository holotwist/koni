#ifndef DSP_RACK_H
#define DSP_RACK_H

#include <stdint.h>
#include <stdbool.h>
#include "replaygain/replaygain.h"

void dsp_rack_init(void);

/* Processes audio using the chain
 * Converts int32 to 32-bit float [-1.0, +1.0]
 * ReplayGain normalization
 * 10-Band Graphic Equalizer
 * Cubic Volume curve
 * Feed Visualizer tap (pre-soft-limiter)
 * True Peak Soft Limiter (tanh knee)
 * TPDF Dithering back to 32-bit hardware PCM
 */
void dsp_rack_process(const int32_t *pcm_in, float *float_out, uint32_t num_frames,
                      uint16_t num_channels, uint32_t sample_rate,
                      RGainState *rgain, int rgain_mode, int volume_percent);

#endif // DSP_RACK_H