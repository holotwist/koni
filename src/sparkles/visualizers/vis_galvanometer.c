#include "sparkles_vis.h"
#include "sparkles_theme.h"
#include "state.h"
#include <math.h>

#define VU_MIN_DB -20.0f
#define VU_MAX_DB  +3.0f

typedef struct {
    float angle;
    float velocity;
    float peak_led_alpha;
} MeterNeedle;

static MeterNeedle s_needle_l = { -50.0f, 0.0f, 0.0f };
static MeterNeedle s_needle_r = { -50.0f, 0.0f, 0.0f };

static inline float db_to_norm(float db) {
    if (db <= VU_MIN_DB) return 0.0f;
    if (db >= VU_MAX_DB) return 1.0f;
    return (db - VU_MIN_DB) / (VU_MAX_DB - VU_MIN_DB);
}

static void update_needle_physics(MeterNeedle *needle, float target_norm, float peak_amp, float dt) {
    // Sanitize state to recover from NaN/Inf if window was stalled or hibernated
    if (isnan(needle->angle) || isinf(needle->angle)) needle->angle = -48.0f;
    if (isnan(needle->velocity) || isinf(needle->velocity)) needle->velocity = 0.0f;

    // Clamp frame delta time
    if (dt > 0.05f) dt = 0.05f;
    if (dt <= 0.0001f) return;

    // Standard VU meter movement, ~50 degrees sweep (-48° to +48°)
    float target_angle = -48.0f + (target_norm * 96.0f);

    float omega_n = 20.0f;
    float zeta = 0.72f;

    // Two-step micro-integration
    const int sub_steps = 2;
    float sub_dt = dt / (float)sub_steps;

    for (int s = 0; s < sub_steps; s++) {
        float spring_accel = (omega_n * omega_n) * (target_angle - needle->angle);
        float damping_accel = 2.0f * zeta * omega_n * needle->velocity;
        float accel = spring_accel - damping_accel;

        needle->velocity += accel * sub_dt;
        needle->angle += needle->velocity * sub_dt;

        // Needles (-52° and +52°)
        if (needle->angle < -52.0f) {
            needle->angle = -52.0f;
            needle->velocity = -needle->velocity * 0.20f;
        } else if (needle->angle > 52.0f) {
            needle->angle = 52.0f;
            needle->velocity = -needle->velocity * 0.20f;
        }
    }

    // Peak LED trigger
    if (peak_amp > 0.985f) {
        needle->peak_led_alpha = 1.0f;
    } else {
        needle->peak_led_alpha -= dt * 4.0f;
        if (needle->peak_led_alpha < 0.0f) needle->peak_led_alpha = 0.0f;
    }
}

static void draw_single_meter(Rectangle rect, MeterNeedle *needle, const char *channel_label, float current_db) {
    DrawRectangleRec(rect, (Color){ 8, 9, 12, 255 });
    DrawRectangleLinesEx(rect, 1.0f, (Color){ 28, 30, 38, 255 });
    DrawNothingCornerBrackets(rect, 8.0f, (Color){ 45, 48, 58, 200 });

    Vector2 pivot = { rect.x + rect.width * 0.5f, rect.y + rect.height * 0.90f };
    float meter_radius = fminf(rect.width * 0.50f, rect.height * 0.74f);

    // Scale definitions
    static const struct { float db; const char *label; bool is_major; } s_ticks[] = {
        { -20.0f, "20", true  },
        { -15.0f, "",   false },
        { -10.0f, "10", true  },
        { -7.0f,  "7",  true  },
        { -5.0f,  "5",  true  },
        { -3.0f,  "3",  true  },
        { -2.0f,  "2",  false },
        { -1.0f,  "1",  false },
        {  0.0f,  "0",  true  },
        { +1.0f,  "1",  false },
        { +2.0f,  "2",  false },
        { +3.0f,  "+3", true  },
    };

    // Scale arc
    const int arc_segments = 40;
    float norm_zero = db_to_norm(0.0f);
    Vector2 prev_pt = {0};
    bool has_prev = false;

    for (int p = 0; p <= arc_segments; p++) {
        float frac = (float)p / (float)arc_segments;
        float ang_rad = (-48.0f + frac * 96.0f - 90.0f) * ((float)PI / 180.0f);
        Vector2 pt = { pivot.x + cosf(ang_rad) * meter_radius, pivot.y + sinf(ang_rad) * meter_radius };

        if (has_prev) {
            Color arc_col = (frac >= norm_zero) ? COLOR_ACCENT : ColorAlpha(COLOR_TEXT_MUTED, 0.45f);
            DrawLineEx(prev_pt, pt, (frac >= norm_zero) ? 2.0f : 1.2f, arc_col);
        }
        prev_pt = pt;
        has_prev = true;
    }

    // Ticks and decibel numbers
    for (size_t i = 0; i < sizeof(s_ticks) / sizeof(s_ticks[0]); i++) {
        float norm = db_to_norm(s_ticks[i].db);
        float ang_deg = -48.0f + norm * 96.0f;
        float ang_rad = (ang_deg - 90.0f) * ((float)PI / 180.0f);

        bool in_red = (s_ticks[i].db >= 0.0f);
        Color tick_col = in_red ? COLOR_ACCENT : (s_ticks[i].is_major ? COLOR_TEXT_PRIMARY : ColorAlpha(COLOR_TEXT_MUTED, 0.5f));
        float tick_len = s_ticks[i].is_major ? 7.0f : 4.0f;

        Vector2 p1 = { pivot.x + cosf(ang_rad) * (meter_radius - tick_len), pivot.y + sinf(ang_rad) * (meter_radius - tick_len) };
        Vector2 p2 = { pivot.x + cosf(ang_rad) * (meter_radius + 1.0f), pivot.y + sinf(ang_rad) * (meter_radius + 1.0f) };
        DrawLineEx(p1, p2, in_red ? 1.5f : 1.0f, tick_col);

        if (s_ticks[i].is_major && s_ticks[i].label[0] != '\0') {
            Vector2 txt_pos = {
                pivot.x + cosf(ang_rad) * (meter_radius - 18.0f),
                pivot.y + sinf(ang_rad) * (meter_radius - 18.0f)
            };
            int tw = MeasureSparklesText(s_ticks[i].label, 10);
            DrawSparklesText(s_ticks[i].label, (int)(txt_pos.x - tw * 0.5f), (int)(txt_pos.y - 5.0f), 10, tick_col);
        }
    }

    // Galvanometer needle
    float needle_rad = (needle->angle - 90.0f) * ((float)PI / 180.0f);
    Vector2 tip = {
        pivot.x + cosf(needle_rad) * (meter_radius + 3.0f),
        pivot.y + sinf(needle_rad) * (meter_radius + 3.0f)
    };

    // Soft drop shadow
    DrawLineEx((Vector2){ pivot.x + 1.5f, pivot.y + 1.5f }, (Vector2){ tip.x + 1.5f, tip.y + 1.5f }, 1.4f, ColorAlpha(BLACK, 0.40f));
    // Fine needle body
    Color needle_color = (needle->angle >= -48.0f + norm_zero * 96.0f) ? COLOR_ACCENT : COLOR_TEXT_PRIMARY;
    DrawLineEx(pivot, tip, 1.4f, needle_color);

    // Pivot hub
    DrawCircleV(pivot, 7.5f, (Color){ 14, 15, 20, 255 });
    DrawCircleLines((int)pivot.x, (int)pivot.y, 7.5f, (Color){ 36, 40, 52, 255 });
    DrawCircleV(pivot, 3.0f, COLOR_ACCENT);

    // Channel identification
    DrawSparklesText(channel_label, (int)rect.x + 12, (int)rect.y + 10, FONT_SIZE_XS, COLOR_TEXT_MUTED);

    const char *db_str = (current_db > -45.0f) ? TextFormat("%+4.1f dB", current_db) : " -inf dB";
    DrawSparklesText(db_str, (int)rect.x + 12, (int)(rect.y + rect.height - 22), FONT_SIZE_XS, needle_color);

    // Peak LED indicator
    Vector2 led_pos = { rect.x + rect.width - 20.0f, rect.y + 16.0f };
    DrawCircleV(led_pos, 4.0f, (Color){ 24, 20, 24, 255 });
    if (needle->peak_led_alpha > 0.01f) {
        DrawCircleV(led_pos, 7.0f, ColorAlpha(COLOR_ACCENT, needle->peak_led_alpha * 0.35f));
        DrawCircleV(led_pos, 3.5f, ColorAlpha(COLOR_ACCENT, needle->peak_led_alpha));
    }
    DrawCircleLines((int)led_pos.x, (int)led_pos.y, 4.0f, (Color){ 45, 48, 58, 200 });
    DrawSparklesText("PEAK", (int)led_pos.x - 32, (int)led_pos.y - 4, 9, COLOR_TEXT_MUTED);
}

void vis_galvanometer_render(Rectangle b, float dt) {
    uint32_t rpos = atomic_load(&p_frames_consumed);
    uint32_t srate = atomic_load(&vis_srate);
    if (srate == 0) srate = 44100;

    // Sample ~35ms RMS integration window
    uint32_t samples_to_check = (srate * 35u) / 1000u;
    if (samples_to_check > 2048) samples_to_check = 2048;
    if (samples_to_check < 64)   samples_to_check = 64;

    float sum_l = 0.0f, sum_r = 0.0f;
    float peak_l = 0.0f, peak_r = 0.0f;

    for (uint32_t i = 0; i < samples_to_check; i++) {
        uint32_t idx = (rpos - i) & VIS_BUF_MASK;
        float l = vis_ring_l[idx];
        float r = vis_ring_r[idx];

        sum_l += l * l;
        sum_r += r * r;

        float abs_l = fabsf(l);
        float abs_r = fabsf(r);
        if (abs_l > peak_l) peak_l = abs_l;
        if (abs_r > peak_r) peak_r = abs_r;
    }

    PlayState st = (PlayState)atomic_load(&play_state_atomic);
    bool is_stopped = (st == STATE_STOPPED);

    float rms_l = (!is_stopped && samples_to_check > 0) ? sqrtf(sum_l / (float)samples_to_check) : 0.0f;
    float rms_r = (!is_stopped && samples_to_check > 0) ? sqrtf(sum_r / (float)samples_to_check) : 0.0f;

    // Calibrated digital-to-analog reference, typical masters sit dynamically between -12 and +1 dB
    float db_l = (rms_l > 1.0e-5f) ? (20.0f * log10f(rms_l) + 4.5f) : -60.0f;
    float db_r = (rms_r > 1.0e-5f) ? (20.0f * log10f(rms_r) + 4.5f) : -60.0f;

    update_needle_physics(&s_needle_l, is_stopped ? 0.0f : db_to_norm(db_l), is_stopped ? 0.0f : peak_l, dt);
    update_needle_physics(&s_needle_r, is_stopped ? 0.0f : db_to_norm(db_r), is_stopped ? 0.0f : peak_r, dt);

    // Dual meter layout
    float gap = 16.0f;
    float pad_x = 24.0f;
    float pad_y = 16.0f;
    float avail_w = b.width - pad_x * 2.0f;
    float avail_h = b.height - pad_y * 2.0f;

    float meter_w = (avail_w - gap) * 0.5f;
    float meter_h = fminf(avail_h, meter_w * 0.65f);
    if (meter_h < 120.0f) meter_h = 120.0f;

    float start_y = b.y + pad_y + (avail_h - meter_h) * 0.5f;
    Rectangle rect_l = { b.x + pad_x, start_y, meter_w, meter_h };
    Rectangle rect_r = { b.x + pad_x + meter_w + gap, start_y, meter_w, meter_h };

    draw_single_meter(rect_l, &s_needle_l, "CH-1 · LEFT",  db_l);
    draw_single_meter(rect_r, &s_needle_r, "CH-2 · RIGHT", db_r);
}