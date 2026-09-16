#define _DEFAULT_SOURCE
#include "krystal_spatial.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Process UI blocks in 32-sample increments
#define SUB_BLOCK_SIZE 32

typedef struct {
    float delay_l_samples;
    float delay_r_samples;
    float shadow_l_db;
    float shadow_r_db;
    float rear_cut_db;
} SpatialCues;

static SpatialCues compute_cues(float theta_deg, float fs) {
    while (theta_deg > 180.0f) theta_deg -= 360.0f;
    while (theta_deg < -180.0f) theta_deg += 360.0f;
    
    float lateral_deg = theta_deg;
    if (theta_deg > 90.0f) lateral_deg = 180.0f - theta_deg;
    else if (theta_deg < -90.0f) lateral_deg = -180.0f - theta_deg;
    
    float abs_lat = fabsf(lateral_deg);
    float lat_rad = abs_lat * ((float)M_PI / 180.0f);
    
    const float a = 0.0875f; // Head radius (m)
    const float c = 343.0f;  // Speed of sound (m/s)
    float itd_sec = (a / c) * (sinf(lat_rad) + lat_rad);
    
    SpatialCues cues;
    if (lateral_deg > 0.0f) { // Source on the Right
        cues.delay_l_samples = itd_sec * fs;
        cues.delay_r_samples = 0.0f;
        cues.shadow_l_db = -12.0f * (abs_lat / 90.0f);
        cues.shadow_r_db =  +1.5f * (abs_lat / 90.0f);
    } else { // Source on the Left
        cues.delay_l_samples = 0.0f;
        cues.delay_r_samples = itd_sec * fs;
        cues.shadow_l_db =  +1.5f * (abs_lat / 90.0f);
        cues.shadow_r_db = -12.0f * (abs_lat / 90.0f);
    }
    
    float abs_theta = fabsf(theta_deg);
    cues.rear_cut_db = -10.0f * (abs_theta / 180.0f);
    return cues;
}

static void biquad_set_highpass(KrystalBiquad *b, float fc, float q, float fs) {
    if (fc <= 20.0f) fc = 20.0f;
    if (fc >= fs * 0.45f) fc = fs * 0.45f;
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

static void biquad_set_shelf(KrystalBiquad *b, float fc, float gain_db, float fs) {
    if (fabsf(gain_db) < 0.05f) {
        b->b0 = 1.0f; b->b1 = 0.0f; b->b2 = 0.0f; b->a1 = 0.0f; b->a2 = 0.0f; return;
    }
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

static void biquad_set_peak(KrystalBiquad *b, float fc, float gain_db, float q, float fs) {
    if (fabsf(gain_db) < 0.05f) {
        b->b0 = 1.0f; b->b1 = 0.0f; b->b2 = 0.0f; b->a1 = 0.0f; b->a2 = 0.0f; return;
    }
    float A = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * (float)M_PI * (fc / fs);
    float alpha = sinf(w0) / (2.0f * q);
    float cos_w = cosf(w0);
    float a0 = 1.0f + alpha / A;

    b->b0 = (1.0f + alpha * A) / a0;
    b->b1 = (-2.0f * cos_w) / a0;
    b->b2 = (1.0f - alpha * A) / a0;
    b->a1 = (-2.0f * cos_w) / a0;
    b->a2 = (1.0f - alpha / A) / a0;
}

static inline float biquad_step(KrystalBiquad *b, float in) {
    float out = b->b0 * in + b->s1[0];
    b->s1[0] = b->b1 * in - b->a1 * out + b->s2[0];
    b->s2[0] = b->b2 * in - b->a2 * out;
    if (fabsf(out) < 1.0e-15f) out = 0.0f;
    return out;
}

static inline float read_delay_hermite(const float *ring, float delay_samples, uint32_t write_pos) {
    if (delay_samples < 0.0f) delay_samples = 0.0f;
    if (delay_samples > (float)(SPATIAL_RING_SIZE - 4)) delay_samples = (float)(SPATIAL_RING_SIZE - 4);

    uint32_t mask = SPATIAL_RING_SIZE - 1;
    uint32_t idelay = (uint32_t)delay_samples;
    float frac = delay_samples - (float)idelay;

    uint32_t idx1 = (write_pos - idelay - 1) & mask;
    uint32_t idx0 = (idx1 + 1) & mask;
    uint32_t idx2 = (idx1 - 1) & mask;
    uint32_t idx3 = (idx1 - 2) & mask;

    float y0 = ring[idx0];
    float y1 = ring[idx1];
    float y2 = ring[idx2];
    float y3 = ring[idx3];

    float c0 = y1;
    float c1 = 0.5f * (y2 - y0);
    float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

static inline float allpass_step(float in, float *buf, uint32_t size, uint32_t *idx, float g) {
    float buf_out = buf[*idx];
    float w = in + g * buf_out;
    float out = -g * w + buf_out;
    buf[*idx] = w;
    *idx = (*idx + 1) % size;
    return out;
}

void krystal_spatial_init(KrystalSpatialState *state, uint32_t sample_rate) {
    if (!state) return;
    memset(state, 0, sizeof(KrystalSpatialState));
    state->sample_rate = sample_rate ? sample_rate : 44100;
    state->last_mono_cut = 120.0f;
    state->smoothed_distance = 1.4f;
    state->smoothed_stage_angle = 60.0f;
    state->smoothed_room_refl = 0.15f;
    state->smoothed_center_mult = 1.0f;
    state->phase_corr_filtered = 1.0f;

    biquad_set_highpass(&state->hp_side1, state->last_mono_cut, 0.7071f, (float)state->sample_rate);
    biquad_set_highpass(&state->hp_side2, state->last_mono_cut, 0.7071f, (float)state->sample_rate);
}

void krystal_spatial_reset(KrystalSpatialState *state) {
    if (!state) return;
    krystal_spatial_init(state, state->sample_rate);
}

void krystal_spatial_process(KrystalSpatialState *state, float *samples, uint32_t num_frames,
                             const KrystalSpatialConfig *cfg, uint32_t sample_rate,
                             float *out_mid_energy, float *out_side_energy,
                             float *out_phase_corr, bool *out_clamped) {
    if (!state || !samples || num_frames == 0 || !cfg || !cfg->enabled) {
        if (out_mid_energy)  *out_mid_energy = 0.0f;
        if (out_side_energy) *out_side_energy = 0.0f;
        if (out_phase_corr)  *out_phase_corr = 1.0f;
        if (out_clamped)     *out_clamped = false;
        return;
    }

    float fs = (float)sample_rate;
    if (sample_rate != state->sample_rate || fabsf(cfg->mono_cut_hz - state->last_mono_cut) > 1.0f) {
        state->sample_rate = sample_rate;
        state->last_mono_cut = cfg->mono_cut_hz;
        biquad_set_highpass(&state->hp_side1, cfg->mono_cut_hz, 0.7071f, fs);
        biquad_set_highpass(&state->hp_side2, cfg->mono_cut_hz, 0.7071f, fs);
    }

    float sum_mid_sq = 0.0f, sum_side_sq = 0.0f;
    float dot_lr = 0.0f, sum_l_sq = 0.0f, sum_r_sq = 0.0f;

    uint32_t frames_processed = 0;

    // Fast sub-block chunking (allows us to do heavy 3D math and interpolate per-sample)
    while (frames_processed < num_frames) {
        uint32_t block_frames = num_frames - frames_processed;
        if (block_frames > SUB_BLOCK_SIZE) block_frames = SUB_BLOCK_SIZE;

        // ~25ms time constant ensures extreme UI drags create smooth Doppler shifting
        float smooth_alpha = 1.0f - expf(-((float)block_frames) / (fs * 0.025f));
        
        state->smoothed_azimuth     += smooth_alpha * (cfg->azimuth_deg - state->smoothed_azimuth);
        state->smoothed_elevation   += smooth_alpha * (cfg->elevation_deg - state->smoothed_elevation);
        state->smoothed_distance    += smooth_alpha * (cfg->distance_m - state->smoothed_distance);
        state->smoothed_stage_angle += smooth_alpha * (cfg->stage_angle_deg - state->smoothed_stage_angle);
        state->smoothed_room_refl   += smooth_alpha * (cfg->room_refl - state->smoothed_room_refl);

        float target_center_mult = powf(10.0f, cfg->center_gain_db / 20.0f);
        state->smoothed_center_mult += smooth_alpha * (target_center_mult - state->smoothed_center_mult);

        // Mid represents Phantom Center. Side represents Orthogonal Width. 
        float az_m = state->smoothed_azimuth;
        float az_s = state->smoothed_azimuth - 90.0f; 

        SpatialCues cues_m = compute_cues(az_m, fs);
        SpatialCues cues_s = compute_cues(az_s, fs);

        // Update Physical Head Shadow Shelving
        biquad_set_shelf(&state->m_shadow_l, 1500.0f, cues_m.shadow_l_db, fs);
        biquad_set_shelf(&state->m_shadow_r, 1500.0f, cues_m.shadow_r_db, fs);
        biquad_set_shelf(&state->m_rear_l,   4000.0f, cues_m.rear_cut_db, fs);
        biquad_set_shelf(&state->m_rear_r,   4000.0f, cues_m.rear_cut_db, fs);

        biquad_set_shelf(&state->s_shadow_l, 1500.0f, cues_s.shadow_l_db, fs);
        biquad_set_shelf(&state->s_shadow_r, 1500.0f, cues_s.shadow_r_db, fs);
        biquad_set_shelf(&state->s_rear_l,   4000.0f, cues_s.rear_cut_db, fs);
        biquad_set_shelf(&state->s_rear_r,   4000.0f, cues_s.rear_cut_db, fs);

        // Sweep Pinna Elevation Notch to give Height localization 
        float norm_el = (state->smoothed_elevation + 45.0f) / 135.0f;
        if (norm_el < 0.0f) norm_el = 0.0f;
        if (norm_el > 1.0f) norm_el = 1.0f;
        float pinna_fc = 5000.0f + norm_el * 5000.0f; // Range, 5kHz to 10kHz depending on height
        biquad_set_peak(&state->pinna_l, pinna_fc, -5.0f, 3.0f, fs);
        biquad_set_peak(&state->pinna_r, pinna_fc, -5.0f, 3.0f, fs);

        // Air Absorption Filtering based on physical distance 
        float air_fc = 20000.0f / sqrtf(state->smoothed_distance);
        if (air_fc > 20000.0f) air_fc = 20000.0f;
        if (air_fc < 1000.0f) air_fc = 1000.0f;
        float air_alpha = 1.0f - expf(-2.0f * (float)M_PI * air_fc / fs);
        
        // Inverse square law scaling preventing infinity
        float dist_att = 1.5f / (0.5f + state->smoothed_distance);

        // Pre-calculate the delta per-sample for the delay lengths to get analog Doppler
        float step_m_l = (cues_m.delay_l_samples - state->cur_delay_m_l) / (float)block_frames;
        float step_m_r = (cues_m.delay_r_samples - state->cur_delay_m_r) / (float)block_frames;
        float step_s_l = (cues_s.delay_l_samples - state->cur_delay_s_l) / (float)block_frames;
        float step_s_r = (cues_s.delay_r_samples - state->cur_delay_s_r) / (float)block_frames;

        float stage_width_factor = sinf((state->smoothed_stage_angle / 2.0f) * ((float)M_PI / 180.0f));
        float refl = state->smoothed_room_refl;

        // Dynamic Early Reflection timings scale to mimic larger rooms naturally
        float er_d1 = fs * (0.005f + state->smoothed_distance * 0.001f);
        float er_d2 = fs * (0.009f + state->smoothed_distance * 0.0015f);
        float er_d3 = fs * (0.015f + state->smoothed_distance * 0.002f);

        // Inner audio process loop
        for (uint32_t i = 0; i < block_frames; i++) {
            uint32_t f = frames_processed + i;
            float l = samples[f * 2];
            float r = samples[f * 2 + 1];

            float mid  = (l + r) * 0.70710678f * state->smoothed_center_mult;
            float side = (l - r) * 0.70710678f;

            // Strip heavy bass from side channel to lock phase for speakers
            float side_filt = biquad_step(&state->hp_side1, side);
            side_filt = biquad_step(&state->hp_side2, side_filt);

            uint32_t wpos = state->write_idx;
            state->delay_ring_m[wpos] = mid;
            state->delay_ring_s[wpos] = side_filt;
            
            // Apply Dynamic Doppler Steps
            state->cur_delay_m_l += step_m_l;
            state->cur_delay_m_r += step_m_r;
            state->cur_delay_s_l += step_s_l;
            state->cur_delay_s_r += step_s_r;

            float ml = read_delay_hermite(state->delay_ring_m, state->cur_delay_m_l + 1.0f, wpos);
            float mr = read_delay_hermite(state->delay_ring_m, state->cur_delay_m_r + 1.0f, wpos);
            float sl = read_delay_hermite(state->delay_ring_s, state->cur_delay_s_l + 1.0f, wpos);
            float sr = read_delay_hermite(state->delay_ring_s, state->cur_delay_s_r + 1.0f, wpos);

            ml = biquad_step(&state->m_shadow_l, ml);
            ml = biquad_step(&state->m_rear_l, ml);
            mr = biquad_step(&state->m_shadow_r, mr);
            mr = biquad_step(&state->m_rear_r, mr);

            sl = biquad_step(&state->s_shadow_l, sl);
            sl = biquad_step(&state->s_rear_l, sl);
            sr = biquad_step(&state->s_shadow_r, -sr); // Negative Side to Right Ear for true stereo width
            sr = biquad_step(&state->s_rear_r, sr);

            float ear_l = ml + (sl * stage_width_factor);
            float ear_r = mr + (sr * stage_width_factor);

            ear_l = biquad_step(&state->pinna_l, ear_l);
            ear_r = biquad_step(&state->pinna_r, ear_r);

            // Apply Distance attenuation and continuous Air Absorption lowpass
            state->air_damp_l += air_alpha * (ear_l - state->air_damp_l);
            state->air_damp_r += air_alpha * (ear_r - state->air_damp_r);
            ear_l = state->air_damp_l * dist_att;
            ear_r = state->air_damp_r * dist_att;

            state->refl_ring_l[wpos] = ear_l;
            state->refl_ring_r[wpos] = ear_r;
            state->write_idx = (wpos + 1) & (SPATIAL_RING_SIZE - 1);

            if (refl > 0.001f) {
                // Multi-tap room specular arrival
                float refl_raw_l = read_delay_hermite(state->refl_ring_l, er_d1, wpos) * 0.45f
                                 + read_delay_hermite(state->refl_ring_r, er_d2, wpos) * 0.30f
                                 + read_delay_hermite(state->refl_ring_l, er_d3, wpos) * 0.20f;

                float refl_raw_r = read_delay_hermite(state->refl_ring_r, er_d1, wpos) * 0.45f
                                 + read_delay_hermite(state->refl_ring_l, er_d2, wpos) * 0.30f
                                 + read_delay_hermite(state->refl_ring_r, er_d3, wpos) * 0.20f;

                // 1-pole high-frequency room absorption damping (~4.2 kHz)
                float damp_alpha = 1.0f - expf(-2.0f * (float)M_PI * 4200.0f / fs);
                state->refl_damp_l += damp_alpha * (refl_raw_l - state->refl_damp_l);
                state->refl_damp_r += damp_alpha * (refl_raw_r - state->refl_damp_r);

                // 2-stage Schroeder All-Pass phase diffusion
                float diff_l = allpass_step(state->refl_damp_l, state->diff1_buf_l, 167, &state->diff1_idx_l, 0.55f);
                diff_l       = allpass_step(diff_l,             state->diff2_buf_l, 257, &state->diff2_idx_l, 0.45f);

                float diff_r = allpass_step(state->refl_damp_r, state->diff1_buf_r, 197, &state->diff1_idx_r, 0.55f);
                diff_r       = allpass_step(diff_r,             state->diff2_buf_r, 283, &state->diff2_idx_r, 0.45f);

                ear_l += diff_l * refl;
                ear_r += diff_r * refl;
            }

            samples[f * 2]     = ear_l;
            samples[f * 2 + 1] = ear_r;

            sum_mid_sq  += mid * mid;
            sum_side_sq += side_filt * side_filt;
            dot_lr      += ear_l * ear_r;
            sum_l_sq    += ear_l * ear_l;
            sum_r_sq    += ear_r * ear_r;
        }

        frames_processed += block_frames;
    }

    // Telemetry updates (for the UI)
    float denom = sqrtf(sum_l_sq * sum_r_sq);
    float inst_corr = (denom > 1.0e-9f) ? (dot_lr / denom) : 1.0f;
    if (inst_corr > 1.0f)  inst_corr = 1.0f;
    if (inst_corr < -1.0f) inst_corr = -1.0f;

    float meter_alpha = 1.0f - expf(-((float)num_frames) / (fs * 0.100f));
    state->phase_corr_filtered += meter_alpha * (inst_corr - state->phase_corr_filtered);

    float inst_mid_rms  = sqrtf(sum_mid_sq / (float)num_frames);
    float inst_side_rms = sqrtf(sum_side_sq / (float)num_frames);
    state->mid_energy_filtered  += meter_alpha * (inst_mid_rms - state->mid_energy_filtered);
    state->side_energy_filtered += meter_alpha * (inst_side_rms - state->side_energy_filtered);

    if (out_mid_energy)  *out_mid_energy  = state->mid_energy_filtered;
    if (out_side_energy) *out_side_energy = state->side_energy_filtered;
    if (out_phase_corr)  *out_phase_corr  = state->phase_corr_filtered;
    if (out_clamped)     *out_clamped     = (state->phase_corr_filtered < cfg->safety_limit);
}