#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600

#include "db.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>

static sqlite3 *db = NULL;
static pthread_mutex_t db_mutex = PTHREAD_MUTEX_INITIALIZER;

static void ensure_dir(const char *path) {
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

bool db_init(void) {
    pthread_mutex_lock(&db_mutex);
    const char *home = getenv("HOME");
    if (!home) {
        pthread_mutex_unlock(&db_mutex);
        return false;
    }

    char db_dir[1024];
    snprintf(db_dir, sizeof(db_dir), "%s/.config/koni", home);
    ensure_dir(db_dir);

    char db_path[1024];
    snprintf(db_path, sizeof(db_path), "%s/library.db", db_dir);

    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        fprintf(stderr, "SQLite open error: %s\n", sqlite3_errmsg(db));
        pthread_mutex_unlock(&db_mutex);
        return false;
    }

    // Optimize SQLite for high-read
    sqlite3_exec(db, "PRAGMA journal_mode = WAL;", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA synchronous = NORMAL;", NULL, NULL, NULL);

    const char *schema = 
        "CREATE TABLE IF NOT EXISTS tracks ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  path TEXT UNIQUE NOT NULL,"
        "  mtime INTEGER NOT NULL,"
        "  title TEXT,"
        "  artist TEXT,"
        "  album TEXT,"
        "  duration INTEGER,"
        "  has_gain INTEGER,"
        "  track_gain REAL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_path ON tracks(path);"
        "CREATE INDEX IF NOT EXISTS idx_artist_title ON tracks(artist, title);";

    char *err_msg = NULL;
    if (sqlite3_exec(db, schema, NULL, NULL, &err_msg) != SQLITE_OK) {
        fprintf(stderr, "SQLite schema error: %s\n", err_msg);
        sqlite3_free(err_msg);
        pthread_mutex_unlock(&db_mutex);
        return false;
    }

    pthread_mutex_unlock(&db_mutex);
    return true;
}

void db_shutdown(void) {
    pthread_mutex_lock(&db_mutex);
    if (db) {
        sqlite3_close(db);
        db = NULL;
    }
    pthread_mutex_unlock(&db_mutex);
}

bool db_get_track_mtime(const char *filepath, time_t *out_mtime) {
    pthread_mutex_lock(&db_mutex);
    if (!db) { pthread_mutex_unlock(&db_mutex); return false; }

    const char *sql = "SELECT mtime FROM tracks WHERE path = ? LIMIT 1;";
    sqlite3_stmt *stmt;
    bool found = false;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, filepath, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            *out_mtime = (time_t)sqlite3_column_int64(stmt, 0);
            found = true;
        }
        sqlite3_finalize(stmt);
    }
    pthread_mutex_unlock(&db_mutex);
    return found;
}

bool db_get_track_meta(const char *filepath, time_t mtime, KoniMetadata *out_meta, uint32_t *out_duration) {
    pthread_mutex_lock(&db_mutex);
    if (!db) { pthread_mutex_unlock(&db_mutex); return false; }

    const char *sql = "SELECT mtime, title, artist, album, duration, has_gain, track_gain FROM tracks WHERE path = ? LIMIT 1;";
    sqlite3_stmt *stmt;
    bool found = false;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, filepath, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            time_t cached_mtime = (time_t)sqlite3_column_int64(stmt, 0);
            if (cached_mtime == mtime) {
                const char *title = (const char *)sqlite3_column_text(stmt, 1);
                const char *artist = (const char *)sqlite3_column_text(stmt, 2);
                const char *album = (const char *)sqlite3_column_text(stmt, 3);
                
                if (title && title[0]) out_meta->title = strdup(title);
                if (artist && artist[0]) out_meta->artist = strdup(artist);
                if (album && album[0]) out_meta->album = strdup(album);
                
                if (out_duration) *out_duration = sqlite3_column_int(stmt, 4);
                out_meta->has_track_gain = sqlite3_column_int(stmt, 5) ? true : false;
                out_meta->track_gain = (float)sqlite3_column_double(stmt, 6);
                found = true;
            }
        }
        sqlite3_finalize(stmt);
    }
    pthread_mutex_unlock(&db_mutex);
    return found;
}

bool db_upsert_track(const char *filepath, time_t mtime, const KoniMetadata *meta, uint32_t duration_sec) {
    pthread_mutex_lock(&db_mutex);
    if (!db) { pthread_mutex_unlock(&db_mutex); return false; }

    const char *sql = 
        "INSERT INTO tracks (path, mtime, title, artist, album, duration, has_gain, track_gain) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
        "ON CONFLICT(path) DO UPDATE SET "
        "mtime = excluded.mtime, title = excluded.title, artist = excluded.artist, "
        "album = excluded.album, duration = excluded.duration, has_gain = excluded.has_gain, "
        "track_gain = excluded.track_gain;";

    sqlite3_stmt *stmt;
    bool ok = false;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, filepath, -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 2, (sqlite3_int64)mtime);
        sqlite3_bind_text(stmt, 3, (meta && meta->title) ? meta->title : "", -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, (meta && meta->artist) ? meta->artist : "", -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 5, (meta && meta->album) ? meta->album : "", -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 6, duration_sec);
        sqlite3_bind_int(stmt, 7, (meta && meta->has_track_gain) ? 1 : 0);
        sqlite3_bind_double(stmt, 8, (meta && meta->has_track_gain) ? (double)meta->track_gain : 0.0);

        ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
    }
    pthread_mutex_unlock(&db_mutex);
    return ok;
}

void db_delete_track(const char *filepath) {
    pthread_mutex_lock(&db_mutex);
    if (!db) { pthread_mutex_unlock(&db_mutex); return; }

    const char *sql = "DELETE FROM tracks WHERE path = ?;";
    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, filepath, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    pthread_mutex_unlock(&db_mutex);
}

void db_prune_missing_files(void) {
    pthread_mutex_lock(&db_mutex);
    if (!db) { pthread_mutex_unlock(&db_mutex); return; }

    const char *sql = "SELECT path FROM tracks;";
    sqlite3_stmt *stmt;

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char *path = (const char *)sqlite3_column_text(stmt, 0);
            if (path && access(path, F_OK) != 0) {
                // File no longer exists, remove from DB
                char del_sql[1024];
                snprintf(del_sql, sizeof(del_sql), "DELETE FROM tracks WHERE path = %Q;", path);
                sqlite3_exec(db, del_sql, NULL, NULL, NULL);
            }
        }
        sqlite3_finalize(stmt);
    }
    pthread_mutex_unlock(&db_mutex);
}

int db_load_all_tracks(DBTrack **out_tracks) {
    pthread_mutex_lock(&db_mutex);
    if (!db) { 
        *out_tracks = NULL;
        pthread_mutex_unlock(&db_mutex); 
        return 0; 
    }

    const char *sql = "SELECT id, path, mtime, title, artist, album, duration, has_gain, track_gain FROM tracks ORDER BY artist COLLATE NOCASE, album COLLATE NOCASE, title COLLATE NOCASE, path ASC;";
    sqlite3_stmt *stmt;
    int count = 0;
    int capacity = 256;
    DBTrack *tracks = malloc(sizeof(DBTrack) * capacity);

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            if (count >= capacity) {
                capacity *= 2;
                tracks = realloc(tracks, sizeof(DBTrack) * capacity);
            }
            DBTrack *t = &tracks[count++];
            t->id = sqlite3_column_int64(stmt, 0);
            strncpy(t->path, (const char *)sqlite3_column_text(stmt, 1), sizeof(t->path) - 1);
            t->mtime = (time_t)sqlite3_column_int64(stmt, 2);
            strncpy(t->title, (const char *)sqlite3_column_text(stmt, 3), sizeof(t->title) - 1);
            strncpy(t->artist, (const char *)sqlite3_column_text(stmt, 4), sizeof(t->artist) - 1);
            strncpy(t->album, (const char *)sqlite3_column_text(stmt, 5), sizeof(t->album) - 1);
            t->duration_sec = sqlite3_column_int(stmt, 6);
            t->has_track_gain = sqlite3_column_int(stmt, 7) ? true : false;
            t->track_gain = (float)sqlite3_column_double(stmt, 8);

            const char *slash = strrchr(t->path, '/');
            strncpy(t->name, slash ? slash + 1 : t->path, sizeof(t->name) - 1);
        }
        sqlite3_finalize(stmt);
    }
    pthread_mutex_unlock(&db_mutex);

    *out_tracks = tracks;
    return count;
}

void db_free_tracks(DBTrack *tracks, int count) {
    (void)count;
    if (tracks) free(tracks);
}