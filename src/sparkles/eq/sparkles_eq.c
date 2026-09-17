#include "sparkles_eq.h"
#include "sparkles_theme.h"
#include "equalizer.h"
#include "peq.h"
#include "rlgl.h"
#include "modals/sparkles_text_prompt.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool s_eq_open = false;
static float s_anim_progress = 0.0f; // 0.0 = Closed, 1.0 = Fully Open

// PEQ Graph Interaction
static int s_dragged_node = -1;
static int s_hovered_node = -1;

// GEQ Interaction
static int s_dragged_slider = -1;

// Presets Sidebar
static int s_preset_scroll = 0;
static char s_user_names[64][128];
static char s_user_paths[64][1024];
static int s_user_count = 0;

// Table In-Place Numeric Cell Editing
typedef enum {
    CELL_EDIT_NONE = 0,
    CELL_EDIT_FREQ,
    CELL_EDIT_GAIN,
    CELL_EDIT_Q
} CellEditField;

static int s_edit_band_idx = -1;
static CellEditField s_edit_field = CELL_EDIT_NONE;
static char s_edit_buf[32] = {0};
static int s_edit_buf_len = 0;

#define EQ_GRID_PTS 320
#define PEQ_DB_MIN -24.0f
#define PEQ_DB_MAX  24.0f
#define PEQ_F_MIN   20.0f
#define PEQ_F_MAX   20000.0f

static void on_peq_preset_save_submit(const char *name, void *ud) {
    (void)ud;
    if (!name || !name[0]) return;
    const char *home = getenv("HOME");
    if (home) {
        char path[1024];
        snprintf(path, sizeof(path), "%s/.config/koni/peq/%s.txt", home, name);
        peq_save_file(path);
        s_user_count = peq_scan_user_presets(s_user_names, s_user_paths, 64);
    }
}

void sparkles_eq_init(void) {
    s_eq_open = false;
    s_anim_progress = 0.0f;
    s_dragged_node = -1;
    s_hovered_node = -1;
    s_dragged_slider = -1;
    s_user_count = peq_scan_user_presets(s_user_names, s_user_paths, 64);
}

void sparkles_eq_toggle(void) {
    s_eq_open = !s_eq_open;
    if (s_eq_open) {
        s_user_count = peq_scan_user_presets(s_user_names, s_user_paths, 64);
    } else {
        s_dragged_node = -1;
        s_dragged_slider = -1;
        s_edit_band_idx = -1;
        s_edit_field = CELL_EDIT_NONE;
    }
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

// Biquad math for individual curves
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
        b2 = A * ((A + 1.0f) + (A - 1.0f) * cos_w - alpha_term);
        a0 = (A + 1.0f) - (A - 1.0f) * cos_w + alpha_term;
        a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cos_w);
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
    float dt = GetFrameTime();
    float speed = 5.4f;
    if (s_eq_open) {
        s_anim_progress += dt * speed;
        if (s_anim_progress > 1.0f) s_anim_progress = 1.0f;
    } else {
        s_anim_progress -= dt * speed;
        if (s_anim_progress < 0.0f) s_anim_progress = 0.0f;
    }
}

// Minimal Flat Button Helper (borderless, zero bevel)
static bool DrawBtn(Rectangle bounds, const char *text, bool active) {
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, bounds);
    bool clicked = hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

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

    // Falling from above into the past window position
    float fall_scale = 1.0f + 0.30f * inv;
    float drop_y = -60.0f * (inv * inv);

    rlPushMatrix();
    rlTranslatef(screen_w * 0.5f, screen_h * 0.5f + drop_y, 0.0f);
    rlScalef(fall_scale, fall_scale, 1.0f);
    rlTranslatef(-screen_w * 0.5f, -screen_h * 0.5f, 0.0f);

    Rectangle b = { 0.0f, 0.0f, screen_w, screen_h };
    Vector2 mouse = GetMousePosition();

    bool interactive = (s_anim_progress >= 0.99f);

    // Semi-transparent backdrop
    DrawRectangleRec(b, ColorAlpha((Color){ 3, 3, 5, 255 }, 0.70f * ease));

    // Numeric keyboard input
    if (s_edit_field != CELL_EDIT_NONE && s_edit_band_idx >= 0) {
        int c = GetCharPressed();
        while (c > 0) {
            if (((c >= '0' && c <= '9') || c == '.' || c == '-' || c == '+') &&
                s_edit_buf_len < (int)sizeof(s_edit_buf) - 1) {
                s_edit_buf[s_edit_buf_len++] = (char)c;
                s_edit_buf[s_edit_buf_len] = '\0';
            }
            c = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE) && s_edit_buf_len > 0) {
            s_edit_buf[--s_edit_buf_len] = '\0';
        }
        if (IsKeyPressed(KEY_ESCAPE)) {
            s_edit_field = CELL_EDIT_NONE;
            s_edit_band_idx = -1;
        }
        if (IsKeyPressed(KEY_ENTER)) {
            if (s_edit_buf_len > 0) {
                float val = (float)atof(s_edit_buf);
                PEQBand bnd;
                peq_get_band(s_edit_band_idx, &bnd);
                if (s_edit_field == CELL_EDIT_FREQ) {
                    if (val < PEQ_F_MIN) val = PEQ_F_MIN;
                    if (val > PEQ_F_MAX) val = PEQ_F_MAX;
                    bnd.freq = val;
                } else if (s_edit_field == CELL_EDIT_GAIN) {
                    if (val < PEQ_DB_MIN) val = PEQ_DB_MIN;
                    if (val > PEQ_DB_MAX) val = PEQ_DB_MAX;
                    bnd.gain_db = val;
                } else if (s_edit_field == CELL_EDIT_Q) {
                    if (val < PEQ_MIN_Q) val = PEQ_MIN_Q;
                    if (val > PEQ_MAX_Q) val = PEQ_MAX_Q;
                    bnd.q = val;
                }
                peq_set_band(s_edit_band_idx, &bnd);
            }
            s_edit_field = CELL_EDIT_NONE;
            s_edit_band_idx = -1;
        }
    }

    // Header
    EQMode mode = eq_get_mode();
    bool enabled = eq_is_enabled();

    float top_bar_h = 44.0f;
    DrawRectangle(0, (int)b.y, (int)b.width, (int)top_bar_h, (Color){ 6, 7, 10, 210 });
    DrawLine(0, (int)(b.y + top_bar_h), (int)b.width, (int)(b.y + top_bar_h), (Color){ 25, 26, 32, 200 });

    // Mode selector
    if (interactive && DrawBtn((Rectangle){ b.x + 16, b.y + 8, 96, 28 }, "Parametric", mode == EQ_MODE_PARAMETRIC)) {
        eq_set_mode(EQ_MODE_PARAMETRIC);
    }
    if (interactive && DrawBtn((Rectangle){ b.x + 120, b.y + 8, 80, 28 }, "Graphic", mode == EQ_MODE_GRAPHIC)) {
        eq_set_mode(EQ_MODE_GRAPHIC);
    }

    // Toggle Bypass / Enable
    if (interactive && DrawBtn((Rectangle){ b.x + 215, b.y + 8, 88, 28 }, enabled ? "ACTIVE" : "BYPASS", enabled)) {
        eq_toggle_enabled();
    }

    // Close button
    if (interactive && DrawBtn((Rectangle){ b.x + b.width - 96, b.y + 8, 80, 28 }, "✕  Esc", false)) {
        sparkles_eq_toggle();
    }

    // Layout zones
    float sidebar_w = 240.0f;
    Rectangle side_rect = { b.x, b.y + top_bar_h, sidebar_w, b.height - top_bar_h };
    Rectangle main_rect = { b.x + sidebar_w, b.y + top_bar_h, b.width - sidebar_w, b.height - top_bar_h };

    // Sidebar (presets and preamp)
    DrawRectangleRec(side_rect, (Color){ 6, 7, 10, 180 });
    DrawLine((int)(side_rect.x + side_rect.width), (int)side_rect.y,
             (int)(side_rect.x + side_rect.width), (int)(side_rect.y + side_rect.height), (Color){ 25, 26, 32, 200 });

    // Preamp header
    float preamp = peq_get_preamp();
    DrawText("PREAMP", (int)side_rect.x + 16, (int)side_rect.y + 14, 11, COLOR_TEXT_MUTED);
    DrawText(TextFormat("%+5.1f dB", preamp), (int)side_rect.x + 16, (int)side_rect.y + 28, FONT_SIZE_MD, COLOR_TEXT_PRIMARY);

    // Preamp slider
    Rectangle pre_slider = { side_rect.x + 16, side_rect.y + 54, side_rect.width - 32, 4 };
    DrawRectangleRec(pre_slider, (Color){ 24, 25, 30, 255 });
    float pre_norm = (preamp - PEQ_DB_MIN) / (PEQ_DB_MAX - PEQ_DB_MIN);
    DrawRectangle((int)pre_slider.x, (int)pre_slider.y, (int)(pre_slider.width * pre_norm), (int)pre_slider.height, COLOR_ACCENT);
    DrawCircle((int)(pre_slider.x + (pre_slider.width * pre_norm)), (int)(pre_slider.y + 2), 5.0f, COLOR_TEXT_PRIMARY);

    if (interactive) {
        static bool dragging_preamp = false;
        if (CheckCollisionPointRec(mouse, (Rectangle){ pre_slider.x, pre_slider.y - 8, pre_slider.width, 20 }) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            dragging_preamp = true;
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) dragging_preamp = false;
        if (dragging_preamp) {
            float norm = (mouse.x - pre_slider.x) / pre_slider.width;
            if (norm < 0.0f) norm = 0.0f;
            if (norm > 1.0f) norm = 1.0f;
            peq_set_preamp(PEQ_DB_MIN + norm * (PEQ_DB_MAX - PEQ_DB_MIN));
        }
    }

    DrawLine((int)side_rect.x, (int)(side_rect.y + 74), (int)(side_rect.x + side_rect.width), (int)(side_rect.y + 74), (Color){ 20, 20, 24, 255 });
    DrawText("PRESETS", (int)side_rect.x + 16, (int)side_rect.y + 86, 11, COLOR_TEXT_MUTED);

    if (mode == EQ_MODE_PARAMETRIC) {
        if (interactive && DrawBtn((Rectangle){ side_rect.x + side_rect.width - 68, side_rect.y + 80, 56, 22 }, "+ Save", false)) {
            sparkles_text_prompt_open(&(SparklesTextPromptConfig){
                .tag = "PEQ PRESET",
                .prompt = "Save Parametric EQ Preset As",
                .submit_label = "Save",
                .max_len = 48,
                .on_submit = on_peq_preset_save_submit
            });
        }
    }

    float list_y = side_rect.y + 110;
    float list_h = side_rect.height - 118;
    
    if (interactive && CheckCollisionPointRec(mouse, (Rectangle){ side_rect.x, list_y, side_rect.width, list_h })) {
        s_preset_scroll -= (int)GetMouseWheelMove() * 32;
        if (s_preset_scroll < 0) s_preset_scroll = 0;
    }

    BeginScissorMode((int)side_rect.x, (int)list_y, (int)side_rect.width, (int)list_h);

    if (mode == EQ_MODE_GRAPHIC) {
        int total = eq_get_preset_count();
        int cur_p = eq_get_current_preset();
        for (int i = 0; i < total; i++) {
            float py = list_y + i * 28 - s_preset_scroll;
            if (py < list_y - 28 || py > list_y + list_h) continue;

            Rectangle p_btn = { side_rect.x, py, side_rect.width, 28 };
            bool hover = CheckCollisionPointRec(mouse, p_btn);
            bool selected = (cur_p == i);

            if (selected) {
                DrawRectangleRec(p_btn, (Color){ 16, 16, 20, 255 });
                DrawRectangle((int)p_btn.x, (int)p_btn.y, 3, (int)p_btn.height, COLOR_ACCENT);
            } else if (hover) {
                DrawRectangleRec(p_btn, (Color){ 10, 10, 14, 255 });
            }

            DrawText(eq_get_preset_name(i), (int)p_btn.x + 16, (int)p_btn.y + 7, FONT_SIZE_SM, selected ? COLOR_ACCENT : (hover ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED));

            if (interactive && hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                eq_apply_preset(i);
            }
        }
    } else {
        int b_count = peq_get_builtin_preset_count();
        int total = b_count + s_user_count;
        for (int i = 0; i < total; i++) {
            float py = list_y + i * 28 - s_preset_scroll;
            if (py < list_y - 28 || py > list_y + list_h) continue;

            Rectangle p_btn = { side_rect.x, py, side_rect.width, 28 };
            const char *name = (i < b_count) ? peq_get_builtin_preset(i)->name : s_user_names[i - b_count];
            
            bool hover = CheckCollisionPointRec(mouse, p_btn);
            if (hover) {
                DrawRectangleRec(p_btn, (Color){ 12, 12, 16, 255 });
            }

            DrawText(name, (int)p_btn.x + 16, (int)p_btn.y + 7, FONT_SIZE_SM, (i < b_count) ? (hover ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED) : COLOR_ACCENT);

            if (interactive && hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (i < b_count) peq_apply_builtin_preset(i);
                else peq_load_file(s_user_paths[i - b_count]);
            }
        }
    }
    EndScissorMode();

    // Main area
    DrawRectangleRec(main_rect, (Color){ 4, 5, 7, 140 });

    if (mode == EQ_MODE_GRAPHIC) {
        // Sliders
        const char **labels = eq_get_freq_labels();
        float slider_w = main_rect.width / EQ_NUM_BANDS;
        float track_y = main_rect.y + 40;
        float track_h = main_rect.height - 100;
        float zero_y = db_to_y(0.0f, (Rectangle){0, track_y, 0, track_h}, EQ_MIN_GAIN_DB, EQ_MAX_GAIN_DB);

        // Background grid lines
        DrawLine((int)main_rect.x, (int)zero_y, (int)(main_rect.x + main_rect.width), (int)zero_y, ColorAlpha(COLOR_TEXT_PRIMARY, 0.3f));
        DrawText(" 0dB", (int)main_rect.x + 10, (int)zero_y - 14, 11, COLOR_TEXT_MUTED);
        
        float p12_y = db_to_y(12.0f, (Rectangle){0, track_y, 0, track_h}, EQ_MIN_GAIN_DB, EQ_MAX_GAIN_DB);
        float m12_y = db_to_y(-12.0f, (Rectangle){0, track_y, 0, track_h}, EQ_MIN_GAIN_DB, EQ_MAX_GAIN_DB);
        DrawLine((int)main_rect.x, (int)p12_y, (int)(main_rect.x + main_rect.width), (int)p12_y, ColorAlpha(COLOR_TEXT_DARK, 0.3f));
        DrawLine((int)main_rect.x, (int)m12_y, (int)(main_rect.x + main_rect.width), (int)m12_y, ColorAlpha(COLOR_TEXT_DARK, 0.3f));
        DrawText("+12dB", (int)main_rect.x + 10, (int)p12_y - 14, 11, COLOR_TEXT_MUTED);
        DrawText("-12dB", (int)main_rect.x + 10, (int)m12_y + 4, 11, COLOR_TEXT_MUTED);

        for (int i = 0; i < EQ_NUM_BANDS; i++) {
            float cx = main_rect.x + i * slider_w + slider_w * 0.5f;
            float gain = eq_get_band_gain(i);
            float hy = db_to_y(gain, (Rectangle){0, track_y, 0, track_h}, EQ_MIN_GAIN_DB, EQ_MAX_GAIN_DB);

            // Track Line
            DrawLineEx((Vector2){cx, track_y}, (Vector2){cx, track_y + track_h}, 4.0f, (Color){ 30, 32, 38, 255 });
            
            // Fill from 0dB to handle
            Color fill_col = (gain >= 0.0f) ? COLOR_ACCENT : (Color){ 160, 200, 255, 255 };
            DrawLineEx((Vector2){cx, zero_y}, (Vector2){cx, hy}, 4.0f, fill_col);

            // Fader Handle
            Rectangle handle = { cx - 12, hy - 8, 24, 16 };
            DrawRectangleRounded(handle, 0.2f, 4, COLOR_TEXT_PRIMARY);
            
            // Text values
            DrawText(TextFormat("%+2.0f", gain), (int)cx - MeasureText(TextFormat("%+2.0f", gain), 10)/2, (int)track_y - 20, 10, fill_col);
            DrawText(labels[i], (int)cx - MeasureText(labels[i], 12)/2, (int)(track_y + track_h + 16), 12, COLOR_TEXT_MUTED);

            // Interaction
            if (interactive) {
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, (Rectangle){cx - 20, track_y, 40, track_h})) {
                    s_dragged_slider = i;
                }
            }
        }

        if (s_dragged_slider != -1) {
            if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                float val = y_to_db(mouse.y, (Rectangle){0, track_y, 0, track_h}, EQ_MIN_GAIN_DB, EQ_MAX_GAIN_DB);
                eq_set_band_gain(s_dragged_slider, val);
            } else {
                s_dragged_slider = -1;
            }
        }

    } else {
        // PEQ view
        float graph_h = main_rect.height * 0.52f;
        Rectangle gr = { main_rect.x + 36, main_rect.y + 14, main_rect.width - 56, graph_h - 26 };

        // Background
        DrawRectangleRec(gr, (Color){ 6, 7, 10, 180 });
        DrawRectangleLinesEx(gr, 1.0f, (Color){ 30, 32, 40, 200 });

        static const float db_ticks[] = { 24.0f, 12.0f, 0.0f, -12.0f, -24.0f };
        for (size_t i = 0; i < sizeof(db_ticks)/sizeof(db_ticks[0]); i++) {
            float y = db_to_y(db_ticks[i], gr, PEQ_DB_MIN, PEQ_DB_MAX);
            DrawLineEx((Vector2){ gr.x, y }, (Vector2){ gr.x + gr.width, y }, (db_ticks[i] == 0.0f) ? 1.5f : 1.0f, ColorAlpha(COLOR_TEXT_PRIMARY, db_ticks[i] == 0.0f ? 0.35f : 0.15f));
            DrawText(TextFormat("%+2.0fdB", db_ticks[i]), (int)(gr.x - 36), (int)(y - 5), 10, COLOR_TEXT_DARK);
        }

        static const struct { float f; const char *lbl; } f_ticks[] = {
            { 20.0f, "20" }, { 50.0f, "50" }, { 100.0f, "100" }, { 500.0f, "500" },
            { 1000.0f, "1k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" }, { 20000.0f, "20k" }
        };
        for (size_t i = 0; i < sizeof(f_ticks)/sizeof(f_ticks[0]); i++) {
            float x = freq_to_x(f_ticks[i].f, gr);
            DrawLine((int)x, (int)gr.y, (int)x, (int)(gr.y + gr.height), ColorAlpha(COLOR_TEXT_DARK, 0.15f));
            DrawText(f_ticks[i].lbl, (int)(x - MeasureText(f_ticks[i].lbl, 10)/2), (int)(gr.y + gr.height + 6), 10, COLOR_TEXT_DARK);
        }

        // Filter curves
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

        // Master Curve
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

        // Nodes
        s_hovered_node = -1;
        for (int i = 0; i < band_count; i++) {
            PEQBand b; peq_get_band(i, &b);
            float nx = freq_to_x(b.freq, gr);
            float ny = db_to_y(b.gain_db, gr, PEQ_DB_MIN, PEQ_DB_MAX);
            Vector2 npos = { nx, ny };

            bool is_hover = CheckCollisionPointCircle(mouse, npos, 11.0f);
            if (is_hover) s_hovered_node = i;

            Color rc = b.enabled ? COLOR_ACCENT : COLOR_TEXT_DARK;
            if (i == s_dragged_node) rc = WHITE;

            DrawCircleV(npos, 8.0f, (Color){ 24, 25, 30, 255 });
            DrawCircleLines((int)npos.x, (int)npos.y, 8.0f, rc);
            DrawCircleV(npos, 3.5f, rc);
            DrawText(TextFormat("%d", i + 1), (int)(nx - 3), (int)(ny - 18), 10, rc);

            if (is_hover || i == s_dragged_node) {
                const char *tip = TextFormat("#%d: %.0fHz  %+.1fdB  Q:%.2f", i + 1, b.freq, b.gain_db, b.q);
                int tw = MeasureText(tip, 12);
                DrawRectangle((int)(nx - tw/2 - 6), (int)(ny - 36), tw + 12, 18, (Color){ 20, 22, 26, 230 });
                DrawText(tip, (int)(nx - tw/2), (int)(ny - 33), 12, COLOR_TEXT_PRIMARY);
            }
        }

        if (interactive) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && s_hovered_node != -1) s_dragged_node = s_hovered_node;
            if (s_dragged_node != -1) {
                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                    PEQBand b; peq_get_band(s_dragged_node, &b);
                    b.freq = x_to_freq(mouse.x, gr);
                    b.gain_db = y_to_db(mouse.y, gr, PEQ_DB_MIN, PEQ_DB_MAX);
                    peq_set_band(s_dragged_node, &b);
                } else s_dragged_node = -1;
            }
            if (s_hovered_node != -1) {
                float wheel = GetMouseWheelMove();
                if (wheel != 0.0f) {
                    PEQBand b; peq_get_band(s_hovered_node, &b);
                    b.q += wheel * 0.15f;
                    if (b.q < PEQ_MIN_Q) b.q = PEQ_MIN_Q;
                    if (b.q > PEQ_MAX_Q) b.q = PEQ_MAX_Q;
                    peq_set_band(s_hovered_node, &b);
                }
            }
        }

        // Band data table
        float table_y = main_rect.y + graph_h + 8;
        DrawLine((int)main_rect.x, (int)table_y, (int)(main_rect.x + main_rect.width), (int)table_y, (Color){ 20, 20, 24, 255 });
        
        float cw = (main_rect.width - 32.0f) / 6.0f;
        float ty = table_y + 8;

        // Table Header
        DrawText("BAND", (int)(main_rect.x + 24), (int)ty, 11, COLOR_TEXT_MUTED);
        DrawText("TYPE", (int)(main_rect.x + 24 + cw * 1), (int)ty, 11, COLOR_TEXT_MUTED);
        DrawText("FREQUENCY", (int)(main_rect.x + 24 + cw * 2), (int)ty, 11, COLOR_TEXT_MUTED);
        DrawText("GAIN", (int)(main_rect.x + 24 + cw * 3), (int)ty, 11, COLOR_TEXT_MUTED);
        DrawText("Q FACTOR", (int)(main_rect.x + 24 + cw * 4), (int)ty, 11, COLOR_TEXT_MUTED);
        DrawText("STATE", (int)(main_rect.x + 24 + cw * 5), (int)ty, 11, COLOR_TEXT_MUTED);

        float row_spacing = 22.0f;

        for (int i = 0; i < band_count; i++) {
            PEQBand b; peq_get_band(i, &b);
            float row_y = ty + 20 + i * row_spacing;
            if (row_y + row_spacing > main_rect.y + main_rect.height) break;

            // Zebra background on alternate rows
            if (i % 2 == 1) {
                DrawRectangle((int)main_rect.x + 16, (int)row_y - 2, (int)main_rect.width - 32, (int)row_spacing, (Color){ 6, 6, 8, 255 });
            }

            // Band Index badge
            Color band_badge_col = b.enabled ? band_colors[i % 10] : COLOR_TEXT_DARK;
            DrawCircle((int)(main_rect.x + 30), (int)(row_y + 8), 4.0f, band_badge_col);
            DrawText(TextFormat("%d", i + 1), (int)(main_rect.x + 42), (int)row_y, FONT_SIZE_SM, b.enabled ? COLOR_TEXT_PRIMARY : COLOR_TEXT_DARK);

            // Filter Type
            Rectangle type_rec = { main_rect.x + 24 + cw * 1, row_y - 2, cw - 12, row_spacing - 2 };
            bool hover_type = CheckCollisionPointRec(mouse, type_rec);
            if (hover_type) DrawRectangleRec(type_rec, (Color){ 16, 16, 20, 255 });
            DrawText(peq_get_filter_name(b.type), (int)type_rec.x + 6, (int)row_y, FONT_SIZE_SM, hover_type ? COLOR_ACCENT : COLOR_TEXT_PRIMARY);
            if (interactive && hover_type && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                peq_cycle_band_type(i);
            }

            // Frequency
            Rectangle f_rec = { main_rect.x + 24 + cw * 2, row_y - 2, cw - 12, row_spacing - 2 };
            bool hover_f = CheckCollisionPointRec(mouse, f_rec);
            bool editing_f = (s_edit_band_idx == i && s_edit_field == CELL_EDIT_FREQ);

            if (editing_f) {
                DrawRectangleRec(f_rec, (Color){ 20, 22, 28, 255 });
                DrawRectangleLinesEx(f_rec, 1.0f, COLOR_ACCENT);
                const char *cur = ((int)(GetTime() * 2.5f) % 2 == 0) ? "_" : " ";
                DrawText(TextFormat("%s%s", s_edit_buf, cur), (int)f_rec.x + 6, (int)row_y, FONT_SIZE_SM, COLOR_TEXT_PRIMARY);
            } else {
                if (hover_f) DrawRectangleRec(f_rec, (Color){ 16, 16, 20, 255 });
                DrawText(TextFormat("%.0f Hz", b.freq), (int)f_rec.x + 6, (int)row_y, FONT_SIZE_SM, hover_f ? COLOR_ACCENT : COLOR_TEXT_PRIMARY);
                if (interactive && hover_f) {
                    float w = GetMouseWheelMove();
                    if (w != 0.0f) peq_adjust_band_freq(i, w > 0 ? 1.0595f : 0.9438f);
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                        s_edit_band_idx = i;
                        s_edit_field = CELL_EDIT_FREQ;
                        snprintf(s_edit_buf, sizeof(s_edit_buf), "%.0f", b.freq);
                        s_edit_buf_len = (int)strlen(s_edit_buf);
                    }
                }
            }

            // Gain
            Rectangle g_rec = { main_rect.x + 24 + cw * 3, row_y - 2, cw - 12, row_spacing - 2 };
            bool hover_g = CheckCollisionPointRec(mouse, g_rec);
            bool editing_g = (s_edit_band_idx == i && s_edit_field == CELL_EDIT_GAIN);

            if (b.type != PEQ_FILTER_HIGH_PASS && b.type != PEQ_FILTER_LOW_PASS) {
                if (editing_g) {
                    DrawRectangleRec(g_rec, (Color){ 20, 22, 28, 255 });
                    DrawRectangleLinesEx(g_rec, 1.0f, COLOR_ACCENT);
                    const char *cur = ((int)(GetTime() * 2.5f) % 2 == 0) ? "_" : " ";
                    DrawText(TextFormat("%s%s", s_edit_buf, cur), (int)g_rec.x + 6, (int)row_y, FONT_SIZE_SM, COLOR_TEXT_PRIMARY);
                } else {
                    if (hover_g) DrawRectangleRec(g_rec, (Color){ 16, 16, 20, 255 });
                    Color g_color = (b.gain_db > 0.05f) ? (Color){ 100, 220, 140, 255 } : ((b.gain_db < -0.05f) ? (Color){ 230, 110, 110, 255 } : COLOR_TEXT_MUTED);
                    DrawText(TextFormat("%+.1f dB", b.gain_db), (int)g_rec.x + 6, (int)row_y, FONT_SIZE_SM, hover_g ? COLOR_ACCENT : g_color);
                    if (interactive && hover_g) {
                        float w = GetMouseWheelMove();
                        if (w != 0.0f) peq_adjust_band_gain(i, w > 0 ? 0.5f : -0.5f);
                        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                            s_edit_band_idx = i;
                            s_edit_field = CELL_EDIT_GAIN;
                            snprintf(s_edit_buf, sizeof(s_edit_buf), "%.1f", b.gain_db);
                            s_edit_buf_len = (int)strlen(s_edit_buf);
                        }
                    }
                }
            } else {
                DrawText("—", (int)g_rec.x + 6, (int)row_y, FONT_SIZE_SM, COLOR_TEXT_DARK);
            }

            // Q Factor
            Rectangle q_rec = { main_rect.x + 24 + cw * 4, row_y - 2, cw - 12, row_spacing - 2 };
            bool hover_q = CheckCollisionPointRec(mouse, q_rec);
            bool editing_q = (s_edit_band_idx == i && s_edit_field == CELL_EDIT_Q);

            if (editing_q) {
                DrawRectangleRec(q_rec, (Color){ 20, 22, 28, 255 });
                DrawRectangleLinesEx(q_rec, 1.0f, COLOR_ACCENT);
                const char *cur = ((int)(GetTime() * 2.5f) % 2 == 0) ? "_" : " ";
                DrawText(TextFormat("%s%s", s_edit_buf, cur), (int)q_rec.x + 6, (int)row_y, FONT_SIZE_SM, COLOR_TEXT_PRIMARY);
            } else {
                if (hover_q) DrawRectangleRec(q_rec, (Color){ 16, 16, 20, 255 });
                DrawText(TextFormat("%.2f", b.q), (int)q_rec.x + 6, (int)row_y, FONT_SIZE_SM, hover_q ? COLOR_ACCENT : COLOR_TEXT_PRIMARY);
                if (interactive && hover_q) {
                    float w = GetMouseWheelMove();
                    if (w != 0.0f) peq_adjust_band_q(i, w > 0 ? 0.1f : -0.1f);
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                        s_edit_band_idx = i;
                        s_edit_field = CELL_EDIT_Q;
                        snprintf(s_edit_buf, sizeof(s_edit_buf), "%.2f", b.q);
                        s_edit_buf_len = (int)strlen(s_edit_buf);
                    }
                }
            }

            // Active Toggle
            Rectangle st_rec = { main_rect.x + 24 + cw * 5, row_y - 2, 48, row_spacing - 2 };
            bool hover_st = CheckCollisionPointRec(mouse, st_rec);
            Color st_col = b.enabled ? COLOR_ACCENT : COLOR_TEXT_DARK;
            DrawText(b.enabled ? "ON" : "OFF", (int)st_rec.x + 6, (int)row_y, FONT_SIZE_SM, hover_st ? COLOR_TEXT_PRIMARY : st_col);
            if (interactive && hover_st && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                peq_toggle_band_enabled(i);
            }
        }

        // Corners
        Rectangle tbl_bounds = { main_rect.x + 14, table_y + 4, main_rect.width - 28, main_rect.height - (table_y - main_rect.y) - 8 };
        DrawNothingCornerBrackets(tbl_bounds, 8.0f, (Color){ 45, 48, 58, 160 });
    }

    rlPopMatrix();
}