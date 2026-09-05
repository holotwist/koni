#ifndef KONI_DB_H
#define KONI_DB_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "codec.h"

typedef struct {
    int64_t id;
    const char *path;
    const char *name;
    const char *title;
    const char *artist;
    const char *album;
    uint32_t duration_sec;
    float track_gain;
    bool has_track_gain;
    time_t mtime;
} DBTrack;

bool db_init(void);
void db_shutdown(void);

// Track cache operations
bool db_get_track_mtime(const char *filepath, time_t *out_mtime);
bool db_get_track_meta(const char *filepath, time_t mtime, KoniMetadata *out_meta, uint32_t *out_duration);
bool db_upsert_track(const char *filepath, time_t mtime, const KoniMetadata *meta, uint32_t duration_sec);
void db_delete_track(const char *filepath);
void db_prune_missing_files(void);

typedef enum {
    DB_SORT_TITLE = 0,
    DB_SORT_ARTIST_ALBUM,
    DB_SORT_ALBUM,
    DB_SORT_DURATION,
    DB_SORT_PATH,
    DB_SORT_COUNT
} DBSortMode;

const char* db_get_sort_name(DBSortMode mode);

// Query operations
int db_load_all_tracks(DBTrack **out_tracks, DBSortMode sort_mode);
void db_free_tracks(DBTrack *tracks, int count);

// Scanner background thread control
void library_scanner_start(void);
void library_scanner_shutdown(void);
bool library_scanner_is_running(void);

#endif // KONI_DB_H