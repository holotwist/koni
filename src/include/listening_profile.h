#ifndef KONI_LISTENING_PROFILE_H
#define KONI_LISTENING_PROFILE_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    int total_plays;
    int total_completes;
    int total_hard_skips;
    int total_soft_skips;
    int total_sessions;
    uint32_t total_listen_sec;
    int favorite_hour; // 0..23
    float global_avg_energy;
} ProfileOverview;

// Submodule lifecycle
bool listening_profile_init(void);
void listening_profile_shutdown(void);
void listening_profile_reset_db(void);

// Playback lifecycle telemetry hooks
void listening_profile_on_track_start(const char *path, const char *title, const char *artist);
void listening_profile_on_track_end(const char *path, uint32_t played_sec, uint32_t total_sec, float measured_energy);

// Scoring & selection algorithms
float listening_profile_get_score(const char *path, const char *title, const char *artist, float target_energy);
int   listening_profile_pick_weighted(const char **paths, int count, float target_energy);
void  listening_profile_get_overview(ProfileOverview *out_stats);

#endif // KONI_LISTENING_PROFILE_H