#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600

#include "ui_input.h"
#include "ui_common.h"
#include "state.h"
#include "file_list.h"

#include <ncurses.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

bool ui_handle_input(int ch) {
    switch (ch) {
        case 'q': case 'Q':
            atomic_store(&current_cmd_atomic, CMD_QUIT);
            return false;
            
        case 'h': case 'H':
            show_help_bar = !show_help_bar;
            break;
            
        case '\t':
            current_focus = (current_focus == FOCUS_FILES) ? FOCUS_PLAYLIST : FOCUS_FILES;
            break;
            
        case KEY_UP:
            if (current_focus == FOCUS_FILES && selected_file_idx > 0) selected_file_idx--;
            else if (current_focus == FOCUS_PLAYLIST && selected_playlist_idx > 0) selected_playlist_idx--;
            break;
            
        case KEY_DOWN:
            if (current_focus == FOCUS_FILES && selected_file_idx < num_files - 1) selected_file_idx++;
            else if (current_focus == FOCUS_PLAYLIST && selected_playlist_idx < num_playlist_files - 1) selected_playlist_idx++;
            break;
            
        case KEY_LEFT:
            if (atomic_load(&play_state_atomic) != STATE_STOPPED) {
                int base = (atomic_load(&current_cmd_atomic) == CMD_SEEK) ? atomic_load(&seek_target_sec) : atomic_load(&p_current_sec);
                int t = base - 5;
                if (t < 0) t = 0;
                atomic_store(&seek_target_sec, t);
                atomic_store(&current_cmd_atomic, CMD_SEEK);
            }
            break;
            
        case KEY_RIGHT:
            if (atomic_load(&play_state_atomic) != STATE_STOPPED) {
                int base = (atomic_load(&current_cmd_atomic) == CMD_SEEK) ? atomic_load(&seek_target_sec) : atomic_load(&p_current_sec);
                int t = base + 5;
                int tot = atomic_load(&p_total_sec);
                if (t > tot) t = tot - 1;
                if (t < 0) t = 0;
                atomic_store(&seek_target_sec, t);
                atomic_store(&current_cmd_atomic, CMD_SEEK);
            }
            break;
            
        case 'a':
            if (current_focus == FOCUS_FILES && num_files > 0) {
                if (!files[selected_file_idx].is_dir) {
                    if (num_playlist_files >= playlist_capacity) {
                        playlist_capacity = playlist_capacity == 0 ? 1024 : playlist_capacity * 2;
                        playlist = realloc(playlist, sizeof(PlaylistEntry) * playlist_capacity);
                    }
                    snprintf(playlist[num_playlist_files].path, sizeof(playlist[num_playlist_files].path), "%s/%s", current_dir, files[selected_file_idx].name);
                    strncpy(playlist[num_playlist_files].name, files[selected_file_idx].name, 255);
                    playlist[num_playlist_files].display_width = files[selected_file_idx].display_width;
                    num_playlist_files++;
                }
            }
            break;
            
        case 'A':
            if (current_focus == FOCUS_FILES) {
                for (int i = 0; i < num_files; i++) {
                    if (!files[i].is_dir) {
                        if (num_playlist_files >= playlist_capacity) {
                            playlist_capacity = playlist_capacity == 0 ? 1024 : playlist_capacity * 2;
                            playlist = realloc(playlist, sizeof(PlaylistEntry) * playlist_capacity);
                        }
                        snprintf(playlist[num_playlist_files].path, sizeof(playlist[num_playlist_files].path), "%s/%s", current_dir, files[i].name);
                        strncpy(playlist[num_playlist_files].name, files[i].name, 255);
                        playlist[num_playlist_files].display_width = files[i].display_width;
                        num_playlist_files++;
                    }
                }
            }
            break;
            
        case 'W':
            if (current_focus == FOCUS_PLAYLIST) {
                num_playlist_files = 0;
                selected_playlist_idx = 0;
                playlist_scroll_offset = 0;
            }
            break;
            
        case 'd': case KEY_DC: case KEY_BACKSPACE: case 127:
            if (current_focus == FOCUS_PLAYLIST && num_playlist_files > 0) {
                pthread_mutex_lock(&state_mutex);
                for (int i = selected_playlist_idx; i < num_playlist_files - 1; i++) {
                    playlist[i] = playlist[i + 1];
                }
                num_playlist_files--;
                
                if (playing_from_playlist) {
                    if (playing_file_idx > selected_playlist_idx) playing_file_idx--;
                    history_len = 0; history_idx = -1;
                }
                
                if (selected_playlist_idx >= num_playlist_files && selected_playlist_idx > 0) {
                    selected_playlist_idx--;
                }
                pthread_mutex_unlock(&state_mutex);
            }
            break;
            
        case 10: // Enter
            if (current_focus == FOCUS_FILES) {
                if (files[selected_file_idx].is_dir) {
                    char new_path[1024]; snprintf(new_path, sizeof(new_path), "%s/%s", current_dir, files[selected_file_idx].name);
                    if (chdir(new_path) == 0) { if (getcwd(current_dir, sizeof(current_dir)) != NULL) load_directory("."); }
                } else {
                    pthread_mutex_lock(&state_mutex);
                    snprintf(playing_filepath, sizeof(playing_filepath), "%s/%s", current_dir, files[selected_file_idx].name);
                    strncpy(playing_filename, files[selected_file_idx].name, 255);
                    playing_file_idx = selected_file_idx;
                    playing_from_playlist = false;
                    history_len = 0; history_idx = -1;
                    pthread_mutex_unlock(&state_mutex);
                    atomic_store(&current_cmd_atomic, CMD_PLAY);
                }
            } else if (current_focus == FOCUS_PLAYLIST && num_playlist_files > 0) {
                pthread_mutex_lock(&state_mutex);
                strncpy(playing_filepath, playlist[selected_playlist_idx].path, sizeof(playing_filepath));
                strncpy(playing_filename, playlist[selected_playlist_idx].name, 255);
                playing_file_idx = selected_playlist_idx;
                playing_from_playlist = true;
                history_len = 0; history_idx = -1;
                pthread_mutex_unlock(&state_mutex);
                atomic_store(&current_cmd_atomic, CMD_PLAY);
            }
            break;
            
        case 's': case 'S': atomic_store(&play_mode_shuffle, !atomic_load(&play_mode_shuffle)); break;
        case 'r': case 'R': {
            int r = atomic_load(&play_mode_repeat);
            atomic_store(&play_mode_repeat, (r + 1) % 3);
            break;
        }
        case 'g': case 'G': {
            int current_rg = atomic_load(&play_mode_rgain);
            if (current_rg == 0) {
                // If meta not available, jump to Calc
                atomic_store(&play_mode_rgain, ui_cache.meta.has_track_gain ? 1 : 2);
            } else if (current_rg == 1) {
                atomic_store(&play_mode_rgain, ui_cache.meta.has_track_gain ? 2 : 0);
            } else {
                atomic_store(&play_mode_rgain, 0);
            }
            break;
        }
        case 'l': case 'L': force_vertical_layout = !force_vertical_layout; break;
        case 'm': case 'M': {
            int cur_vol = atomic_load(&volume);
            if (cur_vol > 0) {
                saved_volume = cur_vol;
                atomic_store(&volume, 0);
            } else {
                atomic_store(&volume, saved_volume > 0 ? saved_volume : 100);
            }
            break;
        }
        case ' ': case 'p': atomic_store(&current_cmd_atomic, CMD_PAUSE); break;
        case 'n': case '>': atomic_store(&current_cmd_atomic, CMD_NEXT); break;
        case 'b': case '<': atomic_store(&current_cmd_atomic, CMD_PREV); break;
        case '1': active_tab = 1; break;
        case '2': if (ui_cache.meta.lyrics != NULL) active_tab = 2; break;
        case 'c': case 'C': if (active_tab == 1) current_vis_mode = (current_vis_mode + 1) % 4; break;
        case 'f': case 'F': is_fullscreen = !is_fullscreen; break;
        case '+': case '=': if (atomic_load(&volume) < 200) atomic_fetch_add(&volume, 5); break;
        case '-': case '_': if (atomic_load(&volume) > 0) atomic_fetch_sub(&volume, 5); break;
    }
    
    return true;
}