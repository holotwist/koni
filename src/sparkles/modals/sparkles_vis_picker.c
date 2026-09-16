#define _DEFAULT_SOURCE
#include "sparkles_vis_picker.h"
#include "sparkles_theme.h"
#include "visualizers/sparkles_vis.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>

static bool s_open = false;
static float s_anim_progress = 0.0f;
static int s_selected_idx = 0;

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
    [VIS_TAPE_REELS]      = { "Mechanical dual tape" },
    [VIS_WARP_TUNNEL]     = { "Hexagonal warp tunnel" },
    [VIS_SPARKLES]        = { "Diffraction flares and transient starbursts" },
    [VIS_VOYAGER]         = { "3D network voyage with trailing camera" },
    [VIS_GALVANOMETER]    = { "Dual analog mechanical VU needles" },
    [VIS_SEISMOGRAPH]     = { "Continuous strip-chart info" },
    [VIS_GIMBAL]          = { "3-axis gyroscope" },
    [VIS_DANCER]          = { "Koni dancing!" },
};

void sparkles_vis_picker_init(void) {
    s_open = false;
    s_anim_progress = 0.0f;
    s_selected_idx = (int)sparkles_vis_get_mode();
}

void sparkles_vis_picker_open(void) {
    s_open = true;
    s_selected_idx = (int)sparkles_vis_get_mode();
}

void sparkles_vis_picker_close(void) {
    s_open = false;
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
        s_selected_idx = (s_selected_idx >= 2) ? (s_selected_idx - 2) : s_selected_idx;
        return true;
    }
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_J)) {
        s_selected_idx = (s_selected_idx + 2 < VIS_MODE_COUNT) ? (s_selected_idx + 2) : s_selected_idx;
        return true;
    }
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressed(KEY_H)) {
        if (s_selected_idx % 2 == 1) s_selected_idx--;
        return true;
    }
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressed(KEY_L)) {
        if (s_selected_idx % 2 == 0 && s_selected_idx + 1 < VIS_MODE_COUNT) s_selected_idx++;
        return true;
    }

    if (IsKeyPressed(KEY_ENTER) && s_selected_idx >= 0 && s_selected_idx < VIS_MODE_COUNT) {
        sparkles_vis_set_mode((SparklesVisMode)s_selected_idx);
        sparkles_vis_picker_close();
        return true;
    }

    // Dismiss if user clicks outside the modal box
    float box_w = fminf(760.0f, screen_w - 60.0f);
    float box_h = fminf(480.0f, screen_h - 80.0f);
    Rectangle box = { (screen_w - box_w) * 0.5f, (screen_h - box_h) * 0.5f, box_w, box_h };

    Vector2 mouse = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(mouse, box)) {
        sparkles_vis_picker_close();
        return true;
    }

    return true;
}

void sparkles_vis_picker_render(float screen_w, float screen_h) {
    if (s_anim_progress <= 0.001f) return;

    float inv = 1.0f - s_anim_progress;
    float ease = 1.0f - (inv * inv * inv);

    rlPushMatrix();
    rlTranslatef(screen_w * 0.5f, screen_h * 0.5f - 40.0f * (inv * inv), 0.0f);
    rlScalef(1.0f + 0.15f * inv, 1.0f + 0.15f * inv, 1.0f);
    rlTranslatef(-screen_w * 0.5f, -screen_h * 0.5f, 0.0f);

    // Dim backdrop
    DrawRectangle(0, 0, (int)screen_w, (int)screen_h, ColorAlpha((Color){ 3, 3, 5, 255 }, 0.70f * ease));

    float box_w = fminf(760.0f, screen_w - 60.0f);
    float box_h = fminf(580.0f, screen_h - 40.0f);
    Rectangle box = { (screen_w - box_w) * 0.5f, (screen_h - box_h) * 0.5f, box_w, box_h };

    Vector2 mouse = GetMousePosition();
    bool interactive = (s_anim_progress >= 0.99f);

    DrawRectangleRec(box, (Color){ 8, 9, 12, 250 });
    DrawRectangleLinesEx(box, 1.0f, (Color){ 32, 35, 45, 255 });
    DrawNothingCornerBrackets(box, 10.0f, COLOR_ACCENT);

    // Header
    float header_h = 44.0f;
    DrawRectangle((int)box.x, (int)box.y, (int)box.width, (int)header_h, (Color){ 12, 13, 17, 255 });
    DrawLine((int)box.x, (int)(box.y + header_h), (int)(box.x + box.width), (int)(box.y + header_h), (Color){ 28, 30, 38, 255 });

    DrawText(TextFormat("VISUALIZER SELECTOR  ·  %d Visualizers", VIS_MODE_COUNT),
             (int)box.x + 18, (int)box.y + 14, FONT_SIZE_MD, COLOR_TEXT_PRIMARY);

    Rectangle btn_close = { box.x + box.width - 80, box.y + 10, 64, 24 };
    bool hover_close = interactive && CheckCollisionPointRec(mouse, btn_close);
    DrawRectangleRec(btn_close, hover_close ? ColorAlpha(COLOR_ACCENT, 0.20f) : (Color){ 18, 20, 26, 255 });
    DrawRectangleLinesEx(btn_close, 1.0f, hover_close ? COLOR_ACCENT : (Color){ 36, 40, 50, 255 });
    DrawText("✕ Esc", (int)btn_close.x + 10, (int)btn_close.y + 5, FONT_SIZE_SM, hover_close ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED);

    if (interactive && hover_close && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        sparkles_vis_picker_close();
    }

    // 2-Column Grid Area
    float list_y = box.y + header_h + 12.0f;
    float footer_h = 32.0f;
    float avail_content_h = box.height - header_h - footer_h - 20.0f;

    const int cols = 2;
    const int rows = (VIS_MODE_COUNT + cols - 1) / cols;
    float gap_x = 12.0f;
    float gap_y = 8.0f;
    float card_w = (box.width - 28.0f - gap_x) / (float)cols;
    float card_h = (avail_content_h - (gap_y * (float)(rows - 1))) / (float)rows;

    SparklesVisMode cur_active = sparkles_vis_get_mode();

    for (int i = 0; i < VIS_MODE_COUNT; i++) {
        int r = i / cols;
        int c = i % cols;
        float cx = box.x + 14.0f + (float)c * (card_w + gap_x);
        float cy = list_y + (float)r * (card_h + gap_y);
        Rectangle card = { cx, cy, card_w, card_h };

        bool is_active = (cur_active == (SparklesVisMode)i);
        bool is_selected = (s_selected_idx == i);
        bool hover = interactive && CheckCollisionPointRec(mouse, card);

        if (hover) s_selected_idx = i;

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

        // Visualizer name
        Color title_col = is_active ? COLOR_ACCENT : (hover || is_selected ? COLOR_TEXT_PRIMARY : ColorAlpha(COLOR_TEXT_PRIMARY, 0.85f));
        DrawText(name, (int)card.x + 14, (int)card.y + 6, FONT_SIZE_SM, title_col);

        if (is_active) {
            int bw = MeasureText("ACTIVE", FONT_SIZE_XS);
            DrawText("ACTIVE", (int)(card.x + card.width - bw - 10), (int)card.y + 6, FONT_SIZE_XS, COLOR_ACCENT);
        }

        // Subtitle description
        DrawText(tagline, (int)card.x + 10, (int)card.y + (int)card.height - FONT_SIZE_XS - 6, FONT_SIZE_XS,
                 ColorAlpha(COLOR_TEXT_MUTED, 0.75f));

        if (interactive && hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            sparkles_vis_set_mode((SparklesVisMode)i);
            sparkles_vis_picker_close();
            break;
        }
    }

    // Footer Help Tip
    DrawText("Arrow keys / Mouse: Navigate  |  Enter / Click: Select Visualizer  |  Shift+C: Toggle Picker",
             (int)box.x + 18, (int)(box.y + box.height - 24), FONT_SIZE_XS, COLOR_TEXT_DARK);

    rlPopMatrix();
}