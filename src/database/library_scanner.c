#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600

#include "db.h"
#include "config.h"
#include "codec.h"
#include "state.h"
#include "ui_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>
#include <unistd.h>

static pthread_t scanner_thread;
static bool scanner_running = false;
static bool scanner_in_progress = false;

static void scan_directory_recursive(const char *dir_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (!scanner_running) break;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                scan_directory_recursive(full_path);
            } else {
                char *ext = strrchr(entry->d_name, '.');
                if (ext && koni_is_supported_extension(ext)) {
                    time_t db_mtime = 0;
                    if (!db_get_track_mtime(full_path, &db_mtime) || db_mtime != st.st_mtime) {
                        KoniMetadata meta = {0};
                        uint32_t duration = 0;
                        const KoniCodecImpl *codec = koni_find_codec_by_ext(full_path);
                        if (codec && codec->read_metadata) {
                            codec->read_metadata(full_path, &meta, &duration);
                        }
                        db_upsert_track(full_path, st.st_mtime, &meta, duration);
                        koni_metadata_free(&meta);
                    }
                }
            }
        }
    }
    closedir(dir);
}

static void* scanner_worker(void *arg) {
    (void)arg;
    scanner_in_progress = true;

    // Prune deleted tracks first
    db_prune_missing_files();

    char dirs_copy[2048];
    strncpy(dirs_copy, app_config.music_directories, sizeof(dirs_copy) - 1);
    dirs_copy[sizeof(dirs_copy) - 1] = '\0';

    char *token = strtok(dirs_copy, ",;");
    while (token && scanner_running) {
        while (*token == ' ' || *token == '\t') token++;
        char *end = token + strlen(token) - 1;
        while (end > token && (*end == ' ' || *end == '\t')) *end-- = '\0';

        if (token[0] != '\0') {
            char parent_dir[1024];
            // If a superior folder is already configured, skip scanning this subfolder directly
            if (!config_find_parent_music_dir(token, parent_dir, sizeof(parent_dir))) {
                scan_directory_recursive(token);
            }
        }
        token = strtok(NULL, ",;");
    }

    scanner_in_progress = false;
    library_reload();
    force_redraw = true;
    return NULL;
}

void library_scanner_start(void) {
    if (scanner_in_progress) return;
    scanner_running = true;
    pthread_create(&scanner_thread, NULL, scanner_worker, NULL);
    pthread_detach(scanner_thread);
}

void library_scanner_shutdown(void) {
    scanner_running = false;
}

bool library_scanner_is_running(void) {
    return scanner_in_progress;
}