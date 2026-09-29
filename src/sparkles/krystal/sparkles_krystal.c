#define _DEFAULT_SOURCE
#include "sparkles_krystal.h"
#include "sparkles_theme.h"
#include "krystal_engine.h"
#include "rlgl.h"
#include "krystal_profiles.h"
#include "krystal_preset_manager.h"
#include "modals/sparkles_text_prompt.h"
#include "sparkles_nav.h"
#include "input/sparkles_input.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool s_krystal_open = false;
static float s_anim_progress = 0.0f;

typedef enum {
    KRYSTAL_TAB_SPATIAL = 0,
    KRYSTAL_TAB_BASS,
    KRYSTAL_TAB_PRESENCE,
    KRYSTAL_TAB_WARMTH,
    KRYSTAL_TAB_ROUTING,
    KRYSTAL_TAB_COUNT
} KrystalUiTab;

static KrystalUiTab s_active_tab = KRYSTAL_TAB_SPATIAL;

// Profile dropdown popup state
static bool s_profile_popup_open = false;
static float s_profile_popup_scroll = 0.0f;

// Arena modal state
static bool s_spatial_arena_open = false;
static bool s_arena_just_opened = false;
static bool s_dragging_arena_xy = false;
static bool s_dragging_arena_elev = false;
static bool s_dragging_arena_wheel = false;
static float s_arena_elev_drag_start_y = 0.0f;
static float s_arena_elev_drag_start_val = 0.0f;
static float s_arena_wheel_drag_start_x = 0.0f;
static float s_arena_wheel_drag_start_val = 0.0f;

bool sparkles_krystal_is_arena_open(void) {
    return s_spatial_arena_open;
}

void sparkles_krystal_close_arena(void) {
    s_spatial_arena_open = false;
    s_arena_just_opened = false;
    s_dragging_arena_xy = false;
    s_dragging_arena_elev = false;
    s_dragging_arena_wheel = false;
    sparkles_input_consume();
}

// Tab internal scroll
static float s_tab_scroll_y = 0.0f;

// Active interaction tracking
static void *s_active_slider_ptr = NULL;
static bool s_dragging_spatial_orb = false;

// Touch swipe gesture tracking
static Vector2 s_swipe_down_pos = {0};
static bool s_swipe_tracking = false;
static float s_swipe_time = 0.0f;

static void on_krystal_preset_save_submit(const char *name, void *ud) {
    (void)ud;
    if (!name || !name[0]) return;
    KrystalConfig cfg;
    krystal_get_config(&cfg);
    krystal_presets_save(name, &cfg);
    krystal_set_active_preset_name(name);
}

void sparkles_krystal_init(void) {
    s_krystal_open = false;
    s_anim_progress = 0.0f;
    s_active_tab = KRYSTAL_TAB_SPATIAL;
    s_profile_popup_open = false;
    s_profile_popup_scroll = 0.0f;
    s_spatial_arena_open = false;
    s_tab_scroll_y = 0.0f;
    s_active_slider_ptr = NULL;
    s_dragging_spatial_orb = false;
    s_dragging_arena_xy = false;
    s_dragging_arena_elev = false;
    s_dragging_arena_wheel = false;
    s_swipe_tracking = false;
}

void sparkles_krystal_toggle(void) {
    extern void sparkles_nav_set_y(int y);
    extern int sparkles_nav_get_target_y(void);

    if (s_spatial_arena_open) {
        sparkles_krystal_close_arena();
        return;
    }

    if (sparkles_nav_get_target_y() == 2) {
        sparkles_nav_set_y(0);
        s_krystal_open = false;
    } else {
        sparkles_nav_set_y(2);
        s_krystal_open = true;
    }
    s_active_slider_ptr = NULL;
    s_dragging_spatial_orb = false;
    s_profile_popup_open = false;
    s_spatial_arena_open = false;
    s_tab_scroll_y = 0.0f;
}

bool sparkles_krystal_is_open(void) { return s_krystal_open; }
bool sparkles_krystal_is_visible(void) { return (s_krystal_open || s_anim_progress > 0.001f); }
float sparkles_krystal_get_anim_progress(void) { return s_anim_progress; }

void sparkles_krystal_step_tab(int delta) {
    int next = (int)s_active_tab + delta;
    if (next < 0) next = KRYSTAL_TAB_COUNT - 1;
    if (next >= KRYSTAL_TAB_COUNT) next = 0;
    s_active_tab = (KrystalUiTab)next;
    s_tab_scroll_y = 0.0f;
    s_active_slider_ptr = NULL;
    s_dragging_spatial_orb = false;
}

bool sparkles_krystal_is_dragging(void) {
    return (s_active_slider_ptr != NULL || s_dragging_spatial_orb || s_dragging_arena_xy || s_dragging_arena_elev || s_dragging_arena_wheel);
}

void sparkles_krystal_update(float screen_w, float screen_h) {
    (void)screen_w; (void)screen_h;
    s_anim_progress = sparkles_nav_is_krystal_open() ? 1.0f : 0.0f;
}

static bool DrawBtn(Rectangle bounds, const char *text, bool active) {
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, bounds);
    bool clicked = !sparkles_input_is_consumed() && sparkles_input_consume_tap(bounds, NULL);

    Color bg = active ? ColorAlpha(COLOR_ACCENT, 0.22f) : (hover ? (Color){24, 26, 34, 255} : (Color){12, 13, 17, 255});
    Color fg = active ? COLOR_ACCENT : (hover ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED);

    DrawRectangleRounded(bounds, 0.25f, 4, bg);
    DrawRectangleRoundedLinesEx(bounds, 0.25f, 4, 1.0f, active ? COLOR_ACCENT : (hover ? (Color){60, 64, 78, 255} : (Color){30, 32, 42, 255}));

    int tw = MeasureText(text, FONT_SIZE_SM);
    DrawText(text, (int)(bounds.x + (bounds.width - tw) * 0.5f), (int)(bounds.y + (bounds.height - FONT_SIZE_SM) * 0.5f), FONT_SIZE_SM, fg);
    return clicked;
}

static bool DrawSlider(Rectangle bounds, const char *label, float *val, float min_val, float max_val, const char *unit, bool is_interactive) {
    float ui_scale = sparkles_get_ui_scale();
    Vector2 mouse = GetMousePosition();
    bool changed = false;

    char val_buf[32];
    if (strcmp(unit, "%") == 0) snprintf(val_buf, sizeof(val_buf), "%.0f%%", *val * 100.0f);
    else if (strcmp(unit, "dB") == 0) snprintf(val_buf, sizeof(val_buf), "%+.1f dB", *val);
    else if (strcmp(unit, "deg") == 0 || strcmp(unit, "°") == 0) snprintf(val_buf, sizeof(val_buf), "%+.0f°", *val);
    else if (strcmp(unit, "m") == 0) snprintf(val_buf, sizeof(val_buf), "%.2f m", *val);
    else if (strcmp(unit, "Hz") == 0) {
        if (*val >= 1000.0f) snprintf(val_buf, sizeof(val_buf), "%.1f kHz", *val * 0.001f);
        else snprintf(val_buf, sizeof(val_buf), "%.0f Hz", *val);
    } else snprintf(val_buf, sizeof(val_buf), "%.2f", *val);

    float norm = (*val - min_val) / (max_val - min_val);
    if (norm < 0.0f) norm = 0.0f;
    if (norm > 1.0f) norm = 1.0f;

    float track_x, track_y, track_w, track_h;
    float thumb_r = 9.0f * ui_scale;

    if (bounds.width < 500.0f * ui_scale) {
        // Mobile layout
        DrawText(label, (int)(bounds.x + 4.0f * ui_scale), (int)(bounds.y + 2.0f * ui_scale), FONT_SIZE_SM, COLOR_TEXT_MUTED);
        int val_tw = MeasureText(val_buf, FONT_SIZE_SM);
        DrawText(val_buf, (int)(bounds.x + bounds.width - val_tw - 6.0f * ui_scale), (int)(bounds.y + 2.0f * ui_scale), FONT_SIZE_SM, COLOR_ACCENT);

        track_x = bounds.x + 8.0f * ui_scale;
        track_w = bounds.width - 16.0f * ui_scale;
        track_h = 6.0f * ui_scale;
        track_y = bounds.y + 26.0f * ui_scale;
    } else {
        // Desktop layout
        int label_w = MeasureText(label, FONT_SIZE_SM) + (14.0f * ui_scale);
        int val_w = MeasureText(val_buf, FONT_SIZE_SM) + (14.0f * ui_scale);

        track_x = bounds.x + label_w;
        track_w = bounds.width - label_w - val_w - (10.0f * ui_scale);
        track_h = 6.0f * ui_scale;
        track_y = bounds.y + (bounds.height - track_h) * 0.5f;

        DrawText(label, (int)(bounds.x + 4.0f * ui_scale), (int)(bounds.y + (bounds.height - FONT_SIZE_SM) * 0.5f), FONT_SIZE_SM, COLOR_TEXT_MUTED);
        DrawText(val_buf, (int)(track_x + track_w + 10.0f * ui_scale), (int)(bounds.y + (bounds.height - FONT_SIZE_SM) * 0.5f), FONT_SIZE_SM, COLOR_TEXT_PRIMARY);
    }

    Rectangle track_rect = { track_x, track_y, track_w, track_h };
    DrawRectangleRounded(track_rect, 0.5f, 4, (Color){ 24, 25, 32, 255 });

    Rectangle fill_rect = { track_x, track_y, track_w * norm, track_h };
    DrawRectangleRounded(fill_rect, 0.5f, 4, COLOR_ACCENT);

    Vector2 thumb_pos = { track_x + (track_w * norm), track_y + track_h * 0.5f };
    bool is_active_this = (s_active_slider_ptr == (void*)val);
    DrawCircleV(thumb_pos, is_active_this ? (thumb_r + 2.5f) : thumb_r, is_active_this ? WHITE : COLOR_TEXT_PRIMARY);
    DrawCircleLines((int)thumb_pos.x, (int)thumb_pos.y, thumb_r + 1.0f, COLOR_ACCENT);

    if (is_interactive && !sparkles_input_is_consumed()) {
        Rectangle touch_area = (bounds.width < 500.0f * ui_scale)
            ? (Rectangle){ bounds.x, bounds.y + 14.0f * ui_scale, bounds.width, bounds.height - 14.0f * ui_scale }
            : (Rectangle){ track_x - 12.0f * ui_scale, bounds.y, track_w + 24.0f * ui_scale, bounds.height };

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, touch_area)) {
            s_active_slider_ptr = (void*)val;
        }

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            if (s_active_slider_ptr == (void*)val) s_active_slider_ptr = NULL;
        }

        if (s_active_slider_ptr == (void*)val && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            sparkles_input_consume();
            float n = (mouse.x - track_x) / track_w;
            if (n < 0.0f) n = 0.0f;
            if (n > 1.0f) n = 1.0f;
            *val = min_val + n * (max_val - min_val);
            changed = true;
        }

        if (CheckCollisionPointRec(mouse, bounds)) {
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
    float ui_scale = sparkles_get_ui_scale();
    Vector2 mouse = GetMousePosition();
    bool hover = CheckCollisionPointRec(mouse, bounds);

    DrawText(label, (int)(bounds.x + 8.0f * ui_scale), (int)(bounds.y + (bounds.height - FONT_SIZE_SM) * 0.5f), FONT_SIZE_SM, hover ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED);

    float btn_w = 68.0f * ui_scale;
    float btn_h = fmaxf(26.0f, 24.0f * ui_scale);
    Rectangle btn = { bounds.x + bounds.width - btn_w - 6.0f * ui_scale, bounds.y + (bounds.height - btn_h) * 0.5f, btn_w, btn_h };

    bool clicked = is_interactive && !sparkles_input_is_consumed() && sparkles_input_consume_tap(bounds, NULL);
    if (clicked) *val = !(*val);

    Color st_col = *val ? COLOR_ACCENT : (Color){ 24, 26, 32, 255 };
    DrawRectangleRounded(btn, 0.35f, 4, st_col);
    DrawRectangleRoundedLinesEx(btn, 0.35f, 4, 1.0f, *val ? COLOR_ACCENT : (Color){ 45, 48, 58, 255 });

    const char *st_txt = *val ? "ON" : "OFF";
    int tw = MeasureText(st_txt, FONT_SIZE_XS);
    DrawText(st_txt, (int)(btn.x + (btn.width - tw) * 0.5f), (int)(btn.y + (btn.height - FONT_SIZE_XS) * 0.5f), FONT_SIZE_XS, *val ? (Color){ 10, 10, 12, 255 } : COLOR_TEXT_MUTED);

    return clicked;
}

// Sound perspective area
static void draw_spatial_arena_screen(float screen_w, float screen_h, KrystalConfig *cfg, const KrystalTelemetry *telem, bool interactive) {
    float ui_scale = sparkles_get_ui_scale();
    Vector2 mouse = GetMousePosition();

    float box_w = fminf(screen_w - 16.0f * ui_scale, 860.0f * ui_scale);
    float box_h = fminf(screen_h - 40.0f * ui_scale, 680.0f * ui_scale);
    Rectangle arena_rect = { (screen_w - box_w) * 0.5f, (screen_h - box_h) * 0.5f, box_w, box_h };

    DrawRectangle(0, 0, (int)screen_w, (int)screen_h, ColorAlpha(BLACK, 0.85f));
    DrawRectangleRec(arena_rect, (Color){ 8, 9, 13, 252 });
    DrawRectangleLinesEx(arena_rect, 1.2f, COLOR_ACCENT);
    DrawNothingCornerBrackets(arena_rect, 10.0f * ui_scale, COLOR_ACCENT);

    // Header
    float top_h = 42.0f * ui_scale;
    DrawRectangle((int)arena_rect.x, (int)arena_rect.y, (int)arena_rect.width, (int)top_h, (Color){ 12, 13, 18, 240 });
    DrawLine((int)arena_rect.x, (int)(arena_rect.y + top_h), (int)(arena_rect.x + arena_rect.width), (int)(arena_rect.y + top_h), (Color){ 28, 30, 40, 200 });

    DrawText("3D VIEW", (int)(arena_rect.x + 14.0f * ui_scale), (int)(arena_rect.y + 12.0f * ui_scale), FONT_SIZE_MD, COLOR_TEXT_PRIMARY);

    float btn_close_w = 64.0f * ui_scale;
    Rectangle btn_close = { arena_rect.x + arena_rect.width - btn_close_w - 10.0f * ui_scale, arena_rect.y + 7.0f * ui_scale, btn_close_w, 28.0f * ui_scale };
    if (interactive && DrawBtn(btn_close, "✕ Esc", false)) {
        sparkles_krystal_close_arena();
        return;
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        sparkles_krystal_close_arena();
        return;
    }

    // Debouncing
    if (s_arena_just_opened) {
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) || !IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            s_arena_just_opened = false;
        }
        sparkles_input_consume();
        sparkles_input_block_area(arena_rect);
        return;
    }

    // Split layout
    float bottom_panel_h = 58.0f * ui_scale;
    Rectangle scene_box = {
        arena_rect.x + 12.0f * ui_scale,
        arena_rect.y + top_h + 8.0f * ui_scale,
        arena_rect.width - 24.0f * ui_scale,
        arena_rect.height - top_h - bottom_panel_h - 22.0f * ui_scale
    };

    DrawRectangleRec(scene_box, (Color){ 6, 7, 10, 220 });
    DrawRectangleLinesEx(scene_box, 1.0f, (Color){ 28, 30, 40, 200 });

    // Scene perspective geometry
    Vector2 r_center = { scene_box.x + scene_box.width * 0.50f, scene_box.y + scene_box.height * 0.58f };
    float rx_max = fminf(scene_box.width * 0.44f, scene_box.height * 0.46f);
    float ry_scale = 0.52f;
    float ry_max = rx_max * ry_scale;

    // Concentric distance rings
    const float ring_ratios[] = { 0.30f, 0.62f, 1.0f };
    const char *dist_labels[] = { "1.5m", "3.0m", "5.0m" };
    for (int r = 0; r < 3; r++) {
        float rw = rx_max * ring_ratios[r];
        float rh = ry_max * ring_ratios[r];
        const int pts = 48;
        Vector2 prev = { r_center.x + cosf(0) * rw, r_center.y + sinf(0) * rh };
        for (int p = 1; p <= pts; p++) {
            float a = ((float)p / (float)pts) * 2.0f * (float)PI;
            Vector2 cur = { r_center.x + cosf(a) * rw, r_center.y + sinf(a) * rh };
            DrawLineV(prev, cur, (r == 2) ? (Color){ 36, 40, 52, 255 } : (Color){ 20, 22, 28, 255 });
            prev = cur;
        }
        DrawText(dist_labels[r], (int)(r_center.x + rw + 4), (int)r_center.y - 6, 10, (Color){ 70, 75, 88, 255 });
    }

    // Crosshairs
    DrawLine((int)(r_center.x - rx_max), (int)r_center.y, (int)(r_center.x + rx_max), (int)r_center.y, (Color){ 22, 24, 30, 255 });
    DrawLine((int)r_center.x, (int)(r_center.y - ry_max), (int)r_center.x, (int)(r_center.y + ry_max), (Color){ 22, 24, 30, 255 });
    DrawText("F", (int)r_center.x - 3, (int)(r_center.y - ry_max - 14), 10, COLOR_TEXT_MUTED);
    DrawText("B", (int)r_center.x - 3, (int)(r_center.y + ry_max + 4), 10, COLOR_TEXT_DARK);
    DrawText("L", (int)(r_center.x - rx_max - 12), (int)r_center.y - 5, 10, COLOR_TEXT_DARK);
    DrawText("R", (int)(r_center.x + rx_max + 4), (int)r_center.y - 5, 10, COLOR_TEXT_DARK);

    // Virtual listener avatar
    DrawCircleLines((int)r_center.x, (int)r_center.y, 11.0f * ui_scale, COLOR_TEXT_DARK);
    DrawCircleV(r_center, 9.0f * ui_scale, (Color){ 16, 17, 22, 255 });
    DrawLine((int)r_center.x, (int)(r_center.y - 9.0f * ui_scale), (int)r_center.x, (int)(r_center.y - 18.0f * ui_scale), COLOR_ACCENT);
    DrawCircle((int)(r_center.x - 10.0f * ui_scale), (int)r_center.y, 2.5f * ui_scale, COLOR_TEXT_MUTED);
    DrawCircle((int)(r_center.x + 10.0f * ui_scale), (int)r_center.y, 2.5f * ui_scale, COLOR_TEXT_MUTED);
    DrawText("LISTENER", (int)r_center.x - 22, (int)(r_center.y + 14.0f * ui_scale), 9, (Color){ 80, 84, 96, 255 });

    // Sound object in perspective
    float az_rad = cfg->spatial.azimuth_deg * ((float)PI / 180.0f);
    float dist_norm = (cfg->spatial.distance_m - 0.5f) / 4.5f;
    if (dist_norm < 0.05f) dist_norm = 0.05f;
    if (dist_norm > 1.0f) dist_norm = 1.0f;

    Vector2 shadow_pos = {
        r_center.x + sinf(az_rad) * dist_norm * rx_max,
        r_center.y - cosf(az_rad) * dist_norm * ry_max
    };

    float el_rad = cfg->spatial.elevation_deg * ((float)PI / 180.0f);
    float height_span = rx_max * 0.70f;
    float stalk_h = sinf(el_rad) * height_span;
    Vector2 orb_pos = { shadow_pos.x, shadow_pos.y - stalk_h };

    // Virtual speakers L / R
    float stage_half_rad = (cfg->spatial.stage_angle_deg * 0.5f) * ((float)PI / 180.0f);
    Vector2 spk_l_shadow = { r_center.x + sinf(az_rad - stage_half_rad) * dist_norm * rx_max, r_center.y - cosf(az_rad - stage_half_rad) * dist_norm * ry_max };
    Vector2 spk_r_shadow = { r_center.x + sinf(az_rad + stage_half_rad) * dist_norm * rx_max, r_center.y - cosf(az_rad + stage_half_rad) * dist_norm * ry_max };

    DrawLineEx(r_center, spk_l_shadow, 1.0f, (Color){ 30, 36, 48, 160 });
    DrawLineEx(r_center, spk_r_shadow, 1.0f, (Color){ 30, 36, 48, 160 });
    DrawCircleV(spk_l_shadow, 3.5f * ui_scale, (Color){ 100, 160, 255, 180 });
    DrawCircleV(spk_r_shadow, 3.5f * ui_scale, (Color){ 255, 120, 150, 180 });
    DrawText("L", (int)spk_l_shadow.x - 7, (int)spk_l_shadow.y - 12, 9, (Color){ 100, 160, 255, 180 });
    DrawText("R", (int)spk_r_shadow.x + 3, (int)spk_r_shadow.y - 12, 9, (Color){ 255, 120, 150, 180 });

    DrawCircleV(shadow_pos, 4.5f * ui_scale, (Color){ 50, 52, 65, 255 });
    DrawLineV(r_center, shadow_pos, (Color){ 24, 25, 32, 255 });

    // Vertical stalk
    if (fabsf(stalk_h) > 2.0f) {
        int steps = (int)(fabsf(stalk_h) / 4.0f);
        for (int s = 0; s < steps; s += 2) {
            float y1 = shadow_pos.y - (stalk_h * ((float)s / (float)steps));
            float y2 = shadow_pos.y - (stalk_h * ((float)(s + 1) / (float)steps));
            DrawLineEx((Vector2){ shadow_pos.x, y1 }, (Vector2){ shadow_pos.x, y2 }, 1.2f, (Color){ 90, 95, 110, 220 });
        }
    }

    // Audio reactive circle
    float audio_energy = fminf(1.0f, telem->mid_energy * 2.0f + telem->side_energy * 3.0f);
    float orb_radius = (8.0f + audio_energy * 4.0f) * ui_scale;

    DrawCircleV(orb_pos, orb_radius * 2.2f, ColorAlpha(COLOR_ACCENT, 0.12f + audio_energy * 0.18f));
    DrawCircleV(orb_pos, orb_radius * 1.4f, ColorAlpha(COLOR_ACCENT, 0.28f));
    DrawCircleV(orb_pos, orb_radius, COLOR_ACCENT);
    DrawCircleLines((int)orb_pos.x, (int)orb_pos.y, orb_radius, WHITE);

    const char *src_tip = TextFormat("%+.0f°  El:%+.0f°", cfg->spatial.azimuth_deg, cfg->spatial.elevation_deg);
    int tw_src = MeasureText(src_tip, 10);
    DrawRectangle((int)(orb_pos.x - tw_src / 2 - 4), (int)(orb_pos.y - 20), tw_src + 8, 14, (Color){ 10, 11, 14, 230 });
    DrawText(src_tip, (int)(orb_pos.x - tw_src / 2), (int)(orb_pos.y - 18), 10, COLOR_TEXT_PRIMARY);

    // Divider line
    float div_y = scene_box.y + scene_box.height + 6.0f * ui_scale;
    DrawLine((int)(arena_rect.x + 12.0f * ui_scale), (int)div_y,
             (int)(arena_rect.x + arena_rect.width - 12.0f * ui_scale), (int)div_y,
             (Color){ 28, 30, 40, 200 });

    // Bottom controls area (buttons left/mid, thumbwheel knob right)
    float ctrl_y = div_y + 8.0f * ui_scale;
    float ctrl_h = 36.0f * ui_scale;

    // Thumbwheel on the right
    float wheel_w = 140.0f * ui_scale;
    Rectangle wheel_rect = {
        arena_rect.x + arena_rect.width - wheel_w - 14.0f * ui_scale,
        ctrl_y,
        wheel_w,
        ctrl_h
    };

    // Thumbwheel cylinder
    DrawRectangleRounded(wheel_rect, 0.25f, 4, (Color){ 10, 11, 16, 255 });
    DrawRectangleRoundedLinesEx(wheel_rect, 0.25f, 4, 1.0f, (Color){ 36, 40, 52, 255 });

    // Thumbwheel notches
    float elev_range = 90.0f - (-45.0f);
    float elev_deg = cfg->spatial.elevation_deg;
    float norm_deg = (elev_deg - (-45.0f)) / elev_range;
    if (norm_deg < 0.0f) norm_deg = 0.0f;
    if (norm_deg > 1.0f) norm_deg = 1.0f;

    BeginScissorMode((int)wheel_rect.x + 2, (int)wheel_rect.y + 2, (int)wheel_rect.width - 4, (int)wheel_rect.height - 4);

    // Lateral cylinder shade
    DrawRectangleGradientH((int)wheel_rect.x, (int)wheel_rect.y, 24, (int)wheel_rect.height, ColorAlpha(BLACK, 0.7f), ColorAlpha(BLACK, 0.0f));
    DrawRectangleGradientH((int)(wheel_rect.x + wheel_rect.width - 24), (int)wheel_rect.y, 24, (int)wheel_rect.height, ColorAlpha(BLACK, 0.0f), ColorAlpha(BLACK, 0.7f));

    // Rotation tick lines
    float tick_spacing = 14.0f * ui_scale;
    float center_x = wheel_rect.x + wheel_rect.width * 0.5f;
    float offset_px = fmodf(elev_deg * 2.2f, tick_spacing);

    for (float tx = center_x - (wheel_w * 0.5f) - tick_spacing; tx <= center_x + (wheel_w * 0.5f) + tick_spacing; tx += tick_spacing) {
        float draw_tx = tx + offset_px;
        float dist_from_center = fabsf(draw_tx - center_x) / (wheel_w * 0.5f);
        if (dist_from_center < 1.0f) {
            float tick_alpha = cosf(dist_from_center * (float)PI * 0.5f);
            DrawLineEx(
                (Vector2){ draw_tx, wheel_rect.y + 4.0f * ui_scale },
                (Vector2){ draw_tx, wheel_rect.y + wheel_rect.height - 4.0f * ui_scale },
                1.4f, ColorAlpha(COLOR_TEXT_MUTED, tick_alpha * 0.65f)
            );
        }
    }

    // Center index needle
    DrawLineEx(
        (Vector2){ center_x, wheel_rect.y + 2.0f * ui_scale },
        (Vector2){ center_x, wheel_rect.y + wheel_rect.height - 2.0f * ui_scale },
        2.0f, COLOR_ACCENT
    );

    EndScissorMode();

    // Thumbwheel label and value above wheel
    const char *wheel_val_str = TextFormat("ELEV %+.0f°", cfg->spatial.elevation_deg);
    int tw_val = MeasureText(wheel_val_str, 9);
    DrawText(wheel_val_str, (int)(wheel_rect.x + (wheel_rect.width - tw_val) * 0.5f), (int)(wheel_rect.y - 12.0f * ui_scale), 9, COLOR_ACCENT);

    // Elevation quick buttons
    float btn_w = 64.0f * ui_scale;
    float btn_gap = 6.0f * ui_scale;
    float start_btn_x = arena_rect.x + 14.0f * ui_scale;

    Rectangle btn_b20 = { start_btn_x, ctrl_y, btn_w, ctrl_h };
    Rectangle btn_zero = { start_btn_x + (btn_w + btn_gap), ctrl_y, btn_w, ctrl_h };
    Rectangle btn_p45  = { start_btn_x + (btn_w + btn_gap) * 2, ctrl_y, btn_w, ctrl_h };
    Rectangle btn_p90  = { start_btn_x + (btn_w + btn_gap) * 3, ctrl_y, btn_w, ctrl_h };

    if (interactive) {
        if (DrawBtn(btn_b20,  "-20° Low", fabsf(cfg->spatial.elevation_deg - (-20.0f)) < 2.0f)) cfg->spatial.elevation_deg = -20.0f;
        if (DrawBtn(btn_zero, " 0° Ear",  fabsf(cfg->spatial.elevation_deg - 0.0f) < 2.0f))     cfg->spatial.elevation_deg = 0.0f;
        if (DrawBtn(btn_p45,  "+45° High", fabsf(cfg->spatial.elevation_deg - 45.0f) < 2.0f))  cfg->spatial.elevation_deg = 45.0f;
        if (DrawBtn(btn_p90,  "+90° Top",  fabsf(cfg->spatial.elevation_deg - 90.0f) < 2.0f))  cfg->spatial.elevation_deg = 90.0f;
    }

    // Mouse and touch interactions
    if (interactive) {
        bool hover_scene = CheckCollisionPointRec(mouse, scene_box);
        bool hover_wheel = CheckCollisionPointRec(mouse, wheel_rect);

        // Adjust elevation (wheel and drag)
        if (hover_scene || hover_wheel) {
            float wheel = GetMouseWheelMove();
            if (wheel != 0.0f) {
                cfg->spatial.elevation_deg += wheel * 2.5f;
                if (cfg->spatial.elevation_deg < -45.0f) cfg->spatial.elevation_deg = -45.0f;
                if (cfg->spatial.elevation_deg > +90.0f) cfg->spatial.elevation_deg = +90.0f;
            }
        }

        // Right-click drag on scene adjusts elevation
        if (hover_scene && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            s_dragging_arena_elev = true;
            s_arena_elev_drag_start_y = mouse.y;
            s_arena_elev_drag_start_val = cfg->spatial.elevation_deg;
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) s_dragging_arena_elev = false;

        if (s_dragging_arena_elev) {
            float dy = s_arena_elev_drag_start_y - mouse.y;
            cfg->spatial.elevation_deg = s_arena_elev_drag_start_val + dy * 0.45f;
            if (cfg->spatial.elevation_deg < -45.0f) cfg->spatial.elevation_deg = -45.0f;
            if (cfg->spatial.elevation_deg > +90.0f) cfg->spatial.elevation_deg = +90.0f;
        }

        // Dragging thumbwheel knob
        if (hover_wheel && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            s_dragging_arena_wheel = true;
            s_arena_wheel_drag_start_x = mouse.x;
            s_arena_wheel_drag_start_val = cfg->spatial.elevation_deg;
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) s_dragging_arena_wheel = false;

        if (s_dragging_arena_wheel && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            sparkles_input_consume();
            float dx = mouse.x - s_arena_wheel_drag_start_x;
            cfg->spatial.elevation_deg = s_arena_wheel_drag_start_val + dx * 0.50f;
            if (cfg->spatial.elevation_deg < -45.0f) cfg->spatial.elevation_deg = -45.0f;
            if (cfg->spatial.elevation_deg > +90.0f) cfg->spatial.elevation_deg = +90.0f;
        }

        // Left-click/touch pan in scene
        if (hover_scene && !s_dragging_arena_wheel && !s_dragging_arena_elev && !sparkles_input_is_consumed()) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) s_dragging_arena_xy = true;
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) s_dragging_arena_xy = false;

        if (s_dragging_arena_xy && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            sparkles_input_consume();
            float dx = mouse.x - r_center.x;
            float dy = (mouse.y - r_center.y) / ry_scale;
            float dist_px = sqrtf(dx * dx + dy * dy);
            float n_dist = dist_px / rx_max;
            if (n_dist < 0.05f) n_dist = 0.05f;
            if (n_dist > 1.0f)  n_dist = 1.0f;

            cfg->spatial.distance_m = 0.5f + n_dist * 4.5f;
            cfg->spatial.azimuth_deg = atan2f(dx, -dy) * (180.0f / (float)PI);
        }
    }

    // Top hud
    const char *hud_text = TextFormat("AZIMUTH: %+.0f°  |  ELEVATION: %+.0f°  |  DISTANCE: %.2fm  |  SPREAD: %.0f°",
                                      cfg->spatial.azimuth_deg, cfg->spatial.elevation_deg, cfg->spatial.distance_m, cfg->spatial.stage_angle_deg);
    DrawText(hud_text, (int)(scene_box.x + 12.0f * ui_scale), (int)(scene_box.y + 10.0f * ui_scale), 10, COLOR_TEXT_PRIMARY);

    sparkles_input_block_area(arena_rect);
}

void sparkles_krystal_render(float screen_w, float screen_h) {
    if (s_anim_progress <= 0.001f) return;

    float inv = 1.0f - s_anim_progress;
    float ease = 1.0f - (inv * inv * inv);
    float ui_scale = sparkles_get_ui_scale();
    bool is_portrait = (screen_h > screen_w);
    float dt = GetFrameTime();

    float fall_scale = 1.0f + 0.15f * inv;
    float drop_y = -40.0f * (inv * inv);

    rlPushMatrix();
    rlTranslatef(screen_w * 0.5f, screen_h * 0.5f + drop_y, 0.0f);
    rlScalef(fall_scale, fall_scale, 1.0f);
    rlTranslatef(-screen_w * 0.5f, -screen_h * 0.5f, 0.0f);

    Vector2 mouse = GetMousePosition();
    bool interactive = (s_anim_progress >= 0.99f);

    KrystalConfig cfg;
    krystal_get_config(&cfg);

    KrystalTelemetry telem;
    krystal_get_telemetry(&telem);

    // If area is open, render on top
    if (s_spatial_arena_open) {
        draw_spatial_arena_screen(screen_w, screen_h, &cfg, &telem, interactive);
        krystal_set_config(&cfg);
        rlPopMatrix();
        return;
    }

    // Dimmed backdrop
    DrawRectangle(0, 0, (int)screen_w, (int)screen_h, ColorAlpha((Color){ 3, 3, 5, 255 }, 0.72f * ease));

    // Card container
    float card_w = is_portrait ? fminf(screen_w - 16.0f * ui_scale, 500.0f * ui_scale) : fminf(screen_w - 40.0f * ui_scale, 860.0f * ui_scale);
    float card_h = is_portrait ? fminf(screen_h - 48.0f * ui_scale, 660.0f * ui_scale) : fminf(screen_h - 40.0f * ui_scale, 580.0f * ui_scale);
    float card_x = (screen_w - card_w) * 0.5f;
    float card_y = (screen_h - card_h) * 0.5f;
    Rectangle card_rect = { card_x, card_y, card_w, card_h };

    DrawRectangleRec(card_rect, (Color){ 9, 10, 14, 252 });
    DrawRectangleLinesEx(card_rect, 1.2f, (Color){ 34, 38, 48, 255 });
    DrawNothingCornerBrackets(card_rect, 10.0f * ui_scale, COLOR_ACCENT);

    // Top header bar
    float top_bar_h = 42.0f * ui_scale;
    DrawRectangle((int)card_rect.x, (int)card_rect.y, (int)card_rect.width, (int)top_bar_h, (Color){ 12, 13, 18, 230 });
    DrawLine((int)card_rect.x, (int)(card_rect.y + top_bar_h), (int)(card_rect.x + card_rect.width), (int)(card_rect.y + top_bar_h), (Color){ 28, 30, 40, 200 });

    float btn_h = 28.0f * ui_scale;
    float btn_y = card_rect.y + (top_bar_h - btn_h) * 0.5f;

    // Profiles dropdown btn
    const char *active_name = krystal_get_active_preset_name();
    const char *prof_btn_text = TextFormat("Profile: %s ▾", active_name[0] ? active_name : "Bypass");
    float prof_btn_w = fminf((float)MeasureText(prof_btn_text, FONT_SIZE_SM) + 20.0f * ui_scale, card_rect.width * 0.50f);
    Rectangle prof_btn = { card_rect.x + 10.0f * ui_scale, btn_y, prof_btn_w, btn_h };

    if (interactive && DrawBtn(prof_btn, prof_btn_text, s_profile_popup_open)) {
        s_profile_popup_open = !s_profile_popup_open;
    }

    // Header controls
    float btn_close_w = 54.0f * ui_scale;
    float btn_master_w = 78.0f * ui_scale;
    float close_x = card_rect.x + card_rect.width - btn_close_w - 10.0f * ui_scale;
    float master_x = close_x - btn_master_w - 8.0f * ui_scale;

    Rectangle master_btn = { master_x, btn_y, btn_master_w, btn_h };
    Rectangle close_btn = { close_x, btn_y, btn_close_w, btn_h };

    if (interactive && DrawBtn(master_btn, cfg.master_enabled ? "ACTIVE" : "BYPASS", cfg.master_enabled)) {
        krystal_toggle_enabled();
    }
    if (interactive && DrawBtn(close_btn, "✕ Esc", false)) {
        sparkles_krystal_toggle();
    }

    // Tab bar navarea
    float tab_bar_y = card_rect.y + top_bar_h + 4.0f * ui_scale;
    float tab_bar_h = 36.0f * ui_scale;
    Rectangle tab_bar_rect = { card_rect.x + 10.0f * ui_scale, tab_bar_y, card_rect.width - 20.0f * ui_scale, tab_bar_h };

    const char *tab_titles[KRYSTAL_TAB_COUNT] = {
        "Spatial 3D", "Bass & Sub", "Clarity & Air", "Warmth & Tone", "Output & ISO"
    };

    if (!is_portrait && card_rect.width >= 620.0f * ui_scale) {
        float tab_w = (tab_bar_rect.width - (float)(KRYSTAL_TAB_COUNT - 1) * 6.0f * ui_scale) / (float)KRYSTAL_TAB_COUNT;
        for (int t = 0; t < KRYSTAL_TAB_COUNT; t++) {
            Rectangle tb = { tab_bar_rect.x + (float)t * (tab_w + 6.0f * ui_scale), tab_bar_y + 2.0f * ui_scale, tab_w, tab_bar_h - 4.0f * ui_scale };
            if (interactive && DrawBtn(tb, tab_titles[t], s_active_tab == (KrystalUiTab)t)) {
                s_active_tab = (KrystalUiTab)t;
                s_tab_scroll_y = 0.0f;
            }
        }
    } else {
        float arrow_w = 34.0f * ui_scale;
        Rectangle prev_btn = { tab_bar_rect.x, tab_bar_y + 2.0f * ui_scale, arrow_w, tab_bar_h - 4.0f * ui_scale };
        Rectangle next_btn = { tab_bar_rect.x + tab_bar_rect.width - arrow_w, tab_bar_y + 2.0f * ui_scale, arrow_w, tab_bar_h - 4.0f * ui_scale };

        if (interactive && DrawBtn(prev_btn, "<", false)) {
            sparkles_krystal_step_tab(-1);
        }
        if (interactive && DrawBtn(next_btn, ">", false)) {
            sparkles_krystal_step_tab(+1);
        }

        float center_x = tab_bar_rect.x + tab_bar_rect.width * 0.5f;
        const char *tab_header_txt = TextFormat("%s  (%d/%d)", tab_titles[(int)s_active_tab], (int)s_active_tab + 1, KRYSTAL_TAB_COUNT);
        int tw_hdr = MeasureText(tab_header_txt, FONT_SIZE_SM);
        DrawText(tab_header_txt, (int)(center_x - tw_hdr * 0.5f), (int)(tab_bar_y + 6.0f * ui_scale), FONT_SIZE_SM, COLOR_ACCENT);

        float dot_y = tab_bar_y + tab_bar_h - 6.0f * ui_scale;
        float dots_total_w = (float)KRYSTAL_TAB_COUNT * 14.0f * ui_scale;
        float dots_start_x = center_x - dots_total_w * 0.5f;

        for (int t = 0; t < KRYSTAL_TAB_COUNT; t++) {
            float dx = dots_start_x + (float)t * 14.0f * ui_scale + 7.0f * ui_scale;
            bool is_cur = (s_active_tab == (KrystalUiTab)t);

            Rectangle dot_hit = { dx - 6.0f * ui_scale, dot_y - 4.0f * ui_scale, 12.0f * ui_scale, 8.0f * ui_scale };
            if (interactive && sparkles_input_consume_tap(dot_hit, NULL)) {
                s_active_tab = (KrystalUiTab)t;
                s_tab_scroll_y = 0.0f;
            }

            if (is_cur) {
                DrawRectangleRounded((Rectangle){ dx - 6.0f * ui_scale, dot_y - 1.5f * ui_scale, 12.0f * ui_scale, 3.0f * ui_scale }, 0.5f, 4, COLOR_ACCENT);
            } else {
                DrawCircle((int)dx, (int)dot_y, 2.0f * ui_scale, (Color){ 45, 48, 58, 255 });
            }
        }
    }

    // Card content dimensions
    float footer_h = 32.0f * ui_scale;
    float content_y = tab_bar_y + tab_bar_h + 4.0f * ui_scale;
    float content_h = card_rect.y + card_rect.height - footer_h - content_y - 4.0f * ui_scale;
    Rectangle content_rect = { card_rect.x + 10.0f * ui_scale, content_y, card_rect.width - 20.0f * ui_scale, content_h };

    // Horizontal swipe detection
    if (interactive && !s_profile_popup_open) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, content_rect)) {
            s_swipe_down_pos = mouse;
            s_swipe_tracking = true;
            s_swipe_time = 0.0f;
        }

        if (s_swipe_tracking && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            s_swipe_time += dt;
        }

        if (s_swipe_tracking && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            s_swipe_tracking = false;
            if (!sparkles_krystal_is_dragging()) {
                float dx = mouse.x - s_swipe_down_pos.x;
                float dy = mouse.y - s_swipe_down_pos.y;
                if (fabsf(dx) > 42.0f * ui_scale && fabsf(dx) > fabsf(dy) * 1.3f && s_swipe_time < 0.60f) {
                    if (dx < 0.0f) {
                        sparkles_krystal_step_tab(+1);
                    } else {
                        sparkles_krystal_step_tab(-1);
                    }
                    sparkles_input_consume();
                }
            }
        }
    }

    bool cfg_changed = false;
    float row_h = (card_rect.width < 500.0f * ui_scale) ? (46.0f * ui_scale) : fmaxf(40.0f, 34.0f * ui_scale);
    float row_gap = 4.0f * ui_scale;

    float total_tab_h = 0.0f;
    switch (s_active_tab) {
        case KRYSTAL_TAB_SPATIAL:  total_tab_h = (is_portrait ? (190.0f * ui_scale) : (160.0f * ui_scale)) + (7.0f * (row_h + row_gap)); break;
        case KRYSTAL_TAB_BASS:     total_tab_h = 7.0f * (row_h + row_gap); break;
        case KRYSTAL_TAB_PRESENCE: total_tab_h = 7.0f * (row_h + row_gap); break;
        case KRYSTAL_TAB_WARMTH:   total_tab_h = 7.0f * (row_h + row_gap); break;
        case KRYSTAL_TAB_ROUTING:  total_tab_h = 8.0f * (row_h + row_gap); break;
        default: break;
    }

    float max_scroll = fmaxf(0.0f, total_tab_h - content_h);
    if (interactive && !sparkles_krystal_is_dragging() && CheckCollisionPointRec(mouse, content_rect)) {
        float scr = sparkles_input_get_scroll_delta(content_rect);
        if (scr != 0.0f) s_tab_scroll_y += scr * 20.0f * ui_scale;
    }
    if (s_tab_scroll_y < 0.0f) s_tab_scroll_y = 0.0f;
    if (s_tab_scroll_y > max_scroll) s_tab_scroll_y = max_scroll;

    BeginScissorMode((int)content_rect.x, (int)content_rect.y, (int)content_rect.width, (int)content_rect.height);

    float rw = content_rect.width - 8.0f * ui_scale;
    float cy = content_rect.y + 4.0f * ui_scale - s_tab_scroll_y;
    float rx = content_rect.x + 4.0f * ui_scale;

    switch (s_active_tab) {
        case KRYSTAL_TAB_SPATIAL: {
            // 2d area and button
            float pad_h = is_portrait ? fminf(165.0f * ui_scale, content_rect.height * 0.36f) : (140.0f * ui_scale);
            Rectangle pad_rect = { rx, cy, rw, pad_h };
            cy += pad_h + 8.0f * ui_scale;

            DrawRectangleRounded(pad_rect, 0.15f, 4, (Color){ 7, 8, 11, 240 });
            DrawRectangleRoundedLinesEx(pad_rect, 0.15f, 4, 1.0f, (Color){ 28, 30, 38, 255 });

            Vector2 pad_center = { pad_rect.x + pad_rect.width * 0.5f, pad_rect.y + pad_rect.height * 0.54f };
            float pad_r = fminf(pad_rect.width * 0.44f, pad_rect.height * 0.40f);
            if (pad_r < 28.0f) pad_r = 28.0f;

            // Distance rings
            const float dist_ratios[] = { 0.30f, 0.60f, 1.0f };
            const char *dist_names[] = { "1.5m", "3.0m", "5.0m" };
            for (int r = 0; r < 3; r++) {
                DrawCircleLines((int)pad_center.x, (int)pad_center.y, pad_r * dist_ratios[r], (r == 2) ? (Color){ 36, 40, 52, 200 } : (Color){ 20, 22, 28, 200 });
                DrawText(dist_names[r], (int)(pad_center.x + pad_r * dist_ratios[r] + 3), (int)pad_center.y - 5, 9, (Color){ 65, 70, 82, 255 });
            }

            DrawLine((int)(pad_center.x - pad_r), (int)pad_center.y, (int)(pad_center.x + pad_r), (int)pad_center.y, (Color){ 22, 24, 30, 255 });
            DrawLine((int)pad_center.x, (int)(pad_center.y - pad_r), (int)pad_center.x, (int)(pad_center.y + pad_r), (Color){ 22, 24, 30, 255 });
            DrawText("F", (int)pad_center.x - 3, (int)(pad_center.y - pad_r - 12), 10, COLOR_TEXT_MUTED);

            // Listener
            DrawCircleV(pad_center, 9.0f * ui_scale, (Color){ 16, 17, 22, 255 });
            DrawCircleLines((int)pad_center.x, (int)pad_center.y, 9.0f * ui_scale, COLOR_ACCENT);
            DrawLine((int)pad_center.x, (int)(pad_center.y - 8.0f * ui_scale), (int)pad_center.x, (int)(pad_center.y - 14.0f * ui_scale), COLOR_ACCENT);

            float az_rad = cfg.spatial.azimuth_deg * ((float)PI / 180.0f);
            float d_norm = (cfg.spatial.distance_m - 0.5f) / 4.5f;
            if (d_norm < 0.05f) d_norm = 0.05f;
            if (d_norm > 1.0f)  d_norm = 1.0f;

            Vector2 orb_pos = {
                pad_center.x + sinf(az_rad) * d_norm * pad_r,
                pad_center.y - cosf(az_rad) * d_norm * pad_r
            };

            float energy = fminf(1.0f, telem.mid_energy * 2.0f + telem.side_energy * 3.0f);
            float orb_r = (7.0f + energy * 4.0f) * ui_scale;

            DrawCircleV(orb_pos, orb_r * 2.2f, ColorAlpha(COLOR_ACCENT, 0.16f + energy * 0.20f));
            DrawCircleV(orb_pos, orb_r, COLOR_ACCENT);
            DrawCircleV(orb_pos, orb_r * 0.45f, WHITE);

            float stage_half_rad = (cfg.spatial.stage_angle_deg * 0.5f) * ((float)PI / 180.0f);
            Vector2 spk_l = { pad_center.x + sinf(az_rad - stage_half_rad) * d_norm * pad_r, pad_center.y - cosf(az_rad - stage_half_rad) * d_norm * pad_r };
            Vector2 spk_r = { pad_center.x + sinf(az_rad + stage_half_rad) * d_norm * pad_r, pad_center.y - cosf(az_rad + stage_half_rad) * d_norm * pad_r };

            DrawLineEx(pad_center, spk_l, 1.0f, (Color){ 80, 140, 255, 120 });
            DrawLineEx(pad_center, spk_r, 1.0f, (Color){ 255, 100, 140, 120 });
            DrawCircleV(spk_l, 3.0f * ui_scale, (Color){ 100, 160, 255, 200 });
            DrawCircleV(spk_r, 3.0f * ui_scale, (Color){ 255, 120, 160, 200 });

            // Open area btn
            float arena_btn_w = 130.0f * ui_scale;
            Rectangle arena_open_btn = { pad_rect.x + pad_rect.width - arena_btn_w - 6.0f * ui_scale, pad_rect.y + 4.0f * ui_scale, arena_btn_w, 22.0f * ui_scale };
            if (interactive && DrawBtn(arena_open_btn, "⤢ 3D Arena View", false)) {
                s_spatial_arena_open = true;
                s_arena_just_opened = true;
                s_dragging_arena_xy = false;
                s_dragging_arena_elev = false;
                s_dragging_arena_wheel = false;
                s_dragging_spatial_orb = false;
                sparkles_input_consume();
            }

            const char *hud_str = TextFormat("AZ: %+.0f°  |  DIST: %.2fm  |  EL: %+.0f°", cfg.spatial.azimuth_deg, cfg.spatial.distance_m, cfg.spatial.elevation_deg);
            DrawText(hud_str, (int)(pad_rect.x + 8.0f * ui_scale), (int)(pad_rect.y + 6.0f * ui_scale), 10, COLOR_TEXT_PRIMARY);

            // Toucn dragging
            if (interactive && !s_spatial_arena_open) {
                float dist_from_center = sqrtf((mouse.x - pad_center.x) * (mouse.x - pad_center.x) + (mouse.y - pad_center.y) * (mouse.y - pad_center.y));
                bool in_radar = (dist_from_center <= pad_r + 10.0f * ui_scale) && !CheckCollisionPointRec(mouse, arena_open_btn);

                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && in_radar && !sparkles_input_is_consumed()) {
                    s_dragging_spatial_orb = true;
                }
                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                    s_dragging_spatial_orb = false;
                }

                if (s_dragging_spatial_orb && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                    sparkles_input_consume();
                    float dx = mouse.x - pad_center.x;
                    float dy = mouse.y - pad_center.y;
                    float r = sqrtf(dx * dx + dy * dy) / pad_r;
                    if (r < 0.05f) r = 0.05f;
                    if (r > 1.0f)  r = 1.0f;

                    cfg.spatial.distance_m = 0.5f + r * 4.5f;
                    cfg.spatial.azimuth_deg = atan2f(dx, -dy) * (180.0f / (float)PI);
                    cfg_changed = true;
                }
            }

            // Sliders
            if (DrawToggle((Rectangle){ rx, cy, rw, row_h }, "Spatial 3D Engine", &cfg.spatial.enabled, interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawSlider((Rectangle){ rx, cy, rw, row_h }, "Stage Width / Spread", &cfg.spatial.stage_angle_deg, 20.0f, 140.0f, "°", interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawSlider((Rectangle){ rx, cy, rw, row_h }, "Sound Height / Elevation", &cfg.spatial.elevation_deg, -45.0f, +90.0f, "°", interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawSlider((Rectangle){ rx, cy, rw, row_h }, "Distance", &cfg.spatial.distance_m, 0.5f, 5.0f, "m", interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawSlider((Rectangle){ rx, cy, rw, row_h }, "Room Ambience", &cfg.spatial.room_refl, 0.0f, 0.80f, "%", interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawSlider((Rectangle){ rx, cy, rw, row_h }, "Vocal Center Clarity", &cfg.spatial.center_gain_db, -6.0f, +6.0f, "dB", interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            break;
        }

        case KRYSTAL_TAB_BASS: {
            if (DrawToggle((Rectangle){ rx, cy, rw, row_h }, "Sub-Bass Synthesis", &cfg.bass.enabled, interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawSlider((Rectangle){ rx, cy, rw, row_h }, "Bass Harmonic Drive", &cfg.bass.intensity, 0.0f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawSlider((Rectangle){ rx, cy, rw, row_h }, "Harmonic Wet Mix", &cfg.bass.mix, 0.0f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawSlider((Rectangle){ rx, cy, rw, row_h }, "Direct Sub Level", &cfg.bass.sub_weight, 0.0f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawSlider((Rectangle){ rx, cy, rw, row_h }, "Sub-Octave Weight", &cfg.bass.sub_octave, 0.0f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawSlider((Rectangle){ rx, cy, rw, row_h }, "Crossover Frequency", &cfg.bass.cutoff_hz, 40.0f, 140.0f, "Hz", interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawSlider((Rectangle){ rx, cy, rw, row_h }, "Infrasonic Rumble Cut", &cfg.bass.rumble_hz, 10.0f, 35.0f, "Hz", interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            break;
        }

        case KRYSTAL_TAB_PRESENCE: {
            if (DrawToggle((Rectangle){ rx, cy, rw, row_h }, "High-Frequency Exciter", &cfg.exciter.enabled, interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawSlider((Rectangle){ rx, cy, rw, row_h }, "Exciter Drive", &cfg.exciter.drive, 0.0f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawSlider((Rectangle){ rx, cy, rw, row_h }, "10.5kHz+ Air Shimmer", &cfg.exciter.shimmer, 0.0f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawToggle((Rectangle){ rx, cy, rw, row_h }, "Transient Punch Processor", &cfg.transient.enabled, interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawSlider((Rectangle){ rx, cy, rw, row_h }, "Attack (Punch)", &cfg.transient.attack, -1.0f, +1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawSlider((Rectangle){ rx, cy, rw, row_h }, "Sustain (Body)", &cfg.transient.sustain, -1.0f, +1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawToggle((Rectangle){ rx, cy, rw, row_h }, "De-Clip Peak Restorer", &cfg.transient.declip_enable, interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            break;
        }

        case KRYSTAL_TAB_WARMTH: {
            if (DrawToggle((Rectangle){ rx, cy, rw, row_h }, "Analog Saturator", &cfg.saturator.enabled, interactive)) cfg_changed = true;
            cy += row_h + row_gap;

            const char *sat_topologies[] = { "OFF", "Triode Tube", "Pentode Valve", "Tape Machine", "Transformer" };
            DrawText("Analog Topology", (int)(rx + 8.0f * ui_scale), (int)(cy + (row_h - FONT_SIZE_SM) * 0.5f), FONT_SIZE_SM, COLOR_TEXT_MUTED);

            float top_btn_w = 140.0f * ui_scale;
            Rectangle top_btn = { rx + rw - top_btn_w - 6.0f * ui_scale, cy + (row_h - 26.0f * ui_scale) * 0.5f, top_btn_w, 26.0f * ui_scale };
            if (interactive && DrawBtn(top_btn, sat_topologies[(int)cfg.saturator.mode], false)) {
                cfg.saturator.mode = (KrystalSatMode)(((int)cfg.saturator.mode + 1) % SAT_MODE_COUNT);
                cfg_changed = true;
            }
            cy += row_h + row_gap;

            if (DrawSlider((Rectangle){ rx, cy, rw, row_h }, "Warmth Drive", &cfg.saturator.drive, 0.0f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawToggle((Rectangle){ rx, cy, rw, row_h }, "Dynamic De-Harsh Tamer", &cfg.spectral.enabled, interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawSlider((Rectangle){ rx, cy, rw, row_h }, "De-Harsh (3.2kHz)", &cfg.spectral.de_harsh, 0.0f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawSlider((Rectangle){ rx, cy, rw, row_h }, "De-Boom (160Hz)", &cfg.spectral.de_boom, 0.0f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawToggle((Rectangle){ rx, cy, rw, row_h }, "2x Anti-Alias Oversample", &cfg.saturator.oversample, interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            break;
        }

        case KRYSTAL_TAB_ROUTING: {
            if (DrawToggle((Rectangle){ rx, cy, rw, row_h }, "ISO 226 Equal-Loudness", &cfg.loudness.enabled, interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawSlider((Rectangle){ rx, cy, rw, row_h }, "Loudness Compensation", &cfg.loudness.intensity, 0.0f, 1.0f, "%", interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawSlider((Rectangle){ rx, cy, rw, row_h }, "Spectral Tilt", &cfg.spectral.tilt_db, -6.0f, +6.0f, "dB", interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawSlider((Rectangle){ rx, cy, rw, row_h }, "Master Pre-Gain", &cfg.general.pre_gain_db, -12.0f, +12.0f, "dB", interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawSlider((Rectangle){ rx, cy, rw, row_h }, "Stereo Balance", &cfg.general.balance, -1.0f, +1.0f, "", interactive)) cfg_changed = true;
            cy += row_h + row_gap;

            const char *chan_modes[] = { "Stereo", "Swap L/R", "Mono Sum", "Side Only", "Left Solo", "Right Solo" };
            DrawText("Channel Routing", (int)(rx + 8.0f * ui_scale), (int)(cy + (row_h - FONT_SIZE_SM) * 0.5f), FONT_SIZE_SM, COLOR_TEXT_MUTED);

            float ch_btn_w = 130.0f * ui_scale;
            Rectangle ch_btn = { rx + rw - ch_btn_w - 6.0f * ui_scale, cy + (row_h - 26.0f * ui_scale) * 0.5f, ch_btn_w, 26.0f * ui_scale };
            if (interactive && DrawBtn(ch_btn, chan_modes[(int)cfg.general.mode], false)) {
                cfg.general.mode = (KrystalChannelMode)(((int)cfg.general.mode + 1) % CHAN_MODE_COUNT);
                cfg_changed = true;
            }
            cy += row_h + row_gap;

            if (DrawToggle((Rectangle){ rx, cy, rw, row_h }, "Auto Level Matching", &cfg.general.auto_gain, interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            if (DrawSlider((Rectangle){ rx, cy, rw, row_h }, "Safety Headroom", &cfg.general.headroom_db, -3.0f, 0.0f, "dB", interactive)) cfg_changed = true;
            cy += row_h + row_gap;
            break;
        }

        default: break;
    }

    if (cfg_changed) {
        krystal_set_config(&cfg);
    }

    EndScissorMode();

    // Footer bar
    float foot_y = card_rect.y + card_rect.height - footer_h;
    DrawRectangle((int)card_rect.x, (int)foot_y, (int)card_rect.width, (int)footer_h, (Color){ 10, 11, 15, 230 });
    DrawLine((int)card_rect.x, (int)foot_y, (int)(card_rect.x + card_rect.width), (int)(foot_y), (Color){ 28, 30, 40, 200 });

    float ty = foot_y + (footer_h - FONT_SIZE_XS) * 0.5f;

    // Left anchor
    float tx = card_rect.x + 10.0f * ui_scale;
    DrawText("PHASE", (int)tx, (int)ty, FONT_SIZE_XS, COLOR_TEXT_MUTED);
    tx += 40.0f * ui_scale;

    float ph_w = is_portrait ? (50.0f * ui_scale) : (80.0f * ui_scale);
    Rectangle ph_rect = { tx, ty + 2.5f * ui_scale, ph_w, 5.0f * ui_scale };
    DrawRectangleRec(ph_rect, (Color){ 20, 22, 28, 255 });

    float ph_norm = (telem.phase_correlation + 1.0f) * 0.5f;
    if (ph_norm < 0.0f) ph_norm = 0.0f;
    if (ph_norm > 1.0f) ph_norm = 1.0f;

    Color ph_col = (telem.phase_correlation > 0.30f) ? (Color){ 100, 220, 140, 255 } :
                   ((telem.phase_correlation >= 0.0f) ? COLOR_ACCENT : (Color){ 230, 90, 90, 255 });

    DrawRectangle((int)(ph_rect.x + ph_rect.width * 0.5f) - 1, (int)ph_rect.y - 1, 2, (int)ph_rect.height + 2, (Color){ 45, 48, 58, 255 });
    DrawCircle((int)(ph_rect.x + ph_rect.width * ph_norm), (int)(ph_rect.y + ph_rect.height * 0.5f), 3.5f * ui_scale, ph_col);
    tx += ph_w + 8.0f * ui_scale;

    DrawText(TextFormat("%+.2f", telem.phase_correlation), (int)tx, (int)ty, FONT_SIZE_XS, ph_col);

    // Right anchor
    float right_anchor = card_rect.x + card_rect.width - 10.0f * ui_scale;

    const char *pk_str = TextFormat("PK: %+.1fdB", telem.peak_dbfs);
    int pk_w = MeasureText(pk_str, FONT_SIZE_XS);
    right_anchor -= pk_w;
    DrawText(pk_str, (int)right_anchor, (int)ty, FONT_SIZE_XS, COLOR_TEXT_PRIMARY);

    right_anchor -= 14.0f * ui_scale;
    const char *lufs_str = TextFormat("LUFS: %.1f", telem.lufs_momentary);
    int lufs_w = MeasureText(lufs_str, FONT_SIZE_XS);
    right_anchor -= lufs_w;
    DrawText(lufs_str, (int)right_anchor, (int)ty, FONT_SIZE_XS, COLOR_TEXT_PRIMARY);

    if (!is_portrait && card_rect.width >= 620.0f * ui_scale) {
        right_anchor -= 14.0f * ui_scale;
        const char *trim_str = TextFormat("TRIM: %+.1fdB", telem.auto_trim_db);
        int trim_w = MeasureText(trim_str, FONT_SIZE_XS);
        right_anchor -= trim_w;
        DrawText(trim_str, (int)right_anchor, (int)ty, FONT_SIZE_XS, COLOR_TEXT_MUTED);

        right_anchor -= 14.0f * ui_scale;
        const char *crest_str = TextFormat("CREST: %.1fdB", telem.crest_factor_db);
        int crest_w = MeasureText(crest_str, FONT_SIZE_XS);
        right_anchor -= crest_w;
        DrawText(crest_str, (int)right_anchor, (int)ty, FONT_SIZE_XS, COLOR_TEXT_MUTED);
    }

    // Profiles dropdown popup
    if (s_profile_popup_open) {
        float pop_w = fminf(300.0f * ui_scale, card_rect.width - 24.0f * ui_scale);
        float pop_h = fminf(320.0f * ui_scale, card_rect.height - top_bar_h - 20.0f * ui_scale);
        Rectangle pop_box = { prof_btn.x, prof_btn.y + prof_btn.height + 4.0f * ui_scale, pop_w, pop_h };

        if (sparkles_input_consume_tap_outside(pop_box, NULL)) {
            s_profile_popup_open = false;
        }

        DrawRectangleRec(pop_box, (Color){ 12, 13, 17, 252 });
        DrawRectangleLinesEx(pop_box, 1.2f, COLOR_ACCENT);
        DrawNothingCornerBrackets(pop_box, 8.0f * ui_scale, COLOR_ACCENT);

        int p_cnt = krystal_get_profile_count();
        int c_cnt = krystal_presets_get_count();
        int total_opts = 1 + p_cnt + c_cnt;

        float item_h = 28.0f * ui_scale;
        float max_scr = fmaxf(0.0f, (float)total_opts * item_h - (pop_h - 10.0f * ui_scale));

        if (CheckCollisionPointRec(mouse, pop_box)) {
            float scroll = sparkles_input_get_scroll_delta(pop_box);
            if (scroll != 0.0f) s_profile_popup_scroll += scroll * 16.0f * ui_scale;
        }
        if (s_profile_popup_scroll < 0.0f) s_profile_popup_scroll = 0.0f;
        if (s_profile_popup_scroll > max_scr) s_profile_popup_scroll = max_scr;

        BeginScissorMode((int)pop_box.x + 1, (int)pop_box.y + 1, (int)pop_box.width - 2, (int)pop_box.height - 2);

        for (int opt = 0; opt < total_opts; opt++) {
            float iy = pop_box.y + 6.0f * ui_scale + (float)opt * item_h - s_profile_popup_scroll;
            if (iy + item_h < pop_box.y || iy > pop_box.y + pop_box.height) continue;

            Rectangle item_r = { pop_box.x + 4.0f * ui_scale, iy, pop_box.width - 8.0f * ui_scale, item_h };
            bool hover = CheckCollisionPointRec(mouse, item_r);

            if (opt == 0) {
                if (hover) DrawRectangleRec(item_r, ColorAlpha(COLOR_ACCENT, 0.20f));
                DrawText("+ Save As New Preset...", (int)(item_r.x + 8.0f * ui_scale), (int)(item_r.y + 6.0f * ui_scale), FONT_SIZE_SM, COLOR_ACCENT);

                if (hover && !sparkles_input_is_consumed() && sparkles_input_consume_tap(item_r, NULL)) {
                    s_profile_popup_open = false;
                    sparkles_text_prompt_open(&(SparklesTextPromptConfig){
                        .tag = "KPRESET",
                        .prompt = "Save Krystal Preset As",
                        .submit_label = "Save",
                        .max_len = 48,
                        .on_submit = on_krystal_preset_save_submit
                    });
                    break;
                }
            } else {
                int p_idx = opt - 1;
                bool is_custom = (p_idx >= p_cnt);
                const char *p_name = is_custom ? krystal_presets_get_name(p_idx - p_cnt) : krystal_get_profile_name(p_idx);
                bool is_sel = (strcmp(active_name, p_name) == 0);

                if (is_sel) {
                    DrawRectangleRec(item_r, (Color){ 24, 28, 38, 255 });
                    DrawRectangle((int)item_r.x, (int)item_r.y, 3, (int)item_r.height, COLOR_ACCENT);
                } else if (hover) {
                    DrawRectangleRec(item_r, (Color){ 18, 20, 26, 255 });
                }

                Color text_col = is_sel ? COLOR_ACCENT : (is_custom ? (Color){ 160, 210, 255, 255 } : (hover ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED));
                DrawText(p_name, (int)(item_r.x + 8.0f * ui_scale), (int)(item_r.y + 6.0f * ui_scale), FONT_SIZE_SM, text_col);

                if (hover && !sparkles_input_is_consumed() && sparkles_input_consume_tap(item_r, NULL)) {
                    if (!is_custom) {
                        krystal_apply_profile(p_idx);
                    } else {
                        KrystalConfig c;
                        if (krystal_presets_get_config(p_idx - p_cnt, &c)) {
                            krystal_set_config(&c);
                            krystal_set_active_preset_name(p_name);
                        }
                    }
                    s_profile_popup_open = false;
                    break;
                }
            }
        }

        EndScissorMode();
        sparkles_input_block_area(pop_box);
    }

    // Tap outside card closes Krystal
    if (interactive && !sparkles_krystal_is_dragging() && !s_profile_popup_open && !s_spatial_arena_open) {
        Vector2 tap;
        if (sparkles_input_consume_tap((Rectangle){ 0, 0, screen_w, screen_h }, &tap)) {
            if (!CheckCollisionPointRec(tap, card_rect)) {
                sparkles_krystal_toggle();
            }
        }
    }
    sparkles_input_block_area(card_rect);

    rlPopMatrix();
}