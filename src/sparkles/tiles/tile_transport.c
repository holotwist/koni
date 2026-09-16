#include "sparkles_widgets.h"
#include "state.h"

void tile_transport_render(SparklesTile *tile, Rectangle b) {
    (void)tile;
    PlayState st = (PlayState)atomic_load(&play_state_atomic);
    bool is_playing = (st == STATE_PLAYING);

    Vector2 c = { b.x + b.width / 2.0f, b.y + b.height / 2.0f };

    // Previous text glyph (|<<)
    DrawText("|<<", (int)c.x - 75, (int)c.y - 10, FONT_SIZE_LG, COLOR_TEXT_MUTED);

    // Play/Pause glyph
    if (is_playing) {
        DrawRectangle((int)c.x - 8, (int)c.y - 10, 5, 20, COLOR_ACCENT);
        DrawRectangle((int)c.x + 3, (int)c.y - 10, 5, 20, COLOR_ACCENT);
    } else {
        DrawTriangle(
            (Vector2){ c.x - 7, c.y - 10 },
            (Vector2){ c.x - 7, c.y + 10 },
            (Vector2){ c.x + 10, c.y },
            COLOR_ACCENT
        );
    }

    // Next text glyph (>>|)
    DrawText(">>|", (int)c.x + 50, (int)c.y - 10, FONT_SIZE_LG, COLOR_TEXT_MUTED);
}

void tile_transport_input(SparklesTile *tile, Rectangle b) {
    (void)tile;
    Vector2 m = GetMousePosition();
    Vector2 c = { b.x + b.width / 2.0f, b.y + b.height / 2.0f };

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (CheckCollisionPointCircle(m, c, 36.0f)) {
            if (atomic_load(&play_state_atomic) == STATE_STOPPED && playing_filepath[0] != '\0') {
                atomic_store(&current_cmd_atomic, CMD_PLAY);
            } else {
                atomic_store(&current_cmd_atomic, CMD_PAUSE);
            }
        }
        if (CheckCollisionPointRec(m, (Rectangle){ c.x - 90, c.y - 25, 45, 50 })) {
            atomic_store(&current_cmd_atomic, CMD_PREV);
        }
        if (CheckCollisionPointRec(m, (Rectangle){ c.x + 45, c.y - 25, 45, 50 })) {
            atomic_store(&current_cmd_atomic, CMD_NEXT);
        }
    }
}