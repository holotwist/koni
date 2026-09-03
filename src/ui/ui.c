#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600

#include "ui.h"
#include "ui_common.h"
#include "ui_input.h"
#include "state.h"
#include "file_list.h"
#include "lyrics.h"
#include "config.h"
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static void ui_update_state(void) {
    ui_frame_counter++;

    pthread_mutex_lock(&state_mutex);
    if (playing_file_idx != ui_cache.idx || strncmp(playing_filepath, ui_cache.filepath, sizeof(ui_cache.filepath)) != 0) {
        koni_metadata_free(&ui_cache.meta);
        memset(&ui_cache.meta, 0, sizeof(ui_cache.meta));
        memset(&ui_cache.fmt, 0, sizeof(ui_cache.fmt));
        ui_cache.filename[0] = '\0';
        
        if (current_play_source == SOURCE_FILES && playing_file_idx >= 0 && playing_file_idx < num_files) {
            selected_file_idx = playing_file_idx;
        } else if (current_play_source == SOURCE_QUEUE && playing_file_idx >= 0 && playing_file_idx < num_playlist_files) {
            selected_playlist_idx = playing_file_idx;
        } else if (current_play_source == SOURCE_LIBRARY && playing_file_idx >= 0 && playing_file_idx < num_library_tracks) {
            selected_library_idx = playing_file_idx;
        }
        ui_cache.idx = playing_file_idx;
        strncpy(ui_cache.filepath, playing_filepath, sizeof(ui_cache.filepath) - 1);
        ui_cache.filepath[sizeof(ui_cache.filepath) - 1] = '\0';
        ui_cache.header_loaded_for_idx = -2;
        if (ui_cache.lrc_doc) {
            lyric_document_free(ui_cache.lrc_doc);
            ui_cache.lrc_doc = NULL;
        }
        force_redraw = true;
    }

    if (ui_cache.header_loaded_for_idx != playing_file_idx) {
        if (atomic_load(&header_ready_for_idx) == playing_file_idx) {
            koni_metadata_free(&ui_cache.meta);
            memset(&ui_cache.meta, 0, sizeof(ui_cache.meta));
            
            if (p_metadata.title) ui_cache.meta.title = strdup(p_metadata.title);
            if (p_metadata.artist) ui_cache.meta.artist = strdup(p_metadata.artist);
            if (p_metadata.album) ui_cache.meta.album = strdup(p_metadata.album);
            if (p_metadata.lyrics) ui_cache.meta.lyrics = strdup(p_metadata.lyrics);
            
            if (ui_cache.lrc_doc) {
                lyric_document_free(ui_cache.lrc_doc);
                ui_cache.lrc_doc = NULL;
            }
            
            strncpy(current_lyrics_backend, "Searching...", sizeof(current_lyrics_backend) - 1);
            
            bool pending_questions = !app_config.online_lyrics_asked || 
                                    (app_config.online_lyrics && !app_config.download_online_lyrics_asked);
                                    
            if (!pending_questions) {
                lyrics_engine_fetch_async(ui_cache.meta.title, ui_cache.meta.artist, ui_cache.meta.album,
                                          atomic_load(&p_total_sec), ui_cache.filepath, ui_cache.meta.lyrics);
            }
            
            ui_cache.fmt = p_format;
            strncpy(ui_cache.filename, playing_filename, 255);
            ui_cache.header_loaded_for_idx = playing_file_idx;
            force_redraw = true;
        }
    }
    pthread_mutex_unlock(&state_mutex);

    uint32_t target_play_pos = atomic_load(&p_frames_consumed);
    uint32_t srate = atomic_load(&vis_srate);
    if (srate == 0) srate = 44100;
    uint32_t nominal_advance = (srate * 15u) / 1000u;
    int32_t diff = (int32_t)(target_play_pos - ui_cache.smooth_rpos);
    
    if (abs(diff) > (int32_t)srate) ui_cache.smooth_rpos = target_play_pos;
    else ui_cache.smooth_rpos += nominal_advance + (diff / 5); 
}

typedef struct {
    const char *key;
    const char *desc;
} HelpItem;

static const HelpItem help_items[] = {
    {"/", "Search"},
    {"h", "Hide"},
    {"Tab", "Tabs"},
    {"space", "Pause"},
    {"Enter", "Play"},
    {"n/b", "Next/Prev"},
    {"a/A", "Add/All"},
    {"u", "Rescan"},
    {"o", "Sort Music"},
    {"s", "Shuffle"},
    {"r", "Repeat"},
    {"g", "RGain"},
    {"l", "Layout"},
    {"m", "Mute"},
    {"c", "Vis"},
    {"v", "Vis Toggle"},
    {"f", "Fullscr"},
    {"w", "Clear Q"},
    {"y", "LRC Ovl"},
    {"q", "Quit"}
};

static int get_help_bar_lines(int max_x) {
    if (!show_help_bar || max_x < 10) return 0;
    int lines = 1;
    int hx = 1;
    size_t count = sizeof(help_items) / sizeof(help_items[0]);

    for (size_t i = 0; i < count; i++) {
        int item_len = (int)strlen(help_items[i].key) + (int)strlen(help_items[i].desc) + 3;
        if (hx + item_len >= max_x - 1) {
            lines++;
            hx = 1;
        }
        hx += item_len;
    }
    return lines;
}

static void ui_loop(void) {
    if (force_redraw) {
        erase();
        force_redraw = false;
        vis_needs_full_redraw = true;
    }

    ui_update_state();

    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    int help_lines = get_help_bar_lines(max_x);
    int draw_max_y = max_y - help_lines;

    bool is_vertical = (draw_max_y * 2 > max_x) || force_vertical_layout;
    int player_h = 5;
    if (player_h > draw_max_y) player_h = draw_max_y;
    
    int lrc_h = (show_lrc_overlay && ui_cache.lrc_doc && active_tab != 2) ? 3 : 0;
    if (draw_max_y - player_h - lrc_h < 5) lrc_h = 0; // Fallback for tiny screens
    
    int top_h = draw_max_y - player_h - lrc_h;

    // Single left/main browser block
    int browser_w = show_visualizer ? (max_x * 40 / 100) : max_x;
    if (show_visualizer && browser_w < 35 && max_x >= 70) browser_w = 35;
    int vis_w = max_x - browser_w;

    if (is_fullscreen) {
        if (show_visualizer && top_h > 0) draw_vis_panel(0, 0, top_h, max_x);
    } else if (is_vertical) {
        int vis_h = show_visualizer ? (top_h * 40 / 100) : 0;
        int browser_h = top_h - vis_h;

        if (show_visualizer) draw_vis_panel(0, 0, vis_h, max_x);

        // Draw browser box with tabs header
        ui_draw_box(vis_h, 0, browser_h, max_x, NULL, 1);
        mvprintw(vis_h, 2, " ");
        attron(current_browser_tab == TAB_QUEUE ? A_REVERSE : A_NORMAL);
        printw("Queue (%d)", num_playlist_files);
        attroff(current_browser_tab == TAB_QUEUE ? A_REVERSE : A_NORMAL);
        printw(" ");

        attron(current_browser_tab == TAB_MUSIC ? A_REVERSE : A_NORMAL);
        printw("Music (%d)", num_library_tracks);
        attroff(current_browser_tab == TAB_MUSIC ? A_REVERSE : A_NORMAL);
        printw(" ");

        attron(current_browser_tab == TAB_FILES ? A_REVERSE : A_NORMAL);
        printw("Files");
        attroff(current_browser_tab == TAB_FILES ? A_REVERSE : A_NORMAL);
        printw(" ");

        if (current_browser_tab == TAB_QUEUE) draw_queue_panel(vis_h, 0, browser_h, max_x);
        else if (current_browser_tab == TAB_MUSIC) draw_musiclist_panel(vis_h, 0, browser_h, max_x);
        else draw_files_panel(vis_h, 0, browser_h, max_x);
    } else {
        // Horizontal, Left Single Browser, Right Visualizer/Lyrics
        ui_draw_box(0, 0, top_h, browser_w, NULL, 1);
        mvprintw(0, 2, " ");
        attron(current_browser_tab == TAB_QUEUE ? A_REVERSE : A_NORMAL);
        printw("Queue (%d)", num_playlist_files);
        attroff(current_browser_tab == TAB_QUEUE ? A_REVERSE : A_NORMAL);
        printw(" ");

        attron(current_browser_tab == TAB_MUSIC ? A_REVERSE : A_NORMAL);
        printw("Music (%d)", num_library_tracks);
        attroff(current_browser_tab == TAB_MUSIC ? A_REVERSE : A_NORMAL);
        printw(" ");

        attron(current_browser_tab == TAB_FILES ? A_REVERSE : A_NORMAL);
        printw("Files");
        attroff(current_browser_tab == TAB_FILES ? A_REVERSE : A_NORMAL);
        printw(" ");

        if (current_browser_tab == TAB_QUEUE) draw_queue_panel(0, 0, top_h, browser_w);
        else if (current_browser_tab == TAB_MUSIC) draw_musiclist_panel(0, 0, top_h, browser_w);
        else draw_files_panel(0, 0, top_h, browser_w);

        if (show_visualizer && vis_w > 0) {
            draw_vis_panel(0, browser_w, top_h, vis_w);
        }
    }
    
    if (lrc_h > 0) draw_lrc_overlay(top_h, 0, lrc_h, max_x);
    draw_player_panel(top_h + lrc_h, 0, player_h, max_x);
    
    // Bottom help bar
    if (show_help_bar && help_lines > 0) {
        int start_y = max_y - help_lines;
        for (int row = 0; row < help_lines; row++) {
            mvhline(start_y + row, 0, ' ', max_x);
        }

        int curr_y = start_y;
        int hx = 1;
        size_t count = sizeof(help_items) / sizeof(help_items[0]);

        for (size_t i = 0; i < count; i++) {
            int item_len = (int)strlen(help_items[i].key) + (int)strlen(help_items[i].desc) + 3;
            if (hx + item_len >= max_x - 1 && hx > 1) {
                curr_y++;
                hx = 1;
            }
            if (curr_y < max_y) {
                attron(COLOR_PAIR(4) | A_BOLD);
                mvprintw(curr_y, hx, "%s", help_items[i].key);
                hx += strlen(help_items[i].key);
                attroff(COLOR_PAIR(4) | A_BOLD);

                attron(COLOR_PAIR(2));
                mvprintw(curr_y, hx, " %s  ", help_items[i].desc);
                hx += strlen(help_items[i].desc) + 3;
                attroff(COLOR_PAIR(2));
            }
        }
    }
    
    // Overlay safeguard confirmation dialog if active
    if (folder_dialog.active) {
        int dw = 60;
        int dh = 6;
        int dx = (max_x - dw) / 2;
        int dy = (draw_max_y - dh) / 2;
        if (dx < 0) dx = 0;
        if (dy < 0) dy = 0;

        ui_draw_box(dy, dx, dh, dw, "Folder Selection Warning", 4);
        for (int row = 1; row < dh - 1; row++) mvhline(dy + row, dx + 1, ' ', dw - 2);

        attron(COLOR_PAIR(2));
        mvprintw(dy + 2, dx + 3, "%s", folder_dialog.message);
        attroff(COLOR_PAIR(2));

        attron(A_BOLD | COLOR_PAIR(4));
        mvprintw(dy + 3, dx + 3, "[Y] Confirm / [N] Cancel");
        attroff(A_BOLD | COLOR_PAIR(4));
    }

    refresh();
    vis_needs_full_redraw = false;
}

void ui_run(bool force_colors) {
    // timeout(25) sets refresh interval to 25ms, 40FPS
    initscr(); cbreak(); noecho(); keypad(stdscr, TRUE); curs_set(0); timeout(25);
    set_escdelay(25); // Eliminates delay when pressing standalone ESC
    start_color(); use_default_colors();
    
    if (force_colors && COLORS >= 256) {
        init_pair(1, 51,  -1); // Cyan
        init_pair(2, 15,  -1); // White
        init_pair(3, 46,  -1); // Green
        init_pair(4, 226, -1); // Yellow
        init_pair(5, 201, -1); // Magenta
        init_pair(6, 39,  -1); // Blue
        init_pair(7, 51,  -1); 
        init_pair(8, 46,  -1); 
        init_pair(9, 226, -1); 
        init_pair(10, 196,-1); // Red
    } else {
        init_pair(1, COLOR_CYAN,    -1); 
        init_pair(2, COLOR_WHITE,   -1); 
        init_pair(3, COLOR_GREEN,   -1);
        init_pair(4, COLOR_YELLOW,  -1); 
        init_pair(5, COLOR_MAGENTA, -1); 
        init_pair(6, COLOR_BLUE,    -1);
        init_pair(7, COLOR_CYAN,    -1); 
        init_pair(8, COLOR_GREEN,   -1); 
        init_pair(9, COLOR_YELLOW,  -1);
        init_pair(10, COLOR_RED,    -1);
    }

    srand(time(NULL));

    bool running = true;
    while (running) {
        ui_loop();

        int ch = getch();
        if (ch != ERR) {
            force_redraw = true;
            if (!ui_handle_input(ch)) {
                running = false;
            }
        }
    }
    
    koni_metadata_free(&ui_cache.meta);
    if (ui_cache.lrc_doc) { lyric_document_free(ui_cache.lrc_doc); ui_cache.lrc_doc = NULL; }
    endwin();
}