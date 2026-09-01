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

BrowserTab current_browser_tab = TAB_QUEUE;
PlaybackSource current_play_source = SOURCE_FILES;

void library_reload(void) {
    pthread_mutex_lock(&state_mutex);
    if (library_tracks) {
        db_free_tracks(library_tracks, num_library_tracks);
        library_tracks = NULL;
    }
    num_library_tracks = db_load_all_tracks(&library_tracks);
    if (selected_library_idx >= num_library_tracks) selected_library_idx = (num_library_tracks > 0) ? num_library_tracks - 1 : 0;
    pthread_mutex_unlock(&state_mutex);
}

char playing_filepath[1024] = "";
char playing_filename[256] = "<Empty>";
int playing_file_idx = -1;

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
        char *key = strtok(line, "=");
        char *val = strtok(NULL, "\n");
        if (key && val) {
            if (strcmp(key, "current_dir") == 0) strncpy(current_dir, val, sizeof(current_dir)-1);
            else if (strcmp(key, "volume") == 0) atomic_store(&volume, atoi(val));
            else if (strcmp(key, "shuffle") == 0) atomic_store(&play_mode_shuffle, atoi(val));
            else if (strcmp(key, "repeat") == 0) atomic_store(&play_mode_repeat, atoi(val));
            else if (strcmp(key, "rgain") == 0) atomic_store(&play_mode_rgain, atoi(val));
            else if (strcmp(key, "vis_mode") == 0) current_vis_mode = atoi(val);
            else if (strcmp(key, "layout") == 0) force_vertical_layout = atoi(val) ? true : false;
            else if (strcmp(key, "show_visualizer") == 0) show_visualizer = atoi(val) ? true : false;
            else if (strcmp(key, "show_lrc_overlay") == 0) show_lrc_overlay = atoi(val) ? true : false;
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
    fprintf(f, "layout=%d\n", force_vertical_layout ? 1 : 0);
    fprintf(f, "show_visualizer=%d\n", show_visualizer ? 1 : 0);
    fprintf(f, "show_lrc_overlay=%d\n", show_lrc_overlay ? 1 : 0);
    
    fclose(f);
    save_playlist_queue();
}

bool player_advance_track(PlayerCommand cmd) {
    if (cmd != CMD_NEXT && cmd != CMD_NEXT_AUTO && cmd != CMD_PREV) return false;
    
    bool found = false;
    int repeat_mode = atomic_load(&play_mode_repeat);
    bool shuffle = atomic_load(&play_mode_shuffle);
    
    pthread_mutex_lock(&state_mutex);
    int total_items = 0;
    if (current_play_source == SOURCE_QUEUE) total_items = num_playlist_files;
    else if (current_play_source == SOURCE_LIBRARY) total_items = num_library_tracks;
    else total_items = num_files;

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
                    if (current_play_source == SOURCE_FILES) {
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
                        if (current_play_source != SOURCE_FILES || !files[next_idx].is_dir) break;
                        next_idx++;
                    }
                    if (next_idx >= 0 && current_play_source == SOURCE_FILES && files[next_idx].is_dir) next_idx = -1;
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
        if (current_play_source == SOURCE_QUEUE) {
            strncpy(playing_filepath, playlist[next_idx].path, sizeof(playing_filepath));
            strncpy(playing_filename, playlist[next_idx].name, 255);
        } else if (current_play_source == SOURCE_LIBRARY) {
            strncpy(playing_filepath, library_tracks[next_idx].path, sizeof(playing_filepath));
            strncpy(playing_filename, library_tracks[next_idx].name, 255);
        } else {
            snprintf(playing_filepath, sizeof(playing_filepath), "%s/%s", current_dir, files[next_idx].name);
            strncpy(playing_filename, files[next_idx].name, 255);
        }
        playing_file_idx = next_idx;
        found = true;
    } else if (next_idx >= 0) {
        history_len = 0; history_idx = -1;
    }
    pthread_mutex_unlock(&state_mutex);
    
    return found;
}