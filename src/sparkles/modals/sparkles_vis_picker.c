#define _DEFAULT_SOURCE
#include "sparkles_vis_picker.h"
#include "sparkles_theme.h"
#include "visualizers/sparkles_vis.h"
#include "input/sparkles_input.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static bool s_open = false;
static float s_anim_progress = 0.0f;
static int s_selected_idx = 0;
static float s_picker_scroll = 0.0f;

// Touch drag tracking
static Vector2 s_touch_start_pos = {0};
static bool s_touch_active = false;
static bool s_dragged_significantly = false;

static const struct {
    const char *tagline;
} s_vis_desc[VIS_MODE_COUNT] = {
    [VIS_ORB_FLUID]       = { "Clean organic reactive orb" },
    [VIS_WAVES_FLOW]      = { "Flowing chromatic multi-band wave layers" },
    [VIS_RADIAL_SPECTRUM] = { "Circular frequency equalizer" },
    [VIS_VECTOR_SCOPE]    = { "Stereo Lissajous phase oscilloscope" },
    [VIS_PARTICLE_VORTEX] = { "Audio-reactive particle vortex" },
    [VIS_CYMATICS]        = { "Chladni plate acoustic resonance mandala" },
    [VIS_TERRAIN]         = { "Joy Division wireframe horizon landscape" },
    [VIS_VECTOR_POLY]     = { "3D Vectrex audio-reactive polyhedron" },
    [VIS_TAPE_REELS]      = { "Mechanical dual tape reels" },
    [VIS_WARP_TUNNEL]     = { "Hexagonal warp tunnel" },
    [VIS_SPARKLES]        = { "Diffraction flares and transient starbursts" },
    [VIS_VOYAGER]         = { "3D network voyage with trailing camera" },
    [VIS_GALVANOMETER]    = { "Dual analog mechanical VU needles" },
    [VIS_SEISMOGRAPH]     = { "Continuous strip-chart" },
    [VIS_GIMBAL]          = { "3-axis gyroscope" },
    [VIS_DANCER]          = { "Koni dancing!" },
};

void sparkles_vis_picker_init(void) {
    s_open = false;
    s_anim_progress = 0.0f;
    s_selected_idx = (int)sparkles_vis_get_mode();
    s_picker_scroll = 0.0f;
}

void sparkles_vis_picker_open(void) {
    s_open = true;
    s_selected_idx = (int)sparkles_vis_get_mode();
    s_picker_scroll = 0.0f;
    s_touch_active = false;
    s_dragged_significantly = false;
}

void sparkles_vis_picker_close(void) {
    s_open = false;
    s_touch_active = false;
}

void sparkles_vis_picker_toggle(void) {
    if (s_open) sparkles_vis_picker_close();
    else sparkles_vis_picker_open();
}

bool sparkles_vis_picker_is_open(void) { return s_open; }
bool sparkles_vis_picker_is_visible(void) { return (s_open || s_anim_progress > 0.001f); }
float sparkles_vis_picker_get_anim_progress(void) { return s_anim_progress; }

void sparkles_vis_picker_update(float screen_w, float screen_h) {
    (void)screen_w; (void)screen_h;
    float dt = GetFrameTime();
    float speed = 5.4f;

    if (s_open) {
        s_anim_progress += dt * speed;
        if (s_anim_progress > 1.0f) s_anim_progress = 1.0f;
    } else {
        s_anim_progress -= dt * speed;
        if (s_anim_progress < 0.0f) s_anim_progress = 0.0f;
    }
}

bool sparkles_vis_picker_handle_input(float screen_w, float screen_h) {
    if (!s_open || s_anim_progress < 0.95f) return false;

    if (IsKeyPressed(KEY_ESCAPE)) {
        sparkles_vis_picker_close();
        return true;
    }

    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_K)) {
        if (s_selected_idx > 0) s_selected_idx--;
        return true;
    }
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_J)) {
        if (s_selected_idx < VIS_MODE_COUNT - 1) s_selected_idx++;
        return true;
    }
    if (IsKeyPressed(KEY_ENTER)) {
        sparkles_vis_set_mode((SparklesVisMode)s_selected_idx);
        sparkles_vis_picker_close();
        return true;
    }

    float ui_scale = sparkles_get_ui_scale();
    bool is_mobile = (screen_h > screen_w);
    float box_w = is_mobile ? (screen_w - 24.0f * ui_scale) : fminf(760.0f * ui_scale, screen_w - 40.0f);
    float box_h = is_mobile ? (screen_h - 80.0f * ui_scale) : fminf(600.0f * ui_scale, screen_h - 40.0f);
    Rectangle box = { (screen_w - box_w) * 0.5f, (screen_h - box_h) * 0.5f, box_w, box_h };

    if (sparkles_input_consume_tap_outside(box, NULL)) {
        sparkles_vis_picker_close();
        return true;
    }

    return true;
}

void sparkles_vis_picker_render(float screen_w, float screen_h) {
    if (s_anim_progress <= 0.001f) return;

    float ui_scale = sparkles_get_ui_scale();
    bool is_mobile = (screen_h > screen_w);

    float inv = 1.0f - s_anim_progress;
    float ease = 1.0f - (inv * inv * inv);

    rlPushMatrix();
    rlTranslatef(screen_w * 0.5f, screen_h * 0.5f - 40.0f * (inv * inv), 0.0f);
    rlScalef(1.0f + 0.15f * inv, 1.0f + 0.15f * inv, 1.0f);
    rlTranslatef(-screen_w * 0.5f, -screen_h * 0.5f, 0.0f);

    DrawRectangle(0, 0, (int)screen_w, (int)screen_h, ColorAlpha((Color){ 3, 3, 5, 255 }, 0.75f * ease));

    float box_w = is_mobile ? (screen_w - 24.0f * ui_scale) : fminf(760.0f * ui_scale, screen_w - 40.0f);
    float box_h = is_mobile ? (screen_h - 80.0f * ui_scale) : fminf(600.0f * ui_scale, screen_h - 40.0f);
    Rectangle box = { (screen_w - box_w) * 0.5f, (screen_h - box_h) * 0.5f, box_w, box_h };

    Vector2 mouse = GetMousePosition();
    bool interactive = (s_anim_progress >= 0.99f);

    DrawRectangleRec(box, (Color){ 8, 9, 12, 250 });
    DrawRectangleLinesEx(box, 1.5f, (Color){ 32, 35, 45, 255 });
    DrawNothingCornerBrackets(box, 10.0f * ui_scale, COLOR_ACCENT);

    // Header
    float header_h = 48.0f * ui_scale;
    DrawRectangle((int)box.x, (int)box.y, (int)box.width, (int)header_h, (Color){ 12, 13, 17, 255 });
    DrawLine((int)box.x, (int)(box.y + header_h), (int)(box.x + box.width), (int)(box.y + header_h), (Color){ 28, 30, 38, 255 });

    float header_text_y = box.y + (header_h - (float)FONT_SIZE_MD) * 0.5f;
    DrawText("VISUALIZER SELECTOR", (int)(box.x + 16 * ui_scale), (int)header_text_y, FONT_SIZE_MD, COLOR_TEXT_PRIMARY);

    float btn_close_w = 64.0f * ui_scale;
    float btn_close_h = 28.0f * ui_scale;
    Rectangle btn_close = { box.x + box.width - btn_close_w - 12.0f * ui_scale, box.y + (header_h - btn_close_h) * 0.5f, btn_close_w, btn_close_h };
    bool hover_close = interactive && CheckCollisionPointRec(mouse, btn_close);
    DrawRectangleRec(btn_close, hover_close ? ColorAlpha(COLOR_ACCENT, 0.20f) : (Color){ 18, 20, 26, 255 });
    DrawRectangleLinesEx(btn_close, 1.0f, hover_close ? COLOR_ACCENT : (Color){ 36, 40, 50, 255 });
    int tw_esc = MeasureText("✕ Esc", FONT_SIZE_SM);
    DrawText("X Esc", (int)(btn_close.x + (btn_close_w - tw_esc) / 2), (int)(btn_close.y + (btn_close_h - FONT_SIZE_SM) / 2), FONT_SIZE_SM, hover_close ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED);

    if (interactive && sparkles_input_consume_tap(btn_close, NULL)) {
        sparkles_vis_picker_close();
    }

    // Scrollable cards area
    float list_y = box.y + header_h + 8.0f * ui_scale;
    float footer_h = 30.0f * ui_scale;
    float list_h = box.height - header_h - footer_h - 16.0f * ui_scale;

    const int cols = is_mobile ? 1 : 2;
    float gap_x = 10.0f * ui_scale;
    float gap_y = 8.0f * ui_scale;
    float card_w = is_mobile ? (box.width - 24.0f * ui_scale) : ((box.width - 24.0f * ui_scale - gap_x) / 2.0f);
    float card_h = (float)FONT_SIZE_SM + (float)FONT_SIZE_XS + (22.0f * ui_scale);

    // Track drag or tap gesture
    if (interactive && CheckCollisionPointRec(mouse, box)) {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            s_touch_start_pos = mouse;
            s_touch_active = true;
            s_dragged_significantly = false;
        }

        if (s_touch_active && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            float dy = mouse.y - s_touch_start_pos.y;
            float dx = mouse.x - s_touch_start_pos.x;
            if (sqrtf(dx * dx + dy * dy) > 10.0f * ui_scale) {
                s_dragged_significantly = true;
            }
            Vector2 delta = GetMouseDelta();
            s_picker_scroll -= delta.y;
        }

        s_picker_scroll -= GetMouseWheelMove() * 32.0f;
    }

    int total_rows = (VIS_MODE_COUNT + cols - 1) / cols;
    float total_content_h = (float)total_rows * (card_h + gap_y);
    float max_scroll = fmaxf(0.0f, total_content_h - list_h);
    if (s_picker_scroll < 0.0f) s_picker_scroll = 0.0f;
    if (s_picker_scroll > max_scroll) s_picker_scroll = max_scroll;

    BeginScissorMode((int)box.x, (int)list_y, (int)box.width, (int)list_h);

    SparklesVisMode cur_active = sparkles_vis_get_mode();
    int clicked_idx = -1;

    for (int i = 0; i < VIS_MODE_COUNT; i++) {
        int r = i / cols;
        int c = i % cols;
        float cx = box.x + 12.0f * ui_scale + (float)c * (card_w + gap_x);
        float cy = list_y + (float)r * (card_h + gap_y) - s_picker_scroll;
        Rectangle card = { cx, cy, card_w, card_h };

        if (cy + card_h < list_y - 10.0f || cy > list_y + list_h + 10.0f) continue;

        bool is_active = (cur_active == (SparklesVisMode)i);
        bool is_selected = (s_selected_idx == i);
        bool hover = interactive && CheckCollisionPointRec(mouse, card);

        Color bg_col = is_active ? (Color){ 18, 20, 28, 255 }
                                 : (hover || is_selected ? (Color){ 14, 15, 21, 255 } : (Color){ 10, 11, 14, 255 });
        DrawRectangleRec(card, bg_col);

        Color border_col = is_active ? COLOR_ACCENT : (hover || is_selected ? (Color){ 60, 65, 80, 255 } : (Color){ 24, 26, 34, 255 });
        DrawRectangleLinesEx(card, 1.0f, border_col);

        if (is_active) {
            DrawRectangle((int)card.x, (int)card.y, 3, (int)card.height, COLOR_ACCENT);
        }

        const char *name = sparkles_vis_get_name((SparklesVisMode)i);
        const char *tagline = s_vis_desc[i].tagline;

        // Vertical title and desc
        float title_y = card.y + 6.0f * ui_scale;
        float desc_y = title_y + (float)FONT_SIZE_SM + (3.0f * ui_scale);

        Color title_col = is_active ? COLOR_ACCENT : (hover || is_selected ? COLOR_TEXT_PRIMARY : ColorAlpha(COLOR_TEXT_PRIMARY, 0.85f));
        DrawText(name, (int)(card.x + 12 * ui_scale), (int)title_y, FONT_SIZE_SM, title_col);

        if (is_active) {
            int bw = MeasureText("ACTIVE", FONT_SIZE_XS);
            DrawText("ACTIVE", (int)(card.x + card.width - bw - 10 * ui_scale), (int)title_y, FONT_SIZE_XS, COLOR_ACCENT);
        }

        DrawText(tagline, (int)(card.x + 12 * ui_scale), (int)desc_y, FONT_SIZE_XS, ColorAlpha(COLOR_TEXT_MUTED, 0.75f));

        if (interactive && sparkles_input_consume_tap(card, NULL)) {
            clicked_idx = i;
        }
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        s_touch_active = false;
        s_dragged_significantly = false;
    }

    if (clicked_idx >= 0) {
        sparkles_vis_set_mode((SparklesVisMode)clicked_idx);
        sparkles_vis_picker_close();
    }

    EndScissorMode();

    // Footer
    float footer_y = box.y + box.height - footer_h + 4.0f;
    DrawText("Swipe to scroll  |  Tap to select", (int)(box.x + 16 * ui_scale), (int)footer_y, FONT_SIZE_XS, COLOR_TEXT_DARK);

    rlPopMatrix();
}