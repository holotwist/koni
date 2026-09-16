#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#include "peq.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    double b0, b1, b2, a1, a2;
    double s1[PEQ_MAX_CHANNELS];
    double s2[PEQ_MAX_CHANNELS];
} PEQBiquad;

static pthread_mutex_t s_peq_mutex = PTHREAD_MUTEX_INITIALIZER;
static PEQBand s_bands[PEQ_MAX_BANDS];
static PEQBiquad s_filters[PEQ_MAX_BANDS];
static float s_preamp_db = 0.0f;
static float s_current_preamp_mult = 1.0f;
static uint32_t s_sample_rate = 44100;

static const char *s_filter_names[] = {
    "PK", "LSQ", "HSQ", "HP", "LP"
};

const char* peq_get_filter_name(PEQFilterType type) {
    if (type < 0 || type >= PEQ_FILTER_COUNT) return "PK";
    return s_filter_names[type];
}

static void compute_coefficients(PEQBiquad *bq, const PEQBand *band, uint32_t srate) {
    if (!band->enabled || (band->type != PEQ_FILTER_HIGH_PASS && band->type != PEQ_FILTER_LOW_PASS && fabsf(band->gain_db) < 0.01f)) {
        bq->b0 = 1.0; bq->b1 = 0.0; bq->b2 = 0.0;
        bq->a1 = 0.0; bq->a2 = 0.0;
        return;
    }

    double fs = (double)srate;
    double f0 = (double)band->freq;
    if (f0 < (double)PEQ_MIN_FREQ) f0 = (double)PEQ_MIN_FREQ;
    if (f0 >= fs * 0.49) f0 = fs * 0.49;

    double q = band->q > 0.05f ? (double)band->q : 0.7071067811865476;
    double A = pow(10.0, (double)band->gain_db / 40.0);
    double w0 = 2.0 * M_PI * (f0 / fs);
    double cos_w = cos(w0);
    double sin_w = sin(w0);
    double alpha = sin_w / (2.0 * q);

    double b0, b1, b2, a0, a1, a2;

    switch (band->type) {
        case PEQ_FILTER_PEAK:
            b0 = 1.0 + alpha * A;
            b1 = -2.0 * cos_w;
            b2 = 1.0 - alpha * A;
            a0 = 1.0 + alpha / A;
            a1 = -2.0 * cos_w;
            a2 = 1.0 - alpha / A;
            break;

        case PEQ_FILTER_LOW_SHELF: {
            // Strict Equalizer APO / AutoEQ LSQ derivation with Q factor
            double diff = (A + 1.0 / A) * (1.0 / q - 1.0) + 2.0;
            double alpha_term = sin_w * sqrt(diff > 0.0 ? diff : 0.0);

            b0 =    A * ((A + 1.0) - (A - 1.0) * cos_w + alpha_term);
            b1 =  2.0 * A * ((A - 1.0) - (A + 1.0) * cos_w);
            b2 =    A * ((A + 1.0) - (A - 1.0) * cos_w - alpha_term);
            a0 =        (A + 1.0) + (A - 1.0) * cos_w + alpha_term;
            a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cos_w);
            a2 =        (A + 1.0) + (A - 1.0) * cos_w - alpha_term;
            break;
        }

        case PEQ_FILTER_HIGH_SHELF: {
            // Strict Equalizer APO / AutoEQ HSQ derivation with Q factor
            double diff = (A + 1.0 / A) * (1.0 / q - 1.0) + 2.0;
            double alpha_term = sin_w * sqrt(diff > 0.0 ? diff : 0.0);

            b0 =    A * ((A + 1.0) + (A - 1.0) * cos_w + alpha_term);
            b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cos_w);
            b2 =    A * ((A + 1.0) + (A - 1.0) * cos_w - alpha_term);
            a0 =        (A + 1.0) - (A - 1.0) * cos_w + alpha_term;
            a1 =  2.0 * ((A - 1.0) - (A + 1.0) * cos_w);
            a2 =        (A + 1.0) - (A - 1.0) * cos_w - alpha_term;
            break;
        }

        case PEQ_FILTER_HIGH_PASS:
            b0 = (1.0f + cos_w) * 0.5f;
            b1 = -(1.0f + cos_w);
            b2 = (1.0f + cos_w) * 0.5f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cos_w;
            a2 = 1.0f - alpha;
            break;

        case PEQ_FILTER_LOW_PASS:
            b0 = (1.0f - cos_w) * 0.5f;
            b1 = 1.0f - cos_w;
            b2 = (1.0f - cos_w) * 0.5f;
            a0 = 1.0f + alpha;
            a1 = -2.0f * cos_w;
            a2 = 1.0f - alpha;
            break;

        default:
            b0 = 1.0f; b1 = 0.0f; b2 = 0.0f;
            a0 = 1.0f; a1 = 0.0f; a2 = 0.0f;
            break;
    }

    bq->b0 = b0 / a0;
    bq->b1 = b1 / a0;
    bq->b2 = b2 / a0;
    bq->a1 = a1 / a0;
    bq->a2 = a2 / a0;
}

static void recalculate_all_locked(uint32_t srate) {
    if (srate == 0) srate = 44100;
    s_sample_rate = srate;
    for (int i = 0; i < PEQ_MAX_BANDS; i++) {
        compute_coefficients(&s_filters[i], &s_bands[i], srate);
    }
}

void peq_init(void) {
    pthread_mutex_lock(&s_peq_mutex);
    s_preamp_db = 0.0f;
    s_current_preamp_mult = 1.0f;

    static const float def_freqs[PEQ_MAX_BANDS] = {
        32.0f, 64.0f, 125.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f
    };

    for (int i = 0; i < PEQ_MAX_BANDS; i++) {
        s_bands[i].enabled = true;
        s_bands[i].type = (i == 0) ? PEQ_FILTER_LOW_SHELF : ((i == PEQ_MAX_BANDS - 1) ? PEQ_FILTER_HIGH_SHELF : PEQ_FILTER_PEAK);
        s_bands[i].freq = def_freqs[i];
        s_bands[i].gain_db = 0.0f;
        s_bands[i].q = 0.7071f;
    }

    memset(s_filters, 0, sizeof(s_filters));
    recalculate_all_locked(44100);
    pthread_mutex_unlock(&s_peq_mutex);
}

static void peq_reset_locked(void) {
    s_preamp_db = 0.0f;
    for (int i = 0; i < PEQ_MAX_BANDS; i++) {
        s_bands[i].gain_db = 0.0f;
        memset(s_filters[i].s1, 0, sizeof(s_filters[i].s1));
        memset(s_filters[i].s2, 0, sizeof(s_filters[i].s2));
    }
}

void peq_reset(void) {
    pthread_mutex_lock(&s_peq_mutex);
    peq_reset_locked();
    recalculate_all_locked(s_sample_rate);
    pthread_mutex_unlock(&s_peq_mutex);
}

int peq_get_band_count(void) {
    return PEQ_MAX_BANDS;
}

bool peq_get_band(int idx, PEQBand *out_band) {
    if (idx < 0 || idx >= PEQ_MAX_BANDS || !out_band) return false;
    pthread_mutex_lock(&s_peq_mutex);
    *out_band = s_bands[idx];
    pthread_mutex_unlock(&s_peq_mutex);
    return true;
}

void peq_set_band(int idx, const PEQBand *band) {
    if (idx < 0 || idx >= PEQ_MAX_BANDS || !band) return;
    pthread_mutex_lock(&s_peq_mutex);
    s_bands[idx] = *band;
    compute_coefficients(&s_filters[idx], &s_bands[idx], s_sample_rate);
    pthread_mutex_unlock(&s_peq_mutex);
}

float peq_get_preamp(void) {
    pthread_mutex_lock(&s_peq_mutex);
    float p = s_preamp_db;
    pthread_mutex_unlock(&s_peq_mutex);
    return p;
}

void peq_set_preamp(float preamp_db) {
    if (preamp_db < PEQ_MIN_GAIN_DB) preamp_db = PEQ_MIN_GAIN_DB;
    if (preamp_db > PEQ_MAX_GAIN_DB) preamp_db = PEQ_MAX_GAIN_DB;
    pthread_mutex_lock(&s_peq_mutex);
    s_preamp_db = preamp_db;
    pthread_mutex_unlock(&s_peq_mutex);
}

void peq_adjust_preamp(float delta_db) {
    pthread_mutex_lock(&s_peq_mutex);
    s_preamp_db += delta_db;
    if (s_preamp_db < PEQ_MIN_GAIN_DB) s_preamp_db = PEQ_MIN_GAIN_DB;
    if (s_preamp_db > PEQ_MAX_GAIN_DB) s_preamp_db = PEQ_MAX_GAIN_DB;
    pthread_mutex_unlock(&s_peq_mutex);
}

void peq_toggle_band_enabled(int idx) {
    if (idx < 0 || idx >= PEQ_MAX_BANDS) return;
    pthread_mutex_lock(&s_peq_mutex);
    s_bands[idx].enabled = !s_bands[idx].enabled;
    compute_coefficients(&s_filters[idx], &s_bands[idx], s_sample_rate);
    pthread_mutex_unlock(&s_peq_mutex);
}

void peq_cycle_band_type(int idx) {
    if (idx < 0 || idx >= PEQ_MAX_BANDS) return;
    pthread_mutex_lock(&s_peq_mutex);
    s_bands[idx].type = (s_bands[idx].type + 1) % PEQ_FILTER_COUNT;
    compute_coefficients(&s_filters[idx], &s_bands[idx], s_sample_rate);
    pthread_mutex_unlock(&s_peq_mutex);
}

void peq_adjust_band_freq(int idx, float multiplier) {
    if (idx < 0 || idx >= PEQ_MAX_BANDS) return;
    pthread_mutex_lock(&s_peq_mutex);
    s_bands[idx].freq *= multiplier;
    if (s_bands[idx].freq < PEQ_MIN_FREQ) s_bands[idx].freq = PEQ_MIN_FREQ;
    if (s_bands[idx].freq > PEQ_MAX_FREQ) s_bands[idx].freq = PEQ_MAX_FREQ;
    compute_coefficients(&s_filters[idx], &s_bands[idx], s_sample_rate);
    pthread_mutex_unlock(&s_peq_mutex);
}

void peq_adjust_band_gain(int idx, float delta_db) {
    if (idx < 0 || idx >= PEQ_MAX_BANDS) return;
    pthread_mutex_lock(&s_peq_mutex);
    s_bands[idx].gain_db += delta_db;
    if (s_bands[idx].gain_db < PEQ_MIN_GAIN_DB) s_bands[idx].gain_db = PEQ_MIN_GAIN_DB;
    if (s_bands[idx].gain_db > PEQ_MAX_GAIN_DB) s_bands[idx].gain_db = PEQ_MAX_GAIN_DB;
    compute_coefficients(&s_filters[idx], &s_bands[idx], s_sample_rate);
    pthread_mutex_unlock(&s_peq_mutex);
}

void peq_adjust_band_q(int idx, float delta_q) {
    if (idx < 0 || idx >= PEQ_MAX_BANDS) return;
    pthread_mutex_lock(&s_peq_mutex);
    s_bands[idx].q += delta_q;
    if (s_bands[idx].q < PEQ_MIN_Q) s_bands[idx].q = PEQ_MIN_Q;
    if (s_bands[idx].q > PEQ_MAX_Q) s_bands[idx].q = PEQ_MAX_Q;
    compute_coefficients(&s_filters[idx], &s_bands[idx], s_sample_rate);
    pthread_mutex_unlock(&s_peq_mutex);
}

static const PEQPresetDef s_builtin_presets[] = {
    { "Harman Target (Over-Ear)",    "Calibrated reference curve for over-ear headphones" },
    { "Harman Target (In-Ear)",      "Punchier sub-bass shelf for IEMs" },
    { "Diffuse Field (Neutral)",     "Classical acoustic flat reference; unboosted bass" },
    { "Warm & Relaxed",              "Smooth sub-bass with gentle anti-sibilance treble" },
    { "Vocal & Acoustic Presence",   "Clean vocal articulation with sub-rumble cut" },
    { "Deep Sub-Bass (Club)",        "Sub-110Hz low-end shelf for electronic music" },
    { "Treble Air & Sparkle",        "Ultra-high presence boost for dark headphones" },
    { "Flat (Pass-through)",         "Bit-neutral 0 dB baseline across all bands" }
};

int peq_get_builtin_preset_count(void) {
    return (int)(sizeof(s_builtin_presets) / sizeof(s_builtin_presets[0]));
}

const PEQPresetDef* peq_get_builtin_preset(int idx) {
    if (idx < 0 || idx >= peq_get_builtin_preset_count()) return &s_builtin_presets[0];
    return &s_builtin_presets[idx];
}

void peq_apply_builtin_preset(int idx) {
    pthread_mutex_lock(&s_peq_mutex);
    peq_reset_locked();

    switch (idx) {
        case 0: // Harman Over-Ear
            s_preamp_db = -5.5f;
            s_bands[0] = (PEQBand){ true, PEQ_FILTER_LOW_SHELF,   105.0f, +5.5f, 0.71f };
            s_bands[1] = (PEQBand){ true, PEQ_FILTER_PEAK,        200.0f, -1.0f, 1.20f };
            s_bands[2] = (PEQBand){ true, PEQ_FILTER_PEAK,       1250.0f, -1.2f, 2.00f };
            s_bands[3] = (PEQBand){ true, PEQ_FILTER_PEAK,       3000.0f, +3.5f, 1.80f };
            s_bands[4] = (PEQBand){ true, PEQ_FILTER_PEAK,       5800.0f, -2.0f, 3.50f };
            s_bands[5] = (PEQBand){ true, PEQ_FILTER_HIGH_SHELF, 10000.0f, -1.5f, 0.71f };
            break;

        case 1: // Harman In-Ear (IEM)
            s_preamp_db = -8.0f;
            s_bands[0] = (PEQBand){ true, PEQ_FILTER_LOW_SHELF,    85.0f, +8.0f, 0.71f };
            s_bands[1] = (PEQBand){ true, PEQ_FILTER_PEAK,        250.0f, -1.8f, 1.40f };
            s_bands[2] = (PEQBand){ true, PEQ_FILTER_PEAK,       1000.0f, -1.0f, 2.00f };
            s_bands[3] = (PEQBand){ true, PEQ_FILTER_PEAK,       2800.0f, +4.5f, 2.20f };
            s_bands[4] = (PEQBand){ true, PEQ_FILTER_PEAK,       6000.0f, -3.0f, 4.00f };
            s_bands[5] = (PEQBand){ true, PEQ_FILTER_HIGH_SHELF, 10000.0f, -2.0f, 0.71f };
            break;

        case 2: // Diffuse Field (Neutral)
            s_preamp_db = -3.0f;
            s_bands[0] = (PEQBand){ true, PEQ_FILTER_HIGH_PASS,    25.0f,  0.0f, 0.71f };
            s_bands[1] = (PEQBand){ true, PEQ_FILTER_PEAK,        3000.0f, +3.0f, 1.50f };
            s_bands[2] = (PEQBand){ true, PEQ_FILTER_PEAK,        6000.0f, -2.5f, 3.00f };
            s_bands[3] = (PEQBand){ true, PEQ_FILTER_HIGH_SHELF, 12000.0f, -1.0f, 0.71f };
            break;

        case 3: // Warm & Relaxed
            s_preamp_db = -4.0f;
            s_bands[0] = (PEQBand){ true, PEQ_FILTER_LOW_SHELF,   120.0f, +4.0f, 0.71f };
            s_bands[1] = (PEQBand){ true, PEQ_FILTER_PEAK,        250.0f, +1.0f, 1.00f };
            s_bands[2] = (PEQBand){ true, PEQ_FILTER_PEAK,       3500.0f, -2.0f, 2.00f };
            s_bands[3] = (PEQBand){ true, PEQ_FILTER_PEAK,       6000.0f, -3.5f, 3.00f };
            s_bands[4] = (PEQBand){ true, PEQ_FILTER_HIGH_SHELF,  9000.0f, -2.5f, 0.71f };
            break;

        case 4: // Vocal & Acoustic Presence
            s_preamp_db = -3.5f;
            s_bands[0] = (PEQBand){ true, PEQ_FILTER_HIGH_PASS,    35.0f,  0.0f, 0.71f };
            s_bands[1] = (PEQBand){ true, PEQ_FILTER_PEAK,        200.0f, -1.5f, 1.20f };
            s_bands[2] = (PEQBand){ true, PEQ_FILTER_PEAK,       1500.0f, +2.5f, 1.40f };
            s_bands[3] = (PEQBand){ true, PEQ_FILTER_PEAK,       3200.0f, +3.0f, 1.80f };
            s_bands[4] = (PEQBand){ true, PEQ_FILTER_HIGH_SHELF, 11000.0f, +1.5f, 0.71f };
            break;

        case 5: // Deep Sub-Bass
            s_preamp_db = -7.0f;
            s_bands[0] = (PEQBand){ true, PEQ_FILTER_HIGH_PASS,    20.0f,  0.0f, 0.71f };
            s_bands[1] = (PEQBand){ true, PEQ_FILTER_LOW_SHELF,    80.0f, +7.0f, 0.85f };
            s_bands[2] = (PEQBand){ true, PEQ_FILTER_PEAK,        180.0f, -2.0f, 1.50f };
            s_bands[3] = (PEQBand){ true, PEQ_FILTER_PEAK,       4500.0f, +1.5f, 2.00f };
            break;

        case 6: // Treble Air & Sparkle
            s_preamp_db = -3.5f;
            s_bands[0] = (PEQBand){ true, PEQ_FILTER_PEAK,       4000.0f, +2.0f, 1.80f };
            s_bands[1] = (PEQBand){ true, PEQ_FILTER_HIGH_SHELF,  8500.0f, +3.5f, 0.71f };
            s_bands[2] = (PEQBand){ true, PEQ_FILTER_PEAK,      12000.0f, +2.5f, 2.50f };
            break;

        case 7: // Flat
        default:
            s_preamp_db = 0.0f;
            break;
    }

    recalculate_all_locked(s_sample_rate);
    pthread_mutex_unlock(&s_peq_mutex);
}

void peq_load_harman_target(bool in_ear) {
    peq_apply_builtin_preset(in_ear ? 1 : 0);
}

static PEQFilterType parse_filter_type(const char *type_str) {
    if (strcasecmp(type_str, "PK") == 0 || strcasecmp(type_str, "PEAK") == 0)
        return PEQ_FILTER_PEAK;
    if (strcasecmp(type_str, "LS") == 0 || strcasecmp(type_str, "LSC") == 0 || strcasecmp(type_str, "LSQ") == 0 || strcasecmp(type_str, "LOWSHELF") == 0)
        return PEQ_FILTER_LOW_SHELF;
    if (strcasecmp(type_str, "HS") == 0 || strcasecmp(type_str, "HSC") == 0 || strcasecmp(type_str, "HSQ") == 0 || strcasecmp(type_str, "HIGHSHELF") == 0)
        return PEQ_FILTER_HIGH_SHELF;
    if (strcasecmp(type_str, "HP") == 0 || strcasecmp(type_str, "HPC") == 0 || strcasecmp(type_str, "HIGHPASS") == 0)
        return PEQ_FILTER_HIGH_PASS;
    if (strcasecmp(type_str, "LP") == 0 || strcasecmp(type_str, "LPC") == 0 || strcasecmp(type_str, "LOWPASS") == 0)
        return PEQ_FILTER_LOW_PASS;
    return PEQ_FILTER_PEAK;
}

bool peq_load_file(const char *filepath) {
    if (!filepath || !filepath[0]) return false;
    FILE *fp = fopen(filepath, "r");
    if (!fp) return false;

    pthread_mutex_lock(&s_peq_mutex);
    peq_reset_locked();

    char line[512];
    int band_idx = 0;

    while (fgets(line, sizeof(line), fp)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == ';' || *p == '\r' || *p == '\n' || !*p) continue;

        if (strncasecmp(p, "Preamp:", 7) == 0) {
            float pre = 0.0f;
            if (sscanf(p + 7, "%f", &pre) == 1) {
                if (pre < PEQ_MIN_GAIN_DB) pre = PEQ_MIN_GAIN_DB;
                if (pre > PEQ_MAX_GAIN_DB) pre = PEQ_MAX_GAIN_DB;
                s_preamp_db = pre;
            }
            continue;
        }

        if (strncasecmp(p, "Filter", 6) == 0 && band_idx < PEQ_MAX_BANDS) {
            char *colon = strchr(p, ':');
            char *cursor = colon ? colon + 1 : p + 6;

            char status_str[16] = {0};
            char type_str[16] = {0};
            float freq = 1000.0f;
            float gain = 0.0f;
            float q = 0.7071f;

            // Parse both standard AutoEQ exports and raw number lines
            char *fc_pos = strcasestr(cursor, "Fc");
            char *gain_pos = strcasestr(cursor, "Gain");
            char *q_pos = strcasestr(cursor, "Q");

            sscanf(cursor, "%15s %15s", status_str, type_str);

            if (fc_pos && gain_pos && q_pos) {
                sscanf(fc_pos + 2, "%f", &freq);
                sscanf(gain_pos + 4, "%f", &gain);
                sscanf(q_pos + 1, "%f", &q);
            } else if (sscanf(cursor, "%15s %15s %f %f %f", status_str, type_str, &freq, &gain, &q) < 5) {
                continue;
            }

            bool enabled = (strcasecmp(status_str, "ON") == 0);
            PEQFilterType ftype = parse_filter_type(type_str);

            if (freq < PEQ_MIN_FREQ) freq = PEQ_MIN_FREQ;
            if (freq > PEQ_MAX_FREQ) freq = PEQ_MAX_FREQ;
            if (gain < PEQ_MIN_GAIN_DB) gain = PEQ_MIN_GAIN_DB;
            if (gain > PEQ_MAX_GAIN_DB) gain = PEQ_MAX_GAIN_DB;
            if (q < PEQ_MIN_Q) q = PEQ_MIN_Q;
            if (q > PEQ_MAX_Q) q = PEQ_MAX_Q;

            s_bands[band_idx].enabled = enabled;
            s_bands[band_idx].type = ftype;
            s_bands[band_idx].freq = freq;
            s_bands[band_idx].gain_db = gain;
            s_bands[band_idx].q = q;
            band_idx++;
        }
    }

    fclose(fp);
    recalculate_all_locked(s_sample_rate);
    pthread_mutex_unlock(&s_peq_mutex);
    return true;
}

static void ensure_peq_dir(void) {
    const char *home = getenv("HOME");
    if (!home) return;
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s/.config/koni/peq", home);
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

bool peq_save_file(const char *filepath) {
    if (!filepath || !filepath[0]) return false;
    FILE *fp = fopen(filepath, "w");
    if (!fp) return false;

    pthread_mutex_lock(&s_peq_mutex);
    fprintf(fp, "# Equalizer APO / AutoEQ Parametric EQ export\n");
    fprintf(fp, "Preamp: %.2f dB\n\n", s_preamp_db);

    for (int b = 0; b < PEQ_MAX_BANDS; b++) {
        const char *tname = peq_get_filter_name(s_bands[b].type);
        fprintf(fp, "Filter %d: %s %s Fc %.1f Hz Gain %.1f dB Q %.2f\n",
                b + 1,
                s_bands[b].enabled ? "ON" : "OFF",
                tname,
                s_bands[b].freq,
                s_bands[b].gain_db,
                s_bands[b].q);
    }
    pthread_mutex_unlock(&s_peq_mutex);
    fclose(fp);
    return true;
}

int peq_scan_user_presets(char out_names[][128], char out_paths[][1024], int max_count) {
    ensure_peq_dir();
    const char *home = getenv("HOME");
    if (!home || max_count <= 0) return 0;

    char dir_path[1024];
    snprintf(dir_path, sizeof(dir_path), "%s/.config/koni/peq", home);

    DIR *d = opendir(dir_path);
    if (!d) return 0;

    int count = 0;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && count < max_count) {
        if (ent->d_name[0] == '.') continue;
        char *ext = strrchr(ent->d_name, '.');
        if (ext && (strcasecmp(ext, ".txt") == 0 || strcasecmp(ext, ".peq") == 0)) {
            size_t name_len = ext - ent->d_name;
            if (name_len >= 128) name_len = 127;
            strncpy(out_names[count], ent->d_name, name_len);
            out_names[count][name_len] = '\0';
            snprintf(out_paths[count], 1024, "%s/%s", dir_path, ent->d_name);
            count++;
        }
    }
    closedir(d);
    return count;
}

void peq_save_state(void *file_ptr) {
    FILE *f = (FILE*)file_ptr;
    if (!f) return;
    pthread_mutex_lock(&s_peq_mutex);
    fprintf(f, "peq_preamp=%.2f\n", s_preamp_db);
    for (int i = 0; i < PEQ_MAX_BANDS; i++) {
        fprintf(f, "peq_b%d=%d,%d,%.1f,%.2f,%.2f\n",
                i, s_bands[i].enabled ? 1 : 0, (int)s_bands[i].type,
                s_bands[i].freq, s_bands[i].gain_db, s_bands[i].q);
    }
    pthread_mutex_unlock(&s_peq_mutex);
}

void peq_load_state_key(const char *key, const char *val) {
    if (!key || !val) return;
    pthread_mutex_lock(&s_peq_mutex);
    if (strcmp(key, "peq_preamp") == 0) {
        s_preamp_db = (float)atof(val);
    } else if (strncmp(key, "peq_b", 5) == 0) {
        int idx = atoi(key + 5);
        if (idx >= 0 && idx < PEQ_MAX_BANDS) {
            int en = 1, type = 0;
            float freq = 1000.0f, gain = 0.0f, q = 0.7071f;
            if (sscanf(val, "%d,%d,%f,%f,%f", &en, &type, &freq, &gain, &q) == 5) {
                s_bands[idx].enabled = (en != 0);
                s_bands[idx].type = (PEQFilterType)type;
                s_bands[idx].freq = freq;
                s_bands[idx].gain_db = gain;
                s_bands[idx].q = q;
                compute_coefficients(&s_filters[idx], &s_bands[idx], s_sample_rate);
            }
        }
    }
    pthread_mutex_unlock(&s_peq_mutex);
}

#define PEQ_SUB_BLOCK 32

void peq_process_float(float *samples_interleaved, uint32_t num_frames, uint16_t num_channels, uint32_t sample_rate) {
    if (!samples_interleaved || num_frames == 0 || num_channels == 0) return;

    pthread_mutex_lock(&s_peq_mutex);
    if (sample_rate != s_sample_rate) {
        recalculate_all_locked(sample_rate);
    }

    uint16_t channels = (num_channels <= PEQ_MAX_CHANNELS) ? num_channels : PEQ_MAX_CHANNELS;
    float target_preamp = powf(10.0f, s_preamp_db / 20.0f);
    float smooth_alpha = 1.0f - expf(-((float)PEQ_SUB_BLOCK) / ((float)sample_rate * 0.020f));

    uint32_t frames_processed = 0;
    while (frames_processed < num_frames) {
        uint32_t block_frames = num_frames - frames_processed;
        if (block_frames > PEQ_SUB_BLOCK) block_frames = PEQ_SUB_BLOCK;

        s_current_preamp_mult += smooth_alpha * (target_preamp - s_current_preamp_mult);

        for (uint32_t f = 0; f < block_frames; f++) {
            uint32_t base = (frames_processed + f) * num_channels;
            for (uint16_t c = 0; c < channels; c++) {
                double x = (double)(samples_interleaved[base + c] * s_current_preamp_mult);

                for (int b = 0; b < PEQ_MAX_BANDS; b++) {
                    if (!s_bands[b].enabled) continue;
                    PEQBiquad *filter = &s_filters[b];
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
    pthread_mutex_unlock(&s_peq_mutex);
}

float peq_calculate_gain_at(float freq_hz) {
    pthread_mutex_lock(&s_peq_mutex);
    float total_db = s_preamp_db;
    float fs = (float)s_sample_rate;
    if (fs <= 0.0f) fs = 44100.0f;

    if (freq_hz < PEQ_MIN_FREQ) freq_hz = PEQ_MIN_FREQ;
    if (freq_hz >= fs * 0.49f) freq_hz = fs * 0.49f;

    float w = 2.0f * (float)M_PI * (freq_hz / fs);
    float cos_w = cosf(w);
    float cos_2w = cosf(2.0f * w);
    float sin_w = sinf(w);
    float sin_2w = sinf(2.0f * w);

    for (int b = 0; b < PEQ_MAX_BANDS; b++) {
        if (!s_bands[b].enabled) continue;
        PEQBiquad *bq = &s_filters[b];

        float num_re = bq->b0 + bq->b1 * cos_w + bq->b2 * cos_2w;
        float num_im = -(bq->b1 * sin_w + bq->b2 * sin_2w);
        float den_re = 1.0f + bq->a1 * cos_w + bq->a2 * cos_2w;
        float den_im = -(bq->a1 * sin_w + bq->a2 * sin_2w);

        float num_sq = num_re * num_re + num_im * num_im;
        float den_sq = den_re * den_re + den_im * den_im;

        if (den_sq > 1e-12f && num_sq > 1e-12f) {
            total_db += 10.0f * log10f(num_sq / den_sq);
        }
    }
    pthread_mutex_unlock(&s_peq_mutex);
    return total_db;
}

void peq_calculate_curve(float *out_db, int count, float min_freq, float max_freq) {
    if (!out_db || count <= 0) return;

    pthread_mutex_lock(&s_peq_mutex);
    float preamp = s_preamp_db;
    float fs = (float)s_sample_rate;
    if (fs <= 0.0f) fs = 44100.0f;

    PEQBiquad bqs[PEQ_MAX_BANDS];
    int active_cnt = 0;
    for (int b = 0; b < PEQ_MAX_BANDS; b++) {
        if (s_bands[b].enabled) {
            bqs[active_cnt++] = s_filters[b];
        }
    }
    pthread_mutex_unlock(&s_peq_mutex);

    float log_ratio = log10f(max_freq / min_freq);

    for (int i = 0; i < count; i++) {
        float f = (count > 1) ? (min_freq * powf(10.0f, ((float)i / (float)(count - 1)) * log_ratio)) : min_freq;
        if (f < PEQ_MIN_FREQ) f = PEQ_MIN_FREQ;
        if (f >= fs * 0.49f) f = fs * 0.49f;

        float w = 2.0f * (float)M_PI * (f / fs);
        float cos_w = cosf(w);
        float cos_2w = cosf(2.0f * w);
        float sin_w = sinf(w);
        float sin_2w = sinf(2.0f * w);

        float total = preamp;
        for (int b = 0; b < active_cnt; b++) {
            PEQBiquad *bq = &bqs[b];
            float num_re = bq->b0 + bq->b1 * cos_w + bq->b2 * cos_2w;
            float num_im = -(bq->b1 * sin_w + bq->b2 * sin_2w);
            float den_re = 1.0f + bq->a1 * cos_w + bq->a2 * cos_2w;
            float den_im = -(bq->a1 * sin_w + bq->a2 * sin_2w);

            float num_sq = num_re * num_re + num_im * num_im;
            float den_sq = den_re * den_re + den_im * den_im;

            if (den_sq > 1e-12f && num_sq > 1e-12f) {
                total += 10.0f * log10f(num_sq / den_sq);
            }
        }
        out_db[i] = total;
    }
}