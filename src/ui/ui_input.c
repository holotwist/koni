#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600

#include "ui_input.h"
#include "ui_common.h"
#include "ui_keybinds.h"
#include "state.h"
#include "config.h"
#include "lyrics.h"
#include "file_list.h"

#include <ncurses.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>

#include "extension.h"
#include "ext_registry.h"

static void browser_navigate_delta(int delta) {
    int *sel = NULL;
    int max = 0;

    switch (current_browser_tab) {
        case TAB_FILES:
            sel = &selected_file_idx;
            max = num_files;
            break;
        case TAB_QUEUE:
            sel = &selected_playlist_idx;
            max = num_playlist_files;
            break;
        case TAB_MUSIC:
            sel = &selected_library_idx;
            max = num_library_tracks;
            break;
    }

    if (!sel || max <= 0) return;

    *sel += delta;
    if (*sel < 0) *sel = 0;
    if (*sel >= max) *sel = max - 1;
}

static void browser_navigate_to(bool to_bottom) {
    int *sel = NULL;
    int max = 0;

    switch (current_browser_tab) {
        case TAB_FILES:
            sel = &selected_file_idx;
            max = num_files;
            break;
        case TAB_QUEUE:
            sel = &selected_playlist_idx;
            max = num_playlist_files;
            break;
        case TAB_MUSIC:
            sel = &selected_library_idx;
            max = num_library_tracks;
            break;
    }

    if (!sel || max <= 0) return;

    *sel = to_bottom ? (max - 1) : 0;
}

static void perform_seek_relative(int delta_ms) {
    if (atomic_load(&play_state_atomic) == STATE_STOPPED) return;

    uint32_t srate = atomic_load(&vis_srate);
    if (srate == 0) srate = 44100;

    static int s_last_target_ms = -1;
    static struct timespec s_last_seek_time = {0};

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long elapsed_ms = (now.tv_sec - s_last_seek_time.tv_sec) * 1000 +
                      (now.tv_nsec - s_last_seek_time.tv_nsec) / 1000000;

    int base_ms;
    if (s_last_target_ms >= 0 && elapsed_ms < 400) {
        base_ms = s_last_target_ms;
    } else {
        base_ms = (int)(((uint64_t)atomic_load(&p_frames_consumed) * 1000ULL) / srate);
    }

    int t_ms = base_ms + delta_ms;
    int tot_ms = (int)atomic_load(&p_total_sec) * 1000;
    if (tot_ms > 0 && t_ms > tot_ms) t_ms = tot_ms - 1000;
    if (t_ms < 0) t_ms = 0;

    s_last_target_ms = t_ms;
    s_last_seek_time = now;

    atomic_store(&seek_target_ms, t_ms);
    atomic_store(&current_cmd_atomic, CMD_SEEK);
}

bool ui_handle_input(int ch) {
    if (ui_search_handle_input(ch, current_browser_tab)) {
        return true;
    }

    // Allow active extensions to process hotkeys
    if (koni_extensions_handle_key(ch)) {
        return true;
    }

    // Handle active confirmation dialog for folder selection
    if (folder_dialog.active) {
        if (ch == 'y' || ch == 'Y') {
            config_add_music_dir(folder_dialog.target_path);
            folder_dialog.active = false;
            library_scanner_start();
            return true;
        } else if (ch == 'n' || ch == 'N' || ch == 27) { // 'n' or Escape
            folder_dialog.active = false;
            return true;
        }
        return true; // Block other interactions while modal is shown
    }

    if (active_tab == 2) {
        if (!app_config.online_lyrics_asked) {
            if (ch == 'y' || ch == 'Y') {
                app_config.online_lyrics = true;
                app_config.online_lyrics_asked = true;
                config_save();
                return true;
            } else if (ch == 'n' || ch == 'N') {
                app_config.online_lyrics = false;
                app_config.online_lyrics_asked = true;
                config_save();
                lyrics_engine_fetch_async(ui_cache.meta.title, ui_cache.meta.artist, ui_cache.meta.album,
                                          atomic_load(&p_total_sec), ui_cache.filepath, ui_cache.meta.lyrics,
                                          atomic_load(&current_track_id));
                return true;
            } else if (ch != '1' && ch != 'q' && ch != 'Q') {
                return true; // Block other interactions until answered
            }
        } else if (app_config.online_lyrics && !app_config.download_online_lyrics_asked) {
            if (ch == 'y' || ch == 'Y') {
                app_config.download_online_lyrics = true;
                app_config.download_online_lyrics_asked = true;
                config_save();
                lyrics_engine_fetch_async(ui_cache.meta.title, ui_cache.meta.artist, ui_cache.meta.album,
                                          atomic_load(&p_total_sec), ui_cache.filepath, ui_cache.meta.lyrics,
                                          atomic_load(&current_track_id));
                return true;
            } else if (ch == 'n' || ch == 'N') {
                app_config.download_online_lyrics = false;
                app_config.download_online_lyrics_asked = true;
                config_save();
                lyrics_engine_fetch_async(ui_cache.meta.title, ui_cache.meta.artist, ui_cache.meta.album,
                                          atomic_load(&p_total_sec), ui_cache.filepath, ui_cache.meta.lyrics,
                                          atomic_load(&current_track_id));
                return true;
            } else if (ch != '1' && ch != 'q' && ch != 'Q') {
                return true; // Block other interactions until answered
            }
        }
    }

    UIAction action = ui_keybinds_get_action(ch);

    switch (action) {
        case ACTION_QUIT:
            atomic_store(&current_cmd_atomic, CMD_QUIT);
            return false;
            
        case ACTION_SEARCH:
            ui_search_open();
            break;

        case ACTION_HELP:
            show_help_bar = !show_help_bar;
            break;
            
        case ACTION_SWITCH_TAB:
            current_browser_tab = (current_browser_tab + 1) % 3;
            break;
            
        case ACTION_RESCAN:
            library_scanner_start();
            break;

        case ACTION_SORT:
            if (current_browser_tab == TAB_MUSIC) {
                current_library_sort = (DBSortMode)((current_library_sort + 1) % DB_SORT_COUNT);
                library_reload();
                config_save();
                force_redraw = true;
            }
            break;

        case ACTION_UP:
            browser_navigate_delta(-1);
            break;
            
        case ACTION_DOWN:
            browser_navigate_delta(1);
            break;

        case ACTION_PAGE_UP:
            browser_navigate_delta(-10);
            break;

        case ACTION_PAGE_DOWN:
            browser_navigate_delta(10);
            break;

        case ACTION_TOP:
            browser_navigate_to(false);
            break;

        case ACTION_BOTTOM:
            browser_navigate_to(true);
            break;
            
        case ACTION_SEEK_BACK:
            perform_seek_relative(-5000);
            break;

        case ACTION_SEEK_FWD:
            perform_seek_relative(5000);
            break;
            
        case ACTION_ADD:
            if (current_browser_tab == TAB_FILES && num_files > 0) {
                if (!files[selected_file_idx].is_dir) {
                    pthread_mutex_lock(&state_mutex);
                    if (num_playlist_files >= playlist_capacity) {
                        playlist_capacity = playlist_capacity == 0 ? 1024 : playlist_capacity * 2;
                        playlist = realloc(playlist, sizeof(PlaylistEntry) * playlist_capacity);
                    }
                    snprintf(playlist[num_playlist_files].path, sizeof(playlist[num_playlist_files].path), "%s/%s", current_dir, files[selected_file_idx].name);
                    strncpy(playlist[num_playlist_files].name, files[selected_file_idx].name, 255);
                    playlist[num_playlist_files].display_width = files[selected_file_idx].display_width;
                    memset(&playlist[num_playlist_files].meta, 0, sizeof(KoniMetadata));
                    playlist[num_playlist_files].metadata_loaded = false;
                    playlist[num_playlist_files].duration_sec = 0;
                    num_playlist_files++;
                    pthread_mutex_unlock(&state_mutex);
                }
            } else if (current_browser_tab == TAB_MUSIC && num_library_tracks > 0) {
                pthread_mutex_lock(&state_mutex);
                if (num_playlist_files >= playlist_capacity) {
                    playlist_capacity = playlist_capacity == 0 ? 1024 : playlist_capacity * 2;
                    playlist = realloc(playlist, sizeof(PlaylistEntry) * playlist_capacity);
                }
                strncpy(playlist[num_playlist_files].path, library_tracks[selected_library_idx].path, sizeof(playlist[num_playlist_files].path));
                strncpy(playlist[num_playlist_files].name, library_tracks[selected_library_idx].name, 255);
                playlist[num_playlist_files].display_width = utf8_display_width(library_tracks[selected_library_idx].name);
                memset(&playlist[num_playlist_files].meta, 0, sizeof(KoniMetadata));
                if (library_tracks[selected_library_idx].title[0]) playlist[num_playlist_files].meta.title = strdup(library_tracks[selected_library_idx].title);
                if (library_tracks[selected_library_idx].artist[0]) playlist[num_playlist_files].meta.artist = strdup(library_tracks[selected_library_idx].artist);
                if (library_tracks[selected_library_idx].album[0]) playlist[num_playlist_files].meta.album = strdup(library_tracks[selected_library_idx].album);
                playlist[num_playlist_files].metadata_loaded = true;
                playlist[num_playlist_files].duration_sec = library_tracks[selected_library_idx].duration_sec;
                num_playlist_files++;
                pthread_mutex_unlock(&state_mutex);
            }
            break;
            
        case ACTION_ADD_ALL:
            if (current_browser_tab == TAB_FILES) {
                pthread_mutex_lock(&state_mutex);
                for (int i = 0; i < num_files; i++) {
                    if (!files[i].is_dir) {
                        if (num_playlist_files >= playlist_capacity) {
                            playlist_capacity = playlist_capacity == 0 ? 1024 : playlist_capacity * 2;
                            playlist = realloc(playlist, sizeof(PlaylistEntry) * playlist_capacity);
                        }
                        snprintf(playlist[num_playlist_files].path, sizeof(playlist[num_playlist_files].path), "%s/%s", current_dir, files[i].name);
                        strncpy(playlist[num_playlist_files].name, files[i].name, 255);
                        playlist[num_playlist_files].display_width = files[i].display_width;
                        memset(&playlist[num_playlist_files].meta, 0, sizeof(KoniMetadata));
                        playlist[num_playlist_files].metadata_loaded = false;
                        playlist[num_playlist_files].duration_sec = 0;
                        num_playlist_files++;
                    }
                }
                pthread_mutex_unlock(&state_mutex);
            } else if (current_browser_tab == TAB_MUSIC) {
                pthread_mutex_lock(&state_mutex);
                for (int i = 0; i < num_library_tracks; i++) {
                    if (num_playlist_files >= playlist_capacity) {
                        playlist_capacity = playlist_capacity == 0 ? 1024 : playlist_capacity * 2;
                        playlist = realloc(playlist, sizeof(PlaylistEntry) * playlist_capacity);
                    }
                    strncpy(playlist[num_playlist_files].path, library_tracks[i].path, sizeof(playlist[num_playlist_files].path));
                    strncpy(playlist[num_playlist_files].name, library_tracks[i].name, 255);
                    playlist[num_playlist_files].display_width = utf8_display_width(library_tracks[i].name);
                    memset(&playlist[num_playlist_files].meta, 0, sizeof(KoniMetadata));
                    if (library_tracks[i].title[0]) playlist[num_playlist_files].meta.title = strdup(library_tracks[i].title);
                    if (library_tracks[i].artist[0]) playlist[num_playlist_files].meta.artist = strdup(library_tracks[i].artist);
                    if (library_tracks[i].album[0]) playlist[num_playlist_files].meta.album = strdup(library_tracks[i].album);
                    playlist[num_playlist_files].metadata_loaded = true;
                    playlist[num_playlist_files].duration_sec = library_tracks[i].duration_sec;
                    num_playlist_files++;
                }
                pthread_mutex_unlock(&state_mutex);
            }
            break;
            
        case ACTION_CLEAR_QUEUE:
            if (current_browser_tab == TAB_QUEUE) {
                pthread_mutex_lock(&state_mutex);
                for (int i = 0; i < num_playlist_files; i++) koni_metadata_free(&playlist[i].meta);
                num_playlist_files = 0;
                selected_playlist_idx = 0;
                playlist_scroll_offset = 0;
                pthread_mutex_unlock(&state_mutex);
            }
            break;
            
        case ACTION_DELETE:
            if (current_browser_tab == TAB_QUEUE && num_playlist_files > 0) {
                pthread_mutex_lock(&state_mutex);
                koni_metadata_free(&playlist[selected_playlist_idx].meta);
                for (int i = selected_playlist_idx; i < num_playlist_files - 1; i++) {
                    playlist[i] = playlist[i + 1];
                }
                num_playlist_files--;
                
                if (current_play_source == SOURCE_QUEUE) {
                    if (playing_file_idx > selected_playlist_idx) playing_file_idx--;
                    history_len = 0; history_idx = -1;
                }
                
                if (selected_playlist_idx >= num_playlist_files && selected_playlist_idx > 0) {
                    selected_playlist_idx--;
                }
                pthread_mutex_unlock(&state_mutex);
            }
            break;
            
        case ACTION_PLAY_SELECT:
            if (current_browser_tab == TAB_FILES) {
                if (files[selected_file_idx].is_dir) {
                    char new_path[1024]; snprintf(new_path, sizeof(new_path), "%s/%s", current_dir, files[selected_file_idx].name);
                    if (chdir(new_path) == 0) { if (getcwd(current_dir, sizeof(current_dir)) != NULL) load_directory("."); }
                } else {
                    pthread_mutex_lock(&state_mutex);
                    for (int i = 0; i < active_folder.count; i++) free(active_folder.file_names[i]);
                    free(active_folder.file_names);
                    active_folder.file_names = NULL;
                    active_folder.count = 0;
                    strncpy(active_folder.dir, current_dir, sizeof(active_folder.dir) - 1);

                    int active_idx = 0;
                    int playable_count = 0;
                    for (int i = 0; i < num_files; i++) {
                        if (!files[i].is_dir) playable_count++;
                    }

                    if (playable_count > 0) {
                        active_folder.file_names = malloc(sizeof(char*) * playable_count);
                        int w_idx = 0;
                        for (int i = 0; i < num_files; i++) {
                            if (!files[i].is_dir) {
                                active_folder.file_names[w_idx] = strdup(files[i].name);
                                if (i == selected_file_idx) active_idx = w_idx;
                                w_idx++;
                            }
                        }
                        active_folder.count = w_idx;
                    }

                    snprintf(playing_filepath, sizeof(playing_filepath), "%s/%s", current_dir, files[selected_file_idx].name);
                    strncpy(playing_filename, files[selected_file_idx].name, 255);
                    playing_file_idx = active_idx;
                    current_play_source = SOURCE_FILES;
                    base_play_source = SOURCE_NONE;
                    base_playing_idx = -1;
                    history_len = 0; history_idx = -1;
                    pthread_mutex_unlock(&state_mutex);
                    atomic_store(&current_cmd_atomic, CMD_PLAY);
                }
            } else if (current_browser_tab == TAB_QUEUE && num_playlist_files > 0) {
                pthread_mutex_lock(&state_mutex);
                strncpy(playing_filepath, playlist[selected_playlist_idx].path, sizeof(playing_filepath));
                strncpy(playing_filename, playlist[selected_playlist_idx].name, 255);
                playing_file_idx = selected_playlist_idx;
                current_play_source = SOURCE_QUEUE;
                history_len = 0; history_idx = -1;
                pthread_mutex_unlock(&state_mutex);
                atomic_store(&current_cmd_atomic, CMD_PLAY);
            } else if (current_browser_tab == TAB_MUSIC && num_library_tracks > 0) {
                pthread_mutex_lock(&state_mutex);
                strncpy(playing_filepath, library_tracks[selected_library_idx].path, sizeof(playing_filepath));
                strncpy(playing_filename, library_tracks[selected_library_idx].name, 255);
                playing_file_idx = selected_library_idx;
                current_play_source = SOURCE_LIBRARY;
                history_len = 0; history_idx = -1;
                pthread_mutex_unlock(&state_mutex);
                atomic_store(&current_cmd_atomic, CMD_PLAY);
            }
            break;
            
        case ACTION_SHUFFLE:
            if (current_browser_tab == TAB_FILES && num_files > 0 && files[selected_file_idx].is_dir) {
                if (strcmp(files[selected_file_idx].name, "..") == 0) break;
                char target_path[1024];
                snprintf(target_path, sizeof(target_path), "%s/%s", current_dir, files[selected_file_idx].name);

                if (config_is_music_dir(target_path)) {
                    config_remove_music_dir(target_path);
                    library_scanner_start();
                } else {
                    char *parent = NULL;
                    if (config_find_parent_music_dir(target_path, &parent)) {
                        folder_dialog.active = true;
                        snprintf(folder_dialog.message, sizeof(folder_dialog.message),
                                 "Parent '%.40s' is already scanned. Add anyway? (y/n)", parent ? parent : "");
                        strncpy(folder_dialog.target_path, target_path, sizeof(folder_dialog.target_path) - 1);
                        free(parent);
                    } else {
                        config_add_music_dir(target_path);
                        library_scanner_start();
                    }
                }
            } else {
                atomic_store(&play_mode_shuffle, !atomic_load(&play_mode_shuffle));
            }
            break;

        case ACTION_REPEAT: {
            int r = atomic_load(&play_mode_repeat);
            atomic_store(&play_mode_repeat, (r + 1) % 3);
            break;
        }

        case ACTION_REPLAYGAIN: {
            int current_rg = atomic_load(&play_mode_rgain);
            if (current_rg == 0) {
                atomic_store(&play_mode_rgain, ui_cache.meta.has_track_gain ? 1 : 2);
            } else if (current_rg == 1) {
                atomic_store(&play_mode_rgain, ui_cache.meta.has_track_gain ? 2 : 0);
            } else {
                atomic_store(&play_mode_rgain, 0);
            }
            break;
        }

        case ACTION_LAYOUT:
            force_vertical_layout = !force_vertical_layout;
            break;

        case ACTION_MUTE: {
            int cur_vol = atomic_load(&volume);
            if (cur_vol > 0) {
                saved_volume = cur_vol;
                atomic_store(&volume, 0);
            } else {
                atomic_store(&volume, saved_volume > 0 ? saved_volume : 100);
            }
            break;
        }

        case ACTION_PLAY_PAUSE:
            atomic_store(&current_cmd_atomic, CMD_PAUSE);
            break;

        case ACTION_NEXT:
            atomic_store(&current_cmd_atomic, CMD_NEXT);
            break;

        case ACTION_PREV:
            atomic_store(&current_cmd_atomic, CMD_PREV);
            break;

        case ACTION_TAB_VIS:
            active_tab = 1;
            break;

        case ACTION_TAB_LYRICS:
            active_tab = 2;
            break;

        case ACTION_VIS_MODE:
            if (active_tab == 1) current_vis_mode = (current_vis_mode + 1) % 4;
            break;

        case ACTION_FULLSCREEN:
            is_fullscreen = !is_fullscreen;
            break;

        case ACTION_TOGGLE_VIS:
            show_visualizer = !show_visualizer;
            break;

        case ACTION_TOGGLE_LRC:
            show_lrc_overlay = !show_lrc_overlay;
            break;

        case ACTION_VOL_UP:
            if (atomic_load(&volume) < 200) atomic_fetch_add(&volume, 5);
            break;

        case ACTION_VOL_DOWN:
            if (atomic_load(&volume) > 0) atomic_fetch_sub(&volume, 5);
            break;

        default: {
            // Dynamically match any active extension tab shortcut ('3', '4', '5'...)
            ExtTabDescriptor *ext_tabs[8];
            int ext_tab_count = koni_extensions_get_active_tabs(ext_tabs, NULL, 8);
            for (int t = 0; t < ext_tab_count; t++) {
                if (ext_tabs[t]->shortcut_key != '\0' && ch == ext_tabs[t]->shortcut_key) {
                    active_tab = ext_tabs[t]->tab_id;
                    force_redraw = true;
                    return true;
                }
            }
            break;
        }
    }

    return true;
}