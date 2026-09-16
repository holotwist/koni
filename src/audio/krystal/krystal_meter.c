#define _DEFAULT_SOURCE
#include "krystal_meter.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ITU-R BS.1770 Stage 1, High shelf (+4.0 dB @ 1500 Hz) 
static void set_bs1770_stage1(KrystalBiquad *b, float fs) {
    float gain_db = 4.0f;
    float fc = 1500.0f;
    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * (float)M_PI * (fc / fs);
    float cos_w = cosf(w0);
    float sin_w = sinf(w0);
    float alpha = sin_w * 0.5f * sqrtf((A + 1.0f / A) * (1.0f / 0.7071f - 1.0f) + 2.0f);

    float a0 = (A + 1.0f) - (A - 1.0f) * cos_w + 2.0f * sqrtf(A) * alpha;
    b->b0 = (A * ((A + 1.0f) + (A - 1.0f) * cos_w + 2.0f * sqrtf(A) * alpha)) / a0;
    b->b1 = (-2.0f * A * ((A - 1.0f) + (A + 1.0f) * cos_w)) / a0;
    b->b2 = (A * ((A + 1.0f) + (A - 1.0f) * cos_w - 2.0f * sqrtf(A) * alpha)) / a0;
    b->a1 = (2.0f * ((A - 1.0f) - (A + 1.0f) * cos_w)) / a0;
    b->a2 = ((A + 1.0f) - (A - 1.0f) * cos_w - 2.0f * sqrtf(A) * alpha) / a0;
}

// ITU-R BS.1770 Stage 2, High pass (RLB weighting @ 38 Hz) 
static void set_bs1770_stage2(KrystalBiquad *b, float fs) {
    float fc = 38.0f;
    float q = 0.50f;
    float w0 = 2.0f * (float)M_PI * (fc / fs);
    float alpha = sinf(w0) / (2.0f * q);
    float cos_w = cosf(w0);
    float a0 = 1.0f + alpha;

    b->b0 = ((1.0f + cos_w) * 0.5f) / a0;
    b->b1 = -(1.0f + cos_w) / a0;
    b->b2 = ((1.0f + cos_w) * 0.5f) / a0;
    b->a1 = (-2.0f * cos_w) / a0;
    b->a2 = (1.0f - alpha) / a0;
}

static inline float biquad_step(KrystalBiquad *b, float in, uint16_t c) {
    float out = b->b0 * in + b->s1[c];
    b->s1[c] = b->b1 * in - b->a1 * out + b->s2[c];
    b->s2[c] = b->b2 * in - b->a2 * out;
    if (fabsf(out) < 1.0e-15f) out = 0.0f;
    return out;
}

void krystal_meter_init(KrystalMeterState *meter, uint32_t sample_rate) {
    if (!meter) return;
    memset(meter, 0, sizeof(KrystalMeterState));
    meter->sample_rate = sample_rate ? sample_rate : 44100;
    meter->lufs_momentary = -70.0f;
    meter->peak_dbfs = -70.0f;
    set_bs1770_stage1(&meter->stage1_shelf, (float)meter->sample_rate);
    set_bs1770_stage2(&meter->stage2_highpass, (float)meter->sample_rate);
}

void krystal_meter_reset(KrystalMeterState *meter) {
    if (!meter) return;
    memset(meter->stage1_shelf.s1, 0, sizeof(meter->stage1_shelf.s1));
    memset(meter->stage1_shelf.s2, 0, sizeof(meter->stage1_shelf.s2));
    memset(meter->stage2_highpass.s1, 0, sizeof(meter->stage2_highpass.s1));
    memset(meter->stage2_highpass.s2, 0, sizeof(meter->stage2_highpass.s2));
    meter->mean_square_acc = 0.0f;
    meter->lufs_momentary = -70.0f;
    meter->peak_dbfs = -70.0f;
}

void krystal_meter_process(KrystalMeterState *meter, const float *samples, uint32_t num_frames,
                           uint16_t channels, uint32_t sample_rate,
                           float *out_lufs, float *out_peak_dbfs) {
    if (!meter || !samples || num_frames == 0 || channels == 0) return;

    if (sample_rate != meter->sample_rate) {
        meter->sample_rate = sample_rate;
        set_bs1770_stage1(&meter->stage1_shelf, (float)sample_rate);
        set_bs1770_stage2(&meter->stage2_highpass, (float)sample_rate);
    }

    uint16_t num_ch = (channels <= KRYSTAL_MAX_CHANNELS) ? channels : KRYSTAL_MAX_CHANNELS;
    float peak = 0.0f;
    float sum_k_weighted = 0.0f;

    for (uint32_t f = 0; f < num_frames; f++) {
        uint32_t base = f * channels;
        for (uint16_t c = 0; c < num_ch; c++) {
            float s = samples[base + c];
            float abs_s = fabsf(s);
            if (abs_s > peak) peak = abs_s;

            // K-weighting filter cascade 
            float y = biquad_step(&meter->stage1_shelf, s, c);
            y = biquad_step(&meter->stage2_highpass, y, c);
            sum_k_weighted += y * y;
        }
    }

    // 400ms time-constant leaky integrator (Momentary LUFS) 
    float alpha = 1.0f - expf(-((float)num_frames) / ((float)sample_rate * 0.400f));
    float frame_ms = sum_k_weighted / (float)(num_frames * num_ch);
    meter->mean_square_acc += alpha * (frame_ms - meter->mean_square_acc);

    // BS.1770-4 loudness calculation 
    float lufs = (meter->mean_square_acc > 1.0e-7f)
                 ? (-0.691f + 10.0f * log10f(meter->mean_square_acc))
                 : -70.0f;
    if (lufs < -70.0f) lufs = -70.0f;
    if (lufs > +10.0f) lufs = +10.0f;
    meter->lufs_momentary = lufs;

    float pk_db = (peak > 1.0e-5f) ? (20.0f * log10f(peak)) : -70.0f;
    meter->peak_dbfs = pk_db;

    if (out_lufs) *out_lufs = meter->lufs_momentary;
    if (out_peak_dbfs) *out_peak_dbfs = meter->peak_dbfs;
}