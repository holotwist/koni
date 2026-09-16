#define _DEFAULT_SOURCE
#include "dc_blocker.h"
#include <math.h>
#include <string.h>

void dc_blocker_init(DCBlocker *dc, uint32_t sample_rate) {
    if (!dc) return;
    memset(dc, 0, sizeof(DCBlocker));
    if (sample_rate == 0) sample_rate = 44100;
    dc->sample_rate = sample_rate;

    // Cutoff frequency fixed at 5.0 Hz
    const float cutoff_hz = 5.0f;
    float r = 1.0f - (2.0f * (float)M_PI * cutoff_hz / (float)sample_rate);
    if (r < 0.99f) r = 0.99f;
    if (r > 0.9999f) r = 0.9999f;
    dc->R = r;
}

void dc_blocker_reset(DCBlocker *dc) {
    if (!dc) return;
    memset(dc->x1, 0, sizeof(dc->x1));
    memset(dc->y1, 0, sizeof(dc->y1));
}

void dc_blocker_process(DCBlocker *dc, float *samples_interleaved, uint32_t num_frames, uint16_t num_channels, uint32_t sample_rate) {
    if (!dc || !samples_interleaved || num_frames == 0 || num_channels == 0) return;

    if (sample_rate != dc->sample_rate) {
        dc_blocker_init(dc, sample_rate);
    }

    uint16_t channels = (num_channels <= DC_BLOCKER_MAX_CHANNELS) ? num_channels : DC_BLOCKER_MAX_CHANNELS;
    float R = dc->R;

    for (uint32_t f = 0; f < num_frames; f++) {
        uint32_t base = f * num_channels;

        for (uint16_t c = 0; c < channels; c++) {
            float x = samples_interleaved[base + c];
            float y = x - dc->x1[c] + R * dc->y1[c];

            // Anti-denormal protection
            if (fabsf(y) < 1.0e-15f) {
                y = 0.0f;
            }

            dc->x1[c] = x;
            dc->y1[c] = y;
            samples_interleaved[base + c] = y;
        }
    }
}