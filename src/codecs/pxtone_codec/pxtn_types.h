/*
 * PxTone sound format playback engine for Koni.
 * Based on the PxTone audio engine created by Daisuke "Pixel" Amaya (Studio Pixel).
 */

#ifndef PXTONE_TYPES_H
#define PXTONE_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_UNITS 64
#define MAX_VOICES 100
#define MAX_DELAYS 4
#define MAX_OVERDRIVES 2
#define MAX_GROUPS 7
#define TIMEPAN_BUF_SIZE 64
#define NOISE_TABLE_SIZE 441

enum {
    PX_EVENT_NULL = 0,
    PX_EVENT_ON,
    PX_EVENT_KEY,
    PX_EVENT_PAN_VOLUME,
    PX_EVENT_VELOCITY,
    PX_EVENT_VOLUME,
    PX_EVENT_PORTAMENTO,
    PX_EVENT_BEATCLOCK,
    PX_EVENT_BEATTEMPO,
    PX_EVENT_BEATNUM,
    PX_EVENT_REPEAT,
    PX_EVENT_LAST,
    PX_EVENT_VOICENO,
    PX_EVENT_GROUPNO,
    PX_EVENT_TUNING,
    PX_EVENT_PAN_TIME
};

typedef struct {
    int32_t clock;
    uint8_t unit_no;
    uint8_t kind;
    int32_t value;
} PxEvent;

typedef struct {
    int32_t x;
    int32_t y;
} PxPoint;

typedef struct {
    int32_t smp_body_w;
    int16_t *p_smp_w;
    uint8_t *p_env;
    int32_t env_size;
    int32_t env_release;
    bool has_env;
    bool loop;
    bool smooth;
    float tuning;
    int32_t basic_key;
} PxVoiceUnit;

typedef struct {
    int num_units;
    PxVoiceUnit units[2];
} PxVoice;

typedef struct {
    double smp_pos;
    float offset_freq;
    int32_t env_volume;
    int32_t life_count;
    int32_t on_count;
    int32_t env_start;
    int32_t env_pos;
    int32_t env_release_clock;
} PxVoiceTone;

typedef struct {
    int voice_idx;
    int group_no;
    int velocity;
    int volume;
    float tuning;
    int key_now;
    int key_start;
    int key_margin;
    int portamento_num;
    int portamento_pos;
    int pan_vols[2];
    int pan_times[2];
    int pan_time_bufs[TIMEPAN_BUF_SIZE][2];
    PxVoiceTone tones[2];
} PxUnit;

typedef struct {
    int group;
    int smp_num;
    int offset;
    int rate_pct;
    int32_t *bufs[2];
} PxDelay;

typedef struct {
    int group;
    float cut_f;
    float amp_f;
    int32_t cut_top;
} PxOverDrive;

typedef struct PxtnTiny PxtnTiny;

typedef struct {
    char name[64];
    char comment[256];
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t total_samples;
    uint32_t duration_sec;
    int beat_num;
    float beat_tempo;
} PxtnSongInfo;

struct PxtnTiny {
    PxtnSongInfo info;
    uint32_t dst_sps;
    float clock_rate;
    float smp_stride;
    int smp_smooth;
    uint64_t cur_sample;
    uint64_t total_samples;
    int cur_event_idx;
    int time_pan_idx;

    PxEvent *events;
    int event_count;

    PxVoice voices[MAX_VOICES];
    int voice_count;

    PxUnit units[MAX_UNITS];
    int unit_count;

    PxDelay delays[MAX_DELAYS];
    int delay_count;

    PxOverDrive ovdrvs[MAX_OVERDRIVES];
    int ovdrv_count;

    int32_t group_smps[MAX_GROUPS];
};

#endif // PXTONE_TYPES_H