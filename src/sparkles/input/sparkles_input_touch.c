#include "sparkles_input.h"
#include <math.h>

static Vector2 s_last_touch_pos = {0};
static Vector2 s_touch_start_pos = {0};
static float s_touch_duration = 0.0f;
static bool s_was_down = false;

static void touch_init(void) {
    s_was_down = false;
    s_touch_duration = 0.0f;
}

static void touch_shutdown(void) {}

static void touch_update(float dt) {
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    int touch_count = GetTouchPointCount();

    if (touch_count > 0) {
        Vector2 pos = GetTouchPosition(0);
        if (!s_was_down) {
            s_touch_start_pos = pos;
            s_last_touch_pos = pos;
            s_touch_duration = 0.0f;
            s_was_down = true;
        } else {
            s_touch_duration += dt;
        }

        sparkles_input_feed_pointer(pos, true, dt);
        s_last_touch_pos = pos;
    } else {
        if (s_was_down) {
            s_was_down = false;
            sparkles_input_feed_pointer(s_last_touch_pos, false, dt);

            Vector2 total_delta = {
                s_last_touch_pos.x - s_touch_start_pos.x,
                s_last_touch_pos.y - s_touch_start_pos.y
            };

            // Kinetic momentum calculation
            if (s_touch_duration > 0.02f) {
                float vel_y = total_delta.y / s_touch_duration;
                if (fabsf(vel_y) > 180.0f) {
                    sparkles_input_feed_fling(-vel_y);
                }
            }

            // Edge swipe and navigation gestures
            extern int sparkles_nav_get_target_x(void);
            extern int sparkles_nav_get_target_y(void);
            extern bool sparkles_radial_list_is_open(void);
            extern void sparkles_radial_list_close(void);
            extern bool sparkles_queue_is_open(void);
            extern bool sparkles_settings_is_open(void);
            extern bool sparkles_vis_picker_is_open(void);
            extern bool sparkles_context_menu_is_open(void);
            extern bool sparkles_eq_is_dragging(void);
            extern bool sparkles_krystal_is_dragging(void);
            extern bool sparkles_text_prompt_is_open(void);

            bool radial_open = sparkles_radial_list_is_open();
            bool queue_open = sparkles_queue_is_open();
            bool eq_dragging = sparkles_eq_is_dragging() || sparkles_krystal_is_dragging();
            bool prompt_open = sparkles_text_prompt_is_open();
            bool modal_open = radial_open || queue_open || prompt_open ||
                              sparkles_settings_is_open() || sparkles_vis_picker_is_open() ||
                              sparkles_context_menu_is_open();

            int nav_x = sparkles_nav_get_target_x();
            int nav_y = sparkles_nav_get_target_y();
            bool in_deck_zone = (s_touch_start_pos.y > sh * 0.65f);

            const SparklesPointerState *ptr = sparkles_input_get_state();

            if (radial_open && total_delta.x > 40.0f && fabsf(total_delta.x) > fabsf(total_delta.y)) {
                sparkles_radial_list_close();
                sparkles_input_consume();
            } else if (queue_open && total_delta.y < -45.0f && fabsf(total_delta.y) > fabsf(total_delta.x) * 1.2f) {
                // Push up to dismiss top queue drawer
                sparkles_input_emit_action(SPARKLES_ACTION_TOGGLE_QUEUE);
                sparkles_input_consume();
            } else if (!modal_open && nav_y == 0 && nav_x == 0 &&
                       s_touch_start_pos.x >= (sw - fmaxf(60.0f, sw * 0.14f)) &&
                       total_delta.x < -35.0f && fabsf(total_delta.x) > fabsf(total_delta.y)) {
                sparkles_input_emit_action(SPARKLES_ACTION_TOGGLE_RADIAL);
                sparkles_input_consume();
            } else if (!modal_open && !in_deck_zone && nav_y == 0 &&
                       fabsf(total_delta.x) > 60.0f && fabsf(total_delta.x) > fabsf(total_delta.y) * 1.5f) {
                if (total_delta.x > 0) sparkles_input_emit_action(SPARKLES_ACTION_NAV_LEFT);
                else sparkles_input_emit_action(SPARKLES_ACTION_NAV_RIGHT);
                sparkles_input_consume();
            } else if (!modal_open && nav_x == 0 && nav_y == 0 &&
                       s_touch_start_pos.y <= (sh * 0.50f) &&
                       total_delta.y > 55.0f && fabsf(total_delta.y) > fabsf(total_delta.x) * 1.3f) {
                // Drag down from upper half slides down the queue from top
                sparkles_input_emit_action(SPARKLES_ACTION_TOGGLE_QUEUE);
                sparkles_input_consume();
            } else if (!modal_open && !eq_dragging && nav_x == 0 && ptr && !ptr->is_dragging &&
                       s_touch_duration < 0.35f &&
                       fabsf(total_delta.y) > 65.0f && fabsf(total_delta.y) > fabsf(total_delta.x) * 1.5f) {
                // Swipe up steps up (Surface -> EQ -> Krystal); swipe down returns (Krystal -> EQ -> Surface)
                if (total_delta.y < 0 && nav_y < 2) sparkles_input_emit_action(SPARKLES_ACTION_NAV_UP);
                else if (total_delta.y > 0 && nav_y > 0) sparkles_input_emit_action(SPARKLES_ACTION_NAV_DOWN);
                sparkles_input_consume();
            }
        } else {
            sparkles_input_feed_pointer(s_last_touch_pos, false, dt);
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