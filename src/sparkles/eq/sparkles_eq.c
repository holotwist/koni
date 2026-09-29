#include "sparkles_eq.h"
#include "sparkles_theme.h"
#include "visualizers/sparkles_vis.h"
#include "equalizer.h"
#include "peq.h"
#include "rlgl.h"
#include "modals/sparkles_text_prompt.h"
#include "sparkles_nav.h"
#include "koni_paths.h"
#include "input/sparkles_input.h"
#include "state.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool s_eq_open = false;
static float s_anim_progress = 0.0f;

// PEQ graph node dragging
static int s_dragged_node = -1;
static int s_hovered_node = -1;

// GEQ slider dragging
static int s_dragged_eq_slider = -1;
static bool s_dragging_preamp = false;

// Presets lists
static float s_preset_list_scroll = 0.0f;
static char s_user_names[64][128];
static char s_user_paths[64][1024];
static int s_user_count = 0;

// PEQ preset selector popup
static bool s_peq_popup_open = false;
static float s_peq_popup_scroll = 0.0f;

#define EQ_GRID_PTS 320
#define PEQ_DB_MIN -24.0f
#define PEQ_DB_MAX  24.0f
#define PEQ_F_MIN   20.0f
#define PEQ_F_MAX   20000.0f

typedef struct {
    int band_idx;
    int field; // 0: freq, 1: gain, 2: q
} PEQCellEditContext;

static PEQCellEditContext s_peq_edit_ctx;

static void on_peq_cell_submit(const char *text, void *ud) {
    PEQCellEditContext *ctx = (PEQCellEditContext*)ud;
    if (!text || !text[0] || !ctx) return;
    float val = (float)atof(text);
    PEQBand b;
    if (peq_get_band(ctx->band_idx, &b)) {
        if (ctx->field == 0) {
            if (val < PEQ_F_MIN) val = PEQ_F_MIN;
            if (val > PEQ_F_MAX) val = PEQ_F_MAX;
            b.freq = val;
        } else if (ctx->field == 1) {
            if (val < PEQ_DB_MIN) val = PEQ_DB_MIN;
            if (val > PEQ_DB_MAX) val = PEQ_DB_MAX;
            b.gain_db = val;
        } else if (ctx->field == 2) {
            if (val < PEQ_MIN_Q) val = PEQ_MIN_Q;
            if (val > PEQ_MAX_Q) val = PEQ_MAX_Q;
            b.q = val;
        }
        peq_set_band(ctx->band_idx, &b);
        save_state();
    }
}

static void on_peq_preamp_submit(const char *text, void *ud) {
    (void)ud;
    if (!text || !text[0]) return;
    peq_set_preamp((float)atof(text));
    save_state();
}

bool sparkles_eq_is_dragging(void) {
    return (s_dragged_eq_slider != -1 || s_dragging_preamp || s_dragged_node != -1);
}

static void on_peq_preset_save_submit(const char *name, void *ud) {
    (void)ud;
    if (!name || !name[0]) return;
    char subpath[256];
    snprintf(subpath, sizeof(subpath), "peq/%s.txt", name);
    char path[1024];
    koni_get_path(path, sizeof(path), subpath);
    peq_save_file(path);
    s_user_count = peq_scan_user_presets(s_user_names, s_user_paths, 64);
}

void sparkles_eq_init(void) {
    s_eq_open = false;
    s_anim_progress = 0.0f;
    s_dragged_node = -1;
    s_hovered_node = -1;
    s_dragged_eq_slider = -1;
    s_dragging_preamp = false;
    s_preset_list_scroll = 0.0f;
    s_peq_popup_open = false;
    s_peq_popup_scroll = 0.0f;
    s_user_count = peq_scan_user_presets(s_user_names, s_user_paths, 64);
}

void sparkles_eq_toggle(void) {
    extern void sparkles_nav_set_y(int y);
    extern int sparkles_nav_get_target_y(void);

    if (sparkles_nav_get_target_y() == 1) {
        sparkles_nav_set_y(0);
        s_eq_open = false;
    } else {
        sparkles_nav_set_y(1);
        s_eq_open = true;
        s_user_count = peq_scan_user_presets(s_user_names, s_user_paths, 64);
    }
    s_dragged_node = -1;
    s_dragged_eq_slider = -1;
    s_dragging_preamp = false;
    s_peq_popup_open = false;
}

bool sparkles_eq_is_open(void) { return s_eq_open; }
bool sparkles_eq_is_visible(void) { return (s_eq_open || s_anim_progress > 0.001f); }
float sparkles_eq_get_anim_progress(void) { return s_anim_progress; }

static inline float freq_to_x(float f, Rectangle r) {
    if (f < PEQ_F_MIN) f = PEQ_F_MIN;
    if (f > PEQ_F_MAX) f = PEQ_F_MAX;
    float norm = log10f(f / PEQ_F_MIN) / log10f(PEQ_F_MAX / PEQ_F_MIN);
    return r.x + norm * r.width;
}

static inline float x_to_freq(float x, Rectangle r) {
    float norm = (x - r.x) / r.width;
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;
    return PEQ_F_MIN * powf(PEQ_F_MAX / PEQ_F_MIN, norm);
}

static inline float db_to_y(float db, Rectangle r, float db_min, float db_max) {
    if (db < db_min) db = db_min;
    if (db > db_max) db = db_max;
    float norm = (db_max - db) / (db_max - db_min);
    return r.y + norm * r.height;
}

static inline float y_to_db(float y, Rectangle r, float db_min, float db_max) {
    float norm = (y - r.y) / r.height;
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;
    return db_max - norm * (db_max - db_min);
}

static float calculate_single_band_db(const PEQBand *b, float f, float fs) {
    if (!b->enabled || fabsf(b->gain_db) < 0.01f) return 0.0f;
    if (b->type == PEQ_FILTER_HIGH_PASS || b->type == PEQ_FILTER_LOW_PASS) return 0.0f;

    float f0 = b->freq, q = b->q > 0.05f ? b->q : 0.7071f;
    float A = powf(10.0f, b->gain_db / 40.0f), w0 = 2.0f * PI * (f0 / fs);
    float cos_w = cosf(w0), sin_w = sinf(w0), alpha = sin_w / (2.0f * q);
    float b0, b1, b2, a0, a1, a2;

    if (b->type == PEQ_FILTER_LOW_SHELF) {
        float diff = (A + 1.0f / A) * (1.0f / q - 1.0f) + 2.0f;
        float alpha_term = sin_w * sqrtf(diff > 0.0f ? diff : 0.0f);
        b0 = A * ((A + 1.0f) - (A - 1.0f) * cos_w + alpha_term);
        b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cos_w);
        b2 = A * ((A + 1.0f) - (A - 1.0f) * cos_w - alpha_term);
        a0 = (A + 1.0f) + (A - 1.0f) * cos_w + alpha_term;
        a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cos_w);
        a2 = (A + 1.0f) + (A - 1.0f) * cos_w - alpha_term;
    } else if (b->type == PEQ_FILTER_HIGH_SHELF) {
        float diff = (A + 1.0f / A) * (1.0f / q - 1.0f) + 2.0f;
        float alpha_term = sin_w * sqrtf(diff > 0.0f ? diff : 0.0f);
        b0 = A * ((A + 1.0f) + (A - 1.0f) * cos_w + alpha_term);
        b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cos_w);
        b2 = A * ((A + 1.0f) - (A - 1.0f) * cos_w - alpha_term);
        a0 = (A + 1.0f) - (A - 1.0f) * cos_w + alpha_term;
        a1 = 2.0f * ((A - 1.0f) + (A + 1.0f) * cos_w);
        a2 = (A + 1.0f) - (A - 1.0f) * cos_w - alpha_term;
    } else {
        b0 = 1.0f + alpha * A; b1 = -2.0f * cos_w; b2 = 1.0f - alpha * A;
        a0 = 1.0f + alpha / A; a1 = -2.0f * cos_w; a2 = 1.0f - alpha / A;
    }

    float w = 2.0f * PI * (f / fs), cos1 = cosf(w), cos2 = cosf(2.0f * w), sin1 = sinf(w), sin2 = sinf(2.0f * w);
    float num_re = b0/a0 + (b1/a0)*cos1 + (b2/a0)*cos2, num_im = -((b1/a0)*sin1 + (b2/a0)*sin2);
    float den_re = 1.0f + (a1/a0)*cos1 + (a2/a0)*cos2, den_im = -((a1/a0)*sin1 + (a2/a0)*sin2);
    float num_sq = num_re*num_re + num_im*num_im, den_sq = den_re*den_re + den_im*den_im;
    if (den_sq > 1e-12f && num_sq > 1e-12f) return 10.0f * log10f(num_sq / den_sq);
    return 0.0f;
}

void sparkles_eq_update(float screen_w, float screen_h) {
    (void)screen_w; (void)screen_h;
    s_anim_progress = sparkles_nav_is_eq_open() ? 1.0f : 0.0f;
}

static bool DrawBtn(Rectangle bounds, const char *text, bool active) {
    Vector2 m = GetMousePosition();
    bool hover = !sparkles_input_is_consumed() && CheckCollisionPointRec(m, bounds);
    bool clicked = sparkles_input_consume_tap(bounds, NULL);

    Color bg = active ? ColorAlpha(COLOR_ACCENT, 0.18f) : (hover ? (Color){18, 19, 23, 255} : (Color){0, 0, 0, 0});
    Color fg = active ? COLOR_ACCENT : (hover ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED);

    DrawRectangleRec(bounds, bg);
    if (active) {
        DrawRectangle((int)bounds.x, (int)(bounds.y + bounds.height - 2), (int)bounds.width, 2, COLOR_ACCENT);
    }

    int tw = MeasureText(text, FONT_SIZE_SM);
    DrawText(text, (int)(bounds.x + (bounds.width - tw) / 2), (int)(bounds.y + (bounds.height - FONT_SIZE_SM) / 2), FONT_SIZE_SM, fg);
    return clicked;
}

void sparkles_eq_render(float screen_w, float screen_h) {
    if (s_anim_progress <= 0.001f) return;

    float inv = 1.0f - s_anim_progress;
    float ease = 1.0f - (inv * inv * inv);
    float ui_scale = sparkles_get_ui_scale();
    bool is_portrait = (screen_h > screen_w);

    float fall_scale = 1.0f + 0.15f * inv;
    float drop_y = -40.0f * (inv * inv);

    rlPushMatrix();
    rlTranslatef(screen_w * 0.5f, screen_h * 0.5f + drop_y, 0.0f);
    rlScalef(fall_scale, fall_scale, 1.0f);
    rlTranslatef(-screen_w * 0.5f, -screen_h * 0.5f, 0.0f);

    Vector2 mouse = GetMousePosition();
    bool interactive = (s_anim_progress >= 0.99f);

    DrawRectangle(0, 0, (int)screen_w, (int)screen_h, ColorAlpha((Color){ 3, 3, 5, 255 }, 0.70f * ease));

    float card_w = is_portrait ? fminf(screen_w - 16.0f * ui_scale, 480.0f * ui_scale) : fminf(screen_w - 40.0f * ui_scale, 860.0f * ui_scale);
    float card_h = is_portrait ? fminf(screen_h - 48.0f * ui_scale, 620.0f * ui_scale) : fminf(screen_h - 40.0f * ui_scale, 580.0f * ui_scale);
    float card_x = (screen_w - card_w) * 0.5f;
    float card_y = (screen_h - card_h) * 0.5f;
    Rectangle card_rect = { card_x, card_y, card_w, card_h };

    DrawRectangleRec(card_rect, (Color){ 9, 10, 14, 252 });
    DrawRectangleLinesEx(card_rect, 1.2f, (Color){ 34, 38, 48, 255 });
    DrawNothingCornerBrackets(card_rect, 10.0f * ui_scale, COLOR_ACCENT);

    EQMode mode = eq_get_mode();
    bool enabled = eq_is_enabled();

    // Top Header Bar
    float top_bar_h = 40.0f * ui_scale;
    DrawRectangle((int)card_rect.x, (int)card_rect.y, (int)card_rect.width, (int)top_bar_h, (Color){ 12, 13, 18, 220 });
    DrawLine((int)card_rect.x, (int)(card_rect.y + top_bar_h), (int)(card_rect.x + card_rect.width), (int)(card_rect.y + top_bar_h), (Color){ 28, 30, 40, 200 });

    float btn_h = 26.0f * ui_scale;
    float btn_y = card_rect.y + (top_bar_h - btn_h) * 0.5f;
    float btn_graph_w = 78.0f * ui_scale;
    float btn_param_w = 92.0f * ui_scale;

    if (interactive && DrawBtn((Rectangle){ card_rect.x + 10.0f * ui_scale, btn_y, btn_graph_w, btn_h }, "Graphic", mode == EQ_MODE_GRAPHIC)) {
        eq_set_mode(EQ_MODE_GRAPHIC);
        s_peq_popup_open = false;
    }
    if (interactive && DrawBtn((Rectangle){ card_rect.x + 14.0f * ui_scale + btn_graph_w, btn_y, btn_param_w, btn_h }, "Parametric", mode == EQ_MODE_PARAMETRIC)) {
        eq_set_mode(EQ_MODE_PARAMETRIC);
        s_peq_popup_open = false;
    }

    float btn_close_w = 54.0f * ui_scale;
    float btn_eq_w = 82.0f * ui_scale;
    float close_x = card_rect.x + card_rect.width - btn_close_w - 10.0f * ui_scale;
    float eq_x = close_x - btn_eq_w - 8.0f * ui_scale;

    if (interactive && DrawBtn((Rectangle){ eq_x, btn_y, btn_eq_w, btn_h }, enabled ? "EQU: ON" : "BYPASS", enabled)) {
        eq_toggle_enabled();
    }
    if (interactive && DrawBtn((Rectangle){ close_x, btn_y, btn_close_w, btn_h }, "✕ Esc", false)) {
        sparkles_eq_toggle();
    }

    if (mode == EQ_MODE_GRAPHIC) {
        const char **labels = eq_get_freq_labels();

        float sliders_y = card_rect.y + top_bar_h + 8.0f * ui_scale;
        float track_y = sliders_y + 6.0f * ui_scale;
        float track_h = is_portrait ? fminf(142.0f * ui_scale, card_rect.height * 0.27f) : fminf(165.0f * ui_scale, card_rect.height * 0.35f);
        float zero_y = db_to_y(0.0f, (Rectangle){0, track_y, 0, track_h}, EQ_MIN_GAIN_DB, EQ_MAX_GAIN_DB);

        for (int g = -12; g <= 12; g += 6) {
            float gy = db_to_y((float)g, (Rectangle){0, track_y, 0, track_h}, EQ_MIN_GAIN_DB, EQ_MAX_GAIN_DB);
            for (float lx = card_rect.x + 8.0f * ui_scale; lx < card_rect.x + card_rect.width - 8.0f * ui_scale; lx += 12.0f) {
                DrawLine((int)lx, (int)gy, (int)(lx + 6.0f), (int)gy, (g == 0) ? (Color){ 60, 64, 76, 200 } : (Color){ 24, 26, 32, 160 });
            }
        }

        float col_w = (card_rect.width - 16.0f * ui_scale) / 11.0f;
        float start_x = card_rect.x + 8.0f * ui_scale;

        int font_val = (int)(12.0f * ui_scale);
        int font_lbl = (int)(13.0f * ui_scale);

        // Preamp Slider
        {
            float cx = start_x + col_w * 0.5f;
            float pre = peq_get_preamp();
            float hy = db_to_y(pre, (Rectangle){0, track_y, 0, track_h}, -12.0f, 12.0f);

            DrawRectangle((int)(cx - 14.0f * ui_scale), (int)track_y, (int)(28.0f * ui_scale), (int)track_h, (Color){ 12, 13, 16, 200 });
            DrawRectangleLines((int)(cx - 14.0f * ui_scale), (int)track_y, (int)(28.0f * ui_scale), (int)track_h, (Color){ 28, 30, 38, 255 });
            DrawLineEx((Vector2){ cx, zero_y }, (Vector2){ cx, hy }, 3.5f, WHITE);

            Rectangle handle = { cx - 12.0f * ui_scale, hy - 9.0f * ui_scale, 24.0f * ui_scale, 18.0f * ui_scale };
            DrawRectangleRounded(handle, 0.4f, 4, (Color){ 36, 38, 48, 255 });
            DrawRectangleRoundedLinesEx(handle, 0.4f, 4, 1.2f, WHITE);
            DrawLine((int)handle.x + 5, (int)(handle.y + handle.height * 0.5f), (int)(handle.x + handle.width - 5), (int)(handle.y + handle.height * 0.5f), WHITE);

            DrawText(TextFormat("%+.1f", pre), (int)(cx - MeasureText(TextFormat("%+.1f", pre), font_val) * 0.5f), (int)(track_y + track_h + 4.0f * ui_scale), font_val, COLOR_TEXT_MUTED);
            DrawText("Pre", (int)(cx - MeasureText("Pre", font_lbl) * 0.5f), (int)(track_y + track_h + 18.0f * ui_scale), font_lbl, WHITE);

            Rectangle handle_hit = { cx - 18.0f * ui_scale, track_y, 36.0f * ui_scale, track_h };
            if (interactive && !s_dragging_preamp && s_dragged_eq_slider == -1 &&
                IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, handle_hit)) {
                s_dragging_preamp = true;
            }
            if (s_dragging_preamp) {
                sparkles_input_consume();
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                    peq_set_preamp(y_to_db(mouse.y, (Rectangle){0, track_y, 0, track_h}, -12.0f, 12.0f));
                }
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                    s_dragging_preamp = false;
                    save_state();
                }
            }
        }

        // 10-Band Sliders
        Color stem_colors[10] = {
            (Color){ 0, 240, 140, 255 },   (Color){ 0, 230, 160, 255 },
            (Color){ 0, 220, 200, 255 },   (Color){ 0, 180, 240, 255 },
            (Color){ 80, 160, 255, 255 },  (Color){ 255, 200, 50, 255 },
            (Color){ 255, 170, 40, 255 },  (Color){ 255, 120, 70, 255 },
            (Color){ 255, 80, 120, 255 },  (Color){ 240, 90, 210, 255 }
        };

        for (int i = 0; i < EQ_NUM_BANDS; i++) {
            float cx = start_x + (i + 1) * col_w + col_w * 0.5f;
            float gain = eq_get_band_gain(i);
            float hy = db_to_y(gain, (Rectangle){0, track_y, 0, track_h}, EQ_MIN_GAIN_DB, EQ_MAX_GAIN_DB);

            DrawLineEx((Vector2){ cx, track_y }, (Vector2){ cx, track_y + track_h }, 2.0f, (Color){ 28, 30, 38, 255 });

            Color col = stem_colors[i];
            if (fabsf(gain) > 0.2f) {
                DrawLineEx((Vector2){ cx, zero_y }, (Vector2){ cx, hy }, 3.5f, col);
            }

            Rectangle handle = { cx - 11.0f * ui_scale, hy - 14.0f * ui_scale, 22.0f * ui_scale, 28.0f * ui_scale };
            DrawRectangleRounded(handle, 0.45f, 6, (Color){ 24, 26, 32, 255 });
            DrawRectangleRoundedLinesEx(handle, 0.45f, 6, 1.2f, (fabsf(gain) > 0.2f) ? col : (Color){ 55, 60, 75, 255 });
            DrawLine((int)handle.x + 4, (int)hy, (int)(handle.x + handle.width - 4), (int)hy, WHITE);

            DrawText(TextFormat("%+.1f", gain), (int)(cx - MeasureText(TextFormat("%+.1f", gain), font_val) * 0.5f), (int)(track_y + track_h + 4.0f * ui_scale), font_val, (fabsf(gain) > 0.2f) ? col : COLOR_TEXT_MUTED);
            DrawText(labels[i], (int)(cx - MeasureText(labels[i], font_lbl) * 0.5f), (int)(track_y + track_h + 18.0f * ui_scale), font_lbl, COLOR_TEXT_PRIMARY);

            Rectangle handle_hit = { cx - 16.0f * ui_scale, track_y, 32.0f * ui_scale, track_h };
            if (interactive && !s_dragging_preamp && s_dragged_eq_slider == -1 &&
                IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, handle_hit)) {
                s_dragged_eq_slider = i;
            }
        }

        if (s_dragged_eq_slider != -1) {
            sparkles_input_consume();
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                float val = y_to_db(mouse.y, (Rectangle){0, track_y, 0, track_h}, EQ_MIN_GAIN_DB, EQ_MAX_GAIN_DB);
                eq_set_band_gain(s_dragged_eq_slider, val);
            }
            if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                s_dragged_eq_slider = -1;
            }
        }

        // Real-time Curve
        float curve_y = track_y + track_h + 34.0f * ui_scale;
        float curve_h = fminf(40.0f * ui_scale, card_rect.height * 0.12f);
        if (curve_h > 20.0f) {
            Rectangle cr_rect = { card_rect.x + 12.0f * ui_scale, curve_y, card_rect.width - 24.0f * ui_scale, curve_h };
            DrawRectangleRec(cr_rect, (Color){ 10, 11, 14, 255 });
            DrawRectangleLinesEx(cr_rect, 1.0f, (Color){ 28, 30, 38, 255 });

            float bins[32];
            sparkles_vis_get_spectrum(bins, 32);
            float bw = cr_rect.width / 32.0f;
            for (int s = 0; s < 32; s++) {
                float bh = bins[s] * (cr_rect.height - 4.0f);
                DrawRectangle((int)(cr_rect.x + s * bw + 1), (int)(cr_rect.y + cr_rect.height - bh - 2), (int)bw - 1, (int)bh, ColorAlpha(COLOR_ACCENT_DIM, 0.35f));
            }

            static float s_eq_curve[160];
            peq_calculate_curve(s_eq_curve, 160, 20.0f, 20000.0f);
            Vector2 prev_pt = {0};
            for (int p = 0; p < 160; p++) {
                float norm_x = (float)p / 159.0f;
                float px = cr_rect.x + norm_x * cr_rect.width;
                float py = db_to_y(s_eq_curve[p], cr_rect, -18.0f, +18.0f);
                Vector2 cur = { px, py };
                if (p > 0) DrawLineEx(prev_pt, cur, 1.8f, COLOR_ACCENT);
                prev_pt = cur;
            }
        }

        // Presets List
        float lower_y = curve_y + curve_h + 10.0f * ui_scale;
        float lower_h = (card_rect.y + card_rect.height) - lower_y - 8.0f * ui_scale;

        if (lower_h > 60.0f * ui_scale) {
            float act_btn_h = 24.0f * ui_scale;
            int cur_p = eq_get_current_preset();
            const char *cur_p_name = eq_get_preset_name(cur_p);

            DrawText(TextFormat("PRESETS (%s)", cur_p_name), (int)(card_rect.x + 14.0f * ui_scale), (int)(lower_y + 4.0f * ui_scale), FONT_SIZE_SM, COLOR_TEXT_MUTED);

            float r_btn_w = 56.0f * ui_scale;
            float s_btn_w = 56.0f * ui_scale;
            float reset_btn_x = card_rect.x + card_rect.width - r_btn_w - 12.0f * ui_scale;
            float save_btn_x  = reset_btn_x - s_btn_w - 6.0f * ui_scale;

            if (interactive && DrawBtn((Rectangle){ save_btn_x, lower_y, s_btn_w, act_btn_h }, "+ Save", false)) {
                sparkles_text_prompt_open(&(SparklesTextPromptConfig){
                    .tag = "PEQ PRESET",
                    .prompt = "Save EQ Preset As",
                    .submit_label = "Save",
                    .max_len = 48,
                    .on_submit = on_peq_preset_save_submit
                });
            }

            if (interactive && DrawBtn((Rectangle){ reset_btn_x, lower_y, r_btn_w, act_btn_h }, "Reset", false)) {
                eq_reset_flat();
            }

            float p_list_y = lower_y + act_btn_h + 6.0f * ui_scale;
            float p_list_h = lower_h - act_btn_h - 8.0f * ui_scale;
            Rectangle p_box = { card_rect.x + 12.0f * ui_scale, p_list_y, card_rect.width - 24.0f * ui_scale, p_list_h };

            DrawRectangleRec(p_box, (Color){ 6, 7, 10, 180 });
            DrawRectangleLinesEx(p_box, 1.0f, (Color){ 24, 26, 34, 255 });

            int total_presets = eq_get_preset_count();
            float item_h = 24.0f * ui_scale;
            float max_scr = fmaxf(0.0f, (float)total_presets * item_h - p_list_h);

            if (interactive && CheckCollisionPointRec(mouse, p_box)) {
                float scroll = sparkles_input_get_scroll_delta(p_box);
                if (scroll != 0.0f) s_preset_list_scroll += scroll * 16.0f * ui_scale;
            }
            if (s_preset_list_scroll < 0.0f) s_preset_list_scroll = 0.0f;
            if (s_preset_list_scroll > max_scr) s_preset_list_scroll = max_scr;

            BeginScissorMode((int)p_box.x, (int)p_box.y, (int)p_box.width, (int)p_box.height);

            for (int p = 0; p < total_presets; p++) {
                float item_y = p_list_y + p * item_h - s_preset_list_scroll;
                if (item_y + item_h < p_list_y || item_y > p_list_y + p_list_h) continue;

                Rectangle item_r = { p_box.x + 2.0f, item_y, p_box.width - 4.0f, item_h };
                bool is_sel = (cur_p == p);
                bool hover = interactive && CheckCollisionPointRec(mouse, item_r);

                if (is_sel) {
                    DrawRectangleRec(item_r, (Color){ 24, 28, 38, 255 });
                    DrawRectangle((int)item_r.x, (int)item_r.y, 3, (int)item_r.height, COLOR_ACCENT);
                } else if (hover) {
                    DrawRectangleRec(item_r, (Color){ 16, 17, 22, 255 });
                }

                DrawText(eq_get_preset_name(p), (int)(item_r.x + 8.0f * ui_scale), (int)(item_r.y + 4.0f * ui_scale), FONT_SIZE_SM, is_sel ? COLOR_ACCENT : (hover ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED));

                // Drag-aware tap to select
                if (interactive && sparkles_input_consume_tap(item_r, NULL)) {
                    eq_apply_preset(p);
                }
            }

            EndScissorMode();
        }
    } else {
        // Parametric (PEQ) mode
        float peq_act_y = card_rect.y + top_bar_h + 6.0f * ui_scale;
        float peq_act_h = 26.0f * ui_scale;

        int b_cnt = peq_get_builtin_preset_count();
        int total_peq = b_cnt + s_user_count;

        float cur_x = card_rect.x + 12.0f * ui_scale;
        const char *active_peq_name = peq_get_active_preset_name();
        const char *p_btn_label = is_portrait ? TextFormat("%s ▾", active_peq_name) : TextFormat("PEQ: %s ▾", active_peq_name);
        float p_btn_w = fminf((float)MeasureText(p_btn_label, FONT_SIZE_SM) + 16.0f * ui_scale, card_rect.width * 0.45f);
        Rectangle p_btn = { cur_x, peq_act_y, p_btn_w, peq_act_h };
        cur_x += p_btn_w + 4.0f * ui_scale;

        float save_btn_w = is_portrait ? (48.0f * ui_scale) : (54.0f * ui_scale);
        Rectangle save_btn = { cur_x, peq_act_y, save_btn_w, peq_act_h };
        cur_x += save_btn_w + 4.0f * ui_scale;

        float reset_btn_w = is_portrait ? (46.0f * ui_scale) : (52.0f * ui_scale);
        Rectangle reset_btn = { cur_x, peq_act_y, reset_btn_w, peq_act_h };
        cur_x += reset_btn_w + 4.0f * ui_scale;

        float pre_val = peq_get_preamp();
        const char *pre_str = TextFormat("Pre: %+.1fdB", pre_val);
        float pre_btn_w = (float)MeasureText(pre_str, FONT_SIZE_SM) + 12.0f * ui_scale;
        float pre_btn_x = card_rect.x + card_rect.width - pre_btn_w - 12.0f * ui_scale;
        if (pre_btn_x < cur_x) pre_btn_x = cur_x;
        Rectangle pre_btn = { pre_btn_x, peq_act_y, pre_btn_w, peq_act_h };

        if (interactive && DrawBtn(p_btn, p_btn_label, s_peq_popup_open)) {
            s_peq_popup_open = !s_peq_popup_open;
        }

        if (interactive && DrawBtn(save_btn, "+ Save", false)) {
            sparkles_text_prompt_open(&(SparklesTextPromptConfig){
                .tag = "PEQ PRESET",
                .prompt = "Save PEQ Preset As",
                .submit_label = "Save",
                .max_len = 48,
                .on_submit = on_peq_preset_save_submit
            });
        }

        if (interactive && DrawBtn(reset_btn, "Reset", false)) {
            peq_reset();
            save_state();
        }

        if (interactive && DrawBtn(pre_btn, pre_str, false)) {
            sparkles_text_prompt_open(&(SparklesTextPromptConfig){
                .tag = "PREAMP",
                .prompt = "Preamp Gain (dB)",
                .initial_text = TextFormat("%.1f", pre_val),
                .submit_label = "Set",
                .max_len = 16,
                .on_submit = on_peq_preamp_submit
            });
        }

        float graph_h = fminf(card_rect.height * 0.35f, 180.0f * ui_scale);
        Rectangle gr = { card_rect.x + 44 * ui_scale, peq_act_y + peq_act_h + 8.0f * ui_scale, card_rect.width - 60 * ui_scale, graph_h };

        DrawRectangleRec(gr, (Color){ 6, 7, 10, 180 });
        DrawRectangleLinesEx(gr, 1.0f, (Color){ 30, 32, 40, 200 });

        int peq_chart_font = (int)(12.0f * ui_scale);

        static const float db_ticks[] = { 24.0f, 12.0f, 0.0f, -12.0f, -24.0f };
        for (size_t i = 0; i < sizeof(db_ticks)/sizeof(db_ticks[0]); i++) {
            float y = db_to_y(db_ticks[i], gr, PEQ_DB_MIN, PEQ_DB_MAX);
            DrawLineEx((Vector2){ gr.x, y }, (Vector2){ gr.x + gr.width, y }, (db_ticks[i] == 0.0f) ? 1.5f : 1.0f, ColorAlpha(COLOR_TEXT_PRIMARY, db_ticks[i] == 0.0f ? 0.35f : 0.15f));
            DrawText(TextFormat("%+2.0fdB", db_ticks[i]), (int)(gr.x - 40.0f * ui_scale), (int)(y - peq_chart_font * 0.5f), peq_chart_font, COLOR_TEXT_MUTED);
        }

        static const struct { float f; const char *lbl; } f_ticks[] = {
            { 20.0f, "20" }, { 50.0f, "50" }, { 100.0f, "100" }, { 500.0f, "500" },
            { 1000.0f, "1k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" }, { 20000.0f, "20k" }
        };
        for (size_t i = 0; i < sizeof(f_ticks)/sizeof(f_ticks[0]); i++) {
            float x = freq_to_x(f_ticks[i].f, gr);
            DrawLine((int)x, (int)gr.y, (int)x, (int)(gr.y + gr.height), ColorAlpha(COLOR_TEXT_DARK, 0.15f));
            DrawText(f_ticks[i].lbl, (int)(x - MeasureText(f_ticks[i].lbl, peq_chart_font)/2), (int)(gr.y + gr.height + 4.0f * ui_scale), peq_chart_font, COLOR_TEXT_MUTED);
        }

        int band_count = peq_get_band_count();
        Color band_colors[10] = {
            (Color){ 100, 220, 140, 150 }, (Color){ 80, 200, 160, 150 },
            (Color){ 90, 210, 180, 150 },  (Color){ 110, 225, 150, 150 },
            (Color){ 100, 230, 130, 150 }, (Color){ 80, 215, 170, 150 },
            (Color){ 105, 220, 145, 150 }, (Color){ 95, 210, 165, 150 },
            (Color){ 115, 235, 140, 150 }, (Color){ 85, 205, 175, 150 }
        };

        for (int b_idx = 0; b_idx < band_count; b_idx++) {
            PEQBand b; peq_get_band(b_idx, &b);
            if (!b.enabled || fabsf(b.gain_db) < 0.1f) continue;
            Vector2 prev = {0}; bool has_prev = false;
            for (int i = 0; i < EQ_GRID_PTS; i++) {
                float f = x_to_freq(gr.x + ((float)i / (EQ_GRID_PTS - 1)) * gr.width, gr);
                float py = db_to_y(calculate_single_band_db(&b, f, 44100.0f), gr, PEQ_DB_MIN, PEQ_DB_MAX);
                Vector2 cur = { gr.x + ((float)i / (EQ_GRID_PTS - 1)) * gr.width, py };
                if (has_prev) DrawLineEx(prev, cur, 1.2f, band_colors[b_idx % 10]);
                prev = cur; has_prev = true;
            }
        }

        // Composite Curve
        static float curve_db[EQ_GRID_PTS];
        peq_calculate_curve(curve_db, EQ_GRID_PTS, PEQ_F_MIN, PEQ_F_MAX);
        Vector2 prev_comb = {0}; bool has_comb = false;
        for (int i = 0; i < EQ_GRID_PTS; i++) {
            float py = db_to_y(curve_db[i], gr, PEQ_DB_MIN, PEQ_DB_MAX);
            Vector2 cur = { gr.x + ((float)i / (EQ_GRID_PTS - 1)) * gr.width, py };
            if (has_comb) {
                DrawLineEx(prev_comb, cur, 4.0f, (Color){ 230, 80, 105, 90 });
                DrawLineEx(prev_comb, cur, 2.4f, (Color){ 120, 140, 255, 240 });
            }
            prev_comb = cur; has_comb = true;
        }

        // Graph Nodes
        s_hovered_node = -1;
        for (int i = 0; i < band_count; i++) {
            PEQBand b; peq_get_band(i, &b);
            float nx = freq_to_x(b.freq, gr);
            float ny = db_to_y(b.gain_db, gr, PEQ_DB_MIN, PEQ_DB_MAX);
            Vector2 npos = { nx, ny };

            bool is_hover = !s_peq_popup_open && CheckCollisionPointCircle(mouse, npos, 12.0f * ui_scale);
            if (is_hover) s_hovered_node = i;

            Color rc = b.enabled ? COLOR_ACCENT : COLOR_TEXT_DARK;
            if (i == s_dragged_node) rc = WHITE;

            int node_font = (int)(11.5f * ui_scale);
            DrawCircleV(npos, 8.0f * ui_scale, (Color){ 24, 25, 30, 255 });
            DrawCircleLines((int)npos.x, (int)npos.y, 8.0f * ui_scale, rc);
            DrawCircleV(npos, 3.5f * ui_scale, rc);
            DrawText(TextFormat("%d", i + 1), (int)(nx - MeasureText(TextFormat("%d", i + 1), node_font) * 0.5f), (int)(ny - 17.0f * ui_scale), node_font, rc);
        }

        if (interactive && !s_peq_popup_open) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && s_hovered_node != -1) {
                s_dragged_node = s_hovered_node;
            }
            if (s_dragged_node != -1) {
                sparkles_input_consume();
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                    PEQBand b; peq_get_band(s_dragged_node, &b);
                    b.freq = x_to_freq(mouse.x, gr);
                    b.gain_db = y_to_db(mouse.y, gr, PEQ_DB_MIN, PEQ_DB_MAX);
                    peq_set_band(s_dragged_node, &b);
                } else {
                    s_dragged_node = -1;
                    save_state();
                }
            }
            if (s_hovered_node != -1) {
                float wheel = GetMouseWheelMove();
                if (wheel != 0.0f) {
                    PEQBand b; peq_get_band(s_hovered_node, &b);
                    b.q += wheel * 0.15f;
                    if (b.q < PEQ_MIN_Q) b.q = PEQ_MIN_Q;
                    if (b.q > PEQ_MAX_Q) b.q = PEQ_MAX_Q;
                    peq_set_band(s_hovered_node, &b);
                    save_state();
                }
            }
        }

        // Band Table
        float table_y = gr.y + gr.height + 12.0f * ui_scale;
        float side_margin = is_portrait ? 8.0f * ui_scale : 24.0f * ui_scale;
        float avail_table_w = card_rect.width - (side_margin * 2.0f);
        float cw = avail_table_w / 6.0f;
        float ty = table_y + 4.0f;

        int header_font = is_portrait ? 9 : 11;
        DrawText("BND", (int)(card_rect.x + side_margin), (int)ty, header_font, COLOR_TEXT_MUTED);
        DrawText("TYP", (int)(card_rect.x + side_margin + cw * 1), (int)ty, header_font, COLOR_TEXT_MUTED);
        DrawText("FREQ", (int)(card_rect.x + side_margin + cw * 2), (int)ty, header_font, COLOR_TEXT_MUTED);
        DrawText("GAIN", (int)(card_rect.x + side_margin + cw * 3), (int)ty, header_font, COLOR_TEXT_MUTED);
        DrawText("Q", (int)(card_rect.x + side_margin + cw * 4), (int)ty, header_font, COLOR_TEXT_MUTED);
        DrawText("ON", (int)(card_rect.x + side_margin + cw * 5), (int)ty, header_font, COLOR_TEXT_MUTED);

        float row_spacing = 20.0f * ui_scale;
        for (int i = 0; i < band_count; i++) {
            PEQBand b; peq_get_band(i, &b);
            float row_y = ty + 18.0f * ui_scale + i * row_spacing;
            if (row_y + row_spacing > card_rect.y + card_rect.height) break;

            if (i % 2 == 1) {
                DrawRectangle((int)(card_rect.x + side_margin), (int)row_y - 2, (int)avail_table_w, (int)row_spacing, (Color){ 6, 6, 8, 255 });
            }

            Color band_badge_col = b.enabled ? band_colors[i % 10] : COLOR_TEXT_DARK;
            DrawCircle((int)(card_rect.x + side_margin + 6), (int)(row_y + 7 * ui_scale), 3.5f * ui_scale, band_badge_col);
            DrawText(TextFormat("%d", i + 1), (int)(card_rect.x + side_margin + 16 * ui_scale), (int)row_y, FONT_SIZE_SM, b.enabled ? COLOR_TEXT_PRIMARY : COLOR_TEXT_DARK);

            Rectangle type_rec = { card_rect.x + side_margin + cw * 1, row_y - 2, cw - 8, row_spacing - 2 };
            bool hover_type = !s_peq_popup_open && CheckCollisionPointRec(mouse, type_rec);
            if (hover_type) DrawRectangleRec(type_rec, (Color){ 16, 16, 20, 255 });
            DrawText(peq_get_filter_name(b.type), (int)type_rec.x + 4, (int)row_y, FONT_SIZE_SM, hover_type ? COLOR_ACCENT : COLOR_TEXT_PRIMARY);
            if (interactive && !s_peq_popup_open && sparkles_input_consume_tap(type_rec, NULL)) {
                peq_cycle_band_type(i);
                save_state();
            }

            Rectangle f_rec = { card_rect.x + side_margin + cw * 2, row_y - 2, cw - 4, row_spacing - 2 };
            bool hover_f = !s_peq_popup_open && CheckCollisionPointRec(mouse, f_rec);
            if (hover_f) DrawRectangleRec(f_rec, (Color){ 20, 22, 30, 255 });
            DrawText(TextFormat("%.0fHz", b.freq), (int)f_rec.x + 4, (int)row_y, FONT_SIZE_SM, hover_f ? COLOR_ACCENT : COLOR_TEXT_PRIMARY);
            if (interactive && !s_peq_popup_open && sparkles_input_consume_tap(f_rec, NULL)) {
                s_peq_edit_ctx = (PEQCellEditContext){ i, 0 };
                sparkles_text_prompt_open(&(SparklesTextPromptConfig){
                    .tag = TextFormat("BAND %d FREQ", i + 1),
                    .prompt = "Enter Frequency (20 - 20000 Hz)",
                    .initial_text = TextFormat("%.0f", b.freq),
                    .submit_label = "Set",
                    .max_len = 16,
                    .on_submit = on_peq_cell_submit,
                    .user_data = &s_peq_edit_ctx
                });
            }

            Rectangle g_rec = { card_rect.x + side_margin + cw * 3, row_y - 2, cw - 4, row_spacing - 2 };
            bool hover_g = !s_peq_popup_open && CheckCollisionPointRec(mouse, g_rec);
            if (b.type != PEQ_FILTER_HIGH_PASS && b.type != PEQ_FILTER_LOW_PASS) {
                if (hover_g) DrawRectangleRec(g_rec, (Color){ 20, 22, 30, 255 });
                Color g_color = (b.gain_db > 0.05f) ? (Color){ 100, 220, 140, 255 } : ((b.gain_db < -0.05f) ? (Color){ 230, 110, 110, 255 } : COLOR_TEXT_MUTED);
                DrawText(TextFormat("%+.1fdB", b.gain_db), (int)g_rec.x + 4, (int)row_y, FONT_SIZE_SM, hover_g ? COLOR_ACCENT : g_color);
                if (interactive && !s_peq_popup_open && sparkles_input_consume_tap(g_rec, NULL)) {
                    s_peq_edit_ctx = (PEQCellEditContext){ i, 1 };
                    sparkles_text_prompt_open(&(SparklesTextPromptConfig){
                        .tag = TextFormat("BAND %d GAIN", i + 1),
                        .prompt = "Enter Gain (-24 to +24 dB)",
                        .initial_text = TextFormat("%.1f", b.gain_db),
                        .submit_label = "Set",
                        .max_len = 16,
                        .on_submit = on_peq_cell_submit,
                        .user_data = &s_peq_edit_ctx
                    });
                }
            } else {
                DrawText("—", (int)g_rec.x + 4, (int)row_y, FONT_SIZE_SM, COLOR_TEXT_DARK);
            }

            Rectangle q_rec = { card_rect.x + side_margin + cw * 4, row_y - 2, cw - 4, row_spacing - 2 };
            bool hover_q = !s_peq_popup_open && CheckCollisionPointRec(mouse, q_rec);
            if (hover_q) DrawRectangleRec(q_rec, (Color){ 20, 22, 30, 255 });
            DrawText(TextFormat("%.2f", b.q), (int)q_rec.x + 4, (int)row_y, FONT_SIZE_SM, hover_q ? COLOR_ACCENT : COLOR_TEXT_PRIMARY);
            if (interactive && !s_peq_popup_open && sparkles_input_consume_tap(q_rec, NULL)) {
                s_peq_edit_ctx = (PEQCellEditContext){ i, 2 };
                sparkles_text_prompt_open(&(SparklesTextPromptConfig){
                    .tag = TextFormat("BAND %d Q FACTOR", i + 1),
                    .prompt = "Enter Q (0.1 to 20.0)",
                    .initial_text = TextFormat("%.2f", b.q),
                    .submit_label = "Set",
                    .max_len = 16,
                    .on_submit = on_peq_cell_submit,
                    .user_data = &s_peq_edit_ctx
                });
            }

            Rectangle st_rec = { card_rect.x + side_margin + cw * 5, row_y - 2, 44 * ui_scale, row_spacing - 2 };
            bool hover_st = !s_peq_popup_open && CheckCollisionPointRec(mouse, st_rec);
            Color st_col = b.enabled ? COLOR_ACCENT : COLOR_TEXT_DARK;
            DrawText(b.enabled ? "ON" : "OFF", (int)st_rec.x + 4, (int)row_y, FONT_SIZE_SM, hover_st ? COLOR_TEXT_PRIMARY : st_col);
            if (interactive && !s_peq_popup_open && sparkles_input_consume_tap(st_rec, NULL)) {
                peq_toggle_band_enabled(i);
                save_state();
            }
        }

        // PEQ Preset Dropdown Selector
        if (s_peq_popup_open) {
            float pop_w = fminf(300.0f * ui_scale, card_rect.width - 24.0f * ui_scale);
            float pop_h = fminf(320.0f * ui_scale, card_rect.height - (peq_act_y - card_rect.y) - peq_act_h - 16.0f * ui_scale);
            Rectangle pop_box = { p_btn.x, p_btn.y + p_btn.height + 4.0f * ui_scale, pop_w, pop_h };

            // Dismiss only when tapped outside the popup box
            if (sparkles_input_consume_tap_outside(pop_box, NULL)) {
                s_peq_popup_open = false;
            }

            DrawRectangleRec(pop_box, (Color){ 12, 13, 17, 252 });
            DrawRectangleLinesEx(pop_box, 1.2f, COLOR_ACCENT);
            DrawNothingCornerBrackets(pop_box, 8.0f * ui_scale, COLOR_ACCENT);

            float item_h = 26.0f * ui_scale;
            float max_scr = fmaxf(0.0f, (float)total_peq * item_h - (pop_h - 8.0f * ui_scale));

            if (CheckCollisionPointRec(mouse, pop_box)) {
                float scroll = sparkles_input_get_scroll_delta(pop_box);
                if (scroll != 0.0f) s_peq_popup_scroll += scroll * 16.0f * ui_scale;
            }
            if (s_peq_popup_scroll < 0.0f) s_peq_popup_scroll = 0.0f;
            if (s_peq_popup_scroll > max_scr) s_peq_popup_scroll = max_scr;

            BeginScissorMode((int)pop_box.x + 1, (int)pop_box.y + 1, (int)pop_box.width - 2, (int)pop_box.height - 2);

            for (int p = 0; p < total_peq; p++) {
                float iy = pop_box.y + 4.0f * ui_scale + p * item_h - s_peq_popup_scroll;
                if (iy + item_h < pop_box.y || iy > pop_box.y + pop_box.height) continue;

                Rectangle item_r = { pop_box.x + 4.0f * ui_scale, iy, pop_box.width - 8.0f * ui_scale, item_h };
                const char *name = (p < b_cnt) ? peq_get_builtin_preset(p)->name : s_user_names[p - b_cnt];
                bool hover = CheckCollisionPointRec(mouse, item_r);

                if (hover) {
                    DrawRectangleRec(item_r, (Color){ 24, 28, 38, 255 });
                }

                DrawText(name, (int)(item_r.x + 8.0f * ui_scale), (int)(item_r.y + 5.0f * ui_scale), FONT_SIZE_SM, hover ? COLOR_ACCENT : COLOR_TEXT_PRIMARY);

                if (sparkles_input_consume_tap(item_r, NULL)) {
                    if (p < b_cnt) peq_apply_builtin_preset(p);
                    else peq_load_file(s_user_paths[p - b_cnt]);
                    s_peq_popup_open = false;
                    save_state();
                }
            }

            EndScissorMode();
            sparkles_input_block_area(pop_box);
        }
    }

    // Tap outside card closes EQ
    if (interactive && !s_dragging_preamp && s_dragged_eq_slider == -1 && s_dragged_node == -1 && !s_peq_popup_open) {
        Vector2 tap;
        if (sparkles_input_consume_tap((Rectangle){ 0, 0, screen_w, screen_h }, &tap)) {
            if (!CheckCollisionPointRec(tap, card_rect)) {
                sparkles_eq_toggle();
            }
        }
    }
    sparkles_input_block_area(card_rect);

    rlPopMatrix();
}