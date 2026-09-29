#define _DEFAULT_SOURCE
#include "sparkles_context_menu.h"
#include "sparkles_theme.h"
#include "sparkles_widgets.h"
#include "state.h"
#include "playlist_manager.h"
#include "sparkles_text_prompt.h"
#include "input/sparkles_input.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

typedef enum {
    CMENU_TRACK = 0,
    CMENU_PLAYLIST
} ContextMenuMode;

typedef enum {
    CMENU_PAGE_ACTIONS = 0,
    CMENU_PAGE_PLAYLISTS
} ContextMenuPage;

static ContextMenuMode s_cmenu_mode = CMENU_TRACK;
static ContextMenuPage s_cmenu_page = CMENU_PAGE_ACTIONS;
static bool s_menu_open = false;
static bool s_just_opened = false;
static Rectangle s_menu_rect = {0};
static SparklesTrackItem s_track = {0};
static char s_target_playlist[128] = {0};
static float s_pl_scroll = 0.0f;
static Vector2 s_open_pos = {0};

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
    s_cmenu_page = CMENU_PAGE_ACTIONS;
    s_pl_scroll = 0.0f;
}

static void calculate_menu_layout(void) {
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float ui_scale = sparkles_get_ui_scale();
    bool is_mobile = (sh > sw) || (sw < 600.0f);

    int item_count = 0;
    if (s_cmenu_page == CMENU_PAGE_PLAYLISTS) {
        item_count = 6;
    } else if (s_cmenu_mode == CMENU_PLAYLIST) {
        item_count = (strcasecmp(s_target_playlist, "Favourites") == 0) ? 2 : 3;
    } else {
        item_count = s_track.in_playlist ? 5 : 4;
    }

    if (is_mobile) {
        float item_h = fmaxf(48.0f, 44.0f * ui_scale);
        float header_h = 44.0f * ui_scale;
        float cancel_h = item_h;
        float total_h = header_h + (item_count * item_h) + cancel_h + (16.0f * ui_scale);
        if (total_h > sh - 40.0f) total_h = sh - 40.0f;

        float menu_w = fminf(sw - 28.0f * ui_scale, 440.0f * ui_scale);
        float x = (sw - menu_w) * 0.5f;
        float y = (sh - total_h) * 0.5f;
        s_menu_rect = (Rectangle){ x, y, menu_w, total_h };
    } else {
        float item_h = fmaxf(32.0f, 28.0f * ui_scale);
        float menu_w = fmaxf(240.0f, 220.0f * ui_scale);
        float total_h = (item_count * item_h) + (18.0f * ui_scale);

        float x = s_open_pos.x;
        float y = s_open_pos.y;
        if (x + menu_w > sw - 12.0f) x = sw - menu_w - 12.0f;
        if (y + total_h > sh - 12.0f) y = sh - total_h - 12.0f;
        if (x < 12.0f) x = 12.0f;
        if (y < 12.0f) y = 12.0f;
        s_menu_rect = (Rectangle){ x, y, menu_w, total_h };
    }
}

void sparkles_context_menu_open(Vector2 mouse_pos, const SparklesTrackItem *track) {
    if (!track) return;
    s_cmenu_mode = CMENU_TRACK;
    s_cmenu_page = CMENU_PAGE_ACTIONS;
    s_track = *track;
    s_open_pos = mouse_pos;
    s_menu_open = true;
    s_just_opened = true;
    s_pl_scroll = 0.0f;
    calculate_menu_layout();
}

void sparkles_context_menu_open_playlist(Vector2 mouse_pos, const char *playlist_name) {
    if (!playlist_name || !playlist_name[0]) return;
    s_cmenu_mode = CMENU_PLAYLIST;
    s_cmenu_page = CMENU_PAGE_ACTIONS;
    strncpy(s_target_playlist, playlist_name, sizeof(s_target_playlist) - 1);
    s_open_pos = mouse_pos;
    s_menu_open = true;
    s_just_opened = true;
    s_pl_scroll = 0.0f;
    calculate_menu_layout();
}

void sparkles_context_menu_close(void) {
    s_menu_open = false;
    s_cmenu_page = CMENU_PAGE_ACTIONS;
}

bool sparkles_context_menu_is_open(void) {
    return s_menu_open;
}

bool sparkles_context_menu_update(void) {
    if (!s_menu_open) return false;
    if (s_just_opened) {
        s_just_opened = false;
        sparkles_input_consume();
        return true;
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        if (s_cmenu_page == CMENU_PAGE_PLAYLISTS) {
            s_cmenu_page = CMENU_PAGE_ACTIONS;
            calculate_menu_layout();
        } else {
            sparkles_context_menu_close();
        }
        return true;
    }

    // Dismiss when tapped outside
    if (sparkles_input_consume_tap_outside(s_menu_rect, NULL)) {
        sparkles_context_menu_close();
        return true;
    }

    return true;
}

void sparkles_context_menu_render(float screen_w, float screen_h) {
    if (!s_menu_open) return;

    calculate_menu_layout();
    float ui_scale = sparkles_get_ui_scale();
    bool is_mobile = (screen_h > screen_w) || (screen_w < 600.0f);
    Vector2 mouse = GetMousePosition();

    // Overlay on mobile
    if (is_mobile) {
        DrawRectangle(0, 0, (int)screen_w, (int)screen_h, ColorAlpha(BLACK, 0.65f));
    }

    DrawRectangle((int)s_menu_rect.x + 4, (int)s_menu_rect.y + 4, (int)s_menu_rect.width, (int)s_menu_rect.height, (Color){ 0, 0, 0, 160 });
    DrawRectangleRec(s_menu_rect, (Color){ 14, 15, 20, 252 });
    DrawRectangleLinesEx(s_menu_rect, 1.2f, COLOR_ACCENT);
    DrawNothingCornerBrackets(s_menu_rect, 8.0f * ui_scale, COLOR_ACCENT);

    float item_h = is_mobile ? fmaxf(48.0f, 44.0f * ui_scale) : fmaxf(32.0f, 28.0f * ui_scale);
    float pad_y = 10.0f * ui_scale;

    // Header bar (mobile)
    if (is_mobile) {
        float header_h = 44.0f * ui_scale;
        const char *head_txt = (s_cmenu_mode == CMENU_PLAYLIST) ? s_target_playlist : (s_track.title[0] ? s_track.title : s_track.path);
        DrawRectangle((int)s_menu_rect.x, (int)s_menu_rect.y, (int)s_menu_rect.width, (int)header_h, (Color){ 10, 11, 14, 255 });
        DrawLine((int)s_menu_rect.x, (int)(s_menu_rect.y + header_h), (int)(s_menu_rect.x + s_menu_rect.width), (int)(s_menu_rect.y + header_h), (Color){ 30, 32, 42, 255 });

        Rectangle head_box = { s_menu_rect.x + 14.0f * ui_scale, s_menu_rect.y + 10.0f * ui_scale, s_menu_rect.width - 28.0f * ui_scale, header_h - 16.0f * ui_scale };
        DrawTextMarquee(head_txt, head_box, (Rectangle){ 0, 0, 0, 0 }, FONT_SIZE_SM, COLOR_TEXT_PRIMARY, 25.0f);
        pad_y = header_h + 8.0f * ui_scale;
    }

    if (s_cmenu_page == CMENU_PAGE_ACTIONS) {
        const char *items[5];
        int num_items = 0;

        if (s_cmenu_mode == CMENU_PLAYLIST) {
            items[num_items++] = ">  Play Playlist";
            items[num_items++] = "+  Add to Queue";
            if (strcasecmp(s_target_playlist, "Favourites") != 0) {
                items[num_items++] = "✕  Delete Playlist";
            }
        } else {
            bool is_fav = playlist_mgmt_is_favourite(s_track.path);
            items[num_items++] = ">  Play Next";
            items[num_items++] = "+  Add to Queue";
            items[num_items++] = is_fav ? "★  Remove from Favourites" : "☆  Add to Favourites";
            items[num_items++] = ">  Add to Playlist…";
            if (s_track.in_playlist) {
                items[num_items++] = "X  Remove from Playlist";
            }
        }

        for (int i = 0; i < num_items; i++) {
            Rectangle item_r = { s_menu_rect.x + 6.0f * ui_scale, s_menu_rect.y + pad_y + i * item_h, s_menu_rect.width - 12.0f * ui_scale, item_h };
            bool hover = CheckCollisionPointRec(mouse, item_r);

            if (hover) {
                DrawRectangleRec(item_r, ColorAlpha(COLOR_ACCENT, 0.18f));
                DrawRectangle((int)item_r.x, (int)item_r.y + 3, 3, (int)item_r.height - 6, COLOR_ACCENT);
            }

            int text_y = (int)(item_r.y + (item_h - FONT_SIZE_SM) * 0.5f);
            DrawText(items[i], (int)(item_r.x + 14.0f * ui_scale), text_y, FONT_SIZE_SM, hover ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED);

            if (sparkles_input_consume_tap(item_r, NULL)) {
                if (s_cmenu_mode == CMENU_PLAYLIST) {
                    LoadedPlaylist lp;
                    if (playlist_mgmt_load_playlist(s_target_playlist, &lp) && lp.count > 0) {
                        if (i == 0) {
                            pthread_mutex_lock(&state_mutex);
                            if (active_playlist_playback.paths) {
                                for (int k = 0; k < active_playlist_playback.count; k++) {
                                    free(active_playlist_playback.paths[k]);
                                    free(active_playlist_playback.titles[k]);
                                }
                                free(active_playlist_playback.paths);
                                free(active_playlist_playback.titles);
                            }
                            active_playlist_playback.count = lp.count;
                            active_playlist_playback.paths = malloc(sizeof(char*) * lp.count);
                            active_playlist_playback.titles = malloc(sizeof(char*) * lp.count);
                            for (int k = 0; k < lp.count; k++) {
                                active_playlist_playback.paths[k] = strdup(lp.items[k].path);
                                active_playlist_playback.titles[k] = strdup(lp.items[k].title[0] ? lp.items[k].title : lp.items[k].path);
                            }
                            strncpy(active_playlist_playback.name, s_target_playlist, sizeof(active_playlist_playback.name) - 1);
                            current_play_source = SOURCE_PLAYLIST;
                            playing_file_idx = 0;
                            strncpy(playing_filepath, lp.items[0].path, sizeof(playing_filepath) - 1);
                            strncpy(playing_filename, lp.items[0].title[0] ? lp.items[0].title : lp.items[0].path, 255);
                            pthread_mutex_unlock(&state_mutex);

                            atomic_store(&seek_target_ms, -1);
                            atomic_store(&current_cmd_atomic, CMD_PLAY);
                        } else if (i == 1) {
                            pthread_mutex_lock(&state_mutex);
                            for (int k = 0; k < lp.count; k++) {
                                if (num_playlist_files >= playlist_capacity) {
                                    playlist_capacity = playlist_capacity == 0 ? 1024 : playlist_capacity * 2;
                                    playlist = realloc(playlist, sizeof(PlaylistEntry) * playlist_capacity);
                                }
                                int qi = num_playlist_files++;
                                strncpy(playlist[qi].path, lp.items[k].path, sizeof(playlist[qi].path) - 1);
                                strncpy(playlist[qi].name, lp.items[k].title[0] ? lp.items[k].title : lp.items[k].path, 255);
                                playlist[qi].display_width = MeasureSparklesText(playlist[qi].name, FONT_SIZE_SM);
                                memset(&playlist[qi].meta, 0, sizeof(KoniMetadata));
                                playlist[qi].duration_sec = lp.items[k].duration_sec;
                            }
                            pthread_mutex_unlock(&state_mutex);
                        }
                        playlist_mgmt_free_loaded(&lp);
                    }
                    if (i == 2 && strcasecmp(s_target_playlist, "Favourites") != 0) {
                        playlist_mgmt_delete(s_target_playlist);
                        tile_playlists_refresh();
                    }
                    sparkles_context_menu_close();
                    return;
                }

                if (i == 0) {
                    queue_play_next(&s_track);
                    sparkles_context_menu_close();
                } else if (i == 1) {
                    queue_add_tail(&s_track);
                    sparkles_context_menu_close();
                } else if (i == 2) {
                    playlist_mgmt_toggle_favourite(s_track.path, s_track.title, s_track.artist, s_track.duration_sec);
                    tile_playlists_refresh();
                    sparkles_context_menu_close();
                } else if (i == 3) {
                    s_cmenu_page = CMENU_PAGE_PLAYLISTS;
                    s_pl_scroll = 0.0f;
                    calculate_menu_layout();
                } else if (i == 4 && s_track.in_playlist) {
                    playlist_mgmt_remove_track(s_track.playlist_name, s_track.playlist_track_idx);
                    tile_playlists_refresh();
                    sparkles_context_menu_close();
                }
            }
        }

        if (is_mobile) {
            float cancel_y = s_menu_rect.y + pad_y + (num_items * item_h) + 4.0f * ui_scale;
            Rectangle cancel_r = { s_menu_rect.x + 6.0f * ui_scale, cancel_y, s_menu_rect.width - 12.0f * ui_scale, item_h };
            bool hover_can = CheckCollisionPointRec(mouse, cancel_r);
            DrawRectangleRec(cancel_r, (Color){ 20, 22, 28, 255 });
            DrawRectangleLinesEx(cancel_r, 1.0f, hover_can ? COLOR_ACCENT : (Color){ 34, 38, 48, 255 });
            int tw_c = MeasureText("Cancel", FONT_SIZE_SM);
            DrawText("Cancel", (int)(cancel_r.x + (cancel_r.width - tw_c) * 0.5f), (int)(cancel_r.y + (item_h - FONT_SIZE_SM) * 0.5f), FONT_SIZE_SM, hover_can ? COLOR_ACCENT : COLOR_TEXT_MUTED);

            if (sparkles_input_consume_tap(cancel_r, NULL)) {
                sparkles_context_menu_close();
                return;
            }
        }
    } else {
        // Add To Playlist (2nd page)
        Rectangle back_r = { s_menu_rect.x + 6.0f * ui_scale, s_menu_rect.y + pad_y, s_menu_rect.width - 12.0f * ui_scale, item_h };
        bool hover_back = CheckCollisionPointRec(mouse, back_r);
        DrawText("<  Back", (int)(back_r.x + 10.0f * ui_scale), (int)(back_r.y + (item_h - FONT_SIZE_SM) * 0.5f), FONT_SIZE_SM, hover_back ? WHITE : COLOR_ACCENT);

        if (sparkles_input_consume_tap(back_r, NULL)) {
            s_cmenu_page = CMENU_PAGE_ACTIONS;
            calculate_menu_layout();
            return;
        }

        float py = back_r.y + back_r.height + 4.0f * ui_scale;

        Rectangle new_pl_r = { s_menu_rect.x + 6.0f * ui_scale, py, s_menu_rect.width - 12.0f * ui_scale, item_h };
        bool hover_new = CheckCollisionPointRec(mouse, new_pl_r);
        if (hover_new) DrawRectangleRec(new_pl_r, ColorAlpha(COLOR_ACCENT, 0.18f));
        DrawText("+  [Create New Playlist]", (int)(new_pl_r.x + 14.0f * ui_scale), (int)(new_pl_r.y + (item_h - FONT_SIZE_SM) * 0.5f), FONT_SIZE_SM, hover_new ? COLOR_ACCENT : COLOR_TEXT_PRIMARY);

        if (sparkles_input_consume_tap(new_pl_r, NULL)) {
            sparkles_context_menu_close();
            sparkles_text_prompt_open(&(SparklesTextPromptConfig){
                .tag = "NEW PLAYLIST",
                .prompt = "New Playlist Name",
                .submit_label = "Create",
                .max_len = 64,
                .on_submit = on_new_playlist_submitted
            });
            return;
        }

        py += item_h + 4.0f * ui_scale;

        int pl_count = playlist_mgmt_get_count();
        float list_h = (s_menu_rect.y + s_menu_rect.height) - py - 8.0f * ui_scale;
        Rectangle list_box = { s_menu_rect.x + 6.0f * ui_scale, py, s_menu_rect.width - 12.0f * ui_scale, list_h };

        if (CheckCollisionPointRec(mouse, list_box)) {
            float scr = sparkles_input_get_scroll_delta(list_box);
            if (scr != 0.0f) s_pl_scroll += scr * item_h;
        }
        float max_scr = fmaxf(0.0f, (float)pl_count * item_h - list_h);
        if (s_pl_scroll < 0.0f) s_pl_scroll = 0.0f;
        if (s_pl_scroll > max_scr) s_pl_scroll = max_scr;

        BeginScissorMode((int)list_box.x, (int)list_box.y, (int)list_box.width, (int)list_h);

        for (int i = 0; i < pl_count; i++) {
            float row_y = py + i * item_h - s_pl_scroll;
            if (row_y + item_h < py || row_y > py + list_h) continue;

            const PlaylistSummary *ps = playlist_mgmt_get_summary(i);
            if (!ps) continue;

            Rectangle row_r = { list_box.x, row_y, list_box.width, item_h };
            bool hover = CheckCollisionPointRec(mouse, row_r);
            if (hover) DrawRectangleRec(row_r, (Color){ 22, 25, 34, 255 });

            DrawText(ps->name, (int)(row_r.x + 14.0f * ui_scale), (int)(row_r.y + (item_h - FONT_SIZE_SM) * 0.5f), FONT_SIZE_SM, hover ? COLOR_ACCENT : COLOR_TEXT_MUTED);

            if (sparkles_input_consume_tap(row_r, NULL)) {
                playlist_mgmt_add_track(ps->name, s_track.path, s_track.title, s_track.artist, s_track.duration_sec);
                tile_playlists_refresh();
                sparkles_context_menu_close();
                break;
            }
        }

        EndScissorMode();
    }

    if (is_mobile) {
        sparkles_input_block_area((Rectangle){ 0, 0, screen_w, screen_h });
    } else {
        sparkles_input_block_area(s_menu_rect);
    }
}