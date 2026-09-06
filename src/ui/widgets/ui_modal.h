#ifndef UI_MODAL_H
#define UI_MODAL_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    MODAL_NONE = 0,
    MODAL_TRACK_ACTIONS,
    MODAL_ADD_TO_PLAYLIST,
    MODAL_TEXT_INPUT,
    MODAL_TRACK_DETAILS,
    MODAL_PLAYLIST_ACTIONS
} ModalType;

typedef struct {
    char path[1024];
    char title[256];
    char artist[256];
    char album[256];
    uint32_t duration_sec;
    bool in_playlist;
    char playlist_name[128];
    int playlist_track_idx;
} ModalTrackContext;

void ui_modal_init(void);
bool ui_modal_is_active(void);
void ui_modal_close(void);

// Modal openers
void ui_modal_open_track_actions(const ModalTrackContext *ctx);
void ui_modal_open_playlist_actions(const char *playlist_name);

// Input handling & rendering
bool ui_modal_handle_input(int ch);
void ui_modal_render(int max_y, int max_x);

#endif // UI_MODAL_H