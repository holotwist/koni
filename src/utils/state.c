#include "state.h"
#include "ui_common.h"

pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

char current_dir[1024] = ".";
FileEntry *files = NULL;
int num_files = 0;
int files_capacity = 0;
int selected_file_idx = 0;
int scroll_offset = 0;

PlaylistEntry *playlist = NULL;
int num_playlist_files = 0;
int playlist_capacity = 0;
int selected_playlist_idx = 0;
int playlist_scroll_offset = 0;

DBTrack *library_tracks = NULL;
int num_library_tracks = 0;
int selected_library_idx = 0;
int library_scroll_offset = 0;
DBSortMode current_library_sort = DB_SORT_TITLE;

int selected_playlist_browser_idx = 0;
int playlist_browser_scroll_offset = 0;
bool playlist_in_drilldown = false;
char active_playlist_name[128] = "";
int selected_playlist_track_idx = 0;
int playlist_track_scroll_offset = 0;

ActiveFolderContext active_folder = { .dir = "", .file_names = NULL, .count = 0 };
PlaybackSource base_play_source = SOURCE_NONE;
int base_playing_idx = -1;

FolderDialog folder_dialog = {0};
BrowserTab current_browser_tab = TAB_MUSIC;
PlaybackSource current_play_source = SOURCE_NONE;

void library_reload(void) {
    pthread_mutex_lock(&state_mutex);
    if (library_tracks) {
        db_free_tracks(library_tracks, num_library_tracks);
        library_tracks = NULL;
    }
    num_library_tracks = db_load_all_tracks(&library_tracks, current_library_sort);
    if (selected_library_idx >= num_library_tracks) selected_library_idx = (num_library_tracks > 0) ? num_library_tracks - 1 : 0;
    pthread_mutex_unlock(&state_mutex);
}

char playing_filepath[1024] = "";
char playing_filename[256] = "<Empty>";
int playing_file_idx = -1;

const KoniCodecImpl *active_codec = NULL;
KoniDecoder *active_decoder = NULL;

KoniAudioFormat p_format = {0};
KoniMetadata p_metadata = {0};

atomic_int header_ready_for_idx = -1;
atomic_int play_state_atomic = STATE_STOPPED;
atomic_int current_cmd_atomic = CMD_NONE;
atomic_int current_track_id = 0;
atomic_int volume = 100;
atomic_int seek_target_ms = -1;
atomic_int play_mode_shuffle = 0;
atomic_int play_mode_repeat = 0; // 0=Off, 1=All, 2=One

int play_history[256] = {0};
int history_len = 0;
int history_idx = -1;

atomic_uint p_current_sec = 0;
atomic_uint p_total_sec = 0;

int active_tab = 1;
int current_vis_mode = 0;
bool is_fullscreen = false;
bool force_redraw = true;

bool show_help_bar = false;
bool force_vertical_layout = false;
bool show_visualizer = true;
bool show_lrc_overlay = false;
int saved_volume = 100;;
atomic_int play_mode_rgain = 0;

float vis_ring_l[VIS_BUF_SIZE] = {0};
float vis_ring_r[VIS_BUF_SIZE] = {0};
atomic_uint vis_wpos = 0;
atomic_uint vis_srate = 44100;
atomic_uint p_frames_consumed = 0;
atomic_ullong p_hw_frames_played = 0;
atomic_ullong p_track_hw_start = 0;

// Create directory recursively if it doesn't exist
static void ensure_config_dir(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    char *p = NULL;
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static void get_state_path(char *buf, size_t size) {
    const char *home = getenv("HOME");
    if (home) {
        snprintf(buf, size, "%s/.config/koni/state", home);
    } else {
        buf[0] = '\0';
    }
}

static void load_playlist_queue(void) {
    char path[1024];
    const char *home = getenv("HOME");
    if (home) snprintf(path, sizeof(path), "%s/.config/koni/queue.m3u", home);
    else return;

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] && line[0] != '#') {
            if (num_playlist_files >= playlist_capacity) {
                playlist_capacity = playlist_capacity == 0 ? 1024 : playlist_capacity * 2;
                playlist = realloc(playlist, sizeof(PlaylistEntry) * playlist_capacity);
            }
            strncpy(playlist[num_playlist_files].path, line, sizeof(playlist[num_playlist_files].path));
            const char *name = strrchr(line, '/');
            name = name ? name + 1 : line;
            strncpy(playlist[num_playlist_files].name, name, 255);
            playlist[num_playlist_files].display_width = utf8_display_width(name);
            memset(&playlist[num_playlist_files].meta, 0, sizeof(KoniMetadata));
            playlist[num_playlist_files].metadata_loaded = false;
            playlist[num_playlist_files].duration_sec = 0;
            num_playlist_files++;
        }
    }
    fclose(f);
}

static void save_playlist_queue(void) {
    char path[1024];
    const char *home = getenv("HOME");
    if (home) snprintf(path, sizeof(path), "%s/.config/koni/queue.m3u", home);
    else return;

    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < num_playlist_files; i++) {
        fprintf(f, "%s\n", playlist[i].path);
    }
    fclose(f);
}

void load_state(void) {
    load_playlist_queue();
    char path[1024];
    get_state_path(path, sizeof(path));
    if (path[0] == '\0') return;

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *saveptr = NULL;
        char *key = strtok_r(line, "=", &saveptr);
        char *val = strtok_r(NULL, "\n", &saveptr);
        if (key && val) {
            if (strcmp(key, "current_dir") == 0) strncpy(current_dir, val, sizeof(current_dir)-1);
            else if (strcmp(key, "volume") == 0) atomic_store(&volume, atoi(val));
            else if (strcmp(key, "shuffle") == 0) atomic_store(&play_mode_shuffle, atoi(val));
            else if (strcmp(key, "repeat") == 0) atomic_store(&play_mode_repeat, atoi(val));
            else if (strcmp(key, "rgain") == 0) atomic_store(&play_mode_rgain, atoi(val));
            else if (strcmp(key, "vis_mode") == 0) current_vis_mode = atoi(val);
            else if (strcmp(key, "library_sort") == 0) current_library_sort = (DBSortMode)atoi(val);
            else if (strcmp(key, "layout") == 0) force_vertical_layout = atoi(val) ? true : false;
            else if (strcmp(key, "show_visualizer") == 0) show_visualizer = atoi(val) ? true : false;
            else if (strcmp(key, "show_lrc_overlay") == 0) show_lrc_overlay = atoi(val) ? true : false;
            else if (strcmp(key, "active_tab") == 0) active_tab = atoi(val);
            else if (strcmp(key, "browser_tab") == 0) current_browser_tab = (BrowserTab)atoi(val);
        }
    }
    fclose(f);
}

void save_state(void) {
    char path[1024];
    get_state_path(path, sizeof(path));
    if (path[0] == '\0') return;

    char dir_path[1024];
    const char *home = getenv("HOME");
    if (home) {
        snprintf(dir_path, sizeof(dir_path), "%s/.config/koni/", home);
        ensure_config_dir(dir_path);
    }

    FILE *f = fopen(path, "w");
    if (!f) return;

    fprintf(f, "current_dir=%s\n", current_dir);
    fprintf(f, "volume=%d\n", atomic_load(&volume));
    fprintf(f, "shuffle=%d\n", atomic_load(&play_mode_shuffle));
    fprintf(f, "repeat=%d\n", atomic_load(&play_mode_repeat));
    fprintf(f, "rgain=%d\n", atomic_load(&play_mode_rgain));
    fprintf(f, "vis_mode=%d\n", current_vis_mode);
    fprintf(f, "library_sort=%d\n", (int)current_library_sort);
    fprintf(f, "layout=%d\n", force_vertical_layout ? 1 : 0);
    fprintf(f, "show_visualizer=%d\n", show_visualizer ? 1 : 0);
    fprintf(f, "show_lrc_overlay=%d\n", show_lrc_overlay ? 1 : 0);
    fprintf(f, "active_tab=%d\n", active_tab);
    fprintf(f, "browser_tab=%d\n", (int)current_browser_tab);
    
    fclose(f);
    save_playlist_queue();
}

bool player_peek_next_track(char *out_path, size_t path_sz, char *out_name, size_t name_sz, int *out_idx) {
    bool found = false;
    int repeat_mode = atomic_load(&play_mode_repeat);
    bool shuffle = atomic_load(&play_mode_shuffle);

    pthread_mutex_lock(&state_mutex);

    if (num_playlist_files > 0) {
        if (current_play_source == SOURCE_QUEUE) {
            if (repeat_mode == REPEAT_ONE) {
                if (out_path) strncpy(out_path, playlist[0].path, path_sz - 1);
                if (out_name) strncpy(out_name, playlist[0].name, name_sz - 1);
                if (out_idx) *out_idx = 0;
                found = true;
            } else if (num_playlist_files > 1) {
                if (out_path) strncpy(out_path, playlist[1].path, path_sz - 1);
                if (out_name) strncpy(out_name, playlist[1].name, name_sz - 1);
                if (out_idx) *out_idx = 1;
                found = true;
            }
        } else {
            if (out_path) strncpy(out_path, playlist[0].path, path_sz - 1);
            if (out_name) strncpy(out_name, playlist[0].name, name_sz - 1);
            if (out_idx) *out_idx = 0;
            found = true;
        }
    }

    if (!found) {
        PlaybackSource src = (current_play_source == SOURCE_QUEUE) ? base_play_source : current_play_source;
        int current_idx = (current_play_source == SOURCE_QUEUE) ? base_playing_idx : playing_file_idx;
        int total_items = 0;
        if (src == SOURCE_LIBRARY) total_items = num_library_tracks;
        else if (src == SOURCE_FILES) total_items = active_folder.count;
        else if (src == SOURCE_QUEUE) total_items = num_playlist_files;

        if (total_items > 0) {
            int next_idx = -1;
            if (repeat_mode == REPEAT_ONE) {
                next_idx = current_idx;
            } else if (shuffle) {
                next_idx = rand() % total_items;
            } else {
                next_idx = current_idx + 1;
                if (next_idx >= total_items) {
                    if (repeat_mode == REPEAT_ALL) next_idx = 0;
                    else next_idx = -1;
                }
            }

            if (next_idx >= 0 && next_idx < total_items) {
                if (src == SOURCE_LIBRARY) {
                    if (out_path) strncpy(out_path, library_tracks[next_idx].path, path_sz - 1);
                    if (out_name) strncpy(out_name, library_tracks[next_idx].name, name_sz - 1);
                } else if (src == SOURCE_FILES) {
                    if (out_path) snprintf(out_path, path_sz, "%s/%s", active_folder.dir, active_folder.file_names[next_idx]);
                    if (out_name) strncpy(out_name, active_folder.file_names[next_idx], name_sz - 1);
                } else if (src == SOURCE_QUEUE) {
                    if (out_path) strncpy(out_path, playlist[next_idx].path, path_sz - 1);
                    if (out_name) strncpy(out_name, playlist[next_idx].name, name_sz - 1);
                }
                if (out_idx) *out_idx = next_idx;
                found = true;
            }
        }
    }

    if (out_path && path_sz > 0) out_path[path_sz - 1] = '\0';
    if (out_name && name_sz > 0) out_name[name_sz - 1] = '\0';

    pthread_mutex_unlock(&state_mutex);
    return found;
}

bool player_advance_track(PlayerCommand cmd) {
    if (cmd != CMD_NEXT && cmd != CMD_NEXT_AUTO && cmd != CMD_PREV) return false;
    
    bool found = false;
    int repeat_mode = atomic_load(&play_mode_repeat);
    bool shuffle = atomic_load(&play_mode_shuffle);
    
    pthread_mutex_lock(&state_mutex);

    // If a track just finished and was from the queue, pop it out of the queue
    if (current_play_source == SOURCE_QUEUE && cmd == CMD_NEXT_AUTO) {
        if (repeat_mode != REPEAT_ONE && num_playlist_files > 0) {
            koni_metadata_free(&playlist[0].meta);
            for (int i = 0; i < num_playlist_files - 1; i++) {
                playlist[i] = playlist[i + 1];
            }
            num_playlist_files--;
            if (selected_playlist_idx >= num_playlist_files && selected_playlist_idx > 0) {
                selected_playlist_idx--;
            }
        }
    }

    // If there are items in Queue, switch source to Queue immediately
    if (num_playlist_files > 0 && (current_play_source != SOURCE_QUEUE || cmd == CMD_NEXT_AUTO)) {
        if (current_play_source != SOURCE_QUEUE && current_play_source != SOURCE_NONE) {
            base_play_source = current_play_source;
            base_playing_idx = playing_file_idx;
        }
        current_play_source = SOURCE_QUEUE;
        playing_file_idx = 0;

        strncpy(playing_filepath, playlist[0].path, sizeof(playing_filepath));
        strncpy(playing_filename, playlist[0].name, sizeof(playing_filename) - 1);
        pthread_mutex_unlock(&state_mutex);
        return true;
    }

    // Queue is now empty. Return to base source if available
    if (current_play_source == SOURCE_QUEUE && num_playlist_files == 0) {
        if (base_play_source != SOURCE_NONE) {
            current_play_source = base_play_source;
            playing_file_idx = base_playing_idx;
            base_play_source = SOURCE_NONE;
            base_playing_idx = -1;
        } else {
            current_play_source = SOURCE_NONE;
            playing_file_idx = -1;
            pthread_mutex_unlock(&state_mutex);
            return false;
        }
    }

    // Resolve advancing in base source
    int total_items = 0;
    if (current_play_source == SOURCE_LIBRARY) {
        total_items = num_library_tracks;
    } else if (current_play_source == SOURCE_FILES) {
        total_items = active_folder.count;
    } else if (current_play_source == SOURCE_QUEUE) {
        total_items = num_playlist_files;
    }

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
                } else {
                    next_idx = playing_file_idx + 1;
                    if (next_idx >= total_items) {
                        if (repeat_mode == REPEAT_ALL) next_idx = 0;
                        else next_idx = -1;
                    }
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
        if (current_play_source == SOURCE_LIBRARY) {
            strncpy(playing_filepath, library_tracks[next_idx].path, sizeof(playing_filepath));
            strncpy(playing_filename, library_tracks[next_idx].name, 255);
        } else if (current_play_source == SOURCE_FILES) {
            snprintf(playing_filepath, sizeof(playing_filepath), "%s/%s", active_folder.dir, active_folder.file_names[next_idx]);
            strncpy(playing_filename, active_folder.file_names[next_idx], 255);
        } else if (current_play_source == SOURCE_QUEUE) {
            strncpy(playing_filepath, playlist[next_idx].path, sizeof(playing_filepath));
            strncpy(playing_filename, playlist[next_idx].name, 255);
        }
        playing_file_idx = next_idx;
        found = true;
    } else {
        history_len = 0; 
        history_idx = -1;
    }

    pthread_mutex_unlock(&state_mutex);
    return found;
}