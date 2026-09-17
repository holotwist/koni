#define _DEFAULT_SOURCE
#include "sparkles_context_menu.h"
#include "sparkles_theme.h"
#include "sparkles_widgets.h"
#include "state.h"
#include "playlist_manager.h"
#include "sparkles_text_prompt.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

typedef enum {
    CMENU_TRACK = 0,
    CMENU_PLAYLIST
} ContextMenuMode;

static ContextMenuMode s_cmenu_mode = CMENU_TRACK;
static bool s_menu_open = false;
static bool s_just_opened = false;
static Rectangle s_menu_rect = {0};
static SparklesTrackItem s_track = {0};
static char s_target_playlist[128] = {0};
static int s_hovered_idx = -1;
static bool s_show_subplaylists = false;
static int s_sub_hovered_idx = -1;

#define MENU_WIDTH 210.0f
#define ITEM_HEIGHT 28.0f

static void queue_play_next(const SparklesTrackItem *item) {
    pthread_mutex_lock(&state_mutex);
    if (num_playlist_files >= playlist_capacity) {
        playlist_capacity = playlist_capacity == 0 ? 1024 : playlist_capacity * 2;
        playlist = realloc(playlist, sizeof(PlaylistEntry) * playlist_capacity);
    }
    int insert_pos = (current_play_source == SOURCE_QUEUE && num_playlist_files > 0) ? 1 : 0;
    for (int i = num_playlist_files; i > insert_pos; i--) {
        playlist[i] = playlist[i - 1];
    }
    strncpy(playlist[insert_pos].path, item->path, sizeof(playlist[insert_pos].path) - 1);
    const char *name = (item->title[0]) ? item->title : item->path;
    const char *slash = strrchr(name, '/');
    strncpy(playlist[insert_pos].name, slash ? slash + 1 : name, 255);
    playlist[insert_pos].display_width = MeasureSparklesText(playlist[insert_pos].name, FONT_SIZE_SM);
    memset(&playlist[insert_pos].meta, 0, sizeof(KoniMetadata));
    if (item->title[0]) playlist[insert_pos].meta.title = strdup(item->title);
    if (item->artist[0]) playlist[insert_pos].meta.artist = strdup(item->artist);
    if (item->album[0]) playlist[insert_pos].meta.album = strdup(item->album);
    playlist[insert_pos].duration_sec = item->duration_sec;
    playlist[insert_pos].metadata_loaded = true;
    num_playlist_files++;
    pthread_mutex_unlock(&state_mutex);
}

static void play_entire_playlist(const char *name) {
    LoadedPlaylist lp;
    if (!playlist_mgmt_load_playlist(name, &lp) || lp.count == 0) return;

    pthread_mutex_lock(&state_mutex);
    if (active_playlist_playback.paths) {
        for (int i = 0; i < active_playlist_playback.count; i++) {
            free(active_playlist_playback.paths[i]);
            free(active_playlist_playback.titles[i]);
        }
        free(active_playlist_playback.paths);
        free(active_playlist_playback.titles);
    }

    active_playlist_playback.count = lp.count;
    active_playlist_playback.paths = malloc(sizeof(char*) * lp.count);
    active_playlist_playback.titles = malloc(sizeof(char*) * lp.count);
    for (int i = 0; i < lp.count; i++) {
        active_playlist_playback.paths[i] = strdup(lp.items[i].path);
        active_playlist_playback.titles[i] = strdup(lp.items[i].title[0] ? lp.items[i].title : lp.items[i].path);
    }
    strncpy(active_playlist_playback.name, name, sizeof(active_playlist_playback.name) - 1);
    current_play_source = SOURCE_PLAYLIST;
    playing_file_idx = 0;
    strncpy(playing_filepath, lp.items[0].path, sizeof(playing_filepath) - 1);
    strncpy(playing_filename, lp.items[0].title[0] ? lp.items[0].title : lp.items[0].path, 255);
    pthread_mutex_unlock(&state_mutex);

    atomic_store(&seek_target_ms, -1);
    atomic_store(&current_cmd_atomic, CMD_PLAY);
    playlist_mgmt_free_loaded(&lp);
}

static void queue_add_all_from_playlist(const char *name) {
    LoadedPlaylist lp;
    if (!playlist_mgmt_load_playlist(name, &lp) || lp.count == 0) return;

    pthread_mutex_lock(&state_mutex);
    for (int i = 0; i < lp.count; i++) {
        if (num_playlist_files >= playlist_capacity) {
            playlist_capacity = playlist_capacity == 0 ? 1024 : playlist_capacity * 2;
            playlist = realloc(playlist, sizeof(PlaylistEntry) * playlist_capacity);
        }
        int idx = num_playlist_files;
        strncpy(playlist[idx].path, lp.items[i].path, sizeof(playlist[idx].path) - 1);
        const char *t = lp.items[i].title[0] ? lp.items[i].title : lp.items[i].path;
        const char *slash = strrchr(t, '/');
        strncpy(playlist[idx].name, slash ? slash + 1 : t, 255);
        playlist[idx].display_width = MeasureSparklesText(playlist[idx].name, FONT_SIZE_SM);
        memset(&playlist[idx].meta, 0, sizeof(KoniMetadata));
        playlist[idx].meta.title = strdup(t);
        playlist[idx].duration_sec = lp.items[i].duration_sec;
        playlist[idx].metadata_loaded = true;
        num_playlist_files++;
    }
    pthread_mutex_unlock(&state_mutex);
    playlist_mgmt_free_loaded(&lp);
}

static void queue_add_tail(const SparklesTrackItem *item) {
    pthread_mutex_lock(&state_mutex);
    if (num_playlist_files >= playlist_capacity) {
        playlist_capacity = playlist_capacity == 0 ? 1024 : playlist_capacity * 2;
        playlist = realloc(playlist, sizeof(PlaylistEntry) * playlist_capacity);
    }
    int idx = num_playlist_files;
    strncpy(playlist[idx].path, item->path, sizeof(playlist[idx].path) - 1);
    const char *name = (item->title[0]) ? item->title : item->path;
    const char *slash = strrchr(name, '/');
    strncpy(playlist[idx].name, slash ? slash + 1 : name, 255);
    playlist[idx].display_width = MeasureSparklesText(playlist[idx].name, FONT_SIZE_SM);
    memset(&playlist[idx].meta, 0, sizeof(KoniMetadata));
    if (item->title[0]) playlist[idx].meta.title = strdup(item->title);
    if (item->artist[0]) playlist[idx].meta.artist = strdup(item->artist);
    if (item->album[0]) playlist[idx].meta.album = strdup(item->album);
    playlist[idx].duration_sec = item->duration_sec;
    playlist[idx].metadata_loaded = true;
    num_playlist_files++;
    pthread_mutex_unlock(&state_mutex);
}

static void on_new_playlist_submitted(const char *name, void *ud) {
    (void)ud;
    if (!name || !name[0]) return;
    playlist_mgmt_create(name);
    playlist_mgmt_add_track(name, s_track.path, s_track.title, s_track.artist, s_track.duration_sec);
    tile_playlists_refresh();
}

void sparkles_context_menu_init(void) {
    s_menu_open = false;
    s_show_subplaylists = false;
}

void sparkles_context_menu_open(Vector2 mouse_pos, const SparklesTrackItem *track) {
    if (!track) return;
    s_cmenu_mode = CMENU_TRACK;
    s_track = *track;
    s_menu_open = true;
    s_just_opened = true;
    s_show_subplaylists = false;

    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();

    int item_count = s_track.in_playlist ? 5 : 4;
    float total_h = item_count * ITEM_HEIGHT + 16.0f;
    float x = mouse_pos.x;
    float y = mouse_pos.y;

    if (x + MENU_WIDTH > sw - 10.0f) x = sw - MENU_WIDTH - 10.0f;
    if (y + total_h > sh - 10.0f) y = sh - total_h - 10.0f;
    if (x < 10.0f) x = 10.0f;
    if (y < 10.0f) y = 10.0f;

    s_menu_rect = (Rectangle){ x, y, MENU_WIDTH, total_h };
}

void sparkles_context_menu_open_playlist(Vector2 mouse_pos, const char *playlist_name) {
    if (!playlist_name || !playlist_name[0]) return;
    s_cmenu_mode = CMENU_PLAYLIST;
    strncpy(s_target_playlist, playlist_name, sizeof(s_target_playlist) - 1);
    s_menu_open = true;
    s_just_opened = true;
    s_show_subplaylists = false;

    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();

    bool is_fav = (strcasecmp(s_target_playlist, "Favourites") == 0);
    int item_count = is_fav ? 2 : 3;
    float total_h = item_count * ITEM_HEIGHT + 16.0f;
    float x = mouse_pos.x;
    float y = mouse_pos.y;

    if (x + MENU_WIDTH > sw - 10.0f) x = sw - MENU_WIDTH - 10.0f;
    if (y + total_h > sh - 10.0f) y = sh - total_h - 10.0f;
    if (x < 10.0f) x = 10.0f;
    if (y < 10.0f) y = 10.0f;

    s_menu_rect = (Rectangle){ x, y, MENU_WIDTH, total_h };
}

void sparkles_context_menu_close(void) {
    s_menu_open = false;
    s_show_subplaylists = false;
}

bool sparkles_context_menu_is_open(void) {
    return s_menu_open;
}

bool sparkles_context_menu_update(void) {
    Vector2 m = GetMousePosition();
    if (!s_menu_open) return false;
    if (s_just_opened) {
        s_just_opened = false;
        return true; // Consume opening frame (avoid double triggering)
    }
    bool on_main = CheckCollisionPointRec(m, s_menu_rect);

    int pl_count = playlist_mgmt_get_count();
    float sub_w = 200.0f;
    float sub_h = (float)(pl_count + 1) * ITEM_HEIGHT + 16.0f;
    Rectangle sub_rect = { s_menu_rect.x + s_menu_rect.width, s_menu_rect.y + 3 * ITEM_HEIGHT, sub_w, sub_h };
    if (sub_rect.x + sub_rect.width > (float)GetScreenWidth()) {
        sub_rect.x = s_menu_rect.x - sub_w;
    }
    bool on_sub = s_show_subplaylists && CheckCollisionPointRec(m, sub_rect);

    // Dismiss when left/right clicking outside menu
    if ((IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) && !on_main && !on_sub) {
        sparkles_context_menu_close();
        return true; // Consumed click outside
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        sparkles_context_menu_close();
        return true;
    }

    s_hovered_idx = -1;
    int max_main_items = (s_cmenu_mode == CMENU_PLAYLIST)
                         ? ((strcasecmp(s_target_playlist, "Favourites") == 0) ? 2 : 3)
                         : (s_track.in_playlist ? 5 : 4);

    if (on_main) {
        float rel_y = m.y - (s_menu_rect.y + 8.0f);
        if (rel_y >= 0.0f) {
            int idx = (int)(rel_y / ITEM_HEIGHT);
            if (idx >= 0 && idx < max_main_items) {
                s_hovered_idx = idx;
                s_show_subplaylists = (s_cmenu_mode == CMENU_TRACK && idx == 3);
            }
        }
    } else if (!on_sub) {
        s_show_subplaylists = false;
    }

    s_sub_hovered_idx = -1;
    if (on_sub) {
        float rel_y = m.y - (sub_rect.y + 8.0f);
        if (rel_y >= 0.0f) {
            int idx = (int)(rel_y / ITEM_HEIGHT);
            if (idx >= 0 && idx <= pl_count) {
                s_sub_hovered_idx = idx;
            }
        }
    }

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (s_cmenu_mode == CMENU_PLAYLIST) {
            if (s_hovered_idx == 0) { // Play Playlist
                play_entire_playlist(s_target_playlist);
                sparkles_context_menu_close();
            } else if (s_hovered_idx == 1) { // Add to Queue
                queue_add_all_from_playlist(s_target_playlist);
                sparkles_context_menu_close();
            } else if (s_hovered_idx == 2 && strcasecmp(s_target_playlist, "Favourites") != 0) { // Delete Playlist
                playlist_mgmt_delete(s_target_playlist);
                tile_playlists_refresh();
                sparkles_context_menu_close();
            }
            return true;
        }

        if (s_hovered_idx == 0) { // Play Next
            queue_play_next(&s_track);
            sparkles_context_menu_close();
        } else if (s_hovered_idx == 1) { // Add to Queue
            queue_add_tail(&s_track);
            sparkles_context_menu_close();
        } else if (s_hovered_idx == 2) { // Toggle Favourite
            playlist_mgmt_toggle_favourite(s_track.path, s_track.title, s_track.artist, s_track.duration_sec);
            tile_playlists_refresh();
            sparkles_context_menu_close();
        } else if (s_hovered_idx == 4 && s_track.in_playlist) { // Remove from Playlist
            playlist_mgmt_remove_track(s_track.playlist_name, s_track.playlist_track_idx);
            tile_playlists_refresh();
            sparkles_context_menu_close();
        } else if (s_sub_hovered_idx == 0) {
            s_menu_open = false;
            sparkles_text_prompt_open(&(SparklesTextPromptConfig){
                .tag = "NEW PLAYLIST",
                .prompt = "New Playlist Name",
                .submit_label = "Create",
                .max_len = 64,
                .on_submit = on_new_playlist_submitted
            });
            return true;
        } else if (s_sub_hovered_idx > 0 && s_sub_hovered_idx <= pl_count) {
            const PlaylistSummary *ps = playlist_mgmt_get_summary(s_sub_hovered_idx - 1);
            if (ps) {
                playlist_mgmt_add_track(ps->name, s_track.path, s_track.title, s_track.artist, s_track.duration_sec);
                tile_playlists_refresh();
            }
            sparkles_context_menu_close();
            return true;
        }
    }
    return true;
}

void sparkles_context_menu_render(float screen_w, float screen_h) {
    (void)screen_w; (void)screen_h;
    if (!s_menu_open) return;

    const char *items[5];
    int num_items = 0;

    if (s_cmenu_mode == CMENU_PLAYLIST) {
        items[num_items++] = "Play Playlist";
        items[num_items++] = "Add to Queue";
        if (strcasecmp(s_target_playlist, "Favourites") != 0) {
            items[num_items++] = "Delete Playlist";
        }
    } else {
        bool is_fav = playlist_mgmt_is_favourite(s_track.path);
        items[num_items++] = "Play Next";
        items[num_items++] = "Add to Queue";
        items[num_items++] = is_fav ? "Remove from Fav ★" : "Add to Fav ★";
        items[num_items++] = "Add to Playlist >";
        if (s_track.in_playlist) {
            items[num_items++] = "Remove from Playlist";
        }
    }

    // Backdrop shadow and panel
    DrawRectangle((int)s_menu_rect.x + 3, (int)s_menu_rect.y + 3, (int)s_menu_rect.width, (int)s_menu_rect.height, (Color){ 0, 0, 0, 160 });
    DrawRectangleRec(s_menu_rect, (Color){ 12, 13, 17, 250 });
    DrawRectangleLinesEx(s_menu_rect, 1.0f, (Color){ 36, 40, 50, 255 });
    DrawNothingCornerBrackets(s_menu_rect, 6.0f, COLOR_ACCENT);

    for (int i = 0; i < num_items; i++) {
        Rectangle item_r = { s_menu_rect.x + 4.0f, s_menu_rect.y + 8.0f + i * ITEM_HEIGHT, s_menu_rect.width - 8.0f, ITEM_HEIGHT };
        bool hover = (s_hovered_idx == i);

        if (hover) {
            DrawRectangleRec(item_r, ColorAlpha(COLOR_ACCENT, 0.18f));
            DrawRectangle((int)item_r.x, (int)item_r.y + 4, 2, (int)item_r.height - 8, COLOR_ACCENT);
        }

        DrawText(items[i], (int)item_r.x + 10, (int)item_r.y + 6, FONT_SIZE_SM, hover ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED);
    }

    // Submenu for Playlists
    if (s_show_subplaylists) {
        int pl_count = playlist_mgmt_get_count();
        float sub_w = 200.0f;
        float sub_h = (float)(pl_count + 1) * ITEM_HEIGHT + 16.0f;

        Rectangle sub_rect = { s_menu_rect.x + s_menu_rect.width, s_menu_rect.y + 3 * ITEM_HEIGHT, sub_w, sub_h };
        if (sub_rect.x + sub_rect.width > (float)GetScreenWidth()) {
            sub_rect.x = s_menu_rect.x - sub_w;
        }

        DrawRectangle((int)sub_rect.x + 3, (int)sub_rect.y + 3, (int)sub_rect.width, (int)sub_rect.height, (Color){ 0, 0, 0, 160 });
        DrawRectangleRec(sub_rect, (Color){ 12, 13, 17, 250 });
        DrawRectangleLinesEx(sub_rect, 1.0f, (Color){ 36, 40, 50, 255 });

        // + [New Playlist]
        Rectangle new_pl_r = { sub_rect.x + 4.0f, sub_rect.y + 8.0f, sub_rect.width - 8.0f, ITEM_HEIGHT };
        bool hover_new = (s_sub_hovered_idx == 0);
        if (hover_new) DrawRectangleRec(new_pl_r, ColorAlpha(COLOR_ACCENT, 0.18f));
        DrawText("+ [New Playlist]", (int)new_pl_r.x + 8, (int)new_pl_r.y + 6, FONT_SIZE_SM, hover_new ? COLOR_ACCENT : COLOR_TEXT_PRIMARY);

        // Existing playlists
        for (int i = 0; i < pl_count; i++) {
            const PlaylistSummary *ps = playlist_mgmt_get_summary(i);
            if (!ps) continue;

            Rectangle sub_item_r = { sub_rect.x + 4.0f, sub_rect.y + 8.0f + (i + 1) * ITEM_HEIGHT, sub_rect.width - 8.0f, ITEM_HEIGHT };
            bool hover = (s_sub_hovered_idx == i + 1);

            if (hover) {
                DrawRectangleRec(sub_item_r, ColorAlpha(COLOR_ACCENT, 0.18f));
            }

            DrawText(ps->name, (int)sub_item_r.x + 8, (int)sub_item_r.y + 6, FONT_SIZE_SM, hover ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED);
        }
    }
}