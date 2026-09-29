#include "sparkles_input.h"
#include "sparkles_theme.h"
#include <string.h>
#include <math.h>

#define ACTION_QUEUE_CAP 32
#define TOUCH_SLOP_BASE 10.0f
#define LONG_PRESS_DELAY 0.45f

static const SparklesInputDriver *s_current_driver = NULL;
static SparklesPointerState s_pointer = {0};

static SparklesId s_active_id = 0;
static SparklesId s_hot_id = 0;

bool s_long_press_triggered = false;
static bool s_has_unconsumed_tap = false;
static Vector2 s_unconsumed_tap_pos = {0};

static SparklesActionType s_action_queue[ACTION_QUEUE_CAP];
static int s_action_head = 0;
static int s_action_tail = 0;

static SparklesTextInputSession s_text_session = {0};
static bool s_text_focused = false;

SparklesId sparkles_input_id(const char *str, int index) {
    uint32_t hash = 2166136261u;
    if (str) {
        while (*str) {
            hash ^= (uint8_t)*str++;
            hash *= 16777619u;
        }
    }
    hash ^= (uint32_t)index;
    hash *= 16777619u;
    return (hash == 0) ? 1 : hash;
}

void sparkles_input_init(void) {
    memset(&s_pointer, 0, sizeof(s_pointer));
    s_active_id = 0;
    s_hot_id = 0;
    s_action_head = 0;
    s_action_tail = 0;
    s_long_press_triggered = false;
    s_has_unconsumed_tap = false;
    memset(&s_text_session, 0, sizeof(s_text_session));
    s_text_focused = false;

#if defined(__ANDROID__)
    sparkles_input_set_driver(&g_driver_touch);
#else
    sparkles_input_set_driver(&g_driver_desktop);
#endif
}

void sparkles_input_shutdown(void) {
    if (s_current_driver && s_current_driver->shutdown) {
        s_current_driver->shutdown();
    }
    s_current_driver = NULL;
}

void sparkles_input_set_driver(const SparklesInputDriver *driver) {
    if (s_current_driver && s_current_driver->shutdown) {
        s_current_driver->shutdown();
    }
    s_current_driver = driver;
    if (s_current_driver && s_current_driver->init) {
        s_current_driver->init();
    }
}

void sparkles_input_feed_pointer(Vector2 pos, bool is_down, float dt) {
    s_pointer.prev_pos = s_pointer.pos;
    s_pointer.pos = pos;
    s_pointer.delta = (Vector2){ pos.x - s_pointer.prev_pos.x, pos.y - s_pointer.prev_pos.y };
    s_pointer.is_consumed = false;

    if (is_down) {
        if (!s_pointer.is_down) {
            s_pointer.is_down = true;
            s_pointer.just_pressed = true;
            s_pointer.just_released = false;
            s_pointer.down_pos = pos;
            s_pointer.is_dragging = false;
            s_pointer.hold_time = 0.0f;
            s_long_press_triggered = false;
            s_has_unconsumed_tap = false;
        } else {
            s_pointer.just_pressed = false;
            s_pointer.hold_time += dt;

            float dx = pos.x - s_pointer.down_pos.x;
            float dy = pos.y - s_pointer.down_pos.y;
            float dist = sqrtf(dx * dx + dy * dy);

            float touch_slop = TOUCH_SLOP_BASE * sparkles_get_ui_scale();
            if (dist > touch_slop) {
                s_pointer.is_dragging = true;
            }

            if (!s_pointer.is_dragging && !s_long_press_triggered && s_pointer.hold_time >= LONG_PRESS_DELAY) {
                s_long_press_triggered = true;
            }
        }
    } else {
        if (s_pointer.is_down) {
            s_pointer.is_down = false;
            s_pointer.just_released = true;
            s_pointer.just_pressed = false;

            if (!s_pointer.is_dragging && !s_long_press_triggered && s_pointer.hold_time < 0.50f) {
                s_has_unconsumed_tap = true;
                s_unconsumed_tap_pos = pos;
            }
        } else {
            s_pointer.just_pressed = false;
            s_pointer.just_released = false;
        }
        s_pointer.is_dragging = false;
        s_pointer.hold_time = 0.0f;
    }
}

void sparkles_input_feed_wheel(float delta) {
    s_pointer.wheel_move += delta;
}

void sparkles_input_feed_fling(float velocity_y) {
    s_pointer.fling_velocity_y = velocity_y;
}

void sparkles_input_update(float dt) {
    s_pointer.wheel_move = 0.0f;
    s_has_unconsumed_tap = false;

    // Decay fling velocity
    if (fabsf(s_pointer.fling_velocity_y) > 0.01f) {
        s_pointer.fling_velocity_y *= expf(-dt * 6.0f);
        if (fabsf(s_pointer.fling_velocity_y) < 1.0f) {
            s_pointer.fling_velocity_y = 0.0f;
        }
    }

#if !defined(__ANDROID__)
    if (s_current_driver == &g_driver_desktop && GetTouchPointCount() > 0) {
        sparkles_input_set_driver(&g_driver_touch);
    } else if (s_current_driver == &g_driver_touch && GetTouchPointCount() == 0 &&
               (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || GetMouseWheelMove() != 0.0f)) {
        sparkles_input_set_driver(&g_driver_desktop);
    }
#endif

    if (s_current_driver && s_current_driver->update) {
        s_current_driver->update(dt);
    }

    if (s_pointer.just_released) {
        s_active_id = 0;
    }
}

const SparklesPointerState* sparkles_input_get_state(void) {
    return &s_pointer;
}

bool sparkles_input_is_consumed(void) {
    return s_pointer.is_consumed;
}

void sparkles_input_consume(void) {
    s_pointer.is_consumed = true;
    s_has_unconsumed_tap = false;
    s_long_press_triggered = false;
}

bool sparkles_input_block_area(Rectangle bounds) {
    if (s_pointer.is_consumed) return false;
    if (CheckCollisionPointRec(s_pointer.pos, bounds) || CheckCollisionPointRec(s_pointer.down_pos, bounds)) {
        sparkles_input_consume();
        return true;
    }
    return false;
}

bool sparkles_input_button(SparklesId id, Rectangle bounds, bool *out_hover) {
    bool hovered = !s_pointer.is_consumed && CheckCollisionPointRec(s_pointer.pos, bounds);
    if (out_hover) *out_hover = hovered;

    if (hovered) {
        s_hot_id = id;
        if (s_pointer.just_pressed) {
            s_active_id = id;
        }
    }

    if (s_active_id == id) {
        if (s_pointer.just_released) {
            s_active_id = 0;
            if (hovered && !s_pointer.is_dragging && !s_pointer.is_consumed) {
                sparkles_input_consume();
                return true;
            }
        }
    }
    return false;
}

bool sparkles_input_slider(SparklesId id, Rectangle bounds, float *val, float min_val, float max_val) {
    if (!val || min_val >= max_val) return false;

    bool hovered = !s_pointer.is_consumed && CheckCollisionPointRec(s_pointer.pos, bounds);
    if (hovered && s_pointer.just_pressed) {
        s_active_id = id;
    }

    if (s_active_id == id && s_pointer.is_down) {
        sparkles_input_consume();
        float norm = (s_pointer.pos.x - bounds.x) / bounds.width;
        if (norm < 0.0f) norm = 0.0f;
        if (norm > 1.0f) norm = 1.0f;
        *val = min_val + norm * (max_val - min_val);
        return true;
    }
    return false;
}

bool sparkles_input_scrollable_area(SparklesId id, Rectangle bounds, float *scroll_offset, float content_height) {
    if (!scroll_offset) return false;

    float viewport_h = bounds.height;
    float max_scroll = fmaxf(0.0f, content_height - viewport_h);

    bool inside = !s_pointer.is_consumed && CheckCollisionPointRec(s_pointer.pos, bounds);
    if (inside && s_pointer.just_pressed) {
        s_active_id = id;
    }

    bool changed = false;

    // Direct touch drag
    if (s_active_id == id && s_pointer.is_down && s_pointer.is_dragging) {
        sparkles_input_consume();
        *scroll_offset -= s_pointer.delta.y;
        changed = true;
    }

    // Wheel and inertia
    if (inside && s_pointer.wheel_move != 0.0f) {
        *scroll_offset -= s_pointer.wheel_move * 24.0f;
        changed = true;
    }

    if (fabsf(s_pointer.fling_velocity_y) > 0.01f && (s_active_id == id || inside)) {
        *scroll_offset += s_pointer.fling_velocity_y * GetFrameTime();
        changed = true;
    }

    if (*scroll_offset < 0.0f) *scroll_offset = 0.0f;
    if (*scroll_offset > max_scroll) *scroll_offset = max_scroll;

    return changed;
}

bool sparkles_input_consume_tap(Rectangle bounds, Vector2 *out_pos) {
    if (s_pointer.is_consumed) return false;

    if (s_has_unconsumed_tap && CheckCollisionPointRec(s_unconsumed_tap_pos, bounds)) {
        if (out_pos) *out_pos = s_unconsumed_tap_pos;
        sparkles_input_consume();
        return true;
    }
    return false;
}

bool sparkles_input_consume_tap_outside(Rectangle bounds, Vector2 *out_pos) {
    if (s_pointer.is_consumed) return false;

    if (s_has_unconsumed_tap) {
        if (!CheckCollisionPointRec(s_unconsumed_tap_pos, bounds)) {
            if (out_pos) *out_pos = s_unconsumed_tap_pos;
            sparkles_input_consume();
            return true;
        }
    }
    return false;
}

bool sparkles_input_consume_long_press(Rectangle bounds, Vector2 *out_pos) {
    if (s_pointer.is_consumed) return false;

    if (s_long_press_triggered) {
        Vector2 check_pos = s_pointer.is_down ? s_pointer.down_pos : s_pointer.pos;
        if (bounds.width == 0 && bounds.height == 0) {
            s_long_press_triggered = false;
            return false;
        }
        if (CheckCollisionPointRec(check_pos, bounds)) {
            if (out_pos) *out_pos = check_pos;
            s_long_press_triggered = false;
            sparkles_input_consume();
            return true;
        }
    }
    return false;
}

float sparkles_input_get_scroll_delta(Rectangle bounds) {
    if (s_pointer.is_consumed) return 0.0f;

    bool inside = CheckCollisionPointRec(s_pointer.pos, bounds) ||
                  CheckCollisionPointRec(s_pointer.down_pos, bounds);
    if (!inside) return 0.0f;

    float delta = 0.0f;
    if (s_pointer.wheel_move != 0.0f) {
        delta += s_pointer.wheel_move * 3.0f;
    }
    if (s_pointer.is_down && s_pointer.is_dragging && fabsf(s_pointer.delta.y) > 0.0f) {
        delta -= s_pointer.delta.y / 18.0f;
    }
    if (fabsf(s_pointer.fling_velocity_y) > 1.0f) {
        delta += s_pointer.fling_velocity_y * GetFrameTime() / 18.0f;
    }
    return delta;
}

void sparkles_input_emit_action(SparklesActionType action) {
    int next = (s_action_head + 1) % ACTION_QUEUE_CAP;
    if (next != s_action_tail) {
        s_action_queue[s_action_head] = action;
        s_action_head = next;
    }
}

bool sparkles_input_poll_action(SparklesActionType *out_action) {
    if (s_action_head == s_action_tail || !out_action) return false;
    *out_action = s_action_queue[s_action_tail];
    s_action_tail = (s_action_tail + 1) % ACTION_QUEUE_CAP;
    return true;
}

void sparkles_input_begin_text(const char *title, const char *initial, int max_len,
                               TextInputCallback on_submit, TextCancelCallback on_cancel,
                               void *user_data) {
    s_text_session.active = true;
    strncpy(s_text_session.title, title ? title : "Input", sizeof(s_text_session.title) - 1);
    s_text_session.title[sizeof(s_text_session.title) - 1] = '\0';

    if (initial) {
        strncpy(s_text_session.buffer, initial, sizeof(s_text_session.buffer) - 1);
        s_text_session.buffer[sizeof(s_text_session.buffer) - 1] = '\0';
    } else {
        s_text_session.buffer[0] = '\0';
    }
    s_text_session.len = (int)strlen(s_text_session.buffer);
    s_text_session.max_len = (max_len > 0 && max_len < (int)sizeof(s_text_session.buffer))
                             ? max_len : (int)sizeof(s_text_session.buffer) - 1;
    s_text_session.on_submit = on_submit;
    s_text_session.on_cancel = on_cancel;
    s_text_session.user_data = user_data;
}

void sparkles_input_cancel_text(void) {
    if (!s_text_session.active) return;
    if (s_text_session.on_cancel) {
        s_text_session.on_cancel(s_text_session.user_data);
    }
    s_text_session.active = false;
}

bool sparkles_input_is_text_active(void) {
    return s_text_session.active;
}

SparklesTextInputSession* sparkles_input_get_text_session(void) {
    return &s_text_session;
}

void sparkles_input_set_text_focused(bool focused) {
    s_text_focused = focused;
}

bool sparkles_input_is_text_focused(void) {
    return s_text_focused || s_text_session.active;
}