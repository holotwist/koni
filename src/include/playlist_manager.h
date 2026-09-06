#ifndef PLAYLIST_MANAGER_H
#define PLAYLIST_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "codec.h"

#define MAX_PLAYLIST_NAME 128

typedef struct {
    char name[MAX_PLAYLIST_NAME];
    char filepath[1024];
    int track_count;
    bool is_favourites;
} PlaylistSummary;

typedef struct {
    char path[1024];
    char title[256];
    char artist[256];
    uint32_t duration_sec;
} PlaylistTrackItem;

typedef struct {
    char name[MAX_PLAYLIST_NAME];
    char filepath[1024];
    PlaylistTrackItem *items;
    int count;
    int capacity;
} LoadedPlaylist;

// Lifecycle
void playlist_mgmt_init(void);
void playlist_mgmt_shutdown(void);
void playlist_mgmt_refresh_list(void);

// Playlist Directory Queries
int playlist_mgmt_get_count(void);
const PlaylistSummary* playlist_mgmt_get_summary(int index);

// Favourites Fast Lookup
bool playlist_mgmt_is_favourite(const char *filepath);
bool playlist_mgmt_toggle_favourite(const char *filepath, const char *title, const char *artist, uint32_t duration_sec);

// Playlist Operations
bool playlist_mgmt_create(const char *name);
bool playlist_mgmt_delete(const char *name);
bool playlist_mgmt_rename(const char *old_name, const char *new_name);
bool playlist_mgmt_add_track(const char *playlist_name, const char *track_path, const char *title, const char *artist, uint32_t duration_sec);
bool playlist_mgmt_remove_track(const char *playlist_name, int track_index);

// Active / Loaded Playlist Context (for drilldown view and playback)
bool playlist_mgmt_load_playlist(const char *name, LoadedPlaylist *out_pl);
void playlist_mgmt_free_loaded(LoadedPlaylist *pl);

#endif // PLAYLIST_MANAGER_H