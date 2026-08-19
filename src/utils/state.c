#include "state.h"

pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

char current_dir[1024] = ".";
FileEntry files[MAX_FILES];
int num_files = 0;
int selected_file_idx = 0;
int scroll_offset = 0;

PlaylistEntry playlist[MAX_PLAYLIST_FILES];
int num_playlist_files = 0;
int selected_playlist_idx = 0;
int playlist_scroll_offset = 0;
bool playing_from_playlist = false;
UIFocus current_focus = FOCUS_FILES;

char playing_filepath[1024] = "";
char playing_filename[256] = "<Empty>";
int playing_file_idx = -1;

KoniAudioFormat p_format = {0};
KoniMetadata p_metadata = {0};

atomic_int header_ready_for_idx = -1;
atomic_int play_state_atomic = STATE_STOPPED;
atomic_int current_cmd_atomic = CMD_NONE;
atomic_int volume = 100;
atomic_int seek_target_sec = -1;
atomic_int play_mode_shuffle = 0;
atomic_int play_mode_repeat = 0; // 0=Off, 1=All, 2=One

atomic_uint p_current_sec = 0;
atomic_uint p_total_sec = 0;

int active_tab = 1;
int current_vis_mode = 0;
bool is_fullscreen = false;
bool force_redraw = true;

float vis_ring_l[VIS_BUF_SIZE] = {0};
float vis_ring_r[VIS_BUF_SIZE] = {0};
atomic_uint vis_wpos = 0;
atomic_uint vis_srate = 44100;
atomic_uint p_frames_consumed = 0;