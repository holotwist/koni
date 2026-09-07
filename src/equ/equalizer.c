#define _DEFAULT_SOURCE
#include "equalizer.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <pthread.h>

#define EQ_MAX_CHANNELS 8

typedef struct {
    float b0, b1, b2, a1, a2;
    float s1[EQ_MAX_CHANNELS];
    float s2[EQ_MAX_CHANNELS];
} BiquadBand;

static const float s_frequencies[EQ_NUM_BANDS] = {
    31.25f, 62.5f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
};

static const char *s_freq_labels[EQ_NUM_BANDS] = {
    "31Hz", "62Hz", "125Hz", "250Hz", "500Hz", "1kHz", "2kHz", "4kHz", "8kHz", "16kHz"
};

static const EQPreset s_presets[] = {
    { "Flat",        {  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f } },
    { "Bass Boost",  { +6.0f, +5.0f, +4.0f, +2.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f,  0.0f } },
    { "Rock",        { +5.0f, +4.0f, +2.0f, -1.0f, -1.0f,  0.0f, +2.0f, +3.0f, +4.0f, +5.0f } },
    { "Pop",         { -1.0f, +1.0f, +3.0f, +4.0f, +3.0f, +1.0f, -1.0f, -1.0f, +1.0f, +2.0f } },
    { "Vocal",       { -2.0f, -2.0f, -1.0f, +2.0f, +4.0f, +4.0f, +3.0f, +1.0f,  0.0f, -2.0f } },
    { "Electronic",  { +5.0f, +4.0f, +2.0f,  0.0f, -1.0f, +2.0f,  0.0f, +1.0f, +4.0f, +5.0f } },
    { "Classical",   { +4.0f, +3.0f, +2.0f, +1.0f, -1.0f, -1.0f,  0.0f, +2.0f, +3.0f, +3.0f } },
    { "Acoustic",    { +3.0f, +2.0f, +1.0f, +1.0f, +1.0f, +1.0f, +2.0f, +2.0f, +3.0f, +2.0f } }
};

static const int s_preset_count = (int)(sizeof(s_presets) / sizeof(s_presets[0]));

static pthread_mutex_t s_eq_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool s_enabled = false;
static float s_band_gains[EQ_NUM_BANDS] = {0};
static int s_current_preset = 0; // 0 = Flat, -1 = Custom
static uint32_t s_current_srate = 44100;
static BiquadBand s_filters[EQ_NUM_BANDS];

static void recalculate_coefficients_locked(uint32_t srate) {
    if (srate == 0) srate = 44100;
    s_current_srate = srate;

    const float Q = 1.4142f; // ~1-octave bandwidth

    for (int b = 0; b < EQ_NUM_BANDS; b++) {
        float f0 = s_frequencies[b];
        float gain_db = s_band_gains[b];

        if (fabsf(gain_db) < 0.05f || f0 >= (float)srate * 0.49f) {
            // Bypass filter for this band
            s_filters[b].b0 = 1.0f;
            s_filters[b].b1 = 0.0f;
            s_filters[b].b2 = 0.0f;
            s_filters[b].a1 = 0.0f;
            s_filters[b].a2 = 0.0f;
            continue;
        }

        // RBJ Audio EQ Cookbook Peak Filter
        float A = powf(10.0f, gain_db / 40.0f);
        float w0 = 2.0f * (float)M_PI * (f0 / (float)srate);
        float alpha = sinf(w0) / (2.0f * Q);
        float cos_w = cosf(w0);

        float b0 = 1.0f + alpha * A;
        float b1 = -2.0f * cos_w;
        float b2 = 1.0f - alpha * A;
        float a0 = 1.0f + alpha / A;
        float a1 = -2.0f * cos_w;
        float a2 = 1.0f - alpha / A;

        s_filters[b].b0 = b0 / a0;
        s_filters[b].b1 = b1 / a0;
        s_filters[b].b2 = b2 / a0;
        s_filters[b].a1 = a1 / a0;
        s_filters[b].a2 = a2 / a0;
    }
}

void eq_init(void) {
    pthread_mutex_lock(&s_eq_mutex);
    s_enabled = false;
    s_current_preset = 0;
    memset(s_band_gains, 0, sizeof(s_band_gains));
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

float eq_get_band_gain(int band_idx) {
    if (band_idx < 0 || band_idx >= EQ_NUM_BANDS) return 0.0f;
    pthread_mutex_lock(&s_eq_mutex);
    float g = s_band_gains[band_idx];
    pthread_mutex_unlock(&s_eq_mutex);
    return g;
}

void eq_set_band_gain(int band_idx, float gain_db) {
    if (band_idx < 0 || band_idx >= EQ_NUM_BANDS) return;
    if (gain_db < EQ_MIN_GAIN_DB) gain_db = EQ_MIN_GAIN_DB;
    if (gain_db > EQ_MAX_GAIN_DB) gain_db = EQ_MAX_GAIN_DB;

    pthread_mutex_lock(&s_eq_mutex);
    s_band_gains[band_idx] = gain_db;
    s_current_preset = -1; // Mark as Custom
    recalculate_coefficients_locked(s_current_srate);
    pthread_mutex_unlock(&s_eq_mutex);
}

void eq_adjust_band_gain(int band_idx, float delta_db) {
    if (band_idx < 0 || band_idx >= EQ_NUM_BANDS) return;
    pthread_mutex_lock(&s_eq_mutex);
    float g = s_band_gains[band_idx] + delta_db;
    if (g < EQ_MIN_GAIN_DB) g = EQ_MIN_GAIN_DB;
    if (g > EQ_MAX_GAIN_DB) g = EQ_MAX_GAIN_DB;
    s_band_gains[band_idx] = g;
    s_current_preset = -1;
    recalculate_coefficients_locked(s_current_srate);
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
    memcpy(s_band_gains, s_presets[preset_idx].gains, sizeof(s_band_gains));
    recalculate_coefficients_locked(s_current_srate);
    pthread_mutex_unlock(&s_eq_mutex);
}

void eq_cycle_preset(void) {
    pthread_mutex_lock(&s_eq_mutex);
    s_current_preset = (s_current_preset + 1) % s_preset_count;
    memcpy(s_band_gains, s_presets[s_current_preset].gains, sizeof(s_band_gains));
    recalculate_coefficients_locked(s_current_srate);
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
    fprintf(f, "eq_preset=%d\n", s_current_preset);
    fprintf(f, "eq_gains=");
    for (int i = 0; i < EQ_NUM_BANDS; i++) {
        fprintf(f, "%s%.1f", (i > 0) ? "," : "", s_band_gains[i]);
    }
    fprintf(f, "\n");
    pthread_mutex_unlock(&s_eq_mutex);
}

void eq_load_state_key(const char *key, const char *val) {
    if (!key || !val) return;
    pthread_mutex_lock(&s_eq_mutex);
    if (strcmp(key, "eq_enabled") == 0) {
        s_enabled = (atoi(val) != 0);
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
            s_band_gains[idx++] = g;
            tok = strtok_r(NULL, ",", &saveptr);
        }
        recalculate_coefficients_locked(s_current_srate);
    }
    pthread_mutex_unlock(&s_eq_mutex);
}

/* Soft Limiter, transparent below 0.85, smooth tanh saturation above */
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

            long long val64 = (long long)(x * 2147483647.0f);
            if (val64 > 2147483647LL) val64 = 2147483647LL;
            else if (val64 < -2147483648LL) val64 = -2147483648LL;
            pcm_interleaved[base + c] = (int32_t)val64;
        }
    }

    pthread_mutex_unlock(&s_eq_mutex);
}