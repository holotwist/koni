#define _DEFAULT_SOURCE
#include "equalizer.h"
#include "peq.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <pthread.h>

#define EQ_MAX_CHANNELS 8

typedef struct {
    double b0, b1, b2, a1, a2;
    double s1[EQ_MAX_CHANNELS];
    double s2[EQ_MAX_CHANNELS];
} BiquadBand;

static const float s_frequencies[EQ_NUM_BANDS] = {
    31.25f, 62.5f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
};

static const char *s_freq_labels[EQ_NUM_BANDS] = {
    "31Hz", "62Hz", "125Hz", "250Hz", "500Hz", "1kHz", "2kHz", "4kHz", "8kHz", "16kHz"
};

static const EQPreset s_presets[] = {
    { "Flat",            {  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f } },
    { "Bass Boost",      { +6.0f, +5.0f, +4.0f, +2.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f } },
    { "Hardbass",        { +5.0f, +7.0f, +6.0f, +2.0f, -2.0f, -1.0f, +1.0f, +3.0f, +5.0f, +4.0f } },
    { "EDM / Dance",     { +6.0f, +6.0f, +3.0f,  0.0f, -2.0f, +1.0f, +2.0f, +3.5f, +5.0f, +4.0f } },
    { "Hip-Hop / Trap",  { +6.5f, +5.5f, +2.0f, -1.5f, -2.0f,  0.0f, +1.5f, +2.5f, +5.0f, +3.5f } },
    { "Metal",           { +2.0f, +5.0f, +3.0f,  0.0f, -3.0f, -2.0f, +2.0f, +4.0f, +4.0f, +3.0f } },
    { "Rock",            { +5.0f, +4.0f, +2.0f, -1.0f, -1.0f,  0.0f, +2.0f, +3.0f, +4.0f, +5.0f } },
    { "Pop",             { -1.0f, +1.0f, +3.0f, +4.0f, +3.0f, +1.0f, -1.0f, -1.0f, +1.0f, +2.0f } },
    { "Vocaloid / Synth",{ +2.0f, +4.0f, +3.0f, +2.0f, -1.0f, +2.0f, +1.0f, -1.5f, +1.0f, +3.0f } },
    { "Electronic",      { +5.0f, +4.0f, +2.0f,  0.0f, -1.0f, +2.0f,  0.0f, +1.0f, +4.0f, +5.0f } },
    { "V-Shape (Smile)", { +4.0f, +5.0f, +3.0f, +1.0f, -2.0f, -2.5f, -1.0f, +1.5f, +4.0f, +4.5f } },
    { "Lofi / Chill",    { -4.0f, +1.0f, +3.0f, +3.5f, +1.5f,  0.0f, -1.0f, -2.0f, -3.5f, -6.0f } },
    { "Jazz",            {  0.0f, +2.0f, +3.0f, +1.5f,  0.0f,  0.0f, +1.0f, +2.0f, +2.5f, +1.5f } },
    { "Acoustic",        { +3.0f, +2.0f, +1.0f, +1.0f, +1.0f, +1.0f, +2.0f, +2.0f, +3.0f, +2.0f } },
    { "Classical",       { +4.0f, +3.0f, +2.0f, +1.0f, -1.0f, -1.0f,  0.0f, +2.0f, +3.0f, +3.0f } },
    { "Vocal",           { -2.0f, -2.0f, -1.0f, +2.0f, +4.0f, +4.0f, +3.0f, +1.0f,  0.0f, -2.0f } },
    { "Podcast",         { -8.0f, -4.0f, -1.0f, -2.0f,  0.0f, +2.5f, +3.5f, +2.0f, -1.5f, -4.0f } }
};

static const int s_preset_count = (int)(sizeof(s_presets) / sizeof(s_presets[0]));

static pthread_mutex_t s_eq_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool s_enabled = false;
static EQMode s_eq_mode = EQ_MODE_GRAPHIC;
static float s_target_gains[EQ_NUM_BANDS] = {0};
static float s_current_gains[EQ_NUM_BANDS] = {0};
static bool s_needs_smoothing = false;
static int s_current_preset = 0; // 0 = Flat, -1 = Custom
static uint32_t s_current_srate = 44100;
static BiquadBand s_filters[EQ_NUM_BANDS];

static void compute_biquad_coefficients(BiquadBand *filter, float f0, float gain_db, uint32_t srate) {
    if (fabsf(gain_db) < 0.05f || f0 >= (float)srate * 0.49f) {
        filter->b0 = 1.0;
        filter->b1 = 0.0;
        filter->b2 = 0.0;
        filter->a1 = 0.0;
        filter->a2 = 0.0;
        return;
    }

    const double Q = 1.4142135623730951; // ~1-octave bandwidth
    double A = pow(10.0, (double)gain_db / 40.0);
    double w0 = 2.0 * M_PI * ((double)f0 / (double)srate);
    double alpha = sin(w0) / (2.0 * Q);
    double cos_w = cos(w0);

    double b0 = 1.0 + alpha * A;
    double b1 = -2.0 * cos_w;
    double b2 = 1.0 - alpha * A;
    double a0 = 1.0 + alpha / A;
    double a1 = -2.0 * cos_w;
    double a2 = 1.0 - alpha / A;

    filter->b0 = b0 / a0;
    filter->b1 = b1 / a0;
    filter->b2 = b2 / a0;
    filter->a1 = a1 / a0;
    filter->a2 = a2 / a0;
}

static void recalculate_coefficients_locked(uint32_t srate) {
    if (srate == 0) srate = 44100;
    s_current_srate = srate;

    for (int b = 0; b < EQ_NUM_BANDS; b++) {
        compute_biquad_coefficients(&s_filters[b], s_frequencies[b], s_current_gains[b], srate);
    }
}

void eq_init(void) {
    peq_init();
    pthread_mutex_lock(&s_eq_mutex);
    s_enabled = false;
    s_eq_mode = EQ_MODE_GRAPHIC;
    s_current_preset = 0;
    memset(s_target_gains, 0, sizeof(s_target_gains));
    memset(s_current_gains, 0, sizeof(s_current_gains));
    s_needs_smoothing = false;
    memset(s_filters, 0, sizeof(s_filters));
    recalculate_coefficients_locked(44100);
    pthread_mutex_unlock(&s_eq_mutex);
}

bool eq_is_enabled(void) {
    pthread_mutex_lock(&s_eq_mutex);
    bool en = s_enabled;
    pthread_mutex_unlock(&s_eq_mutex);
    return en;
}

void eq_set_enabled(bool enabled) {
    pthread_mutex_lock(&s_eq_mutex);
    s_enabled = enabled;
    pthread_mutex_unlock(&s_eq_mutex);
}

void eq_toggle_enabled(void) {
    pthread_mutex_lock(&s_eq_mutex);
    s_enabled = !s_enabled;
    pthread_mutex_unlock(&s_eq_mutex);
}

EQMode eq_get_mode(void) {
    pthread_mutex_lock(&s_eq_mutex);
    EQMode m = s_eq_mode;
    pthread_mutex_unlock(&s_eq_mutex);
    return m;
}

void eq_set_mode(EQMode mode) {
    pthread_mutex_lock(&s_eq_mutex);
    s_eq_mode = mode;
    pthread_mutex_unlock(&s_eq_mutex);
}

void eq_toggle_mode(void) {
    pthread_mutex_lock(&s_eq_mutex);
    s_eq_mode = (s_eq_mode == EQ_MODE_GRAPHIC) ? EQ_MODE_PARAMETRIC : EQ_MODE_GRAPHIC;
    pthread_mutex_unlock(&s_eq_mutex);
}

float eq_get_band_gain(int band_idx) {
    if (band_idx < 0 || band_idx >= EQ_NUM_BANDS) return 0.0f;
    pthread_mutex_lock(&s_eq_mutex);
    float g = s_target_gains[band_idx];
    pthread_mutex_unlock(&s_eq_mutex);
    return g;
}

void eq_set_band_gain(int band_idx, float gain_db) {
    if (band_idx < 0 || band_idx >= EQ_NUM_BANDS) return;
    if (gain_db < EQ_MIN_GAIN_DB) gain_db = EQ_MIN_GAIN_DB;
    if (gain_db > EQ_MAX_GAIN_DB) gain_db = EQ_MAX_GAIN_DB;

    pthread_mutex_lock(&s_eq_mutex);
    s_target_gains[band_idx] = gain_db;
    s_current_preset = -1; // Mark as Custom
    s_needs_smoothing = true;
    pthread_mutex_unlock(&s_eq_mutex);
}

void eq_adjust_band_gain(int band_idx, float delta_db) {
    if (band_idx < 0 || band_idx >= EQ_NUM_BANDS) return;
    pthread_mutex_lock(&s_eq_mutex);
    float g = s_target_gains[band_idx] + delta_db;
    if (g < EQ_MIN_GAIN_DB) g = EQ_MIN_GAIN_DB;
    if (g > EQ_MAX_GAIN_DB) g = EQ_MAX_GAIN_DB;
    s_target_gains[band_idx] = g;
    s_current_preset = -1;
    s_needs_smoothing = true;
    pthread_mutex_unlock(&s_eq_mutex);
}

int eq_get_preset_count(void) {
    return s_preset_count;
}

const char* eq_get_preset_name(int preset_idx) {
    if (preset_idx < 0 || preset_idx >= s_preset_count) return "Custom";
    return s_presets[preset_idx].name;
}

int eq_get_current_preset(void) {
    pthread_mutex_lock(&s_eq_mutex);
    int p = s_current_preset;
    pthread_mutex_unlock(&s_eq_mutex);
    return p;
}

void eq_apply_preset(int preset_idx) {
    if (preset_idx < 0 || preset_idx >= s_preset_count) return;
    pthread_mutex_lock(&s_eq_mutex);
    s_current_preset = preset_idx;
    memcpy(s_target_gains, s_presets[preset_idx].gains, sizeof(s_target_gains));
    s_needs_smoothing = true;
    pthread_mutex_unlock(&s_eq_mutex);
}

void eq_cycle_preset(void) {
    pthread_mutex_lock(&s_eq_mutex);
    s_current_preset = (s_current_preset + 1) % s_preset_count;
    memcpy(s_target_gains, s_presets[s_current_preset].gains, sizeof(s_target_gains));
    s_needs_smoothing = true;
    pthread_mutex_unlock(&s_eq_mutex);
}

void eq_reset_flat(void) {
    eq_apply_preset(0);
}

const float* eq_get_frequencies(void) {
    return s_frequencies;
}

const char** eq_get_freq_labels(void) {
    return s_freq_labels;
}

void eq_save_state(void *file_ptr) {
    FILE *f = (FILE*)file_ptr;
    if (!f) return;
    pthread_mutex_lock(&s_eq_mutex);
    fprintf(f, "eq_enabled=%d\n", s_enabled ? 1 : 0);
    fprintf(f, "eq_mode=%d\n", (int)s_eq_mode);
    fprintf(f, "eq_preset=%d\n", s_current_preset);
    fprintf(f, "eq_gains=");
    for (int i = 0; i < EQ_NUM_BANDS; i++) {
        fprintf(f, "%s%.1f", (i > 0) ? "," : "", s_target_gains[i]);
    }
    fprintf(f, "\n");
    pthread_mutex_unlock(&s_eq_mutex);
    peq_save_state(file_ptr);
}

void eq_load_state_key(const char *key, const char *val) {
    if (!key || !val) return;
    pthread_mutex_lock(&s_eq_mutex);
    if (strcmp(key, "eq_enabled") == 0) {
        s_enabled = (atoi(val) != 0);
    } else if (strcmp(key, "eq_mode") == 0) {
        s_eq_mode = (EQMode)atoi(val);
    } else if (strncmp(key, "peq_", 4) == 0) {
        pthread_mutex_unlock(&s_eq_mutex);
        peq_load_state_key(key, val);
        return;
    } else if (strcmp(key, "eq_preset") == 0) {
        s_current_preset = atoi(val);
    } else if (strcmp(key, "eq_gains") == 0) {
        char buf[256];
        strncpy(buf, val, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char *saveptr = NULL;
        char *tok = strtok_r(buf, ",", &saveptr);
        int idx = 0;
        while (tok && idx < EQ_NUM_BANDS) {
            float g = (float)atof(tok);
            if (g < EQ_MIN_GAIN_DB) g = EQ_MIN_GAIN_DB;
            if (g > EQ_MAX_GAIN_DB) g = EQ_MAX_GAIN_DB;
            s_target_gains[idx] = g;
            s_current_gains[idx] = g;
            idx++;
            tok = strtok_r(NULL, ",", &saveptr);
        }
        s_needs_smoothing = false;
        recalculate_coefficients_locked(s_current_srate);
    }
    pthread_mutex_unlock(&s_eq_mutex);
}

// Soft Limiter, transparent below 0.85, smooth tanh saturation above 
static inline float eq_soft_limit(float x) {
    const float threshold = 0.85f;
    if (x > threshold) {
        float excess = x - threshold;
        return threshold + (1.0f - threshold) * tanhf(excess / (1.0f - threshold));
    } else if (x < -threshold) {
        float excess = -x - threshold;
        return -(threshold + (1.0f - threshold) * tanhf(excess / (1.0f - threshold)));
    }
    return x;
}

#define SUB_BLOCK_SIZE 32

void eq_process_float(float *samples_interleaved, uint32_t num_frames, uint16_t num_channels, uint32_t sample_rate) {
    if (!samples_interleaved || num_frames == 0 || num_channels == 0) return;

    pthread_mutex_lock(&s_eq_mutex);
    if (!s_enabled) {
        pthread_mutex_unlock(&s_eq_mutex);
        return;
    }

    if (s_eq_mode == EQ_MODE_PARAMETRIC) {
        pthread_mutex_unlock(&s_eq_mutex);
        peq_process_float(samples_interleaved, num_frames, num_channels, sample_rate);
        return;
    }

    if (sample_rate != s_current_srate) {
        recalculate_coefficients_locked(sample_rate);
    }

    uint16_t channels = (num_channels <= EQ_MAX_CHANNELS) ? num_channels : EQ_MAX_CHANNELS;
    uint32_t frames_processed = 0;

    // ~20ms time constant for EQ band morphing
    float block_alpha = 1.0f - expf(-((float)SUB_BLOCK_SIZE) / ((float)sample_rate * 0.020f));

    while (frames_processed < num_frames) {
        uint32_t block_frames = num_frames - frames_processed;
        if (block_frames > SUB_BLOCK_SIZE) block_frames = SUB_BLOCK_SIZE;

        // Smoothly slew target gains and update biquad coefficients
        if (s_needs_smoothing) {
            bool any_moving = false;
            for (int b = 0; b < EQ_NUM_BANDS; b++) {
                float diff = s_target_gains[b] - s_current_gains[b];
                if (fabsf(diff) > 0.01f) {
                    s_current_gains[b] += block_alpha * diff;
                    compute_biquad_coefficients(&s_filters[b], s_frequencies[b], s_current_gains[b], sample_rate);
                    any_moving = true;
                } else if (s_current_gains[b] != s_target_gains[b]) {
                    s_current_gains[b] = s_target_gains[b];
                    compute_biquad_coefficients(&s_filters[b], s_frequencies[b], s_current_gains[b], sample_rate);
                }
            }
            s_needs_smoothing = any_moving;
        }

        // Filter block
        for (uint32_t f = 0; f < block_frames; f++) {
            uint32_t base = (frames_processed + f) * num_channels;

            for (uint16_t c = 0; c < channels; c++) {
                double x = (double)samples_interleaved[base + c];

                for (int b = 0; b < EQ_NUM_BANDS; b++) {
                    BiquadBand *filter = &s_filters[b];
                    double y = filter->b0 * x + filter->s1[c];
                    filter->s1[c] = filter->b1 * x - filter->a1 * y + filter->s2[c];
                    filter->s2[c] = filter->b2 * x - filter->a2 * y;
                    x = y;
                }

                samples_interleaved[base + c] = (float)x;
            }
        }

        frames_processed += block_frames;
    }

    pthread_mutex_unlock(&s_eq_mutex);
}

void eq_process(int32_t *pcm_interleaved, uint32_t num_frames, uint16_t num_channels, uint32_t sample_rate) {
    if (!pcm_interleaved || num_frames == 0 || num_channels == 0) return;

    pthread_mutex_lock(&s_eq_mutex);
    if (!s_enabled) {
        pthread_mutex_unlock(&s_eq_mutex);
        return;
    }

    if (sample_rate != s_current_srate) {
        recalculate_coefficients_locked(sample_rate);
    }

    uint16_t channels = (num_channels <= EQ_MAX_CHANNELS) ? num_channels : EQ_MAX_CHANNELS;

    for (uint32_t f = 0; f < num_frames; f++) {
        uint32_t base = f * num_channels;

        for (uint16_t c = 0; c < channels; c++) {
            float x = (float)pcm_interleaved[base + c] / 2147483648.0f;

            // Cascade through 10 biquad peak filters (Transposed Direct Form II)
            for (int b = 0; b < EQ_NUM_BANDS; b++) {
                BiquadBand *filter = &s_filters[b];
                float y = filter->b0 * x + filter->s1[c];
                filter->s1[c] = filter->b1 * x - filter->a1 * y + filter->s2[c];
                filter->s2[c] = filter->b2 * x - filter->a2 * y;
                x = y;
            }

            // Soft Limiter to prevent clipping
            x = eq_soft_limit(x);

            // Fast xorshift32 PRNG for TPDF dither
            static uint32_t s_dither_state = 0x12345678;
            s_dither_state ^= s_dither_state << 13;
            s_dither_state ^= s_dither_state >> 17;
            s_dither_state ^= s_dither_state << 5;
            float r1 = (float)(int32_t)s_dither_state * (1.0f / 2147483648.0f);

            s_dither_state ^= s_dither_state << 13;
            s_dither_state ^= s_dither_state >> 17;
            s_dither_state ^= s_dither_state << 5;
            float r2 = (float)(int32_t)s_dither_state * (1.0f / 2147483648.0f);

            // 1 LSB triangular dither at 32-bit boundary
            float dither = (r1 - r2);
            double scaled = ((double)x * 2147483647.0) + (double)dither;

            if (scaled > 2147483647.0) scaled = 2147483647.0;
            else if (scaled < -2147483648.0) scaled = -2147483648.0;
            pcm_interleaved[base + c] = (int32_t)round(scaled);
        }
    }

    pthread_mutex_unlock(&s_eq_mutex);
}