#ifndef UI_KEYBINDS_H
#define UI_KEYBINDS_H

#include <stdbool.h>

typedef enum {
    ACTION_NONE = 0,
    ACTION_QUIT,
    ACTION_SEARCH,
    ACTION_HELP,
    ACTION_SWITCH_TAB,
    ACTION_RESCAN,
    ACTION_SORT,
    ACTION_UP,
    ACTION_DOWN,
    ACTION_PAGE_UP,
    ACTION_PAGE_DOWN,
    ACTION_TOP,
    ACTION_BOTTOM,
    ACTION_SEEK_BACK,
    ACTION_SEEK_FWD,
    ACTION_ADD,
    ACTION_ADD_ALL,
    ACTION_CLEAR_QUEUE,
    ACTION_DELETE,
    ACTION_PLAY_SELECT,
    ACTION_SHUFFLE,
    ACTION_REPEAT,
    ACTION_REPLAYGAIN,
    ACTION_LAYOUT,
    ACTION_MUTE,
    ACTION_PLAY_PAUSE,
    ACTION_NEXT,
    ACTION_PREV,
    ACTION_TAB_VIS,
    ACTION_TAB_LYRICS,
    ACTION_VIS_MODE,
    ACTION_FULLSCREEN,
    ACTION_TOGGLE_VIS,
    ACTION_TOGGLE_LRC,
    ACTION_VOL_UP,
    ACTION_VOL_DOWN,
    ACTION_INFO,
    ACTION_FAVOURITE,
    ACTION_LOCATE_PLAYING,
    ACTION_TOGGLE_EQ,
    ACTION_TOGGLE_KRYSTAL,
    ACTION_COUNT
} UIAction;

typedef struct {
    UIAction action;
    const char *name;
    const char *default_keys;
} KeybindDefault;

#if defined(__ANDROID__) || defined(PLATFORM_ANDROID)
static inline void ui_keybinds_init(void) {}
static inline bool ui_keybinds_set(const char *action_name, const char *keys_str) { (void)action_name; (void)keys_str; return true; }
static inline UIAction ui_keybinds_get_action(int ch) { (void)ch; return ACTION_NONE; }
static inline const KeybindDefault* ui_keybinds_get_defaults(int *out_count) { if (out_count) *out_count = 0; return NULL; }
#else
void ui_keybinds_init(void);
bool ui_keybinds_set(const char *action_name, const char *keys_str);
UIAction ui_keybinds_get_action(int ch);
const KeybindDefault* ui_keybinds_get_defaults(int *out_count);
#endif

#endif // UI_KEYBINDS_H