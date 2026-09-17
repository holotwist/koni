#include "sparkles_input.h"
#include <string.h>

#define EVENT_QUEUE_CAPACITY 32

static const SparklesInputDriver *s_current_driver = NULL;
static SparklesPointer s_pointer = {0};

static SparklesGesture s_gesture_queue[EVENT_QUEUE_CAPACITY];
static int s_gesture_head = 0;
static int s_gesture_tail = 0;

static SparklesActionType s_action_queue[EVENT_QUEUE_CAPACITY];
static int s_action_head = 0;
static int s_action_tail = 0;

static SparklesTextInputSession s_text_session = {0};
static bool s_text_focused = false;

void sparkles_input_init(void) {
    s_gesture_head = 0;
    s_gesture_tail = 0;
    s_action_head = 0;
    s_action_tail = 0;
    memset(&s_text_session, 0, sizeof(s_text_session));
    memset(&s_pointer, 0, sizeof(s_pointer));

    // Default to desktop driver unless overridden
    sparkles_input_set_driver(&g_driver_desktop);
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

void sparkles_input_update(float dt) {
    if (s_current_driver && s_current_driver->update) {
        s_current_driver->update(dt);
    }
}

const SparklesPointer* sparkles_input_get_pointer(void) {
    return &s_pointer;
}

void sparkles_input_emit_gesture(SparklesGesture gesture) {
    int next = (s_gesture_head + 1) % EVENT_QUEUE_CAPACITY;
    if (next != s_gesture_tail) {
        s_gesture_queue[s_gesture_head] = gesture;
        s_gesture_head = next;
    }
}

bool sparkles_input_poll_gesture(SparklesGesture *out_gesture) {
    if (s_gesture_head == s_gesture_tail || !out_gesture) return false;
    *out_gesture = s_gesture_queue[s_gesture_tail];
    s_gesture_tail = (s_gesture_tail + 1) % EVENT_QUEUE_CAPACITY;
    return true;
}

void sparkles_input_emit_action(SparklesActionType action) {
    int next = (s_action_head + 1) % EVENT_QUEUE_CAPACITY;
    if (next != s_action_tail) {
        s_action_queue[s_action_head] = action;
        s_action_head = next;
    }
}

bool sparkles_input_poll_action(SparklesActionType *out_action) {
    if (s_action_head == s_action_tail || !out_action) return false;
    *out_action = s_action_queue[s_action_tail];
    s_action_tail = (s_action_tail + 1) % EVENT_QUEUE_CAPACITY;
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