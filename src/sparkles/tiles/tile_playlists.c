#define _GNU_SOURCE
#include "sparkles_widgets.h"
#include "playlist_manager.h"
#include "state.h"
#include "modals/sparkles_context_menu.h"
#include "input/sparkles_input.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <math.h>

static bool s_drilldown = false;
static char s_active_pl_name[128] = {0};
static LoadedPlaylist s_loaded_pl = {0};
static int s_pl_scroll = 0;

static bool s_pl_search_active = false;
static char s_pl_search_buf[64] = {0};
static int s_pl_search_len = 0;

static bool str_contains_ci(const char *haystack, const char *needle) {
    if (!needle || !needle[0]) return true;
    if (!haystack) return false;
    return strcasestr(haystack, needle) != NULL;
}

bool tile_playlists_is_searching(void) {
    return s_pl_search_active;
}

static int s_pl_saved_scroll = 0;

void tile_playlists_open_search(void) {
    if (!s_drilldown) {
        s_pl_saved_scroll = s_pl_scroll;
        s_pl_search_active = true;
        s_pl_search_buf[0] = '\0';
        s_pl_search_len = 0;
        s_pl_scroll = 0;
    }
}

void tile_playlists_close_search(void) {
    s_pl_search_active = false;
    s_pl_search_buf[0] = '\0';
    s_pl_search_len = 0;
    s_pl_scroll = s_pl_saved_scroll;
    if (s_pl_scroll < 0) s_pl_scroll = 0;
}

void tile_playlists_refresh(void) {
    playlist_mgmt_refresh_list();
    if (s_drilldown && s_active_pl_name[0]) {
        playlist_mgmt_free_loaded(&s_loaded_pl);
        playlist_mgmt_load_playlist(s_active_pl_name, &s_loaded_pl);
        if (s_pl_scroll >= s_loaded_pl.count && s_pl_scroll > 0) {
            s_pl_scroll = s_loaded_pl.count - 1;
        }
    }
}

void tile_playlists_render(SparklesTile *tile, Rectangle b) {
    (void)tile;
    float ui_scale = sparkles_get_ui_scale();
    int row_h = (int)fmaxf(44.0f, 38.0f * ui_scale);
    float header_top = b.y + (16.0f * ui_scale);
    float list_start_y = header_top + (float)FONT_SIZE_LG + (14.0f * ui_scale);
    Vector2 mouse = GetMousePosition();

    if (!s_drilldown) {
        DrawText("Playlists", (int)(b.x + 16 * ui_scale), (int)header_top, FONT_SIZE_LG, COLOR_TEXT_PRIMARY);

        if (!s_pl_search_active) {
            int search_btn_w = MeasureText("[Search]", FONT_SIZE_SM);
            DrawText("[Search]", (int)(b.x + b.width - search_btn_w - 16 * ui_scale), (int)header_top + 2, FONT_SIZE_SM, COLOR_ACCENT);
        } else {
            int title_w = MeasureText("Playlists", FONT_SIZE_LG);
            float sbox_x = b.x + 16 * ui_scale + title_w + 14 * ui_scale;
            float sbox_w = b.width - (sbox_x - b.x) - (48 * ui_scale);
            if (sbox_w < 60) sbox_w = 60;
            Rectangle sbox = { sbox_x, header_top - 2, sbox_w, (float)FONT_SIZE_SM * 1.5f };

            DrawRectangleRounded(sbox, 0.2f, 4, (Color){ 32, 34, 40, 255 });
            DrawRectangleRoundedLinesEx(sbox, 0.2f, 4, 1.0f, COLOR_ACCENT);

            const char *cur = ((int)(GetTime() * 2.5f) % 2 == 0) ? "_" : " ";
            DrawText(TextFormat("/ %s%s", s_pl_search_buf, cur), (int)(sbox.x + 8), (int)(sbox.y + 4), FONT_SIZE_SM, COLOR_TEXT_PRIMARY);
            DrawText("[x]", (int)(b.x + b.width - 32 * ui_scale), (int)header_top + 2, FONT_SIZE_SM, COLOR_TEXT_MUTED);
        }

        int filtered[256];
        int count = 0;
        for (int p = 0; p < playlist_mgmt_get_count() && count < 256; p++) {
            const PlaylistSummary *ps = playlist_mgmt_get_summary(p);
            if (!s_pl_search_active || s_pl_search_len == 0 || (ps && str_contains_ci(ps->name, s_pl_search_buf))) {
                filtered[count++] = p;
            }
        }

        int max_rows = (int)(b.height - (list_start_y - b.y)) / row_h;
        if (s_pl_scroll > count - max_rows) s_pl_scroll = count - max_rows;
        if (s_pl_scroll < 0) s_pl_scroll = 0;

        for (int i = 0; i < max_rows && (i + s_pl_scroll) < count; i++) {
            int idx = filtered[i + s_pl_scroll];
            float y = list_start_y + (i * row_h);
            const PlaylistSummary *ps = playlist_mgmt_get_summary(idx);
            if (!ps) continue;

            bool is_active = (current_play_source == SOURCE_PLAYLIST && 
                              strcmp(active_playlist_playback.name, ps->name) == 0);

            Rectangle row_bar = { b.x + 6, y, b.width - 12, (float)row_h - 2.0f };
            bool is_hover = CheckCollisionPointRec(mouse, row_bar);
            if (is_hover) {
                DrawRectangleRec(row_bar, (Color){ 255, 255, 255, 8 });
            }

            float text_y = y + ((float)row_h - (float)FONT_SIZE_MD) * 0.5f;
            const char *badge = ps->is_favourites ? "*" : ">";
            Color badge_col = ps->is_favourites ? COLOR_ACCENT : (is_hover ? COLOR_TEXT_PRIMARY : COLOR_TEXT_DARK);
            DrawText(badge, (int)(b.x + 16 * ui_scale), (int)text_y, FONT_SIZE_MD, badge_col);

            const char *cnt_str = TextFormat("%d trks", ps->track_count);
            int cnt_w = MeasureText(cnt_str, FONT_SIZE_SM);
            float cnt_x = b.x + b.width - cnt_w - (16.0f * ui_scale);
            DrawText(cnt_str, (int)cnt_x, (int)text_y, FONT_SIZE_SM, is_hover ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED);

            float name_start_x = b.x + (38.0f * ui_scale);
            float name_w = cnt_x - name_start_x - (10.0f * ui_scale);
            if (name_w < 30) name_w = 30;
            Rectangle name_box = { name_start_x, text_y, name_w, (float)FONT_SIZE_MD * 1.3f };
            Color name_col = is_active ? COLOR_ACCENT : (is_hover ? COLOR_TEXT_PRIMARY : ColorAlpha(COLOR_TEXT_PRIMARY, 0.85f));
            DrawTextMarquee(ps->name, name_box, b, FONT_SIZE_MD, name_col, 28.0f);
        }
    } else {
        // Drilldown playlist view
        int back_w = MeasureText("<- [Back]", FONT_SIZE_MD);
        Rectangle back_box = { b.x + 12 * ui_scale, header_top - 4, (float)(back_w + 14 * ui_scale), (float)FONT_SIZE_MD * 1.4f };
        bool hover_back = CheckCollisionPointRec(mouse, back_box);
        if (hover_back) {
            DrawRectangleRounded(back_box, 0.2f, 4, (Color){ 255, 255, 255, 12 });
        }
        DrawText("<- [Back]", (int)(b.x + 16 * ui_scale), (int)header_top, FONT_SIZE_MD, hover_back ? WHITE : COLOR_ACCENT);

        float pl_title_x = b.x + 16 * ui_scale + back_w + (14 * ui_scale);
        Rectangle pl_title_box = { pl_title_x, header_top, b.width - (pl_title_x - b.x) - (16 * ui_scale), (float)FONT_SIZE_LG * 1.3f };
        DrawTextMarquee(s_active_pl_name, pl_title_box, b, FONT_SIZE_LG, COLOR_TEXT_PRIMARY, 24.0f);

        int count = s_loaded_pl.count;
        int max_rows = (int)(b.height - (list_start_y - b.y)) / row_h;
        if (s_pl_scroll > count - max_rows) s_pl_scroll = count - max_rows;
        if (s_pl_scroll < 0) s_pl_scroll = 0;

        if (count == 0) {
            DrawText("Empty playlist", (int)(b.x + 16 * ui_scale), (int)(list_start_y + 10), FONT_SIZE_SM, COLOR_TEXT_MUTED);
        }

        for (int i = 0; i < max_rows && (i + s_pl_scroll) < count; i++) {
            int idx = i + s_pl_scroll;
            float y = list_start_y + (i * row_h);
            const PlaylistTrackItem *ti = &s_loaded_pl.items[idx];

            bool is_playing = (current_play_source == SOURCE_PLAYLIST && strcmp(playing_filepath, ti->path) == 0);

            Rectangle row_bar = { b.x + 6, y, b.width - 12, (float)row_h - 2.0f };
            bool is_hover = CheckCollisionPointRec(mouse, row_bar);
            if (is_hover) {
                DrawRectangleRec(row_bar, (Color){ 255, 255, 255, 8 });
            }

            float text_y = y + ((float)row_h - (float)FONT_SIZE_MD) * 0.5f;
            DrawText(is_playing ? "||" : ">", (int)(b.x + 16 * ui_scale), (int)text_y, FONT_SIZE_SM, is_playing ? COLOR_ACCENT : (is_hover ? COLOR_TEXT_PRIMARY : COLOR_TEXT_DARK));

            const char *display_title = (ti->title[0]) ? ti->title : ti->path;
            const char *slash = strrchr(display_title, '/');
            if (slash) display_title = slash + 1;

            const char *dur_str = TextFormat("%u:%02u", ti->duration_sec / 60, ti->duration_sec % 60);
            int dur_w = MeasureText(dur_str, FONT_SIZE_SM);
            float dur_x = b.x + b.width - dur_w - (16.0f * ui_scale);
            DrawText(dur_str, (int)dur_x, (int)text_y, FONT_SIZE_SM, is_hover ? COLOR_TEXT_MUTED : COLOR_TEXT_DARK);

            float tit_start_x = b.x + (38.0f * ui_scale);
            float tit_w = dur_x - tit_start_x - (10.0f * ui_scale);
            if (tit_w < 30) tit_w = 30;
            Rectangle tit_col = { tit_start_x, text_y, tit_w, (float)FONT_SIZE_MD * 1.3f };
            Color text_col = is_playing ? COLOR_ACCENT : (is_hover ? COLOR_TEXT_PRIMARY : ColorAlpha(COLOR_TEXT_PRIMARY, 0.85f));
            DrawTextMarquee(display_title, tit_col, b, FONT_SIZE_MD, text_col, 26.0f);
        }
    }
}

void tile_playlists_input(SparklesTile *tile, Rectangle b) {
    (void)tile;
    if (sparkles_input_is_consumed()) return;

    // Text input for playlist search
    if (s_pl_search_active) {
        int c = GetCharPressed();
        while (c > 0) {
            if (c >= 32 && c <= 126 && s_pl_search_len < (int)sizeof(s_pl_search_buf) - 1) {
                s_pl_search_buf[s_pl_search_len++] = (char)c;
                s_pl_search_buf[s_pl_search_len] = '\0';
                s_pl_scroll = 0;
            }
            c = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE) && s_pl_search_len > 0) {
            s_pl_search_buf[--s_pl_search_len] = '\0';
            s_pl_scroll = 0;
        }
        if (IsKeyPressed(KEY_ESCAPE)) {
            tile_playlists_close_search();
        }
    }

    // Drag & wheel scroll
    float scroll = sparkles_input_get_scroll_delta(b);
    if (scroll != 0.0f) {
        s_pl_scroll += (int)roundf(scroll);
        if (s_pl_scroll < 0) s_pl_scroll = 0;
    }

   float ui_scale = sparkles_get_ui_scale();
    float header_top = b.y + (16.0f * ui_scale);
    int row_h = (int)fmaxf(44.0f, 38.0f * ui_scale);
    float list_start_y = header_top + (float)FONT_SIZE_LG + (14.0f * ui_scale);

    // Long-press or right-click context menu
    Vector2 lp;
    if (sparkles_input_consume_long_press(b, &lp)) {
        if (lp.y >= list_start_y) {
            int clicked = s_pl_scroll + (int)(lp.y - list_start_y) / row_h;
            if (!s_drilldown) {
                int count = 0;
                for (int p = 0; p < playlist_mgmt_get_count(); p++) {
                    const PlaylistSummary *ps = playlist_mgmt_get_summary(p);
                    if (!s_pl_search_active || s_pl_search_len == 0 || (ps && str_contains_ci(ps->name, s_pl_search_buf))) {
                        if (count == clicked && ps) {
                            sparkles_context_menu_open_playlist(lp, ps->name);
                            return;
                        }
                        count++;
                    }
                }
            } else {
                if (clicked >= 0 && clicked < s_loaded_pl.count) {
                    SparklesTrackItem item = {0};
                    strncpy(item.path, s_loaded_pl.items[clicked].path, sizeof(item.path) - 1);
                    strncpy(item.title, s_loaded_pl.items[clicked].title, sizeof(item.title) - 1);
                    strncpy(item.artist, s_loaded_pl.items[clicked].artist, sizeof(item.artist) - 1);
                    item.duration_sec = s_loaded_pl.items[clicked].duration_sec;
                    item.in_playlist = true;
                    strncpy(item.playlist_name, s_active_pl_name, sizeof(item.playlist_name) - 1);
                    item.playlist_track_idx = clicked;
                    sparkles_context_menu_open(lp, &item);
                    return;
                }
            }
        }
        return;
    }

    // Tap or left-click
    Vector2 tap;
    if (sparkles_input_consume_tap(b, &tap)) {
        int search_btn_w = MeasureText("[Search]", FONT_SIZE_SM);
        if (!s_drilldown && !s_pl_search_active && CheckCollisionPointRec(tap, (Rectangle){ b.x + b.width - search_btn_w - 24, b.y + 12, search_btn_w + 12, 26 })) {
            s_pl_search_active = true;
            return;
        }
        if (s_pl_search_active && CheckCollisionPointRec(tap, (Rectangle){ b.x + b.width - 38, b.y + 12, 28, 25 })) {
            tile_playlists_close_search();
            return;
        }

        float ui_scale = sparkles_get_ui_scale();
        float header_top = b.y + (16.0f * ui_scale);
        int row_h = (int)fmaxf(44.0f, 38.0f * ui_scale);
        float list_start_y = header_top + (float)FONT_SIZE_LG + (14.0f * ui_scale);

        if (!s_drilldown) {
            int clicked = s_pl_scroll + (int)(tap.y - list_start_y) / row_h;
            int count = 0;
            for (int p = 0; p < playlist_mgmt_get_count(); p++) {
                const PlaylistSummary *ps = playlist_mgmt_get_summary(p);
                if (!s_pl_search_active || s_pl_search_len == 0 || (ps && str_contains_ci(ps->name, s_pl_search_buf))) {
                    if (count == clicked && ps) {
                        playlist_mgmt_free_loaded(&s_loaded_pl);
                        if (playlist_mgmt_load_playlist(ps->name, &s_loaded_pl)) {
                            strncpy(s_active_pl_name, ps->name, sizeof(s_active_pl_name) - 1);
                            s_drilldown = true;
                            s_pl_scroll = 0;
                            s_pl_search_active = false;
                        }
                        return;
                    }
                    count++;
                }
            }
        } else {
            int back_w = MeasureText("<- [Back]", FONT_SIZE_MD);
            Rectangle back_hitbox = { b.x + 8.0f * ui_scale, header_top - 6.0f, (float)(back_w + 24.0f * ui_scale), (float)FONT_SIZE_MD * 1.8f };

            if (CheckCollisionPointRec(tap, back_hitbox)) {
                s_drilldown = false;
                s_pl_scroll = 0;
                playlist_mgmt_free_loaded(&s_loaded_pl);
                return;
            }

            int clicked = s_pl_scroll + (int)(tap.y - list_start_y) / row_h;
            if (clicked >= 0 && clicked < s_loaded_pl.count) {
                pthread_mutex_lock(&state_mutex);
                if (active_playlist_playback.paths) {
                    for (int i = 0; i < active_playlist_playback.count; i++) {
                        free(active_playlist_playback.paths[i]);
                        free(active_playlist_playback.titles[i]);
                    }
                    free(active_playlist_playback.paths);
                    free(active_playlist_playback.titles);
                }
                active_playlist_playback.count = s_loaded_pl.count;
                active_playlist_playback.paths = malloc(sizeof(char*) * s_loaded_pl.count);
                active_playlist_playback.titles = malloc(sizeof(char*) * s_loaded_pl.count);
                for (int i = 0; i < s_loaded_pl.count; i++) {
                    active_playlist_playback.paths[i] = strdup(s_loaded_pl.items[i].path);
                    active_playlist_playback.titles[i] = strdup(s_loaded_pl.items[i].title[0] ? s_loaded_pl.items[i].title : s_loaded_pl.items[i].path);
                }
                strncpy(active_playlist_playback.name, s_active_pl_name, sizeof(active_playlist_playback.name) - 1);
                current_play_source = SOURCE_PLAYLIST;
                playing_file_idx = clicked;
                strncpy(playing_filepath, s_loaded_pl.items[clicked].path, sizeof(playing_filepath) - 1);
                strncpy(playing_filename, s_loaded_pl.items[clicked].title[0] ? s_loaded_pl.items[clicked].title : s_loaded_pl.items[clicked].path, 255);
                pthread_mutex_unlock(&state_mutex);

                atomic_store(&seek_target_ms, -1);
                atomic_store(&current_cmd_atomic, CMD_PLAY);
            }
        }
    }
}