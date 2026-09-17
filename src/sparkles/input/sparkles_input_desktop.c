#include "sparkles_input.h"
#include <string.h>
#include <math.h>

static inline float v2_dist(Vector2 a, Vector2 b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return sqrtf(dx * dx + dy * dy);
}

static float s_hold_timer = 0.0f;
static bool s_holding = false;
static Vector2 s_press_pos = {0};

static void desktop_init(void) {
    s_hold_timer = 0.0f;
    s_holding = false;
}

static void desktop_shutdown(void) {
}

static void desktop_handle_text_input(void) {
    SparklesTextInputSession *sess = sparkles_input_get_text_session();
    if (!sess || !sess->active) return;

    int c = GetCharPressed();
    while (c > 0) {
        if (c >= 32 && c <= 126 && sess->len < sess->max_len) {
            sess->buffer[sess->len++] = (char)c;
            sess->buffer[sess->len] = '\0';
        }
        c = GetCharPressed();
    }

    if (IsKeyPressed(KEY_BACKSPACE) && sess->len > 0) {
        sess->buffer[--sess->len] = '\0';
    }

    if (IsKeyPressed(KEY_ENTER) && sess->len > 0) {
        if (sess->on_submit) sess->on_submit(sess->buffer, sess->user_data);
        sess->active = false;
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        sparkles_input_cancel_text();
    }
}

static void desktop_update(float dt) {
    Vector2 m = GetMousePosition();
    float wheel = GetMouseWheelMove();

    // Route active text ingress
    if (sparkles_input_is_text_active()) {
        desktop_handle_text_input();
        return;
    }

    // Suppress shortcut keys while searching or typing
    if (sparkles_input_is_text_focused()) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            sparkles_input_emit_action(SPARKLES_ACTION_BACK);
        }
        return;
    }

    // Right-click emits LONG_PRESS
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        sparkles_input_emit_gesture((SparklesGesture){
            .type = SPARKLES_GESTURE_LONG_PRESS,
            .pos = m,
            .delta = {0},
            .velocity = {0}
        });
    }

    // Left-click tap & drag detection
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        s_press_pos = m;
        s_holding = true;
        s_hold_timer = 0.0f;
    }

    if (s_holding && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        s_hold_timer += dt;
        Vector2 delta = GetMouseDelta();
        if (delta.x != 0.0f || delta.y != 0.0f) {
            sparkles_input_emit_gesture((SparklesGesture){
                .type = SPARKLES_GESTURE_DRAG_MOVE,
                .pos = m,
                .delta = delta,
                .velocity = { delta.x / (dt > 0 ? dt : 0.016f), delta.y / (dt > 0 ? dt : 0.016f) }
            });
        }
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (s_holding) {
            float dist = v2_dist(s_press_pos, m);
            if (dist < 8.0f && s_hold_timer < 0.40f) {
                sparkles_input_emit_gesture((SparklesGesture){
                    .type = SPARKLES_GESTURE_TAP,
                    .pos = m,
                    .delta = {0},
                    .velocity = {0}
                });
            } else {
                sparkles_input_emit_gesture((SparklesGesture){
                    .type = SPARKLES_GESTURE_DRAG_END,
                    .pos = m,
                    .delta = {0},
                    .velocity = {0}
                });
            }
        }
        s_holding = false;
    }

    // Wheel drag simulation
    if (wheel != 0.0f) {
        sparkles_input_emit_gesture((SparklesGesture){
            .type = SPARKLES_GESTURE_DRAG_MOVE,
            .pos = m,
            .delta = { 0.0f, -wheel * 24.0f },
            .velocity = { 0.0f, -wheel * 480.0f }
        });
    }

    // Semantic hotkey translations
    if (IsKeyPressed(KEY_ESCAPE))   sparkles_input_emit_action(SPARKLES_ACTION_BACK);
    if (IsKeyPressed(KEY_SPACE))    sparkles_input_emit_action(SPARKLES_ACTION_PLAY_PAUSE);
    if (IsKeyPressed(KEY_N))        sparkles_input_emit_action(SPARKLES_ACTION_NEXT);
    if (IsKeyPressed(KEY_B))        sparkles_input_emit_action(SPARKLES_ACTION_PREV);
    if (IsKeyPressed(KEY_T))        sparkles_input_emit_action(SPARKLES_ACTION_TOGGLE_VIEW);
    if (IsKeyPressed(KEY_Q))        sparkles_input_emit_action(SPARKLES_ACTION_TOGGLE_QUEUE);
    if (IsKeyPressed(KEY_E))        sparkles_input_emit_action(SPARKLES_ACTION_TOGGLE_EQ);
    if (IsKeyPressed(KEY_K))        sparkles_input_emit_action(SPARKLES_ACTION_TOGGLE_KRYSTAL);
    if (IsKeyPressed(KEY_H))        sparkles_input_emit_action(SPARKLES_ACTION_TOGGLE_HELP);
    if (IsKeyPressed(KEY_COMMA))    sparkles_input_emit_action(SPARKLES_ACTION_TOGGLE_SETTINGS);
    if (IsKeyPressed(KEY_TAB))      sparkles_input_emit_action(SPARKLES_ACTION_TOGGLE_RADIAL);
    if (IsKeyPressed(KEY_LEFT))     sparkles_input_emit_action(SPARKLES_ACTION_NAV_LEFT);
    if (IsKeyPressed(KEY_RIGHT))    sparkles_input_emit_action(SPARKLES_ACTION_NAV_RIGHT);
    if (IsKeyPressed(KEY_UP))       sparkles_input_emit_action(SPARKLES_ACTION_NAV_UP);
    if (IsKeyPressed(KEY_DOWN))     sparkles_input_emit_action(SPARKLES_ACTION_NAV_DOWN);

    if (IsKeyPressed(KEY_C)) {
        if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
            sparkles_input_emit_action(SPARKLES_ACTION_PICK_VIS);
        } else {
            sparkles_input_emit_action(SPARKLES_ACTION_CYCLE_VIS);
        }
    }

    if (IsKeyPressed(KEY_L)) {
        if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
            sparkles_input_emit_action(SPARKLES_ACTION_LOCATE_PLAYING);
        } else {
            sparkles_input_emit_action(SPARKLES_ACTION_TOGGLE_LYRICS);
        }
    }

    bool search_key = IsKeyPressed(KEY_SLASH) ||
                      ((IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) && IsKeyPressed(KEY_SEVEN)) ||
                      ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) && IsKeyPressed(KEY_F));

    if (search_key) {
        sparkles_input_emit_action(SPARKLES_ACTION_TOGGLE_SEARCH);
    }
}

const SparklesInputDriver g_driver_desktop = {
    .name = "Desktop Keyboard & Mouse",
    .init = desktop_init,
    .update = desktop_update,
    .shutdown = desktop_shutdown
};