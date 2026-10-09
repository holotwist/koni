#define _DEFAULT_SOURCE
#include "dancer_physics.h"
#include "state.h"
#include "vis_math.h"
#include "codec.h"
#include <complex.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct {
    float val;
    float vel;
    float tension;
    float damping;
} Spring;

static inline void spring_update(Spring *s, float target, float dt) {
    float force = -s->tension * (s->val - target) - s->damping * s->vel;
    s->vel += force * dt;
    s->val += s->vel * dt;
}

static Spring s_spr_body_y = {0.0f, 0.0f, 300.0f, 18.0f};
static Spring s_spr_squash = {1.0f, 0.0f, 250.0f, 14.0f};
static Spring s_spr_sway   = {0.0f, 0.0f, 150.0f, 12.0f};
static Spring s_spr_hair_l = {0.0f, 0.0f, 50.0f, 3.5f};
static Spring s_spr_hair_r = {0.0f, 0.0f, 50.0f, 3.5f};
static Spring s_spr_arm_l  = {0.0f, 0.0f, 180.0f, 12.0f};
static Spring s_spr_arm_r  = {0.0f, 0.0f, 180.0f, 12.0f};
static Spring s_spr_face_y = {0.0f, 0.0f, 240.0f, 22.0f};
static Spring s_spr_ahoge  = {0.0f, 0.0f, 220.0f, 11.0f};
static Spring s_spr_spin   = {0.0f, 0.0f, 300.0f, 14.0f};

static float s_dance_side = 1.0f;
static float s_face_timer = 0.0f;
static float s_face_cooldown = 0.0f;
static float s_spin_cooldown = 0.0f;
static float s_spin_target = 0.0f;
static float s_headbang_blend = 0.0f;
static float s_prev_bass = 0.0f;
static float s_prev_high = 0.0f;
static float s_rapid_hit_energy = 0.0f;
static float s_time_since_beat = 0.0f;
static float s_time_since_nod = 1.0f;
static int   s_beat_count = 0;
static float s_idle_time = 0.0f;
static bool  s_was_drop = false;

static float s_smooth_l_angle = 0.0f;
static float s_smooth_r_angle = 0.0f;
static float s_smooth_l_lift  = 0.0f;
static float s_smooth_r_lift  = 0.0f;
static float s_smooth_l_scale_y = 1.0f;
static float s_smooth_r_scale_y = 1.0f;
static float s_jump_tuck_side = 1.0f;
static int   s_jump_type = 0;

static float s_phrase_energy = 0.35f;
static float s_baseline_energy = 0.35f;
static float s_smoothed_energy = 0.35f;
static bool  s_is_climax = false;

// FFT and ballistic smoothing
static float s_smooth_bins[32] = {0};
static float s_peak_tracker = 0.25f;

// Autocorrelation BPM estimation
#define DANCER_FLUX_BINS 160
static float s_flux_history[DANCER_FLUX_BINS] = {0};
static int   s_flux_idx = 0;
static float s_flux_timer = 0.0f;
static float s_estimated_bpm = 120.0f;
static float s_smoothed_bpm = 120.0f;
static char  s_last_path[1024] = {0};

typedef enum {
    DANCE_IDLE,
    DANCE_GROOVE_RIGHT,
    DANCE_GROOVE_LEFT,
    DANCE_DOUBLE_HIP,
    DANCE_SWAY_LOW,
    DANCE_HYPE
} DanceMode;

static DanceMode s_current_dance = DANCE_IDLE;

void dancer_physics_init(void) {
    s_spr_body_y = (Spring){0.0f, 0.0f, 300.0f, 18.0f};
    s_spr_squash = (Spring){1.0f, 0.0f, 250.0f, 14.0f};
    s_spr_sway   = (Spring){0.0f, 0.0f, 150.0f, 12.0f};
    s_spr_hair_l = (Spring){0.0f, 0.0f, 50.0f, 3.5f};
    s_spr_hair_r = (Spring){0.0f, 0.0f, 50.0f, 3.5f};
    s_spr_arm_l  = (Spring){0.0f, 0.0f, 180.0f, 12.0f};
    s_spr_arm_r  = (Spring){0.0f, 0.0f, 180.0f, 12.0f};
    s_spr_face_y = (Spring){0.0f, 0.0f, 240.0f, 22.0f};
    s_spr_ahoge  = (Spring){0.0f, 0.0f, 220.0f, 11.0f};
    s_spr_spin   = (Spring){0.0f, 0.0f, 300.0f, 14.0f};
    s_dance_side = 1.0f;
    s_beat_count = 0;
    s_peak_tracker = 0.25f;
    memset(s_smooth_bins, 0, sizeof(s_smooth_bins));
    memset(s_flux_history, 0, sizeof(s_flux_history));
    s_flux_idx = 0;
    s_flux_timer = 0.0f;
    s_estimated_bpm = 120.0f;
    s_smoothed_bpm = 120.0f;
    s_last_path[0] = '\0';
}

static void sample_audio_bands(float *out_bass, float *out_mid, float *out_high) {
    uint32_t rpos = atomic_load(&p_frames_consumed);
    float complex X[FFT_SIZE];
    for (int i = 0; i < FFT_SIZE; i++) {
        uint32_t idx = (rpos - FFT_SIZE + i) & VIS_BUF_MASK;
        float s = (vis_ring_l[idx] + vis_ring_r[idx]) * 0.5f;
        X[i] = s * hann_window[i];
    }
    compute_fft(X, FFT_SIZE);

    const int count = 32;
    const int half_fft = FFT_SIZE / 2;
    float max_mag_frame = 0.01f;
    float raw_bins[32] = {0};

    float min_f = log10f(1.0f);
    float max_f = log10f((float)(half_fft * 0.82f));

    for (int b = 0; b < count; b++) {
        float f1 = powf(10.0f, min_f + (max_f - min_f) * ((float)b / (float)count));
        float f2 = powf(10.0f, min_f + (max_f - min_f) * ((float)(b + 1) / (float)count));
        int b1 = (int)f1;
        int b2 = (int)f2;
        if (b2 <= b1) b2 = b1 + 1;
        if (b1 >= half_fft) b1 = half_fft - 1;
        if (b2 > half_fft) b2 = half_fft;

        float peak_bin = 0.0f;
        for (int k = b1; k < b2; k++) {
            float r = crealf(X[k]);
            float im = cimagf(X[k]);
            float mag = sqrtf(r * r + im * im);
            float tilt = 1.0f + sqrtf((float)k / (float)half_fft) * 2.4f;
            mag *= tilt;
            if (mag > peak_bin) peak_bin = mag;
        }

        raw_bins[b] = peak_bin;
        if (peak_bin > max_mag_frame) max_mag_frame = peak_bin;
    }

    if (max_mag_frame > s_peak_tracker) {
        s_peak_tracker += 0.15f * (max_mag_frame - s_peak_tracker);
    } else {
        s_peak_tracker -= 0.018f * (s_peak_tracker - max_mag_frame);
    }
    if (s_peak_tracker < 0.10f) s_peak_tracker = 0.10f;

    float norm_scale = 1.0f / s_peak_tracker;
    float bins[32];

    for (int b = 0; b < count; b++) {
        float val = raw_bins[b] * norm_scale;
        if (val > 1.0f) val = 1.0f;
        if (val < 0.0f) val = 0.0f;

        if (val >= s_smooth_bins[b]) {
            s_smooth_bins[b] += 0.40f * (val - s_smooth_bins[b]);
        } else {
            s_smooth_bins[b] -= 0.040f;
            if (s_smooth_bins[b] < 0.0f) s_smooth_bins[b] = 0.0f;
        }
        bins[b] = s_smooth_bins[b];
    }

    float b_sum = 0.0f, m_sum = 0.0f, h_sum = 0.0f;
    for (int i = 0; i < 32; i++) {
        if (i < 5) b_sum += bins[i];
        else if (i < 18) m_sum += bins[i];
        else h_sum += bins[i];
    }

    *out_bass = fminf(1.0f, (b_sum / 5.0f) * 1.30f);
    *out_mid  = fminf(1.0f, (m_sum / 13.0f) * 1.25f);
    *out_high = fminf(1.0f, (h_sum / 14.0f) * 1.30f);
}

void dancer_physics_update(DancerState *out, float px_w, float px_h) {
    // True monotonic frame timing
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    static struct timespec s_last_ts = {0};
    float dt = 0.025f;
    if (s_last_ts.tv_sec != 0) {
        dt = (float)(ts.tv_sec - s_last_ts.tv_sec) + (float)(ts.tv_nsec - s_last_ts.tv_nsec) * 1e-9f;
        if (dt < 0.001f) dt = 0.001f;
        if (dt > 0.05f)  dt = 0.05f;
    }
    s_last_ts = ts;

    PlayState st = (PlayState)atomic_load(&play_state_atomic);
    bool is_playing = (st == STATE_PLAYING);

    // Transition detection
    pthread_mutex_lock(&state_mutex);
    if (strncmp(s_last_path, playing_filepath, sizeof(s_last_path)) != 0) {
        strncpy(s_last_path, playing_filepath, sizeof(s_last_path) - 1);
        s_estimated_bpm = 120.0f;
        s_smoothed_bpm = 120.0f;
        s_time_since_beat = 0.0f;
        s_time_since_nod = 1.0f;
        s_face_timer = 0.0f;
        s_face_cooldown = 0.0f;
        s_spin_cooldown = 0.0f;
        s_rapid_hit_energy = 0.0f;
        s_was_drop = false;
        s_current_dance = DANCE_IDLE;
        s_headbang_blend = 0.0f;
        s_phrase_energy = 0.15f;
        s_baseline_energy = 0.15f;
        s_smoothed_energy = 0.15f;
        s_is_climax = false;
        memset(s_flux_history, 0, sizeof(s_flux_history));
        s_flux_idx = 0;
        s_flux_timer = 0.0f;

        // Query tracker BPM directly if available
        if (active_codec && active_codec->get_interface && active_decoder) {
            KoniTrackerInterface *tracker = (KoniTrackerInterface*)active_codec->get_interface(active_decoder, KONI_IFACE_TRACKER);
            if (tracker) {
                uint32_t bpm = tracker->get_bpm(active_decoder);
                if (bpm >= 45 && bpm <= 300) {
                    s_estimated_bpm = (float)bpm;
                    s_smoothed_bpm = (float)bpm;
                }
            }
        }
    }
    pthread_mutex_unlock(&state_mutex);

    float bass = 0.0f, mid = 0.0f, high = 0.0f;
    if (is_playing) sample_audio_bands(&bass, &mid, &high);

    float bass_delta = bass - s_prev_bass;
    float high_delta = high - s_prev_high;
    s_prev_bass = bass;
    s_prev_high = high;

    float kick_drive  = (bass_delta > 0.0f ? bass_delta * 1.6f : 0.0f) + (bass * 0.28f);
    float snare_drive = (high_delta > 0.0f ? high_delta * 1.4f : 0.0f) + (high * 0.22f);

    float inst_power = 0.30f * bass + 0.50f * mid + 0.20f * high + (kick_drive * 0.15f);
    s_phrase_energy   += (inst_power - s_phrase_energy)   * fminf(1.0f, dt * 1.8f);
    s_baseline_energy += (inst_power - s_baseline_energy) * fminf(1.0f, dt * 0.09f);

    float rel_ratio = (s_baseline_energy > 0.025f) ? (s_phrase_energy / s_baseline_energy) : 1.0f;
    float cur_energy = fminf(1.0f, s_phrase_energy * (rel_ratio > 1.15f ? 1.25f : 0.85f));
    s_smoothed_energy += (cur_energy - s_smoothed_energy) * fminf(1.0f, dt * 2.8f);

    if (!s_is_climax && (s_smoothed_energy >= 0.64f || (rel_ratio >= 1.30f && s_phrase_energy > 0.38f))) {
        s_is_climax = true;
    } else if (s_is_climax && (s_smoothed_energy < 0.48f && rel_ratio < 1.05f)) {
        s_is_climax = false;
    }

    // Flux autocorrelation
    s_flux_timer += dt;
    float onset_flux = (bass_delta > 0.0f ? bass_delta * 2.0f : 0.0f) +
                       (high_delta > 0.0f ? high_delta * 1.2f : 0.0f) + (bass * 0.3f);

    while (s_flux_timer >= 0.01667f) {
        s_flux_timer -= 0.01667f;
        s_flux_history[s_flux_idx] = onset_flux;
        s_flux_idx = (s_flux_idx + 1) % DANCER_FLUX_BINS;
    }

    if (is_playing) {
        int search_len = 85;
        float best_corr = 0.0f;
        int best_lag = 0;
        float sum_corr = 0.0f;
        int lag_count = 0;

        for (int lag = 18; lag <= 55; lag++) {
            float corr = 0.0f;
            for (int k = 0; k < search_len; k++) {
                int i1 = (s_flux_idx - 1 - k + DANCER_FLUX_BINS) % DANCER_FLUX_BINS;
                int i2 = (s_flux_idx - 1 - k - lag + DANCER_FLUX_BINS) % DANCER_FLUX_BINS;
                corr += s_flux_history[i1] * s_flux_history[i2];
            }
            sum_corr += corr;
            lag_count++;
            if (corr > best_corr) {
                best_corr = corr;
                best_lag = lag;
            }
        }

        float avg_corr = (lag_count > 0) ? (sum_corr / (float)lag_count) : 0.01f;
        if (best_lag > 0 && best_corr > avg_corr * 1.25f && best_corr > 0.08f) {
            float chunk_bpm = 3600.0f / (float)best_lag;
            while (chunk_bpm < 75.0f) chunk_bpm *= 2.0f;
            while (chunk_bpm > 175.0f) chunk_bpm *= 0.5f;
            s_estimated_bpm += (chunk_bpm - s_estimated_bpm) * fminf(1.0f, dt * 3.2f);
        }
    }

    if (s_estimated_bpm < 70.0f)  s_estimated_bpm = 70.0f;
    if (s_estimated_bpm > 185.0f) s_estimated_bpm = 185.0f;
    s_smoothed_bpm += (s_estimated_bpm - s_smoothed_bpm) * fminf(1.0f, dt * 2.5f);

    s_time_since_beat += dt;
    bool beat_hit = false;
    if (is_playing && s_time_since_beat >= 0.12f) {
        if (kick_drive > 0.26f || snare_drive > 0.30f || (bass_delta > 0.07f && bass > 0.22f) || (mid > 0.65f && high_delta > 0.15f)) {
            beat_hit = true;
            s_time_since_beat = 0.0f;
        }
    }

    if (s_face_cooldown > 0.0f) s_face_cooldown -= dt;
    if (s_face_timer > 0.0f)    s_face_timer -= dt;
    if (s_spin_cooldown > 0.0f) s_spin_cooldown -= dt;

    s_time_since_nod += dt;
    float min_nod_interval = fmaxf(0.28f, (60.0f / s_smoothed_bpm) * 0.70f);

    if (beat_hit) {
        s_beat_count++;
        s_rapid_hit_energy += 1.0f;
        if (s_rapid_hit_energy >= 2.2f && s_face_cooldown <= 0.0f) {
            s_face_timer = 0.65f;
            s_face_cooldown = 2.0f;
        }
    }

    if (!is_playing) {
        s_current_dance = DANCE_IDLE;
    } else if (s_is_climax) {
        s_current_dance = DANCE_HYPE;
    } else if (s_smoothed_energy < 0.20f) {
        s_current_dance = DANCE_IDLE;
    } else {
        int routine_step = (s_beat_count / 8) % 4;
        if (routine_step == 0)      s_current_dance = DANCE_GROOVE_RIGHT;
        else if (routine_step == 1) s_current_dance = DANCE_GROOVE_LEFT;
        else if (routine_step == 2) s_current_dance = DANCE_DOUBLE_HIP;
        else                        s_current_dance = DANCE_SWAY_LOW;
    }

    bool is_drop = (rel_ratio > 1.45f && kick_drive > 0.40f && s_phrase_energy > 0.38f);
    bool is_flourish = (beat_hit && (s_beat_count % 32 == 0) && high_delta > 0.20f && s_current_dance >= DANCE_GROOVE_RIGHT);
    bool can_spin = (s_spin_cooldown <= 0.0f) && (fabsf(s_spr_spin.val - s_spin_target) < 0.25f);

    if ((is_drop || is_flourish) && can_spin && !s_was_drop) {
        s_jump_type = rand() % 3;
        s_jump_tuck_side = s_dance_side;
        if (is_drop) {
            s_spr_body_y.vel -= 580.0f;
            s_spr_squash.vel += 4.0f;
        }
        s_spin_target += 2.0f;
        s_spin_cooldown = 6.0f;
        s_was_drop = true;
    } else if (!is_drop && !is_flourish) {
        s_was_drop = false;
    }

    if (beat_hit) {
        if (s_time_since_nod >= min_nod_interval) {
            s_time_since_nod = 0.0f;
            float nod_mult = 0.85f + (s_smoothed_energy * 0.65f);
            s_dance_side *= -1.0f;

            float bounce_force = 140.0f * nod_mult;
            if (s_current_dance == DANCE_HYPE)      bounce_force = 240.0f * nod_mult;
            else if (s_current_dance == DANCE_IDLE) bounce_force = 40.0f;

            s_spr_body_y.vel += bounce_force * 0.7f;
            s_spr_squash.vel -= bounce_force * 0.005f;

            float head_force = bounce_force * 0.4f;
            if (s_current_dance == DANCE_HYPE || kick_drive > 0.38f) {
                head_force = bounce_force * 1.5f;
            }
            s_spr_face_y.vel += head_force;

            if (s_current_dance == DANCE_HYPE) {
                s_spr_arm_l.vel += bounce_force * 0.8f;
                s_spr_arm_r.vel -= bounce_force * 0.8f;
            } else {
                s_spr_arm_l.vel -= bounce_force * 0.2f;
                s_spr_arm_r.vel -= bounce_force * 0.2f;
            }

            s_spr_hair_l.vel += bounce_force * 1.5f;
            s_spr_hair_r.vel += bounce_force * 1.5f;
            s_spr_ahoge.vel  += s_dance_side * bounce_force * 0.45f;
        }
    }

    s_rapid_hit_energy -= dt * 3.2f;
    if (s_rapid_hit_energy < 0.0f) s_rapid_hit_energy = 0.0f;
    if (!is_playing) s_dance_side = 1.0f;

    s_idle_time += dt * (is_playing ? (s_smoothed_bpm / 80.0f) : 1.5f);
    spring_update(&s_spr_spin, s_spin_target, dt);

    float idle_sway   = sinf(s_idle_time * 1.5f) * 2.0f;
    float idle_squash = 1.0f + sinf(s_idle_time * 3.0f + (float)M_PI) * 0.015f;

    float target_sway = idle_sway;
    if (s_current_dance == DANCE_HYPE) {
        target_sway += s_dance_side * (16.0f + s_smoothed_energy * 12.0f);
    } else if (s_current_dance == DANCE_SWAY_LOW) {
        target_sway += s_dance_side * (15.0f + s_smoothed_energy * 12.0f);
    } else if (s_current_dance == DANCE_GROOVE_LEFT || s_current_dance == DANCE_GROOVE_RIGHT || s_current_dance == DANCE_DOUBLE_HIP) {
        target_sway += s_dance_side * (12.0f + s_smoothed_energy * 10.0f);
    }

    spring_update(&s_spr_sway, target_sway, dt);
    spring_update(&s_spr_body_y, 0.0f, dt);
    spring_update(&s_spr_squash, idle_squash, dt);
    spring_update(&s_spr_face_y, 0.0f, dt);

    float sway = s_spr_sway.val;
    float hip_tilt = (s_current_dance == DANCE_DOUBLE_HIP) ? (sway * 0.65f) : (sway * 0.45f);
    float head_tilt = -hip_tilt * 0.65f + s_spr_sway.vel * 0.04f;

    float target_hb = (s_current_dance == DANCE_HYPE) ? 1.0f : 0.0f;
    s_headbang_blend += (target_hb - s_headbang_blend) * fminf(1.0f, dt * 6.0f);
    head_tilt += s_spr_face_y.val * 1.6f * s_headbang_blend;

    float world_head_rot = hip_tilt + head_tilt;
    float hair_float_l = sinf(s_idle_time * 2.2f) * 8.0f;
    float hair_float_r = sinf(s_idle_time * 2.2f + 1.5f) * 8.0f;
    spring_update(&s_spr_hair_l, -world_head_rot + hair_float_l, dt);
    spring_update(&s_spr_hair_r, -world_head_rot + hair_float_r, dt);
    s_spr_hair_l.vel -= s_spr_sway.vel * 3.5f * dt;
    s_spr_hair_r.vel -= s_spr_sway.vel * 3.5f * dt;

    float target_arm_l = 0.0f, target_arm_r = 0.0f;
    if (s_current_dance == DANCE_HYPE) {
        target_arm_l = 75.0f + sinf(s_idle_time * 3.0f) * 6.0f;
        target_arm_r = -75.0f - sinf(s_idle_time * 3.0f) * 6.0f;
    } else if (s_current_dance == DANCE_SWAY_LOW) {
        target_arm_l = s_spr_sway.val * 1.5f + sinf(s_idle_time * 2.0f) * 3.0f;
        target_arm_r = -s_spr_sway.val * 1.5f + sinf(s_idle_time * 2.0f + 1.0f) * 3.0f;
    } else {
        target_arm_l = s_spr_sway.val * 2.2f + sinf(s_idle_time * 2.0f) * 4.0f;
        target_arm_r = -s_spr_sway.val * 2.2f + sinf(s_idle_time * 2.0f + 1.0f) * 4.0f;
        if (s_current_dance == DANCE_GROOVE_LEFT || s_current_dance == DANCE_DOUBLE_HIP) target_arm_l = 0.0f;
        if (s_current_dance == DANCE_GROOVE_RIGHT || s_current_dance == DANCE_DOUBLE_HIP) target_arm_r = 0.0f;
    }
    spring_update(&s_spr_arm_l, target_arm_l, dt);
    spring_update(&s_spr_arm_r, target_arm_r, dt);

    float rad_tilt = hip_tilt * ((float)M_PI / 180.0f);
    float natural_hip_dip = (1.0f - cosf(rad_tilt)) * 48.0f;

    float hop_amp = 4.0f + (is_playing ? (s_smoothed_energy * 6.0f) : 0.0f);
    float idle_hop = -fabsf(sinf(s_idle_time * 2.0f)) * hop_amp;
    float total_body_y = s_spr_body_y.val + natural_hip_dip + idle_hop;

    float squash_y = s_spr_squash.val;
    if (squash_y < 0.70f) squash_y = 0.70f;
    if (squash_y > 1.30f) squash_y = 1.30f;
    float squash_x = 1.0f / squash_y;

    // Character scaling
    float scale = fminf(px_w / 220.0f, px_h / 240.0f);
    if (scale < 0.28f) scale = 0.28f;

    float char_base_x = px_w * 0.5f;
    float char_base_y = (px_h * 0.5f) - (18.0f * scale);
    float floor_y = char_base_y + 111.5f * scale;
    if (floor_y > px_h - 4.0f) {
        float diff = floor_y - (px_h - 4.0f);
        char_base_y -= diff;
        floor_y = px_h - 4.0f;
    }

    // Leg movement
    float target_l_angle = 0.0f;
    float target_r_angle = 0.0f;
    float target_l_lift  = 0.0f;
    float target_r_lift  = 0.0f;
    float target_l_scale_y = 1.0f;
    float target_r_scale_y = 1.0f;

    bool is_airborne = (total_body_y < -10.0f);

    if (is_airborne) {
        float airborne_lift = (total_body_y + 10.0f) * scale;
        target_l_lift = airborne_lift;
        target_r_lift = airborne_lift;

        if (s_jump_type == 0) {
            if (s_jump_tuck_side > 0) {
                target_l_lift += -16.0f * scale;
                target_l_angle = 18.0f;
                target_l_scale_y = 0.80f;
                target_r_angle = -10.0f;
                target_r_scale_y = 1.05f;
            } else {
                target_r_lift += -16.0f * scale;
                target_r_angle = -18.0f;
                target_r_scale_y = 0.80f;
                target_l_angle = 10.0f;
                target_l_scale_y = 1.05f;
            }
        } else if (s_jump_type == 1) {
            target_l_angle = -22.0f;
            target_r_angle = 22.0f;
            target_l_scale_y = 1.08f;
            target_r_scale_y = 1.08f;
        } else {
            target_l_lift += -10.0f * scale;
            target_r_lift += -10.0f * scale;
            target_l_angle = 8.0f * s_jump_tuck_side;
            target_r_angle = 8.0f * s_jump_tuck_side;
            target_l_scale_y = 0.85f;
            target_r_scale_y = 0.85f;
        }
    } else if (is_playing) {
        if (sway > 0.0f) {
            target_r_angle = sway * 0.35f;
            target_l_angle = sway * 0.85f;
        } else {
            target_l_angle = sway * 0.35f;
            target_r_angle = sway * 0.85f;
        }

        float knee_flex = (1.0f - squash_y) * 26.0f;
        if (knee_flex < 0.0f) knee_flex = 0.0f;
        target_l_angle -= knee_flex;
        target_r_angle += knee_flex;

        if (s_current_dance == DANCE_GROOVE_LEFT || s_current_dance == DANCE_GROOVE_RIGHT || s_current_dance == DANCE_DOUBLE_HIP) {
            float tap_cycle = sinf(s_idle_time * 3.0f);
            if (tap_cycle > 0.45f) {
                float tap = (tap_cycle - 0.45f) / 0.55f;
                target_l_lift = -tap * 6.0f * scale;
                target_l_angle += tap * 8.0f;
            } else if (tap_cycle < -0.45f) {
                float tap = (-tap_cycle - 0.45f) / 0.55f;
                target_r_lift = -tap * 6.0f * scale;
                target_r_angle -= tap * 8.0f;
            }
        }
    } else {
        target_l_angle = -2.5f;
        target_r_angle = 2.5f;
    }

    float leg_blend = fminf(1.0f, dt * 18.0f);
    s_smooth_l_angle   += (target_l_angle   - s_smooth_l_angle)   * leg_blend;
    s_smooth_r_angle   += (target_r_angle   - s_smooth_r_angle)   * leg_blend;
    s_smooth_l_lift    += (target_l_lift    - s_smooth_l_lift)    * leg_blend;
    s_smooth_r_lift    += (target_r_lift    - s_smooth_r_lift)    * leg_blend;
    s_smooth_l_scale_y += (target_l_scale_y - s_smooth_l_scale_y) * leg_blend;
    s_smooth_r_scale_y += (target_r_scale_y - s_smooth_r_scale_y) * leg_blend;

    // Export move k with displacement (scaled)
    out->char_base_x      = char_base_x;
    out->char_base_y      = char_base_y;
    out->floor_y          = floor_y;
    out->scale            = scale;
    out->sway             = sway * scale;
    out->total_body_y     = total_body_y * scale;
    out->squash_x         = squash_x;
    out->squash_y         = squash_y;
    out->h_squash_y       = 1.0f / sqrtf(squash_y);
    out->h_squash_x       = 1.0f / out->h_squash_y;
    out->hip_tilt         = hip_tilt;
    out->head_tilt        = head_tilt;
    out->hair_l           = s_spr_hair_l.val;
    out->hair_r           = s_spr_hair_r.val;
    out->ahoge            = s_spr_body_y.vel * -0.1f + high * 15.0f;
    out->l_arm_angle      = s_spr_arm_l.val;
    out->r_arm_angle      = s_spr_arm_r.val;
    out->face_y           = s_spr_face_y.val;
    out->spin_scale_x     = cosf(s_spr_spin.val * (float)M_PI);
    out->smooth_l_angle   = s_smooth_l_angle;
    out->smooth_r_angle   = s_smooth_r_angle;
    out->smooth_l_lift    = s_smooth_l_lift;
    out->smooth_r_lift    = s_smooth_r_lift;
    out->smooth_l_scale_y = s_smooth_l_scale_y;
    out->smooth_r_scale_y = s_smooth_r_scale_y;
    out->l_arm_on_hip     = (s_current_dance == DANCE_GROOVE_LEFT  || s_current_dance == DANCE_DOUBLE_HIP);
    out->r_arm_on_hip     = (s_current_dance == DANCE_GROOVE_RIGHT || s_current_dance == DANCE_DOUBLE_HIP);
    out->face_type        = (s_face_timer > 0.0f || s_current_dance == DANCE_HYPE) ? 1 : (s_current_dance >= DANCE_GROOVE_RIGHT ? 2 : 0);
    out->bass             = bass;
    out->mid              = mid;
    out->high             = high;
    out->beat_hit         = beat_hit;
    out->is_airborne      = is_airborne;
}