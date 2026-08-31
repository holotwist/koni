#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600

#include "lyrics.h"
#include "ui_common.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>

extern const KoniLyricsPlugin** lyrics_get_plugins(void);

LyricDocument* lyric_document_create(void) {
    return calloc(1, sizeof(LyricDocument));
}

void lyric_document_free(LyricDocument *doc) {
    if (!doc) return;
    for (int i = 0; i < doc->num_lines; i++) {
        if (doc->lines[i].text) free(doc->lines[i].text);
        if (doc->lines[i].raw_text) free(doc->lines[i].raw_text);
    }
    if (doc->lines) free(doc->lines);
    free(doc);
}

int lyric_document_get_active_line(const LyricDocument *doc, uint32_t time_ms) {
    if (!doc || doc->num_lines == 0) return -1;
    int active = -1;
    for (int i = 0; i < doc->num_lines; i++) {
        if (doc->lines[i].start_ms <= time_ms) {
            active = i;
        } else {
            break;
        }
    }
    return active;
}

static char* try_read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return NULL; }

    char *buf = malloc(size + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, size, f) != (size_t)size) { free(buf); fclose(f); return NULL; }
    buf[size] = '\0';
    fclose(f);
    return buf;
}

static void ensure_dir_exists(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static void save_lyrics_cache(const LyricFetchQuery *query, const char *raw_data, const char *ext) {
    if (!app_config.download_online_lyrics || !raw_data || !ext) return;

    char safe_name[512];
    if (query->artist[0]) snprintf(safe_name, sizeof(safe_name), "%s - %s", query->artist, query->title);
    else snprintf(safe_name, sizeof(safe_name), "%s", query->title);

    for (int i = 0; safe_name[i] != '\0'; i++) {
        if (safe_name[i] == '/' || safe_name[i] == '\\' || safe_name[i] == ':') safe_name[i] = '_';
    }

    char target_path[1024];
    ensure_dir_exists(app_config.lyrics_custom_path);
    snprintf(target_path, sizeof(target_path), "%s/%s%s", app_config.lyrics_custom_path, safe_name, ext);
    FILE *f = fopen(target_path, "w");
    if (f) { fputs(raw_data, f); fclose(f); return; }

    const char *home = getenv("HOME");
    if (home) {
        char fallback_dir[1024];
        snprintf(fallback_dir, sizeof(fallback_dir), "%s/.config/koni/lyrics", home);
        ensure_dir_exists(fallback_dir);
        snprintf(target_path, sizeof(target_path), "%s/%s%s", fallback_dir, safe_name, ext);
        f = fopen(target_path, "w");
        if (f) { fputs(raw_data, f); fclose(f); }
    }
}

static void apply_document_to_ui(LyricDocument *doc, const char *backend_label) {
    pthread_mutex_lock(&state_mutex);
    if (ui_cache.lrc_doc) lyric_document_free(ui_cache.lrc_doc);
    ui_cache.lrc_doc = doc;
    strncpy(current_lyrics_backend, backend_label, sizeof(current_lyrics_backend) - 1);
    force_redraw = true;
    pthread_mutex_unlock(&state_mutex);
}

static void* lyrics_engine_worker(void *arg) {
    LyricFetchQuery *query = (LyricFetchQuery*)arg;
    const KoniLyricsPlugin **plugins = lyrics_get_plugins();

    // Check embedded lyrics
    if (query->embedded_text && strlen(query->embedded_text) > 0) {
        for (int i = 0; plugins[i]; i++) {
            LyricDocument *doc = plugins[i]->parse(query->embedded_text, strlen(query->embedded_text));
            if (doc) {
                apply_document_to_ui(doc, "Embedded");
                free(query);
                return NULL;
            }
        }
    }

    // Check local files using registered extensions
    char safe_name[512];
    if (query->artist[0]) snprintf(safe_name, sizeof(safe_name), "%s - %s", query->artist, query->title);
    else snprintf(safe_name, sizeof(safe_name), "%s", query->title);
    for (int i = 0; safe_name[i]; i++) {
        if (safe_name[i] == '/' || safe_name[i] == '\\' || safe_name[i] == ':') safe_name[i] = '_';
    }

    for (int i = 0; plugins[i]; i++) {
        const char **exts = plugins[i]->supported_extensions;
        if (!exts) continue;

        for (int e = 0; exts[e]; e++) {
            char path[1024];

            // Next to song file
            snprintf(path, sizeof(path), "%s", query->filepath);
            char *dot = strrchr(path, '.');
            if (dot) strcpy(dot, exts[e]);
            else strcat(path, exts[e]);

            char *data = try_read_file(path);

            // Custom config path
            if (!data && app_config.lyrics_custom_path[0]) {
                snprintf(path, sizeof(path), "%s/%s%s", app_config.lyrics_custom_path, safe_name, exts[e]);
                data = try_read_file(path);
            }

            // Fallback ~/.config/koni/lyrics/
            if (!data) {
                const char *home = getenv("HOME");
                if (home) {
                    snprintf(path, sizeof(path), "%s/.config/koni/lyrics/%s%s", home, safe_name, exts[e]);
                    data = try_read_file(path);
                }
            }

            if (data) {
                LyricDocument *doc = plugins[i]->parse(data, strlen(data));
                free(data);
                if (doc) {
                    char label[32];
                    snprintf(label, sizeof(label), "Local (%s)", plugins[i]->name);
                    apply_document_to_ui(doc, label);
                    free(query);
                    return NULL;
                }
            }
        }
    }

    // Optional Remote Fetching (only plugins with fetch_remote)
    if (app_config.online_lyrics) {
        for (int i = 0; plugins[i]; i++) {
            if (!plugins[i]->fetch_remote) continue;

            char *raw_data = NULL;
            char method_label[32] = "Online";

            if (plugins[i]->fetch_remote(query, &raw_data, method_label)) {
                if (raw_data) {
                    save_lyrics_cache(query, raw_data, plugins[i]->supported_extensions[0]);
                    LyricDocument *doc = plugins[i]->parse(raw_data, strlen(raw_data));
                    free(raw_data);
                    if (doc) {
                        apply_document_to_ui(doc, method_label);
                        free(query);
                        return NULL;
                    }
                }
            }
        }
    }

    apply_document_to_ui(NULL, "Not Found");
    free(query);
    return NULL;
}

void lyrics_engine_init(void) {
    // Setup if needed
}

void lyrics_engine_fetch_async(const char *title, const char *artist, const char *album, uint32_t duration, const char *filepath, const char *embedded_text) {
    if (!filepath) return;

    LyricFetchQuery *query = calloc(1, sizeof(LyricFetchQuery));
    strncpy(query->filepath, filepath, sizeof(query->filepath) - 1);
    if (title) strncpy(query->title, title, sizeof(query->title) - 1);
    if (artist) strncpy(query->artist, artist, sizeof(query->artist) - 1);
    if (album) strncpy(query->album, album, sizeof(query->album) - 1);
    query->duration_sec = duration;
    query->embedded_text = embedded_text;

    if (query->title[0] == '\0' && query->artist[0] == '\0') {
        const char *slash = strrchr(filepath, '/');
        const char *fname = slash ? slash + 1 : filepath;
        strncpy(query->title, fname, sizeof(query->title) - 1);
        char *dot = strrchr(query->title, '.');
        if (dot) *dot = '\0';
    }

    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &attr, lyrics_engine_worker, query);
    pthread_attr_destroy(&attr);
}