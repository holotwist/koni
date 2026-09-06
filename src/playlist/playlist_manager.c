#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include "playlist_manager.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>

#define FAVOURITES_NAME "Favourites"

static char s_playlist_dir[1024] = {0};
static PlaylistSummary *s_playlists = NULL;
static int s_num_playlists = 0;
static int s_playlists_cap = 0;

/* Fast in-memory hash set for Favourites */
typedef struct FavNode {
    char *path;
    struct FavNode *next;
} FavNode;

#define FAV_BUCKETS 1024
static FavNode *s_fav_buckets[FAV_BUCKETS] = {0};
static pthread_mutex_t s_pl_mutex = PTHREAD_MUTEX_INITIALIZER;

static unsigned int hash_path(const char *p) {
    unsigned int h = 5381;
    while (*p) h = ((h << 5) + h) + (unsigned char)(*p++);
    return h % FAV_BUCKETS;
}

static void fav_cache_clear(void) {
    for (int i = 0; i < FAV_BUCKETS; i++) {
        FavNode *cur = s_fav_buckets[i];
        while (cur) {
            FavNode *next = cur->next;
            free(cur->path);
            free(cur);
            cur = next;
        }
        s_fav_buckets[i] = NULL;
    }
}

static void fav_cache_add(const char *path) {
    if (!path || !path[0]) return;
    unsigned int bucket = hash_path(path);
    FavNode *node = malloc(sizeof(FavNode));
    if (!node) return;
    node->path = strdup(path);
    node->next = s_fav_buckets[bucket];
    s_fav_buckets[bucket] = node;
}

static void fav_cache_remove(const char *path) {
    if (!path || !path[0]) return;
    unsigned int bucket = hash_path(path);
    FavNode **cur = &s_fav_buckets[bucket];
    while (*cur) {
        if (strcmp((*cur)->path, path) == 0) {
            FavNode *del = *cur;
            *cur = (*cur)->next;
            free(del->path);
            free(del);
            return;
        }
        cur = &(*cur)->next;
    }
}

static void ensure_playlists_dir(void) {
    const char *home = getenv("HOME");
    if (!home) return;
    snprintf(s_playlist_dir, sizeof(s_playlist_dir), "%s/.config/koni/playlists", home);
    
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", s_playlist_dir);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);

    // Guarantee Favourites.m3u exists
    char fav_path[1024];
    snprintf(fav_path, sizeof(fav_path), "%s/%s.m3u", s_playlist_dir, FAVOURITES_NAME);
    if (access(fav_path, F_OK) != 0) {
        FILE *f = fopen(fav_path, "w");
        if (f) {
            fprintf(f, "#EXTM3U\n");
            fclose(f);
        }
    }
}

static int count_m3u_tracks(const char *filepath) {
    FILE *f = fopen(filepath, "r");
    if (!f) return 0;
    char line[1024];
    int count = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] && line[0] != '#') count++;
    }
    fclose(f);
    return count;
}

static void reload_favourites_cache(void) {
    fav_cache_clear();
    char fav_path[1024];
    snprintf(fav_path, sizeof(fav_path), "%s/%s.m3u", s_playlist_dir, FAVOURITES_NAME);
    FILE *f = fopen(fav_path, "r");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (line[0] && line[0] != '#') {
            fav_cache_add(line);
        }
    }
    fclose(f);
}

void playlist_mgmt_refresh_list(void) {
    pthread_mutex_lock(&s_pl_mutex);
    ensure_playlists_dir();

    s_num_playlists = 0;
    if (s_playlists_cap == 0) {
        s_playlists_cap = 16;
        s_playlists = malloc(sizeof(PlaylistSummary) * s_playlists_cap);
    }

    // Insert Favourites first
    PlaylistSummary *fav = &s_playlists[s_num_playlists++];
    strncpy(fav->name, FAVOURITES_NAME, sizeof(fav->name) - 1);
    snprintf(fav->filepath, sizeof(fav->filepath), "%s/%s.m3u", s_playlist_dir, FAVOURITES_NAME);
    fav->track_count = count_m3u_tracks(fav->filepath);
    fav->is_favourites = true;

    DIR *d = opendir(s_playlist_dir);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
            char *ext = strrchr(ent->d_name, '.');
            if (ext && strcasecmp(ext, ".m3u") == 0) {
                char base_name[MAX_PLAYLIST_NAME] = {0};
                size_t name_len = ext - ent->d_name;
                if (name_len >= sizeof(base_name)) name_len = sizeof(base_name) - 1;
                strncpy(base_name, ent->d_name, name_len);

                if (strcasecmp(base_name, FAVOURITES_NAME) == 0) continue;

                if (s_num_playlists >= s_playlists_cap) {
                    s_playlists_cap *= 2;
                    s_playlists = realloc(s_playlists, sizeof(PlaylistSummary) * s_playlists_cap);
                }

                PlaylistSummary *ps = &s_playlists[s_num_playlists++];
                strncpy(ps->name, base_name, sizeof(ps->name) - 1);
                snprintf(ps->filepath, sizeof(ps->filepath), "%s/%s", s_playlist_dir, ent->d_name);
                ps->track_count = count_m3u_tracks(ps->filepath);
                ps->is_favourites = false;
            }
        }
        closedir(d);
    }

    reload_favourites_cache();
    pthread_mutex_unlock(&s_pl_mutex);
}

void playlist_mgmt_init(void) {
    playlist_mgmt_refresh_list();
}

void playlist_mgmt_shutdown(void) {
    pthread_mutex_lock(&s_pl_mutex);
    if (s_playlists) {
        free(s_playlists);
        s_playlists = NULL;
    }
    s_num_playlists = 0;
    s_playlists_cap = 0;
    fav_cache_clear();
    pthread_mutex_unlock(&s_pl_mutex);
}

int playlist_mgmt_get_count(void) {
    pthread_mutex_lock(&s_pl_mutex);
    int c = s_num_playlists;
    pthread_mutex_unlock(&s_pl_mutex);
    return c;
}

const PlaylistSummary* playlist_mgmt_get_summary(int index) {
    if (index < 0 || index >= s_num_playlists) return NULL;
    return &s_playlists[index];
}

bool playlist_mgmt_is_favourite(const char *filepath) {
    if (!filepath || !filepath[0]) return false;
    pthread_mutex_lock(&s_pl_mutex);
    unsigned int b = hash_path(filepath);
    FavNode *cur = s_fav_buckets[b];
    while (cur) {
        if (strcmp(cur->path, filepath) == 0) {
            pthread_mutex_unlock(&s_pl_mutex);
            return true;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&s_pl_mutex);
    return false;
}

bool playlist_mgmt_toggle_favourite(const char *filepath, const char *title, const char *artist, uint32_t duration_sec) {
    if (!filepath || !filepath[0]) return false;

    bool is_fav = playlist_mgmt_is_favourite(filepath);
    if (is_fav) {
        // Remove track
        char fav_path[1024];
        snprintf(fav_path, sizeof(fav_path), "%s/%s.m3u", s_playlist_dir, FAVOURITES_NAME);
        FILE *f = fopen(fav_path, "r");
        if (!f) return false;

        char **lines = NULL;
        int count = 0, cap = 64;
        lines = malloc(sizeof(char*) * cap);
        char buf[1024];

        while (fgets(buf, sizeof(buf), f)) {
            buf[strcspn(buf, "\r\n")] = 0;
            if (strcmp(buf, filepath) == 0) continue; // Skip target
            if (count >= cap) {
                cap *= 2;
                lines = realloc(lines, sizeof(char*) * cap);
            }
            lines[count++] = strdup(buf);
        }
        fclose(f);

        f = fopen(fav_path, "w");
        if (f) {
            for (int i = 0; i < count; i++) {
                fprintf(f, "%s\n", lines[i]);
                free(lines[i]);
            }
            fclose(f);
        }
        free(lines);

        pthread_mutex_lock(&s_pl_mutex);
        fav_cache_remove(filepath);
        if (s_num_playlists > 0) s_playlists[0].track_count = count_m3u_tracks(fav_path);
        pthread_mutex_unlock(&s_pl_mutex);
        return false; // Result is now unfavourited
    } else {
        // Add track
        playlist_mgmt_add_track(FAVOURITES_NAME, filepath, title, artist, duration_sec);
        pthread_mutex_lock(&s_pl_mutex);
        fav_cache_add(filepath);
        pthread_mutex_unlock(&s_pl_mutex);
        return true; // Result is now favourited
    }
}

bool playlist_mgmt_create(const char *name) {
    if (!name || !name[0]) return false;
    char clean_name[MAX_PLAYLIST_NAME];
    strncpy(clean_name, name, sizeof(clean_name) - 1);
    clean_name[sizeof(clean_name) - 1] = '\0';
    for (char *p = clean_name; *p; p++) {
        if (*p == '/' || *p == '\\' || *p == ':') *p = '_';
    }

    char target[1024];
    snprintf(target, sizeof(target), "%s/%s.m3u", s_playlist_dir, clean_name);
    if (access(target, F_OK) == 0) return false; // Already exists

    FILE *f = fopen(target, "w");
    if (!f) return false;
    fprintf(f, "#EXTM3U\n");
    fclose(f);

    playlist_mgmt_refresh_list();
    return true;
}

bool playlist_mgmt_delete(const char *name) {
    if (!name || !name[0] || strcasecmp(name, FAVOURITES_NAME) == 0) return false;
    char target[1024];
    snprintf(target, sizeof(target), "%s/%s.m3u", s_playlist_dir, name);
    int res = unlink(target);
    playlist_mgmt_refresh_list();
    return res == 0;
}

bool playlist_mgmt_rename(const char *old_name, const char *new_name) {
    if (!old_name || !new_name || !old_name[0] || !new_name[0]) return false;
    if (strcasecmp(old_name, FAVOURITES_NAME) == 0) return false;

    char src[1024], dst[1024];
    snprintf(src, sizeof(src), "%s/%s.m3u", s_playlist_dir, old_name);
    snprintf(dst, sizeof(dst), "%s/%s.m3u", s_playlist_dir, new_name);
    int res = rename(src, dst);
    playlist_mgmt_refresh_list();
    return res == 0;
}

bool playlist_mgmt_add_track(const char *playlist_name, const char *track_path, const char *title, const char *artist, uint32_t duration_sec) {
    if (!playlist_name || !track_path || !track_path[0]) return false;
    char target[1024];
    snprintf(target, sizeof(target), "%s/%s.m3u", s_playlist_dir, playlist_name);

    // Prevent duplicate entries if it's already in the playlist
    FILE *f = fopen(target, "r");
    if (f) {
        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (strcmp(line, track_path) == 0) {
                fclose(f);
                return false;
            }
        }
        fclose(f);
    }

    f = fopen(target, "a");
    if (!f) return false;
    if (title && title[0]) {
        fprintf(f, "#EXTINF:%u,%s%s%s\n", duration_sec, artist ? artist : "", (artist && artist[0]) ? " - " : "", title);
    }
    fprintf(f, "%s\n", track_path);
    fclose(f);

    playlist_mgmt_refresh_list();
    return true;
}

bool playlist_mgmt_remove_track(const char *playlist_name, int track_index) {
    if (!playlist_name || track_index < 0) return false;
    char target[1024];
    snprintf(target, sizeof(target), "%s/%s.m3u", s_playlist_dir, playlist_name);

    FILE *f = fopen(target, "r");
    if (!f) return false;

    char **lines = NULL;
    int count = 0, cap = 64;
    lines = malloc(sizeof(char*) * cap);
    char buf[1024];
    int current_track = 0;

    while (fgets(buf, sizeof(buf), f)) {
        buf[strcspn(buf, "\r\n")] = 0;
        if (buf[0] && buf[0] != '#') {
            if (current_track == track_index) {
                // If the preceding line was #EXTINF, drop it as well
                if (count > 0 && strncmp(lines[count - 1], "#EXTINF:", 8) == 0) {
                    free(lines[count - 1]);
                    count--;
                }
                current_track++;
                continue;
            }
            current_track++;
        }
        if (count >= cap) {
            cap *= 2;
            lines = realloc(lines, sizeof(char*) * cap);
        }
        lines[count++] = strdup(buf);
    }
    fclose(f);

    f = fopen(target, "w");
    if (f) {
        for (int i = 0; i < count; i++) {
            fprintf(f, "%s\n", lines[i]);
            free(lines[i]);
        }
        fclose(f);
    }
    free(lines);
    playlist_mgmt_refresh_list();
    return true;
}

bool playlist_mgmt_load_playlist(const char *name, LoadedPlaylist *out_pl) {
    if (!name || !out_pl) return false;
    memset(out_pl, 0, sizeof(LoadedPlaylist));
    strncpy(out_pl->name, name, sizeof(out_pl->name) - 1);
    snprintf(out_pl->filepath, sizeof(out_pl->filepath), "%s/%s.m3u", s_playlist_dir, name);

    FILE *f = fopen(out_pl->filepath, "r");
    if (!f) return false;

    out_pl->capacity = 32;
    out_pl->items = malloc(sizeof(PlaylistTrackItem) * out_pl->capacity);

    char line[1024];
    char pending_title[256] = {0};
    char pending_artist[256] = {0};
    uint32_t pending_dur = 0;

    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (strncmp(line, "#EXTINF:", 8) == 0) {
            char *comma = strchr(line, ',');
            if (comma) {
                pending_dur = (uint32_t)atoi(line + 8);
                char *artist_track = comma + 1;
                char *dash = strstr(artist_track, " - ");
                if (dash) {
                    size_t a_len = dash - artist_track;
                    strncpy(pending_artist, artist_track, a_len < sizeof(pending_artist) ? a_len : sizeof(pending_artist) - 1);
                    strncpy(pending_title, dash + 3, sizeof(pending_title) - 1);
                } else {
                    strncpy(pending_title, artist_track, sizeof(pending_title) - 1);
                }
            }
        } else if (line[0] && line[0] != '#') {
            if (out_pl->count >= out_pl->capacity) {
                out_pl->capacity *= 2;
                out_pl->items = realloc(out_pl->items, sizeof(PlaylistTrackItem) * out_pl->capacity);
            }
            PlaylistTrackItem *item = &out_pl->items[out_pl->count++];
            strncpy(item->path, line, sizeof(item->path) - 1);
            strncpy(item->title, pending_title, sizeof(item->title) - 1);
            strncpy(item->artist, pending_artist, sizeof(item->artist) - 1);
            item->duration_sec = pending_dur;

            pending_title[0] = '\0';
            pending_artist[0] = '\0';
            pending_dur = 0;
        }
    }
    fclose(f);
    return true;
}

void playlist_mgmt_free_loaded(LoadedPlaylist *pl) {
    if (!pl) return;
    if (pl->items) {
        free(pl->items);
        pl->items = NULL;
    }
    pl->count = 0;
    pl->capacity = 0;
}