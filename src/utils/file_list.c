#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600

#include "file_list.h"
#include "state.h"
#include "codec.h"
#include "config.h"
#include "db.h"
#include "ui_common.h"
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdlib.h>

static int file_cmp(const void *a, const void *b) {
    const FileEntry *fa = (const FileEntry *)a;
    const FileEntry *fb = (const FileEntry *)b;
    if (fa->is_dir != fb->is_dir) return fb->is_dir - fa->is_dir;
    return strcasecmp(fa->name, fb->name);
}

static pthread_t meta_thread;
static bool meta_thread_running = false;
static uint64_t directory_generation = 0;

static void* metadata_worker(void* arg) {
    (void)arg;
    while (meta_thread_running) {
        char filepath[1024] = {0};
        int target_file_idx = -1;
        int target_playlist_idx = -1;
        uint64_t saved_gen = 0;
        
        pthread_mutex_lock(&state_mutex);
        saved_gen = directory_generation;
        for (int i = 0; i < num_files; i++) {
            if (!files[i].is_dir && !files[i].metadata_loaded) {
                snprintf(filepath, sizeof(filepath), "%s/%s", current_dir, files[i].name);
                target_file_idx = i;
                break;
            }
        }
        if (target_file_idx == -1) {
            for (int i = 0; i < num_playlist_files; i++) {
                if (!playlist[i].metadata_loaded) {
                    strncpy(filepath, playlist[i].path, sizeof(filepath) - 1);
                    filepath[sizeof(filepath) - 1] = '\0';
                    target_playlist_idx = i;
                    break;
                }
            }
        }
        pthread_mutex_unlock(&state_mutex);
        
        if (target_file_idx == -1 && target_playlist_idx == -1) {
            usleep(100000);
            continue;
        }
        
        KoniMetadata meta = {0};
        uint32_t duration = 0;
        struct stat st;
        bool loaded = false;

        if (stat(filepath, &st) == 0) {
            // Check DB cache first
            if (db_get_track_meta(filepath, st.st_mtime, &meta, &duration)) {
                loaded = true;
            }
        }

        if (!loaded) {
            const KoniCodecImpl* codec = koni_find_codec_by_ext(filepath);
            if (codec && codec->read_metadata) {
                codec->read_metadata(filepath, &meta, &duration);
                if (stat(filepath, &st) == 0) {
                    // Only insert into the music library DB if within a configured music folder
                    if (config_find_parent_music_dir(filepath, NULL)) {
                        db_upsert_track(filepath, st.st_mtime, &meta, duration);
                    }
                }
            }
        }
        
        pthread_mutex_lock(&state_mutex);
        if (target_file_idx != -1 && saved_gen == directory_generation && target_file_idx < num_files) {
            char current_filepath[1024];
            snprintf(current_filepath, sizeof(current_filepath), "%s/%s", current_dir, files[target_file_idx].name);
            if (strcmp(filepath, current_filepath) == 0 && !files[target_file_idx].metadata_loaded) {
                koni_metadata_free(&files[target_file_idx].meta);
                files[target_file_idx].meta = meta;
                files[target_file_idx].duration_sec = duration;
                files[target_file_idx].metadata_loaded = true;
                force_redraw = true;
            } else {
                koni_metadata_free(&meta);
            }
        } else if (target_playlist_idx != -1 && target_playlist_idx < num_playlist_files) {
            if (strcmp(filepath, playlist[target_playlist_idx].path) == 0 && !playlist[target_playlist_idx].metadata_loaded) {
                koni_metadata_free(&playlist[target_playlist_idx].meta);
                playlist[target_playlist_idx].meta = meta;
                playlist[target_playlist_idx].duration_sec = duration;
                playlist[target_playlist_idx].metadata_loaded = true;
                force_redraw = true;
            } else {
                koni_metadata_free(&meta);
            }
        } else {
            koni_metadata_free(&meta);
        }
        pthread_mutex_unlock(&state_mutex);
    }
    return NULL;
}

void file_list_init(void) {
    if (!meta_thread_running) {
        meta_thread_running = true;
        pthread_create(&meta_thread, NULL, metadata_worker, NULL);
    }
}

void file_list_shutdown(void) {
    if (meta_thread_running) {
        meta_thread_running = false;
        pthread_join(meta_thread, NULL);
    }
}

void load_directory(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) return;

    pthread_mutex_lock(&state_mutex);
    directory_generation++;

    for (int i = 0; i < num_files; i++) koni_metadata_free(&files[i].meta);
    num_files = 0;
    if (files_capacity == 0) {
        files_capacity = 1024;
        files = malloc(sizeof(FileEntry) * files_capacity);
    }
    
    strncpy(files[num_files].name, "..", sizeof(files[num_files].name) - 1);
    files[num_files].name[sizeof(files[num_files].name) - 1] = '\0';
    files[num_files].is_dir = 1;
    files[num_files].display_width = 2;
    memset(&files[num_files].meta, 0, sizeof(KoniMetadata));
    files[num_files].metadata_loaded = true;
    files[num_files].duration_sec = 0;
    num_files++;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (num_files >= files_capacity) {
            int new_cap = files_capacity * 2;
            FileEntry *new_files = realloc(files, sizeof(FileEntry) * new_cap);
            if (!new_files) break;
            files = new_files;
            files_capacity = new_cap;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            int is_dir = S_ISDIR(st.st_mode);
            char *ext = strrchr(entry->d_name, '.');
            if (is_dir || koni_is_supported_extension(ext)) {
                strncpy(files[num_files].name, entry->d_name, sizeof(files[num_files].name) - 1);
                files[num_files].name[sizeof(files[num_files].name) - 1] = '\0';
                files[num_files].is_dir = is_dir;
                files[num_files].display_width = utf8_display_width(entry->d_name);
                memset(&files[num_files].meta, 0, sizeof(KoniMetadata));
                files[num_files].metadata_loaded = is_dir ? true : false;
                files[num_files].duration_sec = 0;
                num_files++;
            }
        }
    }
    closedir(dir);
    if (num_files > 1) qsort(files + 1, (size_t)(num_files - 1), sizeof(FileEntry), file_cmp);

    selected_file_idx = 0;
    scroll_offset = 0;
    pthread_mutex_unlock(&state_mutex);
}