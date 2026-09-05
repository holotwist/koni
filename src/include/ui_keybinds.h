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
    ACTION_COUNT
} UIAction;

typedef struct {
    UIAction action;
    const char *name;
    const char *default_keys;
} KeybindDefault;

void ui_keybinds_init(void);
bool ui_keybinds_set(const char *action_name, const char *keys_str);
UIAction ui_keybinds_get_action(int ch);
const KeybindDefault* ui_keybinds_get_defaults(int *out_count);

#endif // UI_KEYBINDS_H