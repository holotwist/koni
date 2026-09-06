#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600

#include "ui_keybinds.h"
#include <ncurses.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

#define MAX_KEYS_PER_ACTION 8

typedef struct {
    const char *name;
    UIAction action;
    int keys[MAX_KEYS_PER_ACTION];
    int count;
} KeybindDef;

static KeybindDef s_bindings[ACTION_COUNT];

static const KeybindDefault s_defaults[] = {
    { ACTION_QUIT,        "quit",             "q, Q" },
    { ACTION_SEARCH,      "search",           "/" },
    { ACTION_HELP,        "help",             "h, H" },
    { ACTION_SWITCH_TAB,  "switch_tab",       "tab" },
    { ACTION_RESCAN,      "rescan",           "u, U" },
    { ACTION_SORT,        "sort",             "o, O" },
    { ACTION_UP,          "up",               "up, k" },
    { ACTION_DOWN,        "down",             "down, j" },
    { ACTION_PAGE_UP,     "page_up",          "pageup, ctrl+u" },
    { ACTION_PAGE_DOWN,   "page_down",        "pagedown, ctrl+d" },
    { ACTION_TOP,         "top",              "home" },
    { ACTION_BOTTOM,      "bottom",           "end" },
    { ACTION_SEEK_BACK,   "seek_backward",    "left" },
    { ACTION_SEEK_FWD,    "seek_forward",     "right" },
    { ACTION_ADD,         "add",              "a" },
    { ACTION_ADD_ALL,     "add_all",          "A" },
    { ACTION_CLEAR_QUEUE, "clear_queue",      "w, W" },
    { ACTION_DELETE,      "delete",           "d, delete, backspace" },
    { ACTION_PLAY_SELECT, "play_select",      "enter" },
    { ACTION_SHUFFLE,     "shuffle",          "s, S" },
    { ACTION_REPEAT,      "repeat",           "r, R" },
    { ACTION_REPLAYGAIN,  "replaygain",       "g, G" },
    { ACTION_LAYOUT,      "layout",           "l" },
    { ACTION_MUTE,        "mute",             "m, M" },
    { ACTION_PLAY_PAUSE,  "play_pause",       "space, p, P" },
    { ACTION_NEXT,        "next",             "n, N, >" },
    { ACTION_PREV,        "prev",             "b, B, <" },
    { ACTION_TAB_VIS,     "tab_vis",          "1" },
    { ACTION_TAB_LYRICS,  "tab_lyrics",       "2" },
    { ACTION_VIS_MODE,    "vis_mode",         "c, C" },
    { ACTION_FULLSCREEN,  "fullscreen",       "f" },
    { ACTION_TOGGLE_VIS,  "toggle_vis",       "v, V" },
    { ACTION_TOGGLE_LRC,  "toggle_lrc",       "y, Y" },
    { ACTION_VOL_UP,      "volume_up",        "+, =" },
    { ACTION_VOL_DOWN,    "volume_down",      "-, _" },
    { ACTION_INFO,        "track_info",       "i, I" },
    { ACTION_FAVOURITE,   "favourite",        "*, F" },
    { ACTION_LOCATE_PLAYING, "locate_playing", "L" }
};

void ui_keybinds_init(void) {
    memset(s_bindings, 0, sizeof(s_bindings));
    int count = (int)(sizeof(s_defaults) / sizeof(s_defaults[0]));
    for (int i = 0; i < count; i++) {
        s_bindings[s_defaults[i].action].name = s_defaults[i].name;
        s_bindings[s_defaults[i].action].action = s_defaults[i].action;
        ui_keybinds_set(s_defaults[i].name, s_defaults[i].default_keys);
    }
}

const KeybindDefault* ui_keybinds_get_defaults(int *out_count) {
    if (out_count) *out_count = (int)(sizeof(s_defaults) / sizeof(s_defaults[0]));
    return s_defaults;
}

static int parse_single_key(const char *s) {
    if (!s || !s[0]) return -1;

    while (*s == ' ' || *s == '\t') s++;
    char buf[32];
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *end = buf + strlen(buf) - 1;
    while (end > buf && (*end == ' ' || *end == '\t')) *end-- = '\0';

    if (strcasecmp(buf, "space") == 0) return ' ';
    if (strcasecmp(buf, "tab") == 0) return '\t';
    if (strcasecmp(buf, "enter") == 0 || strcasecmp(buf, "return") == 0) return 10;
    if (strcasecmp(buf, "esc") == 0 || strcasecmp(buf, "escape") == 0) return 27;
    if (strcasecmp(buf, "up") == 0) return KEY_UP;
    if (strcasecmp(buf, "down") == 0) return KEY_DOWN;
    if (strcasecmp(buf, "left") == 0) return KEY_LEFT;
    if (strcasecmp(buf, "right") == 0) return KEY_RIGHT;
    if (strcasecmp(buf, "pageup") == 0 || strcasecmp(buf, "pgup") == 0) return KEY_PPAGE;
    if (strcasecmp(buf, "pagedown") == 0 || strcasecmp(buf, "pgdn") == 0) return KEY_NPAGE;
    if (strcasecmp(buf, "home") == 0) return KEY_HOME;
    if (strcasecmp(buf, "end") == 0) return KEY_END;
    if (strcasecmp(buf, "delete") == 0 || strcasecmp(buf, "del") == 0) return KEY_DC;
    if (strcasecmp(buf, "backspace") == 0) return KEY_BACKSPACE;

    if ((strncasecmp(buf, "ctrl+", 5) == 0 || strncasecmp(buf, "ctrl-", 5) == 0) && buf[5] != '\0') {
        char c = buf[5];
        if (c >= 'a' && c <= 'z') return c - 'a' + 1;
        if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
    }

    if (buf[1] == '\0') {
        return (unsigned char)buf[0];
    }

    return -1;
}

bool ui_keybinds_set(const char *action_name, const char *keys_str) {
    if (!action_name || !keys_str) return false;

    KeybindDef *target = NULL;
    for (int i = 1; i < ACTION_COUNT; i++) {
        if (s_bindings[i].name && strcasecmp(s_bindings[i].name, action_name) == 0) {
            target = &s_bindings[i];
            break;
        }
    }
    if (!target) return false;

    target->count = 0;
    char *dup = strdup(keys_str);
    char *tok = strtok(dup, ",;");
    while (tok && target->count < MAX_KEYS_PER_ACTION) {
        int k = parse_single_key(tok);
        if (k != -1) {
            target->keys[target->count++] = k;
        }
        tok = strtok(NULL, ",;");
    }
    free(dup);
    return true;
}

UIAction ui_keybinds_get_action(int ch) {
    for (int a = 1; a < ACTION_COUNT; a++) {
        for (int i = 0; i < s_bindings[a].count; i++) {
            if (s_bindings[a].keys[i] == ch) {
                return s_bindings[a].action;
            }
        }
    }
    return ACTION_NONE;
}