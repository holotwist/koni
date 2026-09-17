#ifndef SPARKLES_INPUT_H
#define SPARKLES_INPUT_H

#include "raylib.h"
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    SPARKLES_GESTURE_NONE = 0,
    SPARKLES_GESTURE_TAP,
    SPARKLES_GESTURE_LONG_PRESS,
    SPARKLES_GESTURE_DRAG_START,
    SPARKLES_GESTURE_DRAG_MOVE,
    SPARKLES_GESTURE_DRAG_END,
    SPARKLES_GESTURE_FLING,
    SPARKLES_GESTURE_EDGE_SWIPE_LEFT,
    SPARKLES_GESTURE_EDGE_SWIPE_RIGHT
} SparklesGestureType;

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
    SparklesGestureType type;
    Vector2 pos;
    Vector2 delta;
    Vector2 velocity;
} SparklesGesture;

typedef struct {
    Vector2 pos;
    bool is_down;
    bool just_pressed;
    bool just_released;
    float wheel_delta;
} SparklesPointer;

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
const SparklesPointer* sparkles_input_get_pointer(void);

bool sparkles_input_poll_gesture(SparklesGesture *out_gesture);
bool sparkles_input_poll_action(SparklesActionType *out_action);

void sparkles_input_emit_gesture(SparklesGesture gesture);
void sparkles_input_emit_action(SparklesActionType action);

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