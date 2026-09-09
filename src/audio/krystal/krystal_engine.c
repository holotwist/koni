#define _DEFAULT_SOURCE
#include "krystal_engine.h"
#include "krystal_loudness.h"
#include "krystal_bass.h"
#include "krystal_spatial.h"
#include "krystal_exciter.h"
#include "krystal_saturator.h"
#include "krystal_spectral.h"
#include "krystal_transient.h"
#include "krystal_meter.h"
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static pthread_mutex_t s_krystal_mutex = PTHREAD_MUTEX_INITIALIZER;
static KrystalConfig s_active_cfg;
static KrystalTelemetry s_telemetry;
static uint32_t s_sample_rate = 44100;

static KrystalLoudnessState s_loudness;
static KrystalBassState s_bass;
static KrystalSpatialState s_spatial;
static KrystalExciterState s_exciter;
static KrystalSaturatorState s_saturator;
static KrystalSpectralState s_spectral;
static KrystalTransientState s_transient;
static KrystalMeterState s_meter;
static float *s_dry_buffer = NULL;
static size_t s_dry_buffer_cap = 0;

void krystal_init(uint32_t sample_rate) {
    pthread_mutex_lock(&s_krystal_mutex);
    s_sample_rate = sample_rate ? sample_rate : 44100;
    s_active_cfg = *krystal_get_preset_config(KRYSTAL_PROFILE_HEADPHONES_REF);
    memset(&s_telemetry, 0, sizeof(s_telemetry));
    s_telemetry.phase_correlation = 1.0f;

    krystal_bass_init(&s_bass, s_sample_rate);
    krystal_spatial_init(&s_spatial, s_sample_rate);
    krystal_exciter_init(&s_exciter, s_sample_rate);
    krystal_saturator_init(&s_saturator, s_sample_rate);
    krystal_spectral_init(&s_spectral, s_sample_rate);
    krystal_transient_init(&s_transient, s_sample_rate);
    krystal_meter_init(&s_meter, s_sample_rate);
    pthread_mutex_unlock(&s_krystal_mutex);
}

void krystal_reset(void) {
    pthread_mutex_lock(&s_krystal_mutex);
    krystal_bass_reset(&s_bass);
    krystal_spatial_reset(&s_spatial);
    krystal_exciter_reset(&s_exciter);
    krystal_saturator_reset(&s_saturator);
    krystal_spectral_reset(&s_spectral);
    krystal_transient_reset(&s_transient);
    krystal_meter_reset(&s_meter);
    memset(&s_telemetry, 0, sizeof(s_telemetry));
    s_telemetry.phase_correlation = 1.0f;
    pthread_mutex_unlock(&s_krystal_mutex);
}

bool krystal_is_enabled(void) {
    pthread_mutex_lock(&s_krystal_mutex);
    bool en = s_active_cfg.master_enabled;
    pthread_mutex_unlock(&s_krystal_mutex);
    return en;
}

void krystal_set_enabled(bool enabled) {
    pthread_mutex_lock(&s_krystal_mutex);
    s_active_cfg.master_enabled = enabled;
    pthread_mutex_unlock(&s_krystal_mutex);
}

void krystal_toggle_enabled(void) {
    pthread_mutex_lock(&s_krystal_mutex);
    s_active_cfg.master_enabled = !s_active_cfg.master_enabled;
    pthread_mutex_unlock(&s_krystal_mutex);
}

void krystal_get_config(KrystalConfig *out_cfg) {
    if (!out_cfg) return;
    pthread_mutex_lock(&s_krystal_mutex);
    *out_cfg = s_active_cfg;
    pthread_mutex_unlock(&s_krystal_mutex);
}

void krystal_set_config(const KrystalConfig *cfg) {
    if (!cfg) return;
    pthread_mutex_lock(&s_krystal_mutex);
    s_active_cfg = *cfg;
    s_active_cfg.active_profile = -1; // Flag as Custom
    pthread_mutex_unlock(&s_krystal_mutex);
}

void krystal_apply_profile(int profile_idx) {
    if (profile_idx < 0 || profile_idx >= krystal_get_profile_count()) return;
    pthread_mutex_lock(&s_krystal_mutex);
    s_active_cfg = *krystal_get_preset_config(profile_idx);
    pthread_mutex_unlock(&s_krystal_mutex);
}

void krystal_cycle_profile(void) {
    pthread_mutex_lock(&s_krystal_mutex);
    int next = (s_active_cfg.active_profile + 1) % krystal_get_profile_count();
    s_active_cfg = *krystal_get_preset_config(next);
    pthread_mutex_unlock(&s_krystal_mutex);
}

void krystal_get_telemetry(KrystalTelemetry *out_telem) {
    if (!out_telem) return;
    pthread_mutex_lock(&s_krystal_mutex);
    *out_telem = s_telemetry;
    pthread_mutex_unlock(&s_krystal_mutex);
}

void krystal_process(float *samples_interleaved, uint32_t num_frames, uint16_t num_channels, uint32_t sample_rate, int volume_percent) {
    if (!samples_interleaved || num_frames == 0 || num_channels == 0) return;

    pthread_mutex_lock(&s_krystal_mutex);
    if (!s_active_cfg.master_enabled) {
        pthread_mutex_unlock(&s_krystal_mutex);
        return;
    }

    if (sample_rate != s_sample_rate) {
        s_sample_rate = sample_rate;
        krystal_loudness_init(&s_loudness, sample_rate);
        krystal_bass_init(&s_bass, sample_rate);
        krystal_spatial_init(&s_spatial, sample_rate);
        krystal_exciter_init(&s_exciter, sample_rate);
    }

    KrystalConfig cfg = s_active_cfg;
    pthread_mutex_unlock(&s_krystal_mutex);

    size_t total_samples = (size_t)num_frames * (size_t)num_channels;
    if (total_samples > s_dry_buffer_cap) {
        free(s_dry_buffer);
        s_dry_buffer = malloc(total_samples * sizeof(float));
        s_dry_buffer_cap = s_dry_buffer ? total_samples : 0;
    }
    if (s_dry_buffer) {
        memcpy(s_dry_buffer, samples_interleaved, total_samples * sizeof(float));
    }

    // Peak de-clipping
    if (cfg.transient.declip_enable) {
        krystal_declip_process(samples_interleaved, num_frames, num_channels, cfg.transient.declip_thresh);
    }

    // Stage 0, General signal pre-processing
    float pre_mult = powf(10.0f, cfg.general.pre_gain_db / 20.0f);
    float bal = cfg.general.balance;
    float bal_l = (bal > 0.0f) ? (1.0f - bal) : 1.0f;
    float bal_r = (bal < 0.0f) ? (1.0f + bal) : 1.0f;

    for (uint32_t f = 0; f < num_frames; f++) {
        uint32_t idx = f * num_channels;
        float l = samples_interleaved[idx] * pre_mult;
        float r = (num_channels > 1) ? (samples_interleaved[idx + 1] * pre_mult) : l;

        // Polarity inversion
        if (cfg.general.polarity == POLARITY_INVERT_L || cfg.general.polarity == POLARITY_INVERT_BOTH) l = -l;
        if (cfg.general.polarity == POLARITY_INVERT_R || cfg.general.polarity == POLARITY_INVERT_BOTH) r = -r;

        // Channel routing matrix
        if (num_channels > 1) {
            switch (cfg.general.mode) {
                case CHAN_MODE_SWAP: {
                    float tmp = l; l = r; r = tmp;
                    break;
                }
                case CHAN_MODE_MONO_SUM: {
                    float m = (l + r) * 0.5f;
                    l = r = m;
                    break;
                }
                case CHAN_MODE_SIDE_SOLO: {
                    float side = (l - r) * 0.5f;
                    l = side; r = -side;
                    break;
                }
                case CHAN_MODE_LEFT_ONLY:
                    r = l;
                    break;
                case CHAN_MODE_RIGHT_ONLY:
                    l = r;
                    break;
                default:
                    break;
            }
            l *= bal_l;
            r *= bal_r;
            samples_interleaved[idx] = l;
            samples_interleaved[idx + 1] = r;
        } else {
            samples_interleaved[idx] = l;
        }
    }

    // Input RMS calculation for auto-gain trim
    float in_sum_sq = 0.0f;
    float in_peak = 0.0f;
    for (size_t i = 0; i < total_samples; i++) {
        float abs_s = fabsf(samples_interleaved[i]);
        if (abs_s > in_peak) in_peak = abs_s;
        in_sum_sq += samples_interleaved[i] * samples_interleaved[i];
    }
    float in_rms = sqrtf(in_sum_sq / (float)total_samples);
    float crest_db = (in_rms > 1e-5f) ? (20.0f * log10f((in_peak + 1e-6f) / in_rms)) : 0.0f;
    if (crest_db < 0.0f) crest_db = 0.0f;

    // Fletcher-Munson dynamic loudness
    float l_bass_db = 0.0f, l_treb_db = 0.0f;
    if (cfg.loudness.enabled) {
        krystal_loudness_process(&s_loudness, samples_interleaved, num_frames, num_channels,
                                 &cfg.loudness, sample_rate, volume_percent, &l_bass_db, &l_treb_db);
    }

    // 3D Spatial and crossfeed
    float m_eng = 0.0f, s_eng = 0.0f, phase_corr = 1.0f;
    bool clamped = false;
    if (cfg.spatial.enabled && num_channels >= 2) {
        krystal_spatial_process(&s_spatial, samples_interleaved, num_frames, &cfg.spatial, sample_rate,
                                &m_eng, &s_eng, &phase_corr, &clamped);
    }

    // Dynamic spectral balancing
    float harsh_cut = 0.0f, boom_cut = 0.0f;
    if (cfg.spectral.enabled) {
        krystal_spectral_process(&s_spectral, samples_interleaved, num_frames, num_channels,
                                 &cfg.spectral, sample_rate, &harsh_cut, &boom_cut);
    }

    // Transient shaping
    float trans_eng = 0.0f;
    if (cfg.transient.enabled) {
        trans_eng = krystal_transient_process(&s_transient, samples_interleaved, num_frames,
                                              num_channels, &cfg.transient, sample_rate);
    }

    // Psychoacoustic bass
    float bass_inj = 0.0f, sub_eng = 0.0f, oct_eng = 0.0f;
    if (cfg.bass.enabled) {
        bass_inj = krystal_bass_process(&s_bass, samples_interleaved, num_frames, num_channels,
                                        &cfg.bass, sample_rate, &sub_eng, &oct_eng);
    }

    // Spectral exciter
    float exciter_inj = 0.0f;
    if (cfg.exciter.enabled) {
        exciter_inj = krystal_exciter_process(&s_exciter, samples_interleaved, num_frames, num_channels, &cfg.exciter, sample_rate);
    }

    // Color saturation
    float sat_eng = 0.0f;
    if (cfg.saturator.enabled) {
        sat_eng = krystal_saturator_process(&s_saturator, samples_interleaved, num_frames, num_channels, &cfg.saturator, sample_rate);
    }

    // Auto-gain trim
    float auto_trim = 1.0f;
    float trim_db = 0.0f;
    if (cfg.general.auto_gain && in_rms > 0.001f) {
        float out_sum_sq = 0.0f;
        for (size_t i = 0; i < total_samples; i++) {
            out_sum_sq += samples_interleaved[i] * samples_interleaved[i];
        }
        float out_rms = sqrtf(out_sum_sq / (float)total_samples);
        if (out_rms > in_rms) {
            auto_trim = in_rms / out_rms;
            trim_db = 20.0f * log10f(auto_trim);
        }
    }

    // Master Dry/Wet mix & DAC headroom guard
    float wet = cfg.general.master_mix;
    float dry = 1.0f - wet;
    float headroom_mult = powf(10.0f, cfg.general.headroom_db / 20.0f);

    if (s_dry_buffer && wet < 0.999f) {
        for (size_t i = 0; i < total_samples; i++) {
            samples_interleaved[i] = ((s_dry_buffer[i] * dry) + (samples_interleaved[i] * auto_trim * wet)) * headroom_mult;
        }
    } else {
        float final_scale = auto_trim * headroom_mult;
        for (size_t i = 0; i < total_samples; i++) {
            samples_interleaved[i] *= final_scale;
        }
    }

    float tilt = (sub_eng > 1e-5f && exciter_inj > 1e-5f) ? (sub_eng / exciter_inj) : 1.0f;

    float lufs = -70.0f, peak_db = -70.0f;
    krystal_meter_process(&s_meter, samples_interleaved, num_frames, num_channels, sample_rate, &lufs, &peak_db);

    pthread_mutex_lock(&s_krystal_mutex);
    s_telemetry.sub_energy = sub_eng;
    s_telemetry.bass_injected_energy = bass_inj;
    s_telemetry.mid_energy = m_eng;
    s_telemetry.side_energy = s_eng;
    s_telemetry.phase_correlation = phase_corr;
    s_telemetry.safety_clamp_active = clamped;
    s_telemetry.exciter_injected_energy = exciter_inj;
    s_telemetry.sat_energy = sat_eng;
    s_telemetry.loudness_bass_db = l_bass_db;
    s_telemetry.loudness_treb_db = l_treb_db;
    s_telemetry.auto_trim_db = trim_db;
    s_telemetry.crest_factor_db = crest_db;
    s_telemetry.spectral_tilt = tilt;
    s_telemetry.de_harsh_cut_db = harsh_cut;
    s_telemetry.de_boom_cut_db = boom_cut;
    s_telemetry.transient_energy = trans_eng;
    s_telemetry.sub_octave_energy = oct_eng;
    s_telemetry.lufs_momentary = lufs;
    s_telemetry.peak_dbfs = peak_db;
    pthread_mutex_unlock(&s_krystal_mutex);
}

void krystal_save_state(void *file_ptr) {
    FILE *f = (FILE*)file_ptr;
    if (!f) return;
    pthread_mutex_lock(&s_krystal_mutex);
    fprintf(f, "krystal_master=%d\n", s_active_cfg.master_enabled ? 1 : 0);
    fprintf(f, "krystal_profile=%d\n", s_active_cfg.active_profile);
    fprintf(f, "krystal_gen_pre=%.1f\n", s_active_cfg.general.pre_gain_db);
    fprintf(f, "krystal_gen_bal=%.2f\n", s_active_cfg.general.balance);
    fprintf(f, "krystal_gen_mode=%d\n", (int)s_active_cfg.general.mode);
    fprintf(f, "krystal_gen_pol=%d\n", (int)s_active_cfg.general.polarity);
    fprintf(f, "krystal_mix=%.2f\n", s_active_cfg.general.master_mix);
    fprintf(f, "krystal_autogain=%d\n", s_active_cfg.general.auto_gain ? 1 : 0);
    fprintf(f, "krystal_gen_head=%.1f\n", s_active_cfg.general.headroom_db);

    fprintf(f, "krystal_loud_en=%d\n", s_active_cfg.loudness.enabled ? 1 : 0);
    fprintf(f, "krystal_loud_mode=%d\n", (int)s_active_cfg.loudness.mode);
    fprintf(f, "krystal_loud_int=%.2f\n", s_active_cfg.loudness.intensity);
    fprintf(f, "krystal_loud_ref=%.2f\n", s_active_cfg.loudness.ref_vol);

    fprintf(f, "krystal_bass_en=%d\n", s_active_cfg.bass.enabled ? 1 : 0);
    fprintf(f, "krystal_bass_cut=%.1f\n", s_active_cfg.bass.cutoff_hz);
    fprintf(f, "krystal_bass_int=%.2f\n", s_active_cfg.bass.intensity);
    fprintf(f, "krystal_bass_mix=%.2f\n", s_active_cfg.bass.mix);
    fprintf(f, "krystal_bass_sub=%.2f\n", s_active_cfg.bass.sub_weight);
    fprintf(f, "krystal_bass_tone=%.2f\n", s_active_cfg.bass.harmonic_tone);
    fprintf(f, "krystal_bass_rumble=%.1f\n", s_active_cfg.bass.rumble_hz);
    fprintf(f, "krystal_bass_oct=%.2f\n", s_active_cfg.bass.sub_octave);
    fprintf(f, "krystal_bass_phase=%.1f\n", s_active_cfg.bass.sub_phase_deg);

    fprintf(f, "krystal_spat_en=%d\n", s_active_cfg.spatial.enabled ? 1 : 0);
    fprintf(f, "krystal_spat_az=%.1f\n", s_active_cfg.spatial.azimuth_deg);
    fprintf(f, "krystal_spat_el=%.1f\n", s_active_cfg.spatial.elevation_deg);
    fprintf(f, "krystal_spat_dist=%.2f\n", s_active_cfg.spatial.distance_m);
    fprintf(f, "krystal_spat_angle=%.1f\n", s_active_cfg.spatial.stage_angle_deg);
    fprintf(f, "krystal_spat_refl=%.2f\n", s_active_cfg.spatial.room_refl);
    fprintf(f, "krystal_spat_cntr=%.2f\n", s_active_cfg.spatial.center_gain_db);
    fprintf(f, "krystal_spat_cut=%.1f\n", s_active_cfg.spatial.mono_cut_hz);
    fprintf(f, "krystal_spat_safe=%.2f\n", s_active_cfg.spatial.safety_limit);

    fprintf(f, "krystal_exct_en=%d\n", s_active_cfg.exciter.enabled ? 1 : 0);
    fprintf(f, "krystal_exct_cut=%.1f\n", s_active_cfg.exciter.cutoff_hz);
    fprintf(f, "krystal_exct_drv=%.2f\n", s_active_cfg.exciter.drive);
    fprintf(f, "krystal_exct_mix=%.2f\n", s_active_cfg.exciter.mix);
    fprintf(f, "krystal_exct_shim=%.2f\n", s_active_cfg.exciter.shimmer);
    fprintf(f, "krystal_exct_att=%.2f\n", s_active_cfg.exciter.attack_ms);

    fprintf(f, "krystal_sat_en=%d\n", s_active_cfg.saturator.enabled ? 1 : 0);
    fprintf(f, "krystal_sat_mode=%d\n", (int)s_active_cfg.saturator.mode);
    fprintf(f, "krystal_sat_drv=%.2f\n", s_active_cfg.saturator.drive);
    fprintf(f, "krystal_sat_bias=%.2f\n", s_active_cfg.saturator.bias);
    fprintf(f, "krystal_sat_tone=%.2f\n", s_active_cfg.saturator.tone);
    fprintf(f, "krystal_sat_mix=%.2f\n", s_active_cfg.saturator.mix);
    fprintf(f, "krystal_sat_os=%d\n", s_active_cfg.saturator.oversample ? 1 : 0);

    fprintf(f, "krystal_spec_en=%d\n", s_active_cfg.spectral.enabled ? 1 : 0);
    fprintf(f, "krystal_spec_tilt=%.2f\n", s_active_cfg.spectral.tilt_db);
    fprintf(f, "krystal_spec_harsh=%.2f\n", s_active_cfg.spectral.de_harsh);
    fprintf(f, "krystal_spec_boom=%.2f\n", s_active_cfg.spectral.de_boom);
    fprintf(f, "krystal_spec_hfreq=%.1f\n", s_active_cfg.spectral.harsh_freq);
    fprintf(f, "krystal_spec_bfreq=%.1f\n", s_active_cfg.spectral.boom_freq);

    fprintf(f, "krystal_tran_en=%d\n", s_active_cfg.transient.enabled ? 1 : 0);
    fprintf(f, "krystal_tran_att=%.2f\n", s_active_cfg.transient.attack);
    fprintf(f, "krystal_tran_sus=%.2f\n", s_active_cfg.transient.sustain);
    fprintf(f, "krystal_tran_mix=%.2f\n", s_active_cfg.transient.mix);
    fprintf(f, "krystal_tran_decl=%d\n", s_active_cfg.transient.declip_enable ? 1 : 0);
    fprintf(f, "krystal_tran_dthr=%.3f\n", s_active_cfg.transient.declip_thresh);

    pthread_mutex_unlock(&s_krystal_mutex);
}

void krystal_load_state_key(const char *key, const char *val) {
    if (!key || !val) return;
    pthread_mutex_lock(&s_krystal_mutex);
    if (strcmp(key, "krystal_master") == 0) s_active_cfg.master_enabled = (atoi(val) != 0);
    else if (strcmp(key, "krystal_profile") == 0) s_active_cfg.active_profile = atoi(val);
    else if (strcmp(key, "krystal_gen_pre") == 0) s_active_cfg.general.pre_gain_db = (float)atof(val);
    else if (strcmp(key, "krystal_gen_bal") == 0) s_active_cfg.general.balance = (float)atof(val);
    else if (strcmp(key, "krystal_gen_mode") == 0) s_active_cfg.general.mode = (KrystalChannelMode)atoi(val);
    else if (strcmp(key, "krystal_gen_pol") == 0) s_active_cfg.general.polarity = (KrystalPolarity)atoi(val);
    else if (strcmp(key, "krystal_mix") == 0) s_active_cfg.general.master_mix = (float)atof(val);
    else if (strcmp(key, "krystal_autogain") == 0) s_active_cfg.general.auto_gain = (atoi(val) != 0);
    else if (strcmp(key, "krystal_gen_head") == 0) s_active_cfg.general.headroom_db = (float)atof(val);

    else if (strcmp(key, "krystal_loud_en") == 0) s_active_cfg.loudness.enabled = (atoi(val) != 0);
    else if (strcmp(key, "krystal_loud_mode") == 0) s_active_cfg.loudness.mode = (KrystalLoudnessMode)atoi(val);
    else if (strcmp(key, "krystal_loud_int") == 0) s_active_cfg.loudness.intensity = (float)atof(val);
    else if (strcmp(key, "krystal_loud_ref") == 0) s_active_cfg.loudness.ref_vol = (float)atof(val);

    else if (strcmp(key, "krystal_bass_en") == 0) s_active_cfg.bass.enabled = (atoi(val) != 0);
    else if (strcmp(key, "krystal_bass_cut") == 0) s_active_cfg.bass.cutoff_hz = (float)atof(val);
    else if (strcmp(key, "krystal_bass_int") == 0) s_active_cfg.bass.intensity = (float)atof(val);
    else if (strcmp(key, "krystal_bass_mix") == 0) s_active_cfg.bass.mix = (float)atof(val);
    else if (strcmp(key, "krystal_bass_sub") == 0) s_active_cfg.bass.sub_weight = (float)atof(val);
    else if (strcmp(key, "krystal_bass_tone") == 0) s_active_cfg.bass.harmonic_tone = (float)atof(val);
    else if (strcmp(key, "krystal_bass_rumble") == 0) s_active_cfg.bass.rumble_hz = (float)atof(val);
    else if (strcmp(key, "krystal_bass_oct") == 0) s_active_cfg.bass.sub_octave = (float)atof(val);
    else if (strcmp(key, "krystal_bass_phase") == 0) s_active_cfg.bass.sub_phase_deg = (float)atof(val);

    else if (strcmp(key, "krystal_spat_en") == 0) s_active_cfg.spatial.enabled = (atoi(val) != 0);
    else if (strcmp(key, "krystal_spat_az") == 0) s_active_cfg.spatial.azimuth_deg = (float)atof(val);
    else if (strcmp(key, "krystal_spat_el") == 0) s_active_cfg.spatial.elevation_deg = (float)atof(val);
    else if (strcmp(key, "krystal_spat_dist") == 0) s_active_cfg.spatial.distance_m = (float)atof(val);
    else if (strcmp(key, "krystal_spat_angle") == 0) s_active_cfg.spatial.stage_angle_deg = (float)atof(val);
    else if (strcmp(key, "krystal_spat_refl") == 0) s_active_cfg.spatial.room_refl = (float)atof(val);
    else if (strcmp(key, "krystal_spat_cntr") == 0) s_active_cfg.spatial.center_gain_db = (float)atof(val);
    else if (strcmp(key, "krystal_spat_cut") == 0) s_active_cfg.spatial.mono_cut_hz = (float)atof(val);
    else if (strcmp(key, "krystal_spat_safe") == 0) s_active_cfg.spatial.safety_limit = (float)atof(val);

    else if (strcmp(key, "krystal_exct_en") == 0) s_active_cfg.exciter.enabled = (atoi(val) != 0);
    else if (strcmp(key, "krystal_exct_cut") == 0) s_active_cfg.exciter.cutoff_hz = (float)atof(val);
    else if (strcmp(key, "krystal_exct_drv") == 0) s_active_cfg.exciter.drive = (float)atof(val);
    else if (strcmp(key, "krystal_exct_mix") == 0) s_active_cfg.exciter.mix = (float)atof(val);
    else if (strcmp(key, "krystal_exct_shim") == 0) s_active_cfg.exciter.shimmer = (float)atof(val);
    else if (strcmp(key, "krystal_exct_att") == 0) s_active_cfg.exciter.attack_ms = (float)atof(val);

    else if (strcmp(key, "krystal_sat_en") == 0) s_active_cfg.saturator.enabled = (atoi(val) != 0);
    else if (strcmp(key, "krystal_sat_mode") == 0) s_active_cfg.saturator.mode = (KrystalSatMode)atoi(val);
    else if (strcmp(key, "krystal_sat_drv") == 0) s_active_cfg.saturator.drive = (float)atof(val);
    else if (strcmp(key, "krystal_sat_bias") == 0) s_active_cfg.saturator.bias = (float)atof(val);
    else if (strcmp(key, "krystal_sat_tone") == 0) s_active_cfg.saturator.tone = (float)atof(val);
    else if (strcmp(key, "krystal_sat_mix") == 0) s_active_cfg.saturator.mix = (float)atof(val);
    else if (strcmp(key, "krystal_sat_os") == 0) s_active_cfg.saturator.oversample = (atoi(val) != 0);

    else if (strcmp(key, "krystal_spec_en") == 0) s_active_cfg.spectral.enabled = (atoi(val) != 0);
    else if (strcmp(key, "krystal_spec_tilt") == 0) s_active_cfg.spectral.tilt_db = (float)atof(val);
    else if (strcmp(key, "krystal_spec_harsh") == 0) s_active_cfg.spectral.de_harsh = (float)atof(val);
    else if (strcmp(key, "krystal_spec_boom") == 0) s_active_cfg.spectral.de_boom = (float)atof(val);
    else if (strcmp(key, "krystal_spec_hfreq") == 0) s_active_cfg.spectral.harsh_freq = (float)atof(val);
    else if (strcmp(key, "krystal_spec_bfreq") == 0) s_active_cfg.spectral.boom_freq = (float)atof(val);

    else if (strcmp(key, "krystal_tran_en") == 0) s_active_cfg.transient.enabled = (atoi(val) != 0);
    else if (strcmp(key, "krystal_tran_att") == 0) s_active_cfg.transient.attack = (float)atof(val);
    else if (strcmp(key, "krystal_tran_sus") == 0) s_active_cfg.transient.sustain = (float)atof(val);
    else if (strcmp(key, "krystal_tran_mix") == 0) s_active_cfg.transient.mix = (float)atof(val);
    else if (strcmp(key, "krystal_tran_decl") == 0) s_active_cfg.transient.declip_enable = (atoi(val) != 0);
    else if (strcmp(key, "krystal_tran_dthr") == 0) s_active_cfg.transient.declip_thresh = (float)atof(val);
    
    pthread_mutex_unlock(&s_krystal_mutex);
}