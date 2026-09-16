#define _DEFAULT_SOURCE
#include "sparkles_krystal.h"
#include "sparkles_theme.h"
#include "krystal_engine.h"
#include "rlgl.h"
#include "krystal_profiles.h"
#include "krystal_preset_manager.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool s_krystal_open = false;
static float s_anim_progress = 0.0f; // 0.0 = Closed, 1.0 = Fully Open

typedef enum {
    KRYSTAL_TAB_SPATIAL = 0,
    KRYSTAL_TAB_BASS,
    KRYSTAL_TAB_PRESENCE,
    KRYSTAL_TAB_WARMTH,
    KRYSTAL_TAB_ROUTING,
    KRYSTAL_TAB_COUNT
} KrystalUiTab;

static KrystalUiTab s_active_tab = KRYSTAL_TAB_SPATIAL;

// Presets sidebar scroll
static int s_preset_scroll = 0;

// Save Preset Dialog
static bool s_saving_preset = false;
static char s_save_name[64] = {0};
static int s_save_len = 0;

// Radar and spatial scene interaction
static bool s_dragging_source_xy = false;
static bool s_dragging_elevation = false;
static bool s_dragging_elev_arc = false;
static float s_elev_drag_start_y = 0.0f;
static float s_elev_drag_start_val = 0.0f;

void sparkles_krystal_init(void) {
    s_krystal_open = false;
    s_anim_progress = 0.0f;
    s_active_tab = KRYSTAL_TAB_SPATIAL;
    s_saving_preset = false;
    s_dragging_source_xy = false;
    s_dragging_elevation = false;
    s_dragging_elev_arc = false;
}

void sparkles_krystal_toggle(void) {
    s_krystal_open = !s_krystal_open;
    if (!s_krystal_open) {
        s_saving_preset = false;
        s_dragging_source_xy = false;
        s_dragging_elevation = false;
        s_dragging_elev_arc = false;
    }
}

bool sparkles_krystal_is_open(void) { return s_krystal_open; }
bool sparkles_krystal_is_visible(void) { return (s_krystal_open || s_anim_progress > 0.001f); }
float sparkles_krystal_get_anim_progress(void) { return s_anim_progress; }

void sparkles_krystal_update(float screen_w, float screen_h) {
    (void)screen_w; (void)screen_h;
    float dt = GetFrameTime();
    float speed = 5.4f;
    if (s_krystal_open) {
        s_anim_progress += dt * speed;
        if (s_anim_progress > 1.0f) s_anim_progress = 1.0f;
    } else {
        s_anim_progress -= dt * speed;
        if (s_anim_progress < 0.0f) s_anim_progress = 0.0f;
    }
}

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

static bool DrawSlider(Rectangle bounds, const char *label, float *val, float min_val, float max_val, const char *unit, bool is_interactive) {
    Vector2 mouse = GetMousePosition();
    bool hover = CheckCollisionPointRec(mouse, bounds);

    float track_x = bounds.x + 160.0f;
    float track_w = bounds.width - 240.0f;
    float track_y = bounds.y + bounds.height * 0.5f - 2.0f;
    Rectangle track_rect = { track_x, track_y, track_w, 4.0f };

    DrawText(label, (int)bounds.x + 12, (int)bounds.y + (int)(bounds.height - 14) / 2, FONT_SIZE_SM, hover ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED);

    DrawRectangleRec(track_rect, (Color){ 24, 25, 30, 255 });
    float norm = (*val - min_val) / (max_val - min_val);
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;

    DrawRectangle((int)track_x, (int)track_y, (int)(track_w * norm), 4, COLOR_ACCENT);
    DrawCircle((int)(track_x + track_w * norm), (int)(track_y + 2.0f), 5.0f, COLOR_TEXT_PRIMARY);

    char val_buf[32];
    if (strcmp(unit, "%") == 0) {
        snprintf(val_buf, sizeof(val_buf), "%.0f%%", *val * 100.0f);
    } else if (strcmp(unit, "dB") == 0) {
        snprintf(val_buf, sizeof(val_buf), "%+.1f dB", *val);
    } else if (strcmp(unit, "deg") == 0) {
        snprintf(val_buf, sizeof(val_buf), "%+.0f°", *val);
    } else if (strcmp(unit, "m") == 0) {
        snprintf(val_buf, sizeof(val_buf), "%.2f m", *val);
    } else if (strcmp(unit, "Hz") == 0) {
        snprintf(val_buf, sizeof(val_buf), "%.0f Hz", *val);
    } else {
        snprintf(val_buf, sizeof(val_buf), "%.2f", *val);
    }
    DrawText(val_buf, (int)(track_x + track_w + 14), (int)bounds.y + (int)(bounds.height - 14) / 2, FONT_SIZE_SM, COLOR_TEXT_PRIMARY);

    bool changed = false;
    if (is_interactive) {
        if (hover && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            float n = (mouse.x - track_x) / track_w;
            if (n < 0.0f) n = 0.0f;
            if (n > 1.0f) n = 1.0f;
            *val = min_val + n * (max_val - min_val);
            changed = true;
        }
        if (hover) {
            float wheel = GetMouseWheelMove();
            if (wheel != 0.0f) {
                float step = (max_val - min_val) * 0.05f * wheel;
                *val += step;
                if (*val < min_val) *val = min_val;
                if (*val > max_val) *val = max_val;
                changed = true;
            }
        }
    }
    return changed;
}

static bool DrawToggle(Rectangle bounds, const char *label, bool *val, bool is_interactive) {
    Vector2 mouse = GetMousePosition();
    bool hover = CheckCollisionPointRec(mouse, bounds);

    DrawText(label, (int)bounds.x + 12, (int)bounds.y + (int)(bounds.height - 14) / 2, FONT_SIZE_SM, hover ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED);

    Rectangle btn = { bounds.x + bounds.width - 76, bounds.y + (bounds.height - 22) / 2, 64, 22 };
    bool clicked = is_interactive && CheckCollisionPointRec(mouse, btn) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (clicked) *val = !(*val);

    Color st_col = *val ? COLOR_ACCENT : (Color){ 30, 32, 38, 255 };
    DrawRectangleRec(btn, st_col);
    const char *st_txt = *val ? "ON" : "OFF";
    int tw = MeasureText(st_txt, 12);
    DrawText(st_txt, (int)(btn.x + (btn.width - tw) / 2), (int)(btn.y + 5), 12, *val ? (Color){ 10, 10, 12, 255 } : COLOR_TEXT_MUTED);

    return clicked;
}

void sparkles_krystal_render(float screen_w, float screen_h) {
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

    bool interactive = (s_anim_progress >= 0.99f) && !s_saving_preset;

    // Semi-transparent background
    DrawRectangleRec(b, ColorAlpha((Color){ 3, 3, 5, 255 }, 0.70f * ease));

    KrystalConfig cfg;
    krystal_get_config(&cfg);

    KrystalTelemetry telem;
    krystal_get_telemetry(&telem);

    // Save preset dialog
    if (s_saving_preset) {
        int c = GetCharPressed();
        while (c > 0) {
            if (c >= 32 && c <= 126 && s_save_len < (int)sizeof(s_save_name) - 1) {
                s_save_name[s_save_len++] = (char)c;
                s_save_name[s_save_len] = '\0';
            }
            c = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE) && s_save_len > 0) s_save_name[--s_save_len] = '\0';

        bool do_save = (IsKeyPressed(KEY_ENTER) && s_save_len > 0);
        bool do_cancel = IsKeyPressed(KEY_ESCAPE);

        DrawRectangle(0, 0, (int)screen_w, (int)screen_h, ColorAlpha(BLACK, 0.78f));
        float qw = 460.0f;
        float qh = 170.0f;
        Rectangle save_box = { (screen_w - qw) * 0.5f, (screen_h - qh) * 0.5f, qw, qh };

        DrawRectangleRec(save_box, (Color){ 8, 8, 11, 255 });
        DrawRectangleLinesEx(save_box, 1.0f, COLOR_ACCENT);
        DrawNothingCornerBrackets(save_box, 8.0f, COLOR_ACCENT);

        DrawText("// KRYSTAL PRESET", (int)save_box.x + 20, (int)save_box.y + 16, 10, COLOR_TEXT_MUTED);
        DrawText("Save Krystal Preset As", (int)save_box.x + 20, (int)save_box.y + 34, FONT_SIZE_MD, COLOR_TEXT_PRIMARY);

        Rectangle input_box = { save_box.x + 20, save_box.y + 64, save_box.width - 40, 32 };
        DrawRectangleRec(input_box, (Color){ 14, 15, 18, 255 });
        DrawRectangleLinesEx(input_box, 1.0f, (Color){ 35, 38, 46, 255 });
        const char *cur = ((int)(GetTime() * 2.5f) % 2 == 0) ? "_" : " ";
        DrawText(TextFormat("%s%s", s_save_name, cur), (int)input_box.x + 10, (int)input_box.y + 8, FONT_SIZE_MD, COLOR_TEXT_PRIMARY);

        Rectangle btn_save   = { save_box.x + save_box.width - 180, save_box.y + save_box.height - 42, 80, 26 };
        Rectangle btn_cancel = { save_box.x + save_box.width - 92,  save_box.y + save_box.height - 42, 72, 26 };

        bool hover_save = CheckCollisionPointRec(mouse, btn_save);
        bool hover_cancel = CheckCollisionPointRec(mouse, btn_cancel);

        if (hover_save && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && s_save_len > 0) do_save = true;
        if (hover_cancel && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) do_cancel = true;

        DrawRectangleRec(btn_save, (hover_save && s_save_len > 0) ? ColorAlpha(COLOR_ACCENT, 0.25f) : (Color){ 18, 19, 24, 255 });
        DrawRectangleLinesEx(btn_save, 1.0f, (hover_save && s_save_len > 0) ? COLOR_ACCENT : (s_save_len > 0 ? COLOR_TEXT_PRIMARY : COLOR_TEXT_DARK));
        DrawText("Save", (int)btn_save.x + (btn_save.width - MeasureText("Save", FONT_SIZE_SM)) / 2, (int)btn_save.y + 6, FONT_SIZE_SM, s_save_len > 0 ? COLOR_TEXT_PRIMARY : COLOR_TEXT_DARK);

        DrawRectangleRec(btn_cancel, hover_cancel ? ColorAlpha(COLOR_ACCENT, 0.25f) : (Color){ 18, 19, 24, 255 });
        DrawRectangleLinesEx(btn_cancel, 1.0f, hover_cancel ? COLOR_ACCENT : COLOR_TEXT_MUTED);
        DrawText("Cancel", (int)btn_cancel.x + (btn_cancel.width - MeasureText("Cancel", FONT_SIZE_SM)) / 2, (int)btn_cancel.y + 6, FONT_SIZE_SM, COLOR_TEXT_PRIMARY);

        if (do_cancel) {
            s_saving_preset = false;
        } else if (do_save) {
            krystal_presets_save(s_save_name, &cfg);
            krystal_set_active_preset_name(s_save_name);
            s_saving_preset = false;
        }

        rlPopMatrix();
        return;
    }

    // Top toolbar
    float top_bar_h = 44.0f;
    DrawRectangle(0, (int)b.y, (int)b.width, (int)top_bar_h, (Color){ 6, 7, 10, 210 });
    DrawLine(0, (int)(b.y + top_bar_h), (int)b.width, (int)(b.y + top_bar_h), (Color){ 25, 26, 32, 200 });

    // Category navigation tabs
    const char *tab_names[] = { "Spatial 3D", "Bass & Sub", "Presence", "Warmth & Tone", "Master" };
    float tab_x = 16.0f;
    for (int t = 0; t < KRYSTAL_TAB_COUNT; t++) {
        float tw = MeasureText(tab_names[t], FONT_SIZE_SM) + 24.0f;
        if (interactive && DrawBtn((Rectangle){ b.x + tab_x, b.y + 8, tw, 28 }, tab_names[t], s_active_tab == (KrystalUiTab)t)) {
            s_active_tab = (KrystalUiTab)t;
        }
        tab_x += tw + 8.0f;
    }

    // Master DSP Enable / Bypass
    if (interactive && DrawBtn((Rectangle){ b.x + tab_x + 12, b.y + 8, 88, 28 }, cfg.master_enabled ? "ACTIVE" : "BYPASS", cfg.master_enabled)) {
        krystal_toggle_enabled();
    }

    // Exit button
    if (interactive && DrawBtn((Rectangle){ b.x + b.width - 96, b.y + 8, 80, 28 }, "✕  Esc", false)) {
        sparkles_krystal_toggle();
    }

    // Layout panes
    float sidebar_w = 240.0f;
    float footer_h = 36.0f;
    Rectangle side_rect = { b.x, b.y + top_bar_h, sidebar_w, b.height - top_bar_h - footer_h };
    Rectangle main_rect = { b.x + sidebar_w, b.y + top_bar_h, b.width - sidebar_w, b.height - top_bar_h - footer_h };
    Rectangle footer_rect = { b.x, b.y + b.height - footer_h, b.width, footer_h };

    // Sidebar
    DrawRectangleRec(side_rect, (Color){ 6, 7, 10, 180 });
    DrawLine((int)(side_rect.x + side_rect.width), (int)side_rect.y,
             (int)(side_rect.x + side_rect.width), (int)(side_rect.y + side_rect.height), (Color){ 25, 26, 32, 200 });

    DrawText("ACOUSTIC PROFILES", (int)side_rect.x + 16, (int)side_rect.y + 14, 11, COLOR_TEXT_MUTED);

    if (interactive && DrawBtn((Rectangle){ side_rect.x + side_rect.width - 68, side_rect.y + 8, 56, 22 }, "+ Save", false)) {
        s_saving_preset = true;
        s_save_name[0] = '\0';
        s_save_len = 0;
    }

    float list_y = side_rect.y + 38;
    float list_h = side_rect.height - 46;

    if (interactive && CheckCollisionPointRec(mouse, (Rectangle){ side_rect.x, list_y, side_rect.width, list_h })) {
        s_preset_scroll -= (int)GetMouseWheelMove() * 32;
        if (s_preset_scroll < 0) s_preset_scroll = 0;
    }

    BeginScissorMode((int)side_rect.x, (int)list_y, (int)side_rect.width, (int)list_h);
    int profile_count = krystal_get_profile_count();
    int custom_count = krystal_presets_get_count();
    int total_presets = profile_count + custom_count;

    const char *active_preset_name = krystal_get_active_preset_name();

    for (int i = 0; i < total_presets; i++) {
        float py = list_y + i * 28 - s_preset_scroll;
        if (py < list_y - 28 || py > list_y + list_h) continue;

        Rectangle p_btn = { side_rect.x, py, side_rect.width, 28 };
        bool is_custom = (i >= profile_count);
        const char *name = is_custom ? krystal_presets_get_name(i - profile_count) : krystal_get_profile_name(i);

        bool selected = (strcmp(active_preset_name, name) == 0);
        bool hover = CheckCollisionPointRec(mouse, p_btn);

        if (selected) {
            DrawRectangleRec(p_btn, (Color){ 16, 16, 20, 255 });
            DrawRectangle((int)p_btn.x, (int)p_btn.y, 3, (int)p_btn.height, COLOR_ACCENT);
        } else if (hover) {
            DrawRectangleRec(p_btn, (Color){ 10, 10, 14, 255 });
        }

        DrawText(name, (int)p_btn.x + 16, (int)p_btn.y + 7, FONT_SIZE_SM, selected ? COLOR_ACCENT : (is_custom ? (Color){ 160, 210, 255, 255 } : (hover ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED)));

        if (interactive && hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (!is_custom) {
                krystal_apply_profile(i);
            } else {
                KrystalConfig c;
                if (krystal_presets_get_config(i - profile_count, &c)) {
                    krystal_set_config(&c);
                    krystal_set_active_preset_name(name);
                }
            }
        }
    }
    EndScissorMode();

    // Main rack controls
    DrawRectangleRec(main_rect, (Color){ 4, 5, 7, 140 });

    bool cfg_changed = false;
    float row_h = 32.0f;
    float start_row_y = main_rect.y + 20.0f;

    switch (s_active_tab) {
        case KRYSTAL_TAB_SPATIAL: {
            // Left half
            float half_w = main_rect.width * 0.48f;
            float cy = start_row_y;

            if (DrawToggle((Rectangle){ main_rect.x, cy, half_w, row_h }, "Spatial 3D Engine", &cfg.spatial.enabled, interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, half_w, row_h }, "Azimuth Angle", &cfg.spatial.azimuth_deg, -180.0f, +180.0f, "deg", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, half_w, row_h }, "Elevation Height", &cfg.spatial.elevation_deg, -45.0f, +90.0f, "deg", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, half_w, row_h }, "Virtual Distance", &cfg.spatial.distance_m, 0.5f, 5.0f, "m", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, half_w, row_h }, "Stereo Spread", &cfg.spatial.stage_angle_deg, 20.0f, 140.0f, "deg", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, half_w, row_h }, "Room Reflections", &cfg.spatial.room_refl, 0.0f, 0.80f, "%", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, half_w, row_h }, "Center Anchoring", &cfg.spatial.center_gain_db, -6.0f, +6.0f, "dB", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, half_w, row_h }, "Mono Sub Crossover", &cfg.spatial.mono_cut_hz, 60.0f, 300.0f, "Hz", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, half_w, row_h }, "Phase Guard Threshold", &cfg.spatial.safety_limit, 0.0f, 0.60f, "", interactive)) cfg_changed = true;

            // Right Half
            Rectangle scene_box = { main_rect.x + half_w + 16, main_rect.y + 14, main_rect.width - half_w - 32, main_rect.height - 28 };
            DrawRectangleRec(scene_box, (Color){ 6, 7, 10, 180 });
            DrawRectangleLinesEx(scene_box, 1.0f, (Color){ 30, 32, 40, 200 });

            // Scene geometry
            float gauge_w = 48.0f;
            Vector2 r_center = { scene_box.x + (scene_box.width - gauge_w) * 0.48f, scene_box.y + scene_box.height * 0.58f };
            float rx_max = fminf((scene_box.width - gauge_w) * 0.40f, scene_box.height * 0.44f);
            float ry_scale = 0.52f; // Perspective vertical compression
            float ry_max = rx_max * ry_scale;

            // Concentric distance rings (1.5m, 3.0m, 5.0m)
            const float ring_ratios[] = { 0.30f, 0.62f, 1.0f };
            const char *dist_labels[] = { "1.5m", "3.0m", "5.0m" };
            for (int r = 0; r < 3; r++) {
                float rw = rx_max * ring_ratios[r];
                float rh = ry_max * ring_ratios[r];
                // Draw ellipse
                const int pts = 48;
                Vector2 prev = { r_center.x + cosf(0) * rw, r_center.y + sinf(0) * rh };
                for (int p = 1; p <= pts; p++) {
                    float a = ((float)p / (float)pts) * 2.0f * PI;
                    Vector2 cur = { r_center.x + cosf(a) * rw, r_center.y + sinf(a) * rh };
                    DrawLineV(prev, cur, (r == 2) ? (Color){ 32, 34, 42, 255 } : (Color){ 16, 17, 21, 255 });
                    prev = cur;
                }
                DrawText(dist_labels[r], (int)(r_center.x + rw + 4), (int)r_center.y - 6, 10, (Color){ 60, 64, 74, 255 });
            }

            // Crosshair ground axes
            DrawLine((int)(r_center.x - rx_max), (int)r_center.y, (int)(r_center.x + rx_max), (int)r_center.y, (Color){ 20, 21, 26, 255 });
            DrawLine((int)r_center.x, (int)(r_center.y - ry_max), (int)r_center.x, (int)(r_center.y + ry_max), (Color){ 20, 21, 26, 255 });
            DrawText("F", (int)r_center.x - 3, (int)(r_center.y - ry_max - 14), 10, COLOR_TEXT_MUTED);
            DrawText("B", (int)r_center.x - 3, (int)(r_center.y + ry_max + 4), 10, COLOR_TEXT_DARK);
            DrawText("L", (int)(r_center.x - rx_max - 12), (int)r_center.y - 5, 10, COLOR_TEXT_DARK);
            DrawText("R", (int)(r_center.x + rx_max + 4), (int)r_center.y - 5, 10, COLOR_TEXT_DARK);

            // Virtual listener (head and direction vector)
            DrawCircleLines((int)r_center.x, (int)r_center.y, 11.0f, COLOR_TEXT_DARK);
            DrawCircleV(r_center, 9.0f, (Color){ 16, 17, 22, 255 });
            DrawLine((int)r_center.x, (int)r_center.y - 9, (int)r_center.x, (int)r_center.y - 18, COLOR_ACCENT); // Front nose vector
            DrawCircle((int)r_center.x - 10, (int)r_center.y, 2.5f, COLOR_TEXT_MUTED); // Left Ear
            DrawCircle((int)r_center.x + 10, (int)r_center.y, 2.5f, COLOR_TEXT_MUTED); // Right Ear
            DrawText("LISTENER", (int)r_center.x - 22, (int)r_center.y + 14, 9, (Color){ 80, 84, 96, 255 });

            // Sound object math in perspective 3D
            float az_rad = cfg.spatial.azimuth_deg * (PI / 180.0f);
            float dist_norm = (cfg.spatial.distance_m - 0.5f) / 4.5f;
            if (dist_norm < 0.05f) dist_norm = 0.05f;
            if (dist_norm > 1.0f) dist_norm = 1.0f;

            // Ground plane projection point (Floor Shadow)
            Vector2 shadow_pos = {
                r_center.x + sinf(az_rad) * dist_norm * rx_max,
                r_center.y - cosf(az_rad) * dist_norm * ry_max
            };

            // Height displacement based on Elevation (-45° to +90°)
            float el_rad = cfg.spatial.elevation_deg * (PI / 180.0f);
            float height_span = rx_max * 0.70f;
            float stalk_h = sinf(el_rad) * height_span;

            Vector2 orb_pos = { shadow_pos.x, shadow_pos.y - stalk_h };

            // Virtual stereo speaker projections
            float stage_half_rad = (cfg.spatial.stage_angle_deg * 0.5f) * (PI / 180.0f);
            Vector2 spk_l_shadow = {
                r_center.x + sinf(az_rad - stage_half_rad) * dist_norm * rx_max,
                r_center.y - cosf(az_rad - stage_half_rad) * dist_norm * ry_max
            };
            Vector2 spk_r_shadow = {
                r_center.x + sinf(az_rad + stage_half_rad) * dist_norm * rx_max,
                r_center.y - cosf(az_rad + stage_half_rad) * dist_norm * ry_max
            };

            // Speaker sound cones
            DrawLineEx(r_center, spk_l_shadow, 1.0f, (Color){ 30, 36, 48, 160 });
            DrawLineEx(r_center, spk_r_shadow, 1.0f, (Color){ 30, 36, 48, 160 });
            DrawCircleV(spk_l_shadow, 3.5f, (Color){ 100, 160, 255, 180 });
            DrawCircleV(spk_r_shadow, 3.5f, (Color){ 255, 120, 150, 180 });
            DrawText("L", (int)spk_l_shadow.x - 7, (int)spk_l_shadow.y - 12, 9, (Color){ 100, 160, 255, 180 });
            DrawText("R", (int)spk_r_shadow.x + 3, (int)spk_r_shadow.y - 12, 9, (Color){ 255, 120, 150, 180 });

            // Floor shadow indicator
            DrawCircleV(shadow_pos, 4.5f, (Color){ 50, 52, 65, 255 });
            DrawLineV(r_center, shadow_pos, (Color){ 24, 25, 32, 255 });

            // Vertical elevation
            if (fabsf(stalk_h) > 2.0f) {
                // Dashed vertical stalk
                int stalk_steps = (int)(fabsf(stalk_h) / 4.0f);
                for (int s = 0; s < stalk_steps; s += 2) {
                    float y1 = shadow_pos.y - (stalk_h * ((float)s / (float)stalk_steps));
                    float y2 = shadow_pos.y - (stalk_h * ((float)(s + 1) / (float)stalk_steps));
                    DrawLineEx((Vector2){ shadow_pos.x, y1 }, (Vector2){ shadow_pos.x, y2 }, 1.2f, (Color){ 90, 95, 110, 220 });
                }
            }

            // Audio reactive pulse
            float audio_energy = fminf(1.0f, telem.mid_energy * 2.0f + telem.side_energy * 3.0f);
            float orb_radius = 8.0f + audio_energy * 4.0f;

            // Floating sound source orb
            DrawCircleV(orb_pos, orb_radius * 2.2f, ColorAlpha(COLOR_ACCENT, 0.12f + audio_energy * 0.18f));
            DrawCircleV(orb_pos, orb_radius * 1.4f, ColorAlpha(COLOR_ACCENT, 0.28f));
            DrawCircleV(orb_pos, orb_radius, COLOR_ACCENT);
            DrawCircleLines((int)orb_pos.x, (int)orb_pos.y, orb_radius, WHITE);

            // Position tooltip over sound source
            const char *src_tip = TextFormat("%+.0f°  El:%+.0f°", cfg.spatial.azimuth_deg, cfg.spatial.elevation_deg);
            int tw_src = MeasureText(src_tip, 10);
            DrawRectangle((int)(orb_pos.x - tw_src / 2 - 4), (int)(orb_pos.y - 20), tw_src + 8, 14, (Color){ 10, 11, 14, 230 });
            DrawText(src_tip, (int)(orb_pos.x - tw_src / 2), (int)(orb_pos.y - 18), 10, COLOR_TEXT_PRIMARY);

            // Elevation arc gauge
            Rectangle elev_bar = { scene_box.x + scene_box.width - gauge_w + 6, scene_box.y + 44, 16.0f, scene_box.height - 110 };
            DrawRectangleRec(elev_bar, (Color){ 8, 8, 12, 255 });
            DrawRectangleLinesEx(elev_bar, 1.0f, (Color){ 22, 23, 28, 255 });

            // Zero degree horizon line on gauge
            float zero_norm = (+90.0f - 0.0f) / (+90.0f - (-45.0f));
            float zero_gy = elev_bar.y + zero_norm * elev_bar.height;
            DrawLine((int)elev_bar.x - 3, (int)zero_gy, (int)(elev_bar.x + elev_bar.width + 3), (int)zero_gy, (Color){ 60, 64, 76, 255 });
            DrawText("0°", (int)(elev_bar.x + elev_bar.width + 6), (int)zero_gy - 5, 9, COLOR_TEXT_MUTED);
            DrawText("+90°", (int)(elev_bar.x + elev_bar.width + 6), (int)elev_bar.y - 2, 9, (Color){ 60, 64, 76, 255 });
            DrawText("-45°", (int)(elev_bar.x + elev_bar.width + 6), (int)(elev_bar.y + elev_bar.height - 8), 9, (Color){ 60, 64, 76, 255 });

            // Gauge fill and slider thumb handle
            float elev_norm = (+90.0f - cfg.spatial.elevation_deg) / (+90.0f - (-45.0f));
            if (elev_norm < 0.0f) elev_norm = 0.0f;
            if (elev_norm > 1.0f) elev_norm = 1.0f;
            float thumb_y = elev_bar.y + elev_norm * elev_bar.height;

            Rectangle elev_thumb = { elev_bar.x - 2, thumb_y - 4, elev_bar.width + 4, 8.0f };
            DrawRectangleRec(elev_thumb, COLOR_ACCENT);
            DrawRectangleLinesEx(elev_thumb, 1.0f, WHITE);

            DrawText("HEIGHT", (int)elev_bar.x - 2, (int)elev_bar.y - 18, 9, COLOR_TEXT_MUTED);

            // Quick elevation preset buttons
            Rectangle p_top = { elev_bar.x - 4, elev_bar.y + elev_bar.height + 10, gauge_w - 6, 16 };
            Rectangle p_ear = { elev_bar.x - 4, p_top.y + 18, gauge_w - 6, 16 };
            Rectangle p_low = { elev_bar.x - 4, p_ear.y + 18, gauge_w - 6, 16 };

            if (interactive) {
                if (DrawBtn(p_top, "+45°", fabsf(cfg.spatial.elevation_deg - 45.0f) < 2.0f)) {
                    cfg.spatial.elevation_deg = 45.0f;
                    cfg_changed = true;
                }
                if (DrawBtn(p_ear, " 0°", fabsf(cfg.spatial.elevation_deg - 0.0f) < 2.0f)) {
                    cfg.spatial.elevation_deg = 0.0f;
                    cfg_changed = true;
                }
                if (DrawBtn(p_low, "-20°", fabsf(cfg.spatial.elevation_deg - (-20.0f)) < 2.0f)) {
                    cfg.spatial.elevation_deg = -20.0f;
                    cfg_changed = true;
                }
            }

            // Mouse interactions
            if (interactive) {
                bool hover_scene = CheckCollisionPointRec(mouse, scene_box);
                bool hover_orb = CheckCollisionPointCircle(mouse, orb_pos, 16.0f);
                bool hover_gauge = CheckCollisionPointRec(mouse, (Rectangle){ elev_bar.x - 6, elev_bar.y, elev_bar.width + 12, elev_bar.height });

                // Mouse wheel anywhere over the 3D scene adjusts elevation
                if (hover_scene) {
                    float wheel = GetMouseWheelMove();
                    if (wheel != 0.0f) {
                        cfg.spatial.elevation_deg += wheel * 2.0f;
                        if (cfg.spatial.elevation_deg < -45.0f) cfg.spatial.elevation_deg = -45.0f;
                        if (cfg.spatial.elevation_deg > +90.0f) cfg.spatial.elevation_deg = +90.0f;
                        cfg_changed = true;
                    }
                }

                // Right-Click drag anywhere on the scene alters elevation
                if (hover_scene && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
                    s_dragging_elevation = true;
                    s_elev_drag_start_y = mouse.y;
                    s_elev_drag_start_val = cfg.spatial.elevation_deg;
                }
                if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) s_dragging_elevation = false;

                if (s_dragging_elevation) {
                    float dy = s_elev_drag_start_y - mouse.y;
                    cfg.spatial.elevation_deg = s_elev_drag_start_val + dy * 0.45f;
                    if (cfg.spatial.elevation_deg < -45.0f) cfg.spatial.elevation_deg = -45.0f;
                    if (cfg.spatial.elevation_deg > +90.0f) cfg.spatial.elevation_deg = +90.0f;
                    cfg_changed = true;
                }

                // Elevation direct drag gauge
                if (hover_gauge && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    s_dragging_elev_arc = true;
                }
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) s_dragging_elev_arc = false;

                if (s_dragging_elev_arc) {
                    float n = (mouse.y - elev_bar.y) / elev_bar.height;
                    if (n < 0.0f) n = 0.0f;
                    if (n > 1.0f) n = 1.0f;
                    cfg.spatial.elevation_deg = +90.0f - n * (+90.0f - (-45.0f));
                    cfg_changed = true;
                }

                // Left-Clicking and dragging on the dome moves (azimuth, distance)
                if (hover_scene && !hover_gauge && !s_dragging_elev_arc && !s_dragging_elevation) {
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && (hover_orb || CheckCollisionPointCircle(mouse, shadow_pos, 22.0f) || CheckCollisionPointRec(mouse, (Rectangle){ r_center.x - rx_max, r_center.y - ry_max, rx_max * 2, ry_max * 2 }))) {
                        s_dragging_source_xy = true;
                    }
                }
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) s_dragging_source_xy = false;

                if (s_dragging_source_xy) {
                    float dx = mouse.x - r_center.x;
                    // Compensate for vertical isometric tilt
                    float dy = (mouse.y - r_center.y) / ry_scale;
                    float dist_px = sqrtf(dx * dx + dy * dy);
                    float n_dist = dist_px / rx_max;
                    if (n_dist < 0.0f) n_dist = 0.0f;
                    if (n_dist > 1.0f) n_dist = 1.0f;

                    cfg.spatial.distance_m = 0.5f + n_dist * 4.5f;
                    cfg.spatial.azimuth_deg = atan2f(dx, -dy) * (180.0f / PI);
                    cfg_changed = true;
                }
            }

            // HUD overlay
            DrawText("SPATIAL PERSPECTIVE VIEW", (int)scene_box.x + 14, (int)scene_box.y + 12, 11, COLOR_TEXT_MUTED);
            const char *hud_text = TextFormat("AZIMUTH: %+.0f°  |  ELEVATION: %+.0f°  |  DISTANCE: %.2fm",
                                              cfg.spatial.azimuth_deg, cfg.spatial.elevation_deg, cfg.spatial.distance_m);
            DrawText(hud_text, (int)scene_box.x + 14, (int)scene_box.y + 26, 10, COLOR_TEXT_PRIMARY);

            // Controls hint
            DrawText("Left-Drag: Pan X/Y  |  Right-Drag / Scroll: Height (Elevation)  |  R: Reset Center",
                     (int)scene_box.x + 14, (int)(scene_box.y + scene_box.height - 18), 10, (Color){ 80, 84, 96, 255 });
            break;
        }

        case KRYSTAL_TAB_BASS: {
            float cy = start_row_y;
            float rw = main_rect.width - 40.0f;
            if (DrawToggle((Rectangle){ main_rect.x, cy, rw, row_h }, "Sub-Bass Synthesis Engine", &cfg.bass.enabled, interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "Crossover Cutoff", &cfg.bass.cutoff_hz, 40.0f, 140.0f, "Hz", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "Harmonic Drive", &cfg.bass.intensity, 0.0f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "Harmonic Wet Mix", &cfg.bass.mix, 0.0f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "Direct Sub Level", &cfg.bass.sub_weight, 0.0f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "Harmonic Tone (2nd/3rd)", &cfg.bass.harmonic_tone, 0.0f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "Sub-Octave Weight", &cfg.bass.sub_octave, 0.0f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "Sub Phase Alignment", &cfg.bass.sub_phase_deg, 0.0f, 180.0f, "deg", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "Infrasonic Rumble Highpass", &cfg.bass.rumble_hz, 10.0f, 35.0f, "Hz", interactive)) cfg_changed = true;
            break;
        }

        case KRYSTAL_TAB_PRESENCE: {
            float cy = start_row_y;
            float rw = main_rect.width - 40.0f;
            DrawText("EXCITER & AIR SHEEN", (int)main_rect.x + 12, (int)cy - 4, 11, COLOR_TEXT_MUTED);
            cy += 16.0f;
            if (DrawToggle((Rectangle){ main_rect.x, cy, rw, row_h }, "Exciter Status", &cfg.exciter.enabled, interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "Air Crossover", &cfg.exciter.cutoff_hz, 2500.0f, 10000.0f, "Hz", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "Exciter Drive", &cfg.exciter.drive, 0.0f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "Exciter Mix", &cfg.exciter.mix, 0.0f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "10.5kHz+ Shimmer", &cfg.exciter.shimmer, 0.0f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h + 8.0f;

            DrawText("TRANSIENT PROCESSOR & DE-CLIP", (int)main_rect.x + 12, (int)cy - 4, 11, COLOR_TEXT_MUTED);
            cy += 16.0f;
            if (DrawToggle((Rectangle){ main_rect.x, cy, rw, row_h }, "Transient Engine", &cfg.transient.enabled, interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "Attack Punch", &cfg.transient.attack, -1.0f, +1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "Sustain Body", &cfg.transient.sustain, -1.0f, +1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawToggle((Rectangle){ main_rect.x, cy, rw, row_h }, "Hermite De-Clip", &cfg.transient.declip_enable, interactive)) cfg_changed = true;
            break;
        }

        case KRYSTAL_TAB_WARMTH: {
            float cy = start_row_y;
            float rw = main_rect.width - 40.0f;
            DrawText("SPECTRAL TAMERS", (int)main_rect.x + 12, (int)cy - 4, 11, COLOR_TEXT_MUTED);
            cy += 16.0f;
            if (DrawToggle((Rectangle){ main_rect.x, cy, rw, row_h }, "Dynamic Tamer", &cfg.spectral.enabled, interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "De-Harsh (3.2kHz)", &cfg.spectral.de_harsh, 0.0f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "De-Boom (160Hz)", &cfg.spectral.de_boom, 0.0f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h + 8.0f;

            DrawText("ANALOG SATURATOR", (int)main_rect.x + 12, (int)cy - 4, 11, COLOR_TEXT_MUTED);
            cy += 16.0f;
            if (DrawToggle((Rectangle){ main_rect.x, cy, rw, row_h }, "Saturator Engine", &cfg.saturator.enabled, interactive)) cfg_changed = true;
            cy += row_h;

            // Sat mode cycle
            const char *sat_modes[] = { "OFF", "Triode Tube", "Pentode Valve", "Tape Machine", "Transformer" };
            DrawText("Saturation Topology", (int)main_rect.x + 12, (int)cy + (int)(row_h - 14) / 2, FONT_SIZE_SM, COLOR_TEXT_MUTED);
            Rectangle sm_btn = { main_rect.x + main_rect.width - 240, cy + (row_h - 24) / 2, 160, 24 };
            if (interactive && DrawBtn(sm_btn, sat_modes[(int)cfg.saturator.mode], false)) {
                cfg.saturator.mode = (KrystalSatMode)(((int)cfg.saturator.mode + 1) % SAT_MODE_COUNT);
                cfg_changed = true;
            }
            cy += row_h;

            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "Drive Intensity", &cfg.saturator.drive, 0.0f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "Harmonic Bias", &cfg.saturator.bias, -0.5f, +0.5f, "", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "Saturator Wet Mix", &cfg.saturator.mix, 0.0f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawToggle((Rectangle){ main_rect.x, cy, rw, row_h }, "2x Linear-Phase Oversample", &cfg.saturator.oversample, interactive)) cfg_changed = true;
            break;
        }

        case KRYSTAL_TAB_ROUTING: {
            float cy = start_row_y;
            float rw = main_rect.width - 40.0f;
            DrawText("EQUAL-LOUDNESS ISO 226", (int)main_rect.x + 12, (int)cy - 4, 11, COLOR_TEXT_MUTED);
            cy += 16.0f;
            if (DrawToggle((Rectangle){ main_rect.x, cy, rw, row_h }, "Loudness Status", &cfg.loudness.enabled, interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "Loudness Intensity", &cfg.loudness.intensity, 0.0f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "Volume Reference", &cfg.loudness.ref_vol, 0.50f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "Spectral Tilt", &cfg.spectral.tilt_db, -6.0f, +6.0f, "dB", interactive)) cfg_changed = true;
            cy += row_h + 8.0f;

            DrawText("MASTER CHAIN & ROUTING", (int)main_rect.x + 12, (int)cy - 4, 11, COLOR_TEXT_MUTED);
            cy += 16.0f;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "Pre-Gain", &cfg.general.pre_gain_db, -12.0f, +12.0f, "dB", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "Stereo Balance", &cfg.general.balance, -1.0f, +1.0f, "", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "Master Dry/Wet", &cfg.general.master_mix, 0.0f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawSlider((Rectangle){ main_rect.x, cy, rw, row_h }, "Inter-Sample Headroom", &cfg.general.headroom_db, -3.0f, 0.0f, "dB", interactive)) cfg_changed = true;
            cy += row_h;
            if (DrawToggle((Rectangle){ main_rect.x, cy, rw, row_h }, "Automatic Level Matching", &cfg.general.auto_gain, interactive)) cfg_changed = true;
            break;
        }

        default:
            break;
    }

    if (cfg_changed) {
        krystal_set_config(&cfg);
    }

    // Footer telemetry
    DrawRectangleRec(footer_rect, (Color){ 6, 7, 10, 210 });
    DrawLine(0, (int)footer_rect.y, (int)b.width, (int)footer_rect.y, (Color){ 25, 26, 32, 200 });

    float tx = 16.0f;
    float ty = footer_rect.y + 11.0f;

    // Phase Correlation Bar
    DrawText("PHASE", (int)tx, (int)ty, 11, COLOR_TEXT_MUTED);
    tx += 46.0f;

    Rectangle ph_rect = { tx, ty + 2.0f, 100.0f, 6.0f };
    DrawRectangleRec(ph_rect, (Color){ 20, 22, 28, 255 });
    float ph_norm = (telem.phase_correlation + 1.0f) * 0.5f;
    if (ph_norm < 0.0f) ph_norm = 0.0f;
    if (ph_norm > 1.0f) ph_norm = 1.0f;

    Color ph_col = (telem.phase_correlation > 0.30f) ? (Color){ 100, 220, 140, 255 } :
                   ((telem.phase_correlation >= 0.0f) ? COLOR_ACCENT : (Color){ 230, 90, 90, 255 });
    DrawRectangle((int)ph_rect.x + (int)(ph_rect.width * 0.5f) - 1, (int)ph_rect.y - 1, 2, 8, (Color){ 40, 42, 50, 255 });
    DrawCircle((int)(ph_rect.x + ph_rect.width * ph_norm), (int)(ph_rect.y + 3.0f), 4.0f, ph_col);
    tx += 114.0f;

    DrawText(TextFormat("%+.2f", telem.phase_correlation), (int)tx, (int)ty, FONT_SIZE_SM, ph_col);
    tx += 56.0f;

    DrawText(TextFormat("CREST: %.1fdB", telem.crest_factor_db), (int)tx, (int)ty, FONT_SIZE_SM, COLOR_TEXT_MUTED);
    tx += 110.0f;

    DrawText(TextFormat("TRIM: %+.1fdB", telem.auto_trim_db), (int)tx, (int)ty, FONT_SIZE_SM, COLOR_TEXT_MUTED);
    tx += 100.0f;

    DrawText(TextFormat("LUFS: %.1f", telem.lufs_momentary), (int)tx, (int)ty, FONT_SIZE_SM, COLOR_TEXT_PRIMARY);
    tx += 90.0f;

    DrawText(TextFormat("PEAK: %+.1fdB", telem.peak_dbfs), (int)tx, (int)ty, FONT_SIZE_SM, COLOR_TEXT_PRIMARY);

    rlPopMatrix();
}