#ifndef KONI_DB_H
#define KONI_DB_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "codec.h"

typedef struct {
    int64_t id;
    char path[1024];
    char name[256];
    char title[256];
    char artist[256];
    char album[256];
    uint32_t duration_sec;
    float track_gain;
    bool has_track_gain;
    time_t mtime;
} DBTrack;

bool db_init(void);
void db_shutdown(void);

// Track cache operations
bool db_get_track_mtime(const char *filepath, time_t *out_mtime);
bool db_upsert_track(const char *filepath, time_t mtime, const KoniMetadata *meta, uint32_t duration_sec);
void db_delete_track(const char *filepath);
void db_prune_missing_files(void);

// Query operations
int db_load_all_tracks(DBTrack **out_tracks);
void db_free_tracks(DBTrack *tracks, int count);

// Scanner background thread control
void library_scanner_start(void);
void library_scanner_shutdown(void);
bool library_scanner_is_running(void);

#endif // KONI_DB_H