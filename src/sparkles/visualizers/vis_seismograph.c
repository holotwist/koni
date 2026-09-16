#include "sparkles_vis.h"
#include "sparkles_theme.h"
#include "state.h"
#include <math.h>
#include <string.h>

#define SEISMO_HISTORY 320

typedef struct {
    float bass;
    float mid;
    float high;
} SeismoSlice;

static SeismoSlice s_history[SEISMO_HISTORY];
static float s_paper_offset = 0.0f;
static float s_feed_timer = 0.0f;
static float s_mid_phase = 0.0f;
static float s_prev_high = 0.0f;
static float s_trans_decay = 0.0f;
static bool s_inited = false;

void vis_seismograph_render(Rectangle b, float dt) {
    if (!s_inited) {
        memset(s_history, 0, sizeof(s_history));
        s_paper_offset = 0.0f;
        s_feed_timer = 0.0f;
        s_mid_phase = 0.0f;
        s_prev_high = 0.0f;
        s_trans_decay = 0.0f;
        s_inited = true;
    }

    PlayState st = (PlayState)atomic_load(&play_state_atomic);
    bool playing = (st == STATE_PLAYING);

    float bass = 0.0f, mid = 0.0f, high = 0.0f;
    sparkles_vis_get_bands(&bass, &mid, &high);

    uint32_t rpos = atomic_load(&p_frames_consumed);
    uint32_t idx = rpos & VIS_BUF_MASK;
    float raw_wave = (vis_ring_l[idx] + vis_ring_r[idx]) * 0.5f;

    // Feed paper continuously when playing (~45 updates/sec)
    s_feed_timer += dt;
    if (s_feed_timer >= 0.022f) {
        s_feed_timer = 0.0f;

        // Shift slices left
        for (int i = 0; i < SEISMO_HISTORY - 1; i++) {
            s_history[i] = s_history[i + 1];
        }

        if (playing) {
            // Channel 1, Sub/Tremor
            float val_bass = bass * 1.35f + raw_wave * 0.45f;

            // Channel 2, Resonance
            s_mid_phase += 0.35f + (mid * 0.40f);
            float val_mid = mid * 1.20f + sinf(s_mid_phase) * (mid * 0.40f);

            // Channel 3, Transient Snap
            float high_delta = high - s_prev_high;
            s_prev_high = high;
            if (high_delta < 0.0f) high_delta = 0.0f;

            float spike = high_delta * 3.2f + (high * 0.35f);
            if (spike > s_trans_decay) {
                s_trans_decay = spike;
            } else {
                s_trans_decay -= 0.15f;
                if (s_trans_decay < 0.0f) s_trans_decay = 0.0f;
            }

            s_history[SEISMO_HISTORY - 1] = (SeismoSlice){
                .bass = val_bass,
                .mid  = val_mid,
                .high = s_trans_decay
            };
        } else {
            s_history[SEISMO_HISTORY - 1] = (SeismoSlice){ 0 };
        }
    }

    if (playing) {
        s_paper_offset += dt * 45.0f;
    }

    // Chart enclosure
    Rectangle chart = { b.x + 20.0f, b.y + 14.0f, b.width - 40.0f, b.height - 28.0f };
    DrawRectangleRec(chart, (Color){ 8, 9, 12, 255 });
    DrawRectangleLinesEx(chart, 1.0f, (Color){ 28, 30, 38, 255 });
    DrawNothingCornerBrackets(chart, 8.0f, (Color){ 45, 48, 58, 200 });

    // Sprocket feed holes (top and bottom margins)
    float hole_spacing = 18.0f;
    float hole_shift = fmodf(s_paper_offset, hole_spacing);
    for (float hx = chart.x + 10.0f - hole_shift; hx < chart.x + chart.width - 10.0f; hx += hole_spacing) {
        if (hx < chart.x + 6.0f) continue;
        DrawCircle((int)hx, (int)chart.y + 6, 1.6f, (Color){ 28, 30, 38, 255 });
        DrawCircle((int)hx, (int)(chart.y + chart.height - 6), 1.6f, (Color){ 28, 30, 38, 255 });
    }

    // Horizontal baseline calibration rails
    float margin_top = 22.0f;
    float track_h = (chart.height - margin_top * 2.0f) / 3.0f;
    float baseline_bass = chart.y + margin_top + track_h * 0.5f;
    float baseline_mid  = chart.y + margin_top + track_h * 1.5f;
    float baseline_high = chart.y + margin_top + track_h * 2.5f;

    DrawLine((int)chart.x + 10, (int)baseline_bass, (int)(chart.x + chart.width - 10), (int)baseline_bass, (Color){ 20, 22, 28, 255 });
    DrawLine((int)chart.x + 10, (int)baseline_mid,  (int)(chart.x + chart.width - 10), (int)baseline_mid,  (Color){ 20, 22, 28, 255 });
    DrawLine((int)chart.x + 10, (int)baseline_high, (int)(chart.x + chart.width - 10), (int)baseline_high, (Color){ 20, 22, 28, 255 });

    // Scrolling vertical time ticks
    float tick_spacing = 60.0f;
    float tick_shift = fmodf(s_paper_offset, tick_spacing);
    for (float tx = chart.x + 10.0f - tick_shift; tx < chart.x + chart.width - 40.0f; tx += tick_spacing) {
        if (tx < chart.x + 8.0f) continue;
        DrawLine((int)tx, (int)chart.y + 12, (int)tx, (int)(chart.y + chart.height - 12), (Color){ 16, 18, 24, 180 });
    }

    // Track labels
    DrawSparklesText("TREMOR / SUB", (int)chart.x + 12, (int)baseline_bass - 24, 9, COLOR_ACCENT);
    DrawSparklesText("RESONANCE",     (int)chart.x + 12, (int)baseline_mid - 24,  9, (Color){ 140, 180, 255, 200 });
    DrawSparklesText("TRANSIENT SNAP", (int)chart.x + 12, (int)baseline_high - 24, 9, (Color){ 230, 200, 120, 200 });

    // Render traces across chart
    float pen_margin_x = 42.0f;
    float plot_w = chart.width - pen_margin_x - 16.0f;
    float step_x = plot_w / (float)(SEISMO_HISTORY - 1);

    Vector2 prev_bass_pt = {0}, prev_mid_pt = {0}, prev_high_pt = {0};
    bool has_prev = false;

    float max_pen_amp = track_h * 0.44f;

    for (int i = 0; i < SEISMO_HISTORY; i++) {
        float px = chart.x + 12.0f + (float)i * step_x;

        // Fixed coordinates
        float bass_disp = s_history[i].bass * max_pen_amp;
        float mid_disp  = s_history[i].mid  * max_pen_amp;
        float high_disp = s_history[i].high * max_pen_amp;

        Vector2 cur_bass = { px, baseline_bass - bass_disp };
        Vector2 cur_mid  = { px, baseline_mid  - mid_disp  };
        Vector2 cur_high = { px, baseline_high - high_disp };

        if (has_prev) {
            float fade = 0.30f + 0.70f * ((float)i / (float)SEISMO_HISTORY);
            DrawLineEx(prev_bass_pt, cur_bass, 1.6f, ColorAlpha(COLOR_ACCENT, fade));
            DrawLineEx(prev_mid_pt,  cur_mid,  1.3f, ColorAlpha((Color){ 140, 180, 255, 255 }, fade * 0.85f));
            DrawLineEx(prev_high_pt, cur_high, 1.1f, ColorAlpha((Color){ 230, 200, 120, 255 }, fade * 0.80f));
        }

        prev_bass_pt = cur_bass;
        prev_mid_pt  = cur_mid;
        prev_high_pt = cur_high;
        has_prev = true;
    }

    // Stylus carriages (Right Edge)
    float pen_tip_x = chart.x + 12.0f + plot_w;
    float arm_base_x = chart.x + chart.width - 8.0f;

    Vector2 tips[3] = { prev_bass_pt, prev_mid_pt, prev_high_pt };
    Color arm_colors[3] = { COLOR_ACCENT, (Color){ 140, 180, 255, 255 }, (Color){ 230, 200, 120, 255 } };

    for (int p = 0; p < 3; p++) {
        Vector2 t = { pen_tip_x, tips[p].y };
        Vector2 base = { arm_base_x, (p == 0 ? baseline_bass : (p == 1 ? baseline_mid : baseline_high)) };

        // Stylus armature
        DrawLineEx(base, t, 1.8f, (Color){ 45, 48, 58, 255 });
        DrawCircleV(base, 3.5f, (Color){ 36, 40, 52, 255 });
        DrawCircleV(t, 2.5f, arm_colors[p]);
        DrawCircleLines((int)t.x, (int)t.y, 4.5f, ColorAlpha(arm_colors[p], 0.35f));
    }

    // Header strip telemetry
    const char *status_str = playing ? "RECORDING" : "STANDBY";
    int tw = MeasureSparklesText(status_str, FONT_SIZE_XS);
    DrawSparklesText(status_str, (int)(chart.x + chart.width - tw - 14), (int)chart.y + 8, FONT_SIZE_XS,
                     playing ? COLOR_ACCENT : COLOR_TEXT_DARK);
}