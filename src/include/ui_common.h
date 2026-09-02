#ifndef UI_COMMON_H
#define UI_COMMON_H

#include <ncurses.h>
#include <stdint.h>
#include "state.h"
#include "codec.h"
#include "lyrics.h"
#include "ui_search.h"

extern unsigned long ui_frame_counter;
extern bool vis_needs_full_redraw;
extern int ui_last_selected_idx;
extern int ui_last_playlist_idx;
extern char current_lyrics_backend[32];

// Cache structs to avoid re-fetching constantly
typedef struct {
    KoniMetadata meta;
    KoniAudioFormat fmt;
    int idx;
    char filename[256];
    uint32_t smooth_rpos;
    int header_loaded_for_idx;
    char filepath[1024];
    LyricDocument* lrc_doc;
} UICache;

extern UICache ui_cache;

// Utility functions
void ui_draw_box(int y, int x, int h, int w, const char* title, int color_pair);
int utf8_display_width(const char *str);
void format_list_item(char* out_buf, size_t out_size, int max_w, const char* filename, KoniMetadata* meta, uint32_t duration_sec, bool is_dir);
int utf8_byte_offset_for_width(const char *str, int target_width);
int utf8_byte_offset_for_suffix(const char *str, int target_width);

// Component draw functions
void draw_files_panel(int y, int x, int h, int w);
void draw_queue_panel(int y, int x, int h, int w);
void draw_musiclist_panel(int y, int x, int h, int w);
void draw_vis_panel(int y, int x, int h, int w);
void draw_player_panel(int y, int x, int h, int w);
void draw_lrc_overlay(int y, int x, int h, int w);

#endif // UI_COMMON_H