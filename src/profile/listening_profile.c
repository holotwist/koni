#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include "listening_profile.h"
#include "config.h"
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <pthread.h>

static sqlite3 *s_pdb = NULL;
static pthread_mutex_t s_pdb_mutex = PTHREAD_MUTEX_INITIALIZER;
static int64_t s_current_session_id = -1;
static time_t s_last_activity_time = 0;

static unsigned int hash_string(const char *str) {
    unsigned int h = 5381;
    if (!str) return 0;
    while (*str) h = ((h << 5) + h) + (unsigned char)(*str++);
    return h;
}

static void make_track_hash(char *out_hash, size_t sz, const char *title, const char *artist) {
    unsigned int h1 = hash_string(title ? title : "");
    unsigned int h2 = hash_string(artist ? artist : "");
    snprintf(out_hash, sz, "%08x%08x", h1, h2);
}

bool listening_profile_init(void) {
    pthread_mutex_lock(&s_pdb_mutex);
    if (s_pdb) {
        pthread_mutex_unlock(&s_pdb_mutex);
        return true;
    }

    const char *home = getenv("HOME");
    if (!home) {
        pthread_mutex_unlock(&s_pdb_mutex);
        return false;
    }

    char db_path[1024];
    snprintf(db_path, sizeof(db_path), "%s/.config/koni/profile.db", home);

    if (sqlite3_open(db_path, &s_pdb) != SQLITE_OK) {
        pthread_mutex_unlock(&s_pdb_mutex);
        return false;
    }

    sqlite3_exec(s_pdb, "PRAGMA journal_mode = WAL;", NULL, NULL, NULL);
    sqlite3_exec(s_pdb, "PRAGMA synchronous = NORMAL;", NULL, NULL, NULL);

    const char *schema =
        "CREATE TABLE IF NOT EXISTS track_profile ("
        "  path TEXT PRIMARY KEY,"
        "  track_hash TEXT,"
        "  play_count INTEGER DEFAULT 0,"
        "  complete_count INTEGER DEFAULT 0,"
        "  skip_count INTEGER DEFAULT 0,"
        "  soft_skip_count INTEGER DEFAULT 0,"
        "  last_played INTEGER DEFAULT 0,"
        "  total_listen_sec INTEGER DEFAULT 0,"
        "  hourly_distribution BLOB,"
        "  daily_distribution BLOB,"
        "  avg_energy REAL DEFAULT 0.5,"
        "  bpm REAL DEFAULT 120.0"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_profile_hash ON track_profile(track_hash);"
        "CREATE TABLE IF NOT EXISTS listening_sessions ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  start_time INTEGER,"
        "  end_time INTEGER,"
        "  track_count INTEGER DEFAULT 0,"
        "  skip_count INTEGER DEFAULT 0"
        ");";

    sqlite3_exec(s_pdb, schema, NULL, NULL, NULL);

    // Non-destructive column additions for existing profile databases
    sqlite3_exec(s_pdb, "ALTER TABLE track_profile ADD COLUMN soft_skip_count INTEGER DEFAULT 0;", NULL, NULL, NULL);
    sqlite3_exec(s_pdb, "ALTER TABLE track_profile ADD COLUMN daily_distribution BLOB;", NULL, NULL, NULL);

    pthread_mutex_unlock(&s_pdb_mutex);
    return true;
}

void listening_profile_shutdown(void) {
    pthread_mutex_lock(&s_pdb_mutex);
    if (s_pdb) {
        sqlite3_close(s_pdb);
        s_pdb = NULL;
    }
    s_current_session_id = -1;
    pthread_mutex_unlock(&s_pdb_mutex);
}

void listening_profile_reset_db(void) {
    pthread_mutex_lock(&s_pdb_mutex);
    if (s_pdb) {
        sqlite3_exec(s_pdb, "DELETE FROM track_profile;", NULL, NULL, NULL);
        sqlite3_exec(s_pdb, "DELETE FROM listening_sessions;", NULL, NULL, NULL);
        sqlite3_exec(s_pdb, "VACUUM;", NULL, NULL, NULL);
    }
    s_current_session_id = -1;
    pthread_mutex_unlock(&s_pdb_mutex);
}

static void update_session_on_play_locked(time_t now) {
    // New session if 30 minutes of silence elapsed or first track
    if (s_current_session_id < 0 || (now - s_last_activity_time) > 1800) {
        const char *sql = "INSERT INTO listening_sessions (start_time, end_time, track_count) VALUES (?, ?, 1);";
        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(s_pdb, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now);
            sqlite3_bind_int64(stmt, 2, (sqlite3_int64)now);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            s_current_session_id = sqlite3_last_insert_rowid(s_pdb);
        }
    } else {
        const char *sql = "UPDATE listening_sessions SET end_time = ?, track_count = track_count + 1 WHERE id = ?;";
        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(s_pdb, sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, (sqlite3_int64)now);
            sqlite3_bind_int64(stmt, 2, s_current_session_id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    s_last_activity_time = now;
}

void listening_profile_on_track_start(const char *path, const char *title, const char *artist) {
    if (!path || !path[0] || !app_config.listening_profile_asked || !app_config.enable_listening_profile) return;

    pthread_mutex_lock(&s_pdb_mutex);
    if (!s_pdb) { pthread_mutex_unlock(&s_pdb_mutex); return; }

    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    int hour = tm_info.tm_hour;
    int day  = tm_info.tm_wday; // 0=Sun..6=Sat

    update_session_on_play_locked(now);

    char thash[32];
    make_track_hash(thash, sizeof(thash), title, artist);

    const char *select_sql = "SELECT hourly_distribution, daily_distribution FROM track_profile WHERE path = ? LIMIT 1;";
    sqlite3_stmt *stmt;
    uint16_t hours[24] = {0};
    uint16_t days[7] = {0};

    if (sqlite3_prepare_v2(s_pdb, select_sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const void *b_h = sqlite3_column_blob(stmt, 0);
            int sz_h = sqlite3_column_bytes(stmt, 0);
            if (b_h && sz_h == (int)sizeof(hours)) memcpy(hours, b_h, sizeof(hours));

            const void *b_d = sqlite3_column_blob(stmt, 1);
            int sz_d = sqlite3_column_bytes(stmt, 1);
            if (b_d && sz_d == (int)sizeof(days)) memcpy(days, b_d, sizeof(days));
        }
        sqlite3_finalize(stmt);
    }

    if (hour >= 0 && hour < 24 && hours[hour] < 65535) hours[hour]++;
    if (day >= 0 && day < 7 && days[day] < 65535) days[day]++;

    const char *upsert_sql =
        "INSERT INTO track_profile (path, track_hash, play_count, last_played, hourly_distribution, daily_distribution) "
        "VALUES (?, ?, 1, ?, ?, ?) "
        "ON CONFLICT(path) DO UPDATE SET "
        "track_hash = excluded.track_hash, "
        "play_count = play_count + 1, "
        "last_played = excluded.last_played, "
        "hourly_distribution = excluded.hourly_distribution, "
        "daily_distribution = excluded.daily_distribution;";

    if (sqlite3_prepare_v2(s_pdb, upsert_sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, thash, -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 3, (sqlite3_int64)now);
        sqlite3_bind_blob(stmt, 4, hours, sizeof(hours), SQLITE_STATIC);
        sqlite3_bind_blob(stmt, 5, days, sizeof(days), SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    pthread_mutex_unlock(&s_pdb_mutex);
}

void listening_profile_on_track_end(const char *path, uint32_t played_sec, uint32_t total_sec, float measured_energy) {
    if (!path || !path[0] || !app_config.listening_profile_asked || !app_config.enable_listening_profile) return;

    pthread_mutex_lock(&s_pdb_mutex);
    if (!s_pdb) { pthread_mutex_unlock(&s_pdb_mutex); return; }

    bool is_complete = (total_sec > 10 && played_sec >= (total_sec * 8 / 10));
    bool is_hard_skip = (total_sec > 25 && played_sec <= 15);
    bool is_soft_skip = (!is_complete && !is_hard_skip && total_sec > 30 && played_sec < (total_sec / 2));

    if ((is_hard_skip || is_soft_skip) && s_current_session_id >= 0) {
        const char *sess_sql = "UPDATE listening_sessions SET skip_count = skip_count + 1 WHERE id = ?;";
        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(s_pdb, sess_sql, -1, &stmt, NULL) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, s_current_session_id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }

    // Leaky moving average update for acoustic energy
    const char *sql =
        "UPDATE track_profile SET "
        "complete_count = complete_count + ?, "
        "skip_count = skip_count + ?, "
        "soft_skip_count = soft_skip_count + ?, "
        "total_listen_sec = total_listen_sec + ?, "
        "avg_energy = (avg_energy * 0.65) + (? * 0.35) "
        "WHERE path = ?;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(s_pdb, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, is_complete ? 1 : 0);
        sqlite3_bind_int(stmt, 2, is_hard_skip ? 1 : 0);
        sqlite3_bind_int(stmt, 3, is_soft_skip ? 1 : 0);
        sqlite3_bind_int(stmt, 4, played_sec);
        sqlite3_bind_double(stmt, 5, (double)measured_energy);
        sqlite3_bind_text(stmt, 6, path, -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    pthread_mutex_unlock(&s_pdb_mutex);
}

float listening_profile_get_score(const char *path, const char *title, const char *artist, float target_energy) {
    if (!path || !path[0] || !app_config.listening_profile_asked || !app_config.enable_listening_profile) return 1.0f;

    pthread_mutex_lock(&s_pdb_mutex);
    if (!s_pdb) { pthread_mutex_unlock(&s_pdb_mutex); return 1.0f; }

    const char *sql =
        "SELECT play_count, complete_count, skip_count, soft_skip_count, last_played, hourly_distribution, daily_distribution, avg_energy "
        "FROM track_profile WHERE path = ? "
        "UNION "
        "SELECT play_count, complete_count, skip_count, soft_skip_count, last_played, hourly_distribution, daily_distribution, avg_energy "
        "FROM track_profile WHERE track_hash = ? AND path != ? LIMIT 1;";

    sqlite3_stmt *stmt;
    int plays = 0, completes = 0, hard_skips = 0, soft_skips = 0;
    time_t last_played = 0;
    uint16_t hours[24] = {0};
    uint16_t days[7] = {0};
    float avg_energy = 0.5f;
    bool found = false;

    char thash[32];
    make_track_hash(thash, sizeof(thash), title, artist);

    if (sqlite3_prepare_v2(s_pdb, sql, -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, path, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, thash, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, path, -1, SQLITE_STATIC);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            plays       = sqlite3_column_int(stmt, 0);
            completes   = sqlite3_column_int(stmt, 1);
            hard_skips  = sqlite3_column_int(stmt, 2);
            soft_skips  = sqlite3_column_int(stmt, 3);
            last_played = (time_t)sqlite3_column_int64(stmt, 4);

            const void *b_h = sqlite3_column_blob(stmt, 5);
            int sz_h = sqlite3_column_bytes(stmt, 5);
            if (b_h && sz_h == (int)sizeof(hours)) memcpy(hours, b_h, sizeof(hours));

            const void *b_d = sqlite3_column_blob(stmt, 6);
            int sz_d = sqlite3_column_bytes(stmt, 6);
            if (b_d && sz_d == (int)sizeof(days)) memcpy(days, b_d, sizeof(days));

            avg_energy = (float)sqlite3_column_double(stmt, 7);
            found = true;
        }
        sqlite3_finalize(stmt);
    }
    pthread_mutex_unlock(&s_pdb_mutex);

    if (!found || plays == 0) return 1.0f;

    // Bayesian affinity, hard skip (-1.5) vs soft skip (-0.6)
    float penalties = (1.5f * (float)hard_skips) + (0.6f * (float)soft_skips);
    float affinity = ((float)completes - penalties + 2.0f) / ((float)plays + 4.0f);
    if (affinity < 0.10f) affinity = 0.10f;

    time_t now = time(NULL);
    struct tm tm_info;
    localtime_r(&now, &tm_info);
    int h = tm_info.tm_hour;
    int d = tm_info.tm_wday;

    // Hour context
    uint16_t max_h = 0;
    for (int i = 0; i < 24; i++) if (hours[i] > max_h) max_h = hours[i];
    float hour_match = 0.65f + 0.35f * ((float)hours[h] / (float)(max_h + 1));

    // Day context
    uint16_t max_d = 0;
    for (int i = 0; i < 7; i++) if (days[i] > max_d) max_d = days[i];
    float day_match = 0.80f + 0.20f * ((float)days[d] / (float)(max_d + 1));

    // Fatigue recovery, tau ~ 4 hours
    float hours_passed = (float)(now - last_played) / 3600.0f;
    if (hours_passed < 0.0f) hours_passed = 0.0f;
    float fatigue = 1.0f - expf(-hours_passed / 4.0f);
    if (fatigue < 0.15f) fatigue = 0.15f;

    // Acoustic energy continuity factor
    float vibe_match = 1.0f;
    if (target_energy >= 0.0f) {
        float energy_diff = fabsf(avg_energy - target_energy);
        vibe_match = 1.0f - (0.40f * energy_diff);
        if (vibe_match < 0.40f) vibe_match = 0.40f;
    }

    return affinity * hour_match * day_match * fatigue * vibe_match;
}

int listening_profile_pick_weighted(const char **paths, int count, float target_energy) {
    if (!paths || count <= 0) return 0;
    if (count == 1) return 0;

    float *scores = malloc(sizeof(float) * count);
    if (!scores) return rand() % count;

    float sum = 0.0f;
    for (int i = 0; i < count; i++) {
        scores[i] = listening_profile_get_score(paths[i], NULL, NULL, target_energy);
        sum += scores[i];
    }

    if (sum <= 0.0001f) {
        free(scores);
        return rand() % count;
    }

    float r = ((float)rand() / (float)RAND_MAX) * sum;
    float acc = 0.0f;
    int picked = count - 1;

    for (int i = 0; i < count; i++) {
        acc += scores[i];
        if (r <= acc) {
            picked = i;
            break;
        }
    }

    free(scores);
    return picked;
}

void listening_profile_get_overview(ProfileOverview *out_stats) {
    if (!out_stats) return;
    memset(out_stats, 0, sizeof(ProfileOverview));

    pthread_mutex_lock(&s_pdb_mutex);
    if (!s_pdb) { pthread_mutex_unlock(&s_pdb_mutex); return; }

    const char *sql_tracks =
        "SELECT SUM(play_count), SUM(complete_count), SUM(skip_count), SUM(soft_skip_count), "
        "SUM(total_listen_sec), AVG(avg_energy) FROM track_profile;";

    sqlite3_stmt *stmt;
    if (sqlite3_prepare_v2(s_pdb, sql_tracks, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            out_stats->total_plays       = sqlite3_column_int(stmt, 0);
            out_stats->total_completes   = sqlite3_column_int(stmt, 1);
            out_stats->total_hard_skips  = sqlite3_column_int(stmt, 2);
            out_stats->total_soft_skips  = sqlite3_column_int(stmt, 3);
            out_stats->total_listen_sec  = (uint32_t)sqlite3_column_int64(stmt, 4);
            out_stats->global_avg_energy = (float)sqlite3_column_double(stmt, 5);
        }
        sqlite3_finalize(stmt);
    }

    const char *sql_sess = "SELECT COUNT(*) FROM listening_sessions;";
    if (sqlite3_prepare_v2(s_pdb, sql_sess, -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            out_stats->total_sessions = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    pthread_mutex_unlock(&s_pdb_mutex);
}