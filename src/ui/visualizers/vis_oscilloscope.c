#include "ui_common.h"
#include "visualizers.h"
#include "vis_math.h"
#include "vis_renderer.h"
#include "ui_status.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static float *display_buf = NULL;
static int last_draw_w = 0;

static bool s_true_mode = true;
static const uint32_t s_timebases_ms[] = { 5, 10, 20, 40, 80 };
static int s_timebase_idx = 2; // 20ms default

bool vis_oscilloscope_is_true_mode(void) {
    return s_true_mode;
}

void vis_oscilloscope_toggle_true_mode(void) {
    s_true_mode = !s_true_mode;
}

void vis_oscilloscope_step_zoom(int delta) {
    s_timebase_idx += delta;
    if (s_timebase_idx < 0) s_timebase_idx = 0;
    int max_idx = (int)(sizeof(s_timebases_ms) / sizeof(s_timebases_ms[0])) - 1;
    if (s_timebase_idx > max_idx) s_timebase_idx = max_idx;
}

uint32_t vis_oscilloscope_get_timebase_ms(void) {
    return s_timebases_ms[s_timebase_idx];
}

bool vis_oscilloscope_handle_key(int ch) {
    if (ch == 't' || ch == 'T') {
        vis_oscilloscope_toggle_true_mode();
        ui_status_set("Scope: %s", s_true_mode ? "True Mode (Triggered)" : "Continuous (Rolling)");
        return true;
    }
    if (ch == '[' || ch == '<' || ch == ',') {
        vis_oscilloscope_step_zoom(-1);
        ui_status_set("Scope Timebase: %ums", s_timebases_ms[s_timebase_idx]);
        return true;
    }
    if (ch == ']' || ch == '>' || ch == '.') {
        vis_oscilloscope_step_zoom(+1);
        ui_status_set("Scope Timebase: %ums", s_timebases_ms[s_timebase_idx]);
        return true;
    }
    return false;
}

static float s_smooth_peak = 0.5f;

static inline float get_ring_mono(uint32_t idx) {
    uint32_t i = idx & VIS_BUF_MASK;
    return (vis_ring_l[i] + vis_ring_r[i]) * 0.5f;
}

static uint32_t find_trigger_offset(uint32_t cur_rpos, uint32_t window_samples, uint32_t srate) {
    uint32_t anchor = (cur_rpos - window_samples) & VIS_BUF_MASK;
    uint32_t search_span = (srate * 40u) / 1000u;
    if (search_span > 3000) search_span = 3000;

    // Measure peak in search window
    float peak = 0.0f;
    for (uint32_t k = 0; k < search_span; k++) {
        float s = fabsf(get_ring_mono(anchor - k));
        if (s > peak) peak = s;
    }

    if (peak < 0.005f) {
        return anchor;
    }

    // Schmitt trigger
    float hyst = peak * 0.10f;
    if (hyst < 0.002f) hyst = 0.002f;
    float lower_th = -hyst;
    float upper_th = +hyst;

    // Scan forward to locate the latest edge
    uint32_t start_idx = (anchor - search_span) & VIS_BUF_MASK;
    uint32_t latest_trigger = anchor;
    bool found = false;
    bool armed = false;

    for (uint32_t k = 0; k < search_span; k++) {
        uint32_t idx = (start_idx + k) & VIS_BUF_MASK;
        float s = get_ring_mono(idx);

        if (s <= lower_th) {
            armed = true;
        } else if (armed && s >= upper_th) {
            // Locate exact zero-crossing
            uint32_t zc = idx;
            for (int step = 0; step < 32; step++) {
                uint32_t prev_idx = (zc - 1) & VIS_BUF_MASK;
                if (get_ring_mono(prev_idx) <= 0.0f && get_ring_mono(zc) >= 0.0f) {
                    break;
                }
                zc--;
            }
            latest_trigger = zc & VIS_BUF_MASK;
            found = true;
            armed = false;
        }
    }

    return found ? latest_trigger : anchor;
}

void draw_vis_oscilloscope(int y, int x, int draw_w, int draw_h) {
    uint8_t *grid = vis_renderer_begin(draw_w, draw_h);

    if (draw_w != last_draw_w) {
        if (display_buf) free(display_buf);
        display_buf = malloc(sizeof(float) * 8192);
        last_draw_w = draw_w;
    }

    int px_w = draw_w * 2; int px_h = draw_h * 4;
    uint32_t srate = atomic_load(&vis_srate);
    if (srate == 0) srate = 44100;

    uint32_t window_ms = s_timebases_ms[s_timebase_idx];
    uint32_t window_samples = (srate * window_ms) / 1000u;
    if (window_samples < 32) window_samples = 32;

    uint32_t trigger_start = 0;
    if (s_true_mode) {
        trigger_start = find_trigger_offset(ui_cache.smooth_rpos, window_samples, srate);
    } else {
        trigger_start = (ui_cache.smooth_rpos - window_samples) & VIS_BUF_MASK;
    }

    int num_samples = px_w * 2;
    if (num_samples > 8192) num_samples = 8192;

    for (int i = 0; i < num_samples; i++) {
        uint32_t step = (uint32_t)(((uint64_t)i * (uint64_t)window_samples) / (uint64_t)num_samples);
        uint32_t idx = (trigger_start + step) & VIS_BUF_MASK;
        display_buf[i] = (vis_ring_l[idx] + vis_ring_r[idx]) * 0.5f;
    }

    PlayState st = (PlayState)atomic_load(&play_state_atomic);
    if (st == STATE_STOPPED) {
        memset(display_buf, 0, sizeof(float) * num_samples);
        s_smooth_peak = 1.0f;
    } else {
        float peak = 0.01f;
        for (int i = 0; i < num_samples; i++) {
            float v = fabsf(display_buf[i]);
            if (v > peak) peak = v;
        }

        // Vertical autogain
        if (peak > s_smooth_peak) {
            s_smooth_peak += 0.25f * (peak - s_smooth_peak);
        } else {
            s_smooth_peak -= 0.03f * (s_smooth_peak - peak);
        }
        if (s_smooth_peak < 0.05f) s_smooth_peak = 0.05f;
    }

    float scale = (1.0f / s_smooth_peak) * ((float)px_h / 2.2f);
    int center_y_px = px_h / 2;

    for (int i = 0; i < num_samples - 1; i++) {
        int x0 = (i * px_w) / num_samples;
        int x1 = ((i + 1) * px_w) / num_samples;
        int y0 = center_y_px - (int)(display_buf[i] * scale);
        int y1 = center_y_px - (int)(display_buf[i + 1] * scale);
        draw_braille_line(grid, draw_w, draw_h, x0, y0, x1, y1);
    }

    vis_renderer_end(y, x, draw_w, draw_h, VIS_COLOR_COLUMN);
}