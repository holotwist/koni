#ifndef PLAYER_STATE_H
#define PLAYER_STATE_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include "codec.h"

#define VIS_BUF_SIZE  65536u
#define VIS_BUF_MASK  (VIS_BUF_SIZE - 1u)
#define FFT_SIZE      1024

typedef struct {
    char name[256];
    int is_dir;
    int display_width;
    KoniMetadata meta;
    uint32_t duration_sec;
    bool metadata_loaded;
} FileEntry;

typedef struct {
    char path[1024];
    char name[256];
    int display_width;
    KoniMetadata meta;
    uint32_t duration_sec;
    bool metadata_loaded;
} PlaylistEntry;

typedef enum {
    TAB_QUEUE = 0,
    TAB_MUSIC = 1,
    TAB_FILES = 2
} BrowserTab;

typedef enum {
    SOURCE_FILES,
    SOURCE_QUEUE,
    SOURCE_LIBRARY
} PlaybackSource;

typedef enum {
    STATE_STOPPED,
    STATE_PLAYING,
    STATE_PAUSED
} PlayState;

typedef enum {
    CMD_NONE,
    CMD_PLAY,
    CMD_PAUSE,
    CMD_NEXT,
    CMD_NEXT_AUTO, // When a song finishes
    CMD_PREV,
    CMD_STOP,
    CMD_QUIT,
    CMD_SEEK
} PlayerCommand;

typedef enum {
    REPEAT_OFF = 0,
    REPEAT_ALL,
    REPEAT_ONE
} RepeatMode;

/* Shared Globals */
extern pthread_mutex_t state_mutex;

extern char current_dir[1024];
extern FileEntry *files;
extern int num_files;
extern int files_capacity;
extern int selected_file_idx;
extern int scroll_offset;

#include "db.h"

extern PlaylistEntry *playlist;
extern int num_playlist_files;
extern int playlist_capacity;
extern int selected_playlist_idx;
extern int playlist_scroll_offset;

extern DBTrack *library_tracks;
extern int num_library_tracks;
extern int selected_library_idx;
extern int library_scroll_offset;

extern BrowserTab current_browser_tab;
extern PlaybackSource current_play_source;

void library_reload(void);
void load_state(void);
void save_state(void);

extern char playing_filepath[1024];
extern char playing_filename[256];
extern int playing_file_idx;

extern KoniAudioFormat p_format;
extern KoniMetadata p_metadata;

extern atomic_int  header_ready_for_idx;
extern atomic_int  play_state_atomic;
extern atomic_int  current_cmd_atomic;
extern atomic_int  current_track_id;
extern atomic_int  volume;
extern atomic_int  seek_target_ms;
extern atomic_int  play_mode_shuffle;
extern atomic_int  play_mode_repeat;

extern int play_history[256];
extern int history_len;
extern int history_idx;

extern atomic_uint p_current_sec;
extern atomic_uint p_total_sec;

extern int active_tab;
extern int current_vis_mode;
extern bool is_fullscreen;
extern bool force_redraw;

extern bool show_help_bar;
extern bool force_vertical_layout;
extern bool show_visualizer;
extern bool show_lrc_overlay;
extern int saved_volume;
extern atomic_int play_mode_rgain;

extern float       vis_ring_l[VIS_BUF_SIZE];
extern float       vis_ring_r[VIS_BUF_SIZE];
extern atomic_uint vis_wpos;
extern atomic_uint vis_srate;
extern atomic_uint p_frames_consumed;

bool player_advance_track(PlayerCommand cmd);

#endif // PLAYER_STATE_H