#include "sparkles_input.h"
#include <math.h>

static inline float v2_dist(Vector2 a, Vector2 b) {
    float dx = a.x - b.x;
    float dy = a.y - b.y;
    return sqrtf(dx * dx + dy * dy);
}

static Vector2 s_touch_start = {0};
static Vector2 s_touch_current = {0};
static Vector2 s_last_pos = {0};
static float s_touch_time = 0.0f;
static bool s_touch_down = false;
static bool s_long_pressed = false;

static void touch_init(void) {
    s_touch_down = false;
    s_long_pressed = false;
    s_touch_time = 0.0f;
}

static void touch_shutdown(void) {
}

static void touch_update(float dt) {
    float sw = (float)GetScreenWidth();
    int touch_count = GetTouchPointCount();

    if (touch_count > 0) {
        Vector2 pos = GetTouchPosition(0);

        if (!s_touch_down) {
            s_touch_down = true;
            s_touch_start = pos;
            s_touch_current = pos;
            s_last_pos = pos;
            s_touch_time = 0.0f;
            s_long_pressed = false;

            sparkles_input_emit_gesture((SparklesGesture){
                .type = SPARKLES_GESTURE_DRAG_START,
                .pos = pos,
                .delta = {0},
                .velocity = {0}
            });
        } else {
            s_touch_time += dt;
            s_touch_current = pos;
            Vector2 delta = { pos.x - s_last_pos.x, pos.y - s_last_pos.y };
            float dist = v2_dist(s_touch_start, pos);

            // Long press detection (~450ms within 10px slop)
            if (!s_long_pressed && s_touch_time >= 0.45f && dist < 10.0f) {
                s_long_pressed = true;
                sparkles_input_emit_long_press(s_touch_start);
            }

            // Convert vertical finger delta to list scroll steps
            if (fabsf(delta.y) > 0.0f && dist > 10.0f) {
                sparkles_input_add_scroll(pos, -delta.y / 28.0f);
            }
            s_last_pos = pos;
        }
    } else if (s_touch_down) {
        s_touch_down = false;
        float dist = v2_dist(s_touch_start, s_touch_current);
        Vector2 total_delta = { s_touch_current.x - s_touch_start.x, s_touch_current.y - s_touch_start.y };

        // Kinetic fling momentum
        float vel_y = (s_touch_time > 0.02f) ? (total_delta.y / s_touch_time) : 0.0f;
        if (fabsf(vel_y) > 180.0f) {
            sparkles_input_set_fling(-vel_y / 28.0f);
        }

        // Universal edge swipe from right border pulls radial list
        if (s_touch_start.x >= sw - 24.0f && total_delta.x < -40.0f) {
            sparkles_input_emit_action(SPARKLES_ACTION_TOGGLE_RADIAL);
        }
        // Horizontal page slide
        else if (fabsf(total_delta.x) > 60.0f && fabsf(total_delta.x) > fabsf(total_delta.y) * 1.5f) {
            if (total_delta.x > 0) sparkles_input_emit_action(SPARKLES_ACTION_NAV_LEFT);
            else sparkles_input_emit_action(SPARKLES_ACTION_NAV_RIGHT);
        }
        // Vertical page slide
        else if (fabsf(total_delta.y) > 60.0f && fabsf(total_delta.y) > fabsf(total_delta.x) * 1.5f) {
            if (total_delta.y < 0) sparkles_input_emit_action(SPARKLES_ACTION_NAV_UP);
            else sparkles_input_emit_action(SPARKLES_ACTION_NAV_DOWN);
        }
        // Tap
        else if (!s_long_pressed && dist < 12.0f && s_touch_time < 0.40f) {
            sparkles_input_emit_tap(s_touch_current);
        }
    }

    if (IsKeyPressed(KEY_BACK)) {
        sparkles_input_emit_action(SPARKLES_ACTION_BACK);
    }
}

const SparklesInputDriver g_driver_touch = {
    .name = "Touch & Gestures",
    .init = touch_init,
    .update = touch_update,
    .shutdown = touch_shutdown
};