#include "sparkles_vis.h"
#include "vis_math.h"
#include "state.h"
#include <math.h>
#include <string.h>

static SparklesVisMode s_vis_mode = VIS_DANCER;
static float s_smooth_bins[256] = {0};
static float s_peak_tracker = 0.25f;

extern void vis_orb_render(Rectangle b, float dt);
extern void vis_waves_render(Rectangle b, float dt);
extern void vis_radial_bars_render(Rectangle b, float dt);
extern void vis_vector_scope_render(Rectangle b, float dt);
extern void vis_particles_render(Rectangle b, float dt);
extern void vis_cymatics_render(Rectangle b, float dt);
extern void vis_terrain_render(Rectangle b, float dt);
extern void vis_polyhedron_render(Rectangle b, float dt);
extern void vis_sparkles_render(Rectangle b, float dt);
extern void vis_voyager_render(Rectangle b, float dt);
extern void vis_reels_render(Rectangle b, float dt);
extern void vis_tunnel_render(Rectangle b, float dt);
extern void vis_galvanometer_render(Rectangle b, float dt);
extern void vis_seismograph_render(Rectangle b, float dt);
extern void vis_gimbal_render(Rectangle b, float dt);
extern void vis_dancer_render(Rectangle b, float dt);

static float s_badge_timer = 0.0f;

void sparkles_vis_init(void) {
    vis_math_init(); // Initialize Hann window and FFT twiddle factors
    int saved_mode = current_vis_mode;
    if (saved_mode >= 0 && saved_mode < VIS_MODE_COUNT) {
        s_vis_mode = (SparklesVisMode)saved_mode;
    } else {
        s_vis_mode = VIS_DANCER;
    }
    s_badge_timer = 0.0f;
    s_peak_tracker = 0.25f;
    memset(s_smooth_bins, 0, sizeof(s_smooth_bins));
}

void sparkles_vis_cycle(void) {
    s_vis_mode = (s_vis_mode + 1) % VIS_MODE_COUNT;
    current_vis_mode = (int)s_vis_mode; // Save to persistent state
    s_badge_timer = 2.0f; // Display mode badge for 2 seconds on change
}

void sparkles_vis_set_mode(SparklesVisMode mode) {
    if (mode >= 0 && mode < VIS_MODE_COUNT) {
        s_vis_mode = mode;
        current_vis_mode = (int)mode;
        s_badge_timer = 2.0f;
    }
}

float sparkles_vis_get_badge_alpha(void) {
    if (s_badge_timer <= 0.0f) return 0.0f;
    return fminf(1.0f, s_badge_timer);
}

SparklesVisMode sparkles_vis_get_mode(void) {
    return s_vis_mode;
}

const char* sparkles_vis_get_name(SparklesVisMode mode) {
    switch (mode) {
        case VIS_ORB_FLUID:       return "Fluid Orb";
        case VIS_WAVES_FLOW:      return "Chromatic Waves";
        case VIS_RADIAL_SPECTRUM: return "Radial Spectrum";
        case VIS_VECTOR_SCOPE:    return "Vector Scope";
        case VIS_PARTICLE_VORTEX: return "Particle Vortex";
        case VIS_CYMATICS:        return "Cymatics Plate";
        case VIS_TERRAIN:         return "Wireframe Horizon";
        case VIS_VECTOR_POLY:     return "Vector Polyhedron";
        case VIS_TAPE_REELS:      return "Tape Reels";
        case VIS_WARP_TUNNEL:     return "Warp Tunnel";
        case VIS_SPARKLES:        return "Sparkles";
        case VIS_VOYAGER:         return "Voyager";
        case VIS_GALVANOMETER:    return "Galvanometer";
        case VIS_SEISMOGRAPH:     return "Seismograph";
        case VIS_GIMBAL:          return "Gimbal";
        case VIS_DANCER:          return "Koni!";
        default:                  return "Visualizer";
    }
}

void sparkles_vis_get_spectrum(float *out_bins, int count) {
    if (!out_bins || count <= 0) return;
    if (count > 256) count = 256;

    uint32_t rpos = atomic_load(&p_frames_consumed);

    // Collect windowed audio samples
    float complex X[FFT_SIZE];
    for (int i = 0; i < FFT_SIZE; i++) {
        uint32_t idx = (rpos - FFT_SIZE + i) & VIS_BUF_MASK;
        float s = (vis_ring_l[idx] + vis_ring_r[idx]) * 0.5f;
        X[i] = s * hann_window[i];
    }

    // Perform 1024-point FFT
    compute_fft(X, FFT_SIZE);

    // Logarithmic grouping across audible range (30Hz to ~16kHz)
    const int half_fft = FFT_SIZE / 2;
    float max_mag_frame = 0.01f;
    float raw_bins[256] = {0};

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
            // High-frequency tilt compensation (+3dB/octave) to balance natural acoustic decay
            float tilt = 1.0f + sqrtf((float)k / (float)half_fft) * 2.4f;
            mag *= tilt;
            if (mag > peak_bin) peak_bin = mag;
        }

        raw_bins[b] = peak_bin;
        if (peak_bin > max_mag_frame) max_mag_frame = peak_bin;
    }

    // Adaptive peak follower
    if (max_mag_frame > s_peak_tracker) {
        s_peak_tracker += 0.15f * (max_mag_frame - s_peak_tracker);
    } else {
        s_peak_tracker -= 0.018f * (s_peak_tracker - max_mag_frame);
    }
    if (s_peak_tracker < 0.10f) s_peak_tracker = 0.10f;

    float norm_scale = 1.0f / s_peak_tracker;

    // Ballistic decay per frequency band
    for (int b = 0; b < count; b++) {
        float val = raw_bins[b] * norm_scale;
        if (val > 1.0f) val = 1.0f;
        if (val < 0.0f) val = 0.0f;

        if (val >= s_smooth_bins[b]) {
            s_smooth_bins[b] += 0.40f * (val - s_smooth_bins[b]); // Fast attack
        } else {
            s_smooth_bins[b] -= 0.040f; // Smooth decay
            if (s_smooth_bins[b] < 0.0f) s_smooth_bins[b] = 0.0f;
        }
        out_bins[b] = s_smooth_bins[b];
    }
}

void sparkles_vis_get_bands(float *out_bass, float *out_mid, float *out_high) {
    float bins[32];
    sparkles_vis_get_spectrum(bins, 32);

    float b_sum = 0.0f, m_sum = 0.0f, h_sum = 0.0f;
    for (int i = 0; i < 32; i++) {
        if (i < 5) b_sum += bins[i];        // Sub & Bass (30-180 Hz)
        else if (i < 18) m_sum += bins[i];  // Mids (250-2.5 kHz)
        else h_sum += bins[i];              // Treble & Air (3-16 kHz)
    }

    float b_val = (b_sum / 5.0f) * 1.30f;
    float m_val = (m_sum / 13.0f) * 1.25f;
    float h_val = (h_sum / 14.0f) * 1.30f;

    if (b_val > 1.0f) b_val = 1.0f;
    if (m_val > 1.0f) m_val = 1.0f;
    if (h_val > 1.0f) h_val = 1.0f;

    if (out_bass) *out_bass = b_val;
    if (out_mid)  *out_mid  = m_val;
    if (out_high) *out_high = h_val;
}

// Raw time-domain sampler
void sparkles_vis_get_raw_bands(float *out_bass, float *out_mid, float *out_high) {
    uint32_t rpos = atomic_load(&p_frames_consumed);
    float bass = 0.0f, mid = 0.0f, high = 0.0f;

    for (int i = 0; i < 64; i++) {
        uint32_t idx = (rpos - i * 4) & VIS_BUF_MASK;
        float s = fabsf(vis_ring_l[idx] + vis_ring_r[idx]) * 0.5f;

        if (i < 16) bass += s;
        else if (i < 44) mid += s;
        else high += s;
    }

    if (out_bass) *out_bass = (bass / 16.0f) * 2.2f;
    if (out_mid)  *out_mid  = (mid  / 28.0f) * 2.5f;
    if (out_high) *out_high = (high / 20.0f) * 3.0f;
}

void sparkles_vis_render(Rectangle bounds, float dt) {
    if (s_badge_timer > 0.0f) {
        s_badge_timer -= dt;
    }

    switch (s_vis_mode) {
        case VIS_ORB_FLUID:
            vis_orb_render(bounds, dt);
            break;
        case VIS_WAVES_FLOW:
            vis_waves_render(bounds, dt);
            break;
        case VIS_RADIAL_SPECTRUM:
            vis_radial_bars_render(bounds, dt);
            break;
        case VIS_VECTOR_SCOPE:
            vis_vector_scope_render(bounds, dt);
            break;
        case VIS_PARTICLE_VORTEX:
            vis_particles_render(bounds, dt);
            break;
        case VIS_CYMATICS:
            vis_cymatics_render(bounds, dt);
            break;
        case VIS_TERRAIN:
            vis_terrain_render(bounds, dt);
            break;
        case VIS_VECTOR_POLY:
            vis_polyhedron_render(bounds, dt);
            break;
        case VIS_TAPE_REELS:
            vis_reels_render(bounds, dt);
            break;
        case VIS_WARP_TUNNEL:
            vis_tunnel_render(bounds, dt);
            break;
        case VIS_SPARKLES:
            vis_sparkles_render(bounds, dt);
            break;
        case VIS_VOYAGER:
            vis_voyager_render(bounds, dt);
            break;
        case VIS_GALVANOMETER:
            vis_galvanometer_render(bounds, dt);
            break;
        case VIS_SEISMOGRAPH:
            vis_seismograph_render(bounds, dt);
            break;
        case VIS_GIMBAL:
            vis_gimbal_render(bounds, dt);
            break;
        case VIS_DANCER:
            vis_dancer_render(bounds, dt);
            break;
        default:
            break;
    }
}