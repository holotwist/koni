#ifndef SPARKLES_INPUT_H
#define SPARKLES_INPUT_H

#include "raylib.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef uint32_t SparklesId;

typedef enum {
    SPARKLES_ACTION_NONE = 0,
    SPARKLES_ACTION_PLAY_PAUSE,
    SPARKLES_ACTION_NEXT,
    SPARKLES_ACTION_PREV,
    SPARKLES_ACTION_BACK,
    SPARKLES_ACTION_NAV_LEFT,
    SPARKLES_ACTION_NAV_RIGHT,
    SPARKLES_ACTION_NAV_UP,
    SPARKLES_ACTION_NAV_DOWN,
    SPARKLES_ACTION_TOGGLE_VIEW,
    SPARKLES_ACTION_TOGGLE_RADIAL,
    SPARKLES_ACTION_TOGGLE_QUEUE,
    SPARKLES_ACTION_TOGGLE_EQ,
    SPARKLES_ACTION_TOGGLE_KRYSTAL,
    SPARKLES_ACTION_TOGGLE_HELP,
    SPARKLES_ACTION_TOGGLE_SEARCH,
    SPARKLES_ACTION_TOGGLE_SETTINGS,
    SPARKLES_ACTION_CYCLE_VIS,
    SPARKLES_ACTION_PICK_VIS,
    SPARKLES_ACTION_LOCATE_PLAYING,
    SPARKLES_ACTION_TOGGLE_LYRICS
} SparklesActionType;

typedef struct {
    Vector2 pos;
    Vector2 prev_pos;
    Vector2 down_pos;
    Vector2 delta;
    bool is_down;
    bool just_pressed;
    bool just_released;
    bool is_dragging;
    bool is_consumed;
    float hold_time;
    float wheel_move;
    float fling_velocity_y;
} SparklesPointerState;

typedef void (*TextInputCallback)(const char *text, void *user_data);
typedef void (*TextCancelCallback)(void *user_data);

typedef struct {
    bool active;
    char title[64];
    char buffer[256];
    int len;
    int max_len;
    TextInputCallback on_submit;
    TextCancelCallback on_cancel;
    void *user_data;
} SparklesTextInputSession;

typedef struct SparklesInputDriver {
    const char *name;
    void (*init)(void);
    void (*update)(float dt);
    void (*shutdown)(void);
} SparklesInputDriver;

void sparkles_input_init(void);
void sparkles_input_shutdown(void);
void sparkles_input_update(float dt);
void sparkles_input_set_driver(const SparklesInputDriver *driver);

// FNV-1a ID generator
SparklesId sparkles_input_id(const char *str, int index);

// State queries
const SparklesPointerState* sparkles_input_get_state(void);
bool sparkles_input_is_consumed(void);
void sparkles_input_consume(void);

// Top-down modal and hit test blockers
bool sparkles_input_block_area(Rectangle bounds);

// High-level IMGUI interaction primitives
bool sparkles_input_button(SparklesId id, Rectangle bounds, bool *out_hover);
bool sparkles_input_slider(SparklesId id, Rectangle bounds, float *val, float min_val, float max_val);
bool sparkles_input_scrollable_area(SparklesId id, Rectangle bounds, float *scroll_offset, float content_height);

// Immediate-mode queries
bool sparkles_input_consume_tap(Rectangle bounds, Vector2 *out_pos);
bool sparkles_input_consume_tap_outside(Rectangle bounds, Vector2 *out_pos);
bool sparkles_input_consume_long_press(Rectangle bounds, Vector2 *out_pos);
float sparkles_input_get_scroll_delta(Rectangle bounds);

// Driver ingress helpers
void sparkles_input_feed_pointer(Vector2 pos, bool is_down, float dt);
void sparkles_input_feed_wheel(float delta);
void sparkles_input_feed_fling(float velocity_y);

// Action and text session queues
void sparkles_input_emit_action(SparklesActionType action);
bool sparkles_input_poll_action(SparklesActionType *out_action);

void sparkles_input_begin_text(const char *title, const char *initial, int max_len,
                               TextInputCallback on_submit, TextCancelCallback on_cancel,
                               void *user_data);
void sparkles_input_cancel_text(void);
bool sparkles_input_is_text_active(void);
SparklesTextInputSession* sparkles_input_get_text_session(void);

void sparkles_input_set_text_focused(bool focused);
bool sparkles_input_is_text_focused(void);

extern const SparklesInputDriver g_driver_desktop;
extern const SparklesInputDriver g_driver_touch;

#endif // SPARKLES_INPUT_H