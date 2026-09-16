#include "sparkles_widgets.h"
#include "state.h"
#include <math.h>

void tile_waveform_render(SparklesTile *tile, Rectangle b) {
    (void)tile;
    uint32_t cur = atomic_load(&p_current_sec);
    uint32_t tot = atomic_load(&p_total_sec);
    float progress = tot > 0 ? (float)cur / (float)tot : 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    float wave_y = b.y + b.height * 0.45f;
    int bars = (int)(b.width - 36) / 5;
    if (bars < 10) bars = 10;
    float step = (b.width - 36) / (float)bars;

    for (int i = 0; i < bars; i++) {
        float x = b.x + 18 + i * step;
        float norm_i = (float)i / bars;

        float h = sinf(i * 0.28f) * 18.0f + cosf(i * 0.65f) * 10.0f + 20.0f;
        Color bar_color = (norm_i <= progress) ? COLOR_ACCENT : COLOR_TEXT_DARK;

        DrawLineEx((Vector2){ x, wave_y - h * 0.5f }, (Vector2){ x, wave_y + h * 0.5f }, 2.5f, bar_color);
    }

    float scrub_x = b.x + 18 + progress * (b.width - 36);
    DrawLineEx((Vector2){ scrub_x, wave_y - 24 }, (Vector2){ scrub_x, wave_y + 24 }, 2.0f, COLOR_TEXT_PRIMARY);

    DrawText("HQ", b.x + b.width - 36, b.y + b.height - 22, FONT_SIZE_SM, COLOR_TEXT_MUTED);
}

void widget_waveform_input(SparklesTile *tile, Rectangle b) {
    (void)tile;
    Vector2 m = GetMousePosition();
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, b)) {
        float pct = (m.x - (b.x + 18)) / (b.width - 36);
        if (pct < 0.0f) pct = 0.0f;
        if (pct > 1.0f) pct = 1.0f;
        uint32_t tot = atomic_load(&p_total_sec);
        if (tot > 0) {
            atomic_store(&seek_target_ms, (int)(pct * tot * 1000.0f));
            atomic_store(&current_cmd_atomic, CMD_SEEK);
        }
    }
}

void tile_waveform_input(SparklesTile *tile, Rectangle b) {
    widget_waveform_input(tile, b);
}