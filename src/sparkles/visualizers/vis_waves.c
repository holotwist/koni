#include "sparkles_vis.h"
#include "sparkles_theme.h"
#include "state.h"
#include <math.h>

#define WAVE_RESOLUTION 240

void vis_waves_render(Rectangle b, float dt) {
    (void)dt;

    // Use 72% of total height to leave comfortable breathing room
    float active_h = b.height * 0.72f;
    float base_y = b.y + (b.height * 0.50f);
    float max_amp = (active_h * 0.50f);

    uint32_t rpos = atomic_load(&p_frames_consumed);
    uint32_t srate = atomic_load(&vis_srate);
    if (srate == 0) srate = 44100;

    // Window spans ~35ms of real live audio
    uint32_t window_frames = (srate * 35u) / 1000u;
    if (window_frames < WAVE_RESOLUTION) window_frames = WAVE_RESOLUTION;

    // Sample raw audio block and track window peak for auto-gain
    static float raw[WAVE_RESOLUTION];
    float window_peak = 0.01f;

    for (int i = 0; i < WAVE_RESOLUTION; i++) {
        uint32_t offset = (uint32_t)(((float)(WAVE_RESOLUTION - 1 - i) / (float)WAVE_RESOLUTION) * (float)window_frames);
        uint32_t idx = (rpos - offset) & VIS_BUF_MASK;
        raw[i] = (vis_ring_l[idx] + vis_ring_r[idx]) * 0.5f;

        float abs_s = fabsf(raw[i]);
        if (abs_s > window_peak) window_peak = abs_s;
    }

    // Smooth peak follower, fast attack
    static float s_smooth_peak = 0.7f;
    if (window_peak > s_smooth_peak) {
        s_smooth_peak += 0.15f * (window_peak - s_smooth_peak);
    } else {
        s_smooth_peak -= 0.02f * (s_smooth_peak - window_peak);
    }
    if (s_smooth_peak < 0.20f) s_smooth_peak = 0.20f;

    // Overall gain scale
    float dynamic_gain = 0.70f / s_smooth_peak;

    // Split audio stream into frequency bands using cascaded IIR filters
    static float band_bass[WAVE_RESOLUTION];
    static float band_lmid[WAVE_RESOLUTION];
    static float band_hmid[WAVE_RESOLUTION];
    static float band_treb[WAVE_RESOLUTION];

    float lp1 = 0.0f, lp2 = 0.0f, lp3 = 0.0f;
    for (int i = 0; i < WAVE_RESOLUTION; i++) {
        float x = raw[i];

        // Bass low-pass (~200Hz)
        lp1 += 0.055f * (x - lp1);
        band_bass[i] = lp1 * 1.10f;

        // Low-Mid band-pass (~600Hz)
        lp2 += 0.18f * (x - lp2);
        band_lmid[i] = (lp2 - lp1) * 1.15f;

        // High-Mid band-pass (~2.5kHz)
        lp3 += 0.45f * (x - lp3);
        band_hmid[i] = (lp3 - lp2) * 1.20f;

        // Treble high-pass (>3kHz)
        band_treb[i] = (x - lp3) * 1.25f;
    }

    // Band curves configuration
    struct {
        const float *data;
        Color color;
        float thickness;
    } lines[] = {
        { band_bass, (Color){ 0, 95, 255, 240 },   3.0f }, // Deep Blue (Sub-Bass)
        { band_lmid, (Color){ 255, 0, 210, 230 },  2.5f }, // Magenta (Low-Mids)
        { band_hmid, (Color){ 0, 240, 255, 240 },  2.5f }, // Cyan (High-Mids)
        { band_treb, (Color){ 255, 45, 45, 220 },  2.0f }, // Red (Highs / Transients)
        { raw,       (Color){ 255, 245, 120, 210 },1.8f }, // Yellow/White
    };
    int num_lines = sizeof(lines) / sizeof(lines[0]);

    for (int L = 0; L < num_lines; L++) {
        Vector2 prev = {0};
        bool has_prev = false;

        for (int i = 0; i < WAVE_RESOLUTION; i++) {
            float px = b.x + ((float)i / (float)(WAVE_RESOLUTION - 1) * b.width);
            float val = lines[L].data[i];

            // Soft-knee curve
            float soft_val = tanhf(val * dynamic_gain);
            float py = base_y - (soft_val * max_amp);

            Vector2 cur = { px, py };
            if (has_prev) {
                DrawLineEx(prev, cur, lines[L].thickness, lines[L].color);
            }
            prev = cur;
            has_prev = true;
        }
    }
}