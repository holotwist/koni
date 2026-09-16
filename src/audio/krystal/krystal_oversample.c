#define _DEFAULT_SOURCE
#include "krystal_oversample.h"
#include <string.h>

// Polyphase odd-tap half-band coefficients
static const float s_hb_odd[6] = {
    -0.0033f, 0.0142f, -0.0391f, 0.0870f, -0.1862f, 0.6264f
};

void krystal_oversampler_init(KrystalOversampler *os) {
    if (!os) return;
    memset(os, 0, sizeof(KrystalOversampler));
}

void krystal_oversampler_reset(KrystalOversampler *os) {
    if (!os) return;
    memset(os, 0, sizeof(KrystalOversampler));
}

void krystal_oversample_up(KrystalOversampler *os, const float *in, float *out_2x,
                           uint32_t num_frames, uint16_t channels) {
    if (!os || !in || !out_2x || num_frames == 0 || channels == 0) return;

    for (uint32_t f = 0; f < num_frames; f++) {
        uint32_t in_base = f * channels;
        uint32_t out_base = f * 2 * channels;
        uint32_t idx = os->up_idx;

        for (uint16_t c = 0; c < channels; c++) {
            os->up_history[c][idx] = in[in_base + c];

            // Branch 0, pure delay
            uint32_t d_idx = (idx + (HB_HALF / 2)) % (HB_HALF + 1);
            out_2x[out_base + c] = os->up_history[c][d_idx];

            // Branch 1, FIR interpolation
            float acc = 0.0f;
            for (int k = 0; k < 6; k++) {
                uint32_t h1 = (idx + k) % (HB_HALF + 1);
                uint32_t h2 = (idx + HB_HALF - k) % (HB_HALF + 1);
                acc += s_hb_odd[k] * (os->up_history[c][h1] + os->up_history[c][h2]);
            }
            out_2x[out_base + channels + c] = acc;
        }
        os->up_idx = (idx + HB_HALF) % (HB_HALF + 1);
    }
}

void krystal_oversample_down(KrystalOversampler *os, const float *in_2x, float *out,
                             uint32_t num_frames, uint16_t channels) {
    if (!os || !in_2x || !out || num_frames == 0 || channels == 0) return;

    for (uint32_t f = 0; f < num_frames; f++) {
        uint32_t in_base = f * 2 * channels;
        uint32_t out_base = f * channels;

        for (uint16_t c = 0; c < channels; c++) {
            uint32_t idx = os->down_idx;
            os->down_history[c][idx] = in_2x[in_base + c];
            os->down_history[c][(idx + 1) % HB_TAPS] = in_2x[in_base + channels + c];

            float acc = 0.5f * os->down_history[c][(idx + HB_HALF) % HB_TAPS];
            for (int k = 0; k < 6; k++) {
                int odd_pos1 = 2 * k + 1;
                int odd_pos2 = HB_TAPS - 1 - (2 * k + 1);
                uint32_t p1 = (idx + odd_pos1) % HB_TAPS;
                uint32_t p2 = (idx + odd_pos2) % HB_TAPS;
                acc += s_hb_odd[k] * (os->down_history[c][p1] + os->down_history[c][p2]);
            }
            out[out_base + c] = acc;
        }
        os->down_idx = (os->down_idx + 2) % HB_TAPS;
    }
}