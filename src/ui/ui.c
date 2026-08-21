#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600

#include "ui.h"
#include "ui_common.h"
#include "ui_input.h"
#include "state.h"
#include "file_list.h"
#include <ncurses.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

static void ui_update_state(void) {
    ui_frame_counter++;

    pthread_mutex_lock(&state_mutex);
    if (playing_file_idx != ui_cache.idx) {
        koni_metadata_free(&ui_cache.meta);
        memset(&ui_cache.meta, 0, sizeof(ui_cache.meta));
        memset(&ui_cache.fmt, 0, sizeof(ui_cache.fmt));
        ui_cache.filename[0] = '\0';
        
        if (!playing_from_playlist && playing_file_idx >= 0 && playing_file_idx < num_files) {
            selected_file_idx = playing_file_idx;
        } else if (playing_from_playlist && playing_file_idx >= 0 && playing_file_idx < num_playlist_files) {
            selected_playlist_idx = playing_file_idx;
        }
        ui_cache.idx = playing_file_idx;
        ui_cache.header_loaded_for_idx = -2;
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
            
            ui_cache.fmt = p_format;
            strncpy(ui_cache.filename, playing_filename, 255);
            ui_cache.header_loaded_for_idx = playing_file_idx;
            force_redraw = true;
        }
    }
    pthread_mutex_unlock(&state_mutex);

    if (active_tab == 2 && ui_cache.meta.lyrics == NULL) {
        active_tab = 1;
        force_redraw = true;
    }

    uint32_t target_play_pos = atomic_load(&p_frames_consumed);
    uint32_t srate = atomic_load(&vis_srate);
    if (srate == 0) srate = 44100;
    uint32_t nominal_advance = (srate * 15u) / 1000u;
    int32_t diff = (int32_t)(target_play_pos - ui_cache.smooth_rpos);
    
    if (abs(diff) > (int32_t)srate) ui_cache.smooth_rpos = target_play_pos;
    else ui_cache.smooth_rpos += nominal_advance + (diff / 5); 
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

    int draw_max_y = max_y;
    if (show_help_bar) draw_max_y -= 1;

    bool is_vertical = (draw_max_y * 2 > max_x) || force_vertical_layout;
    int player_h = 5;
    if (player_h > draw_max_y) player_h = draw_max_y;

    if (is_fullscreen) {
        if (draw_max_y > player_h + 2) {
            draw_vis_panel(0, 0, draw_max_y - player_h, max_x);
            draw_player_panel(draw_max_y - player_h, 0, player_h, max_x);
        } else {
            draw_vis_panel(0, 0, draw_max_y, max_x);
        }
    } else if (is_vertical) {
        int vis_h = draw_max_y * 40 / 100;
        int files_h = draw_max_y - vis_h - player_h;
        
        int play_w = max_x / 2;
        int file_w = max_x - play_w;

        draw_vis_panel(0, 0, vis_h, max_x);
        draw_playlist_panel(vis_h, 0, files_h, play_w);
        draw_files_panel(vis_h, play_w, files_h, file_w);
        draw_player_panel(vis_h + files_h, 0, player_h, max_x);
    } else {
        int left_w  = max_x * 25 / 100;
        if (left_w < 30 && max_x >= 30) left_w = 30; // Min width 30
        int right_w = max_x - left_w;

        int top_h = draw_max_y - player_h;
        int play_h = top_h / 2;
        int file_h = top_h - play_h;
        
        draw_playlist_panel(0, 0, play_h, left_w);
        draw_files_panel(play_h, 0, file_h, left_w);
        
        draw_vis_panel(0, left_w, top_h, right_w);
        draw_player_panel(top_h, 0, player_h, max_x);
    }
    
    // Bottom help bar
    if (show_help_bar) {
        mvhline(max_y - 1, 0, ' ', max_x); 
        int hx = 1;
        
        #define PRINT_HELP(key, desc) do { \
            if (hx < max_x - 15) { \
                attron(COLOR_PAIR(4) | A_BOLD); \
                mvprintw(max_y - 1, hx, "%s", key); \
                hx += strlen(key); \
                attroff(COLOR_PAIR(4) | A_BOLD); \
                attron(COLOR_PAIR(2)); \
                mvprintw(max_y - 1, hx, " %s  ", desc); \
                hx += strlen(desc) + 3; \
                attroff(COLOR_PAIR(2)); \
            } \
        } while (0)
        
        PRINT_HELP("h", "Hide");
        PRINT_HELP("space", "Pause");
        PRINT_HELP("Enter", "Play");
        PRINT_HELP("n/b", "Next/Prev");
        PRINT_HELP("a/A", "Add/Dir");
        PRINT_HELP("s", "Shuffle");
        PRINT_HELP("r", "Repeat");
        PRINT_HELP("g", "RGain");
        PRINT_HELP("l", "Layout");
        PRINT_HELP("m", "Mute");
        PRINT_HELP("c", "Vis");
        PRINT_HELP("f", "Fullscr");
        PRINT_HELP("w", "Clear Q");
        PRINT_HELP("q", "Quit");
        
        #undef PRINT_HELP
    }
    
    refresh();
    vis_needs_full_redraw = false;
}

void ui_run(bool force_colors) {
    // timeout(25) sets refresh interval to 25ms, 40FPS
    initscr(); cbreak(); noecho(); keypad(stdscr, TRUE); curs_set(0); timeout(25);
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
        
        PlayerCommand cmd = atomic_load(&current_cmd_atomic);
        if (cmd == CMD_NEXT || cmd == CMD_NEXT_AUTO || cmd == CMD_PREV) {
            bool found = false;
            int repeat_mode = atomic_load(&play_mode_repeat);
            bool shuffle = atomic_load(&play_mode_shuffle);
            
            pthread_mutex_lock(&state_mutex);
            int total_items = playing_from_playlist ? num_playlist_files : num_files;
            int next_idx = -1;
            
            if (total_items > 0) {
                if (history_len == 0 && playing_file_idx >= 0) {
                    play_history[0] = playing_file_idx;
                    history_len = 1;
                    history_idx = 0;
                }

                if (cmd == CMD_PREV) {
                    if (history_idx > 0) {
                        history_idx--;
                        next_idx = play_history[history_idx];
                    } else {
                        next_idx = playing_file_idx;
                    }
                } else {
                    if (cmd == CMD_NEXT_AUTO && repeat_mode == REPEAT_ONE) {
                        next_idx = playing_file_idx;
                    } else if (history_idx >= 0 && history_idx < history_len - 1 && cmd == CMD_NEXT) {
                        history_idx++;
                        next_idx = play_history[history_idx];
                    } else {
                        if (shuffle) {
                            next_idx = rand() % total_items;
                            if (!playing_from_playlist) {
                                int attempts = 0;
                                while(files[next_idx].is_dir && attempts < total_items) {
                                    next_idx = (next_idx + 1) % total_items;
                                    attempts++;
                                }
                                if (files[next_idx].is_dir) next_idx = -1;
                            }
                        } else {
                            next_idx = playing_file_idx + 1;
                            for (int attempts = 0; attempts < total_items; attempts++) {
                                if (next_idx >= total_items) {
                                    if (repeat_mode == REPEAT_ALL) next_idx = 0;
                                    else { next_idx = -1; break; }
                                }
                                if (playing_from_playlist || !files[next_idx].is_dir) break;
                                next_idx++;
                            }
                            if (next_idx >= 0 && !playing_from_playlist && files[next_idx].is_dir) next_idx = -1;
                        }

                        if (next_idx >= 0) {
                            if (history_len < 256) {
                                play_history[history_len++] = next_idx;
                                history_idx = history_len - 1;
                            } else {
                                memmove(play_history, play_history + 1, sizeof(int) * 255);
                                play_history[255] = next_idx;
                                history_idx = 255;
                            }
                        }
                    }
                }
            }
            
            if (next_idx >= 0 && next_idx < total_items) {
                if (playing_from_playlist) {
                    strncpy(playing_filepath, playlist[next_idx].path, sizeof(playing_filepath));
                    strncpy(playing_filename, playlist[next_idx].name, 255);
                } else {
                    snprintf(playing_filepath, sizeof(playing_filepath), "%s/%s", current_dir, files[next_idx].name);
                    strncpy(playing_filename, files[next_idx].name, 255);
                }
                playing_file_idx = next_idx;
                atomic_store(&current_cmd_atomic, CMD_PLAY);
                found = true;
            } else if (next_idx >= 0) {
                history_len = 0; history_idx = -1;
            }
            pthread_mutex_unlock(&state_mutex);
            
            if (!found) atomic_store(&current_cmd_atomic, CMD_STOP);
        }

        int ch = getch();
        if (ch != ERR) {
            force_redraw = true;
            if (!ui_handle_input(ch)) {
                running = false;
            }
        }
    }
    
    koni_metadata_free(&ui_cache.meta);
    endwin();
}