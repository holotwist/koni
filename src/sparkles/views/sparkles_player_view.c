#include "sparkles_player_view.h"
#include "visualizers/sparkles_vis.h"
#include "sparkles_theme.h"
#include "sparkles_widgets.h"
#include "state.h"
#include "lyrics.h"
#include "ui_common.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

static bool s_lyrics_enabled = true;
static bool s_help_enabled = false; // Hidden by default
static float s_lyrics_anim = 0.0f;
static int s_last_fetched_track_id = -1;

void sparkles_player_view_toggle_help(void) {
    s_help_enabled = !s_help_enabled;
}

bool sparkles_player_view_is_help_visible(void) {
    return s_help_enabled;
}

static char s_cur_sentence[256] = {0};
static char s_prev_sentence[256] = {0};
static int s_last_active_line = -2;
static float s_sentence_trans = 1.0f;

void sparkles_player_view_init(void) {
    s_lyrics_enabled = true;
    s_lyrics_anim = 0.0f;
    s_last_fetched_track_id = -1;
    s_cur_sentence[0] = '\0';
    s_prev_sentence[0] = '\0';
    s_last_active_line = -2;
    s_sentence_trans = 1.0f;
}

void sparkles_player_view_toggle_lyrics(void) {
    s_lyrics_enabled = !s_lyrics_enabled;
}

static void check_trigger_lyrics_fetch(void) {
    int cur_track_id = atomic_load(&current_track_id);
    if (cur_track_id <= 0) return;

    if (cur_track_id != s_last_fetched_track_id) {
        s_last_fetched_track_id = cur_track_id;
        s_last_active_line = -2;
        s_cur_sentence[0] = '\0';
        s_prev_sentence[0] = '\0';
        s_sentence_trans = 1.0f;

        pthread_mutex_lock(&state_mutex);
        if (playing_filepath[0] != '\0') {
            if (p_metadata.title) sparkles_font_load_for_text(p_metadata.title);
            if (p_metadata.artist) sparkles_font_load_for_text(p_metadata.artist);
            if (p_metadata.lyrics) sparkles_font_load_for_text(p_metadata.lyrics);

            if (ui_cache.lrc_doc) {
                lyric_document_free(ui_cache.lrc_doc);
                ui_cache.lrc_doc = NULL;
            }
            strncpy(current_lyrics_backend, "Searching...", sizeof(current_lyrics_backend) - 1);

            lyrics_engine_fetch_async(p_metadata.title, p_metadata.artist, p_metadata.album,
                                      atomic_load(&p_total_sec), playing_filepath, p_metadata.lyrics,
                                      cur_track_id);
        } else {
            current_lyrics_backend[0] = '\0';
        }
        pthread_mutex_unlock(&state_mutex);
    }
}

// Transport controls
static void draw_minimal_transport(float cx, float cy, bool playing) {
    // Previous (|<)
    DrawRectangle((int)cx - 42, (int)cy - 7, 2, 14, COLOR_TEXT_MUTED);
    DrawTriangle((Vector2){ cx - 30, cy - 7 }, (Vector2){ cx - 40, cy }, (Vector2){ cx - 30, cy + 7 }, COLOR_TEXT_MUTED);

    // Play / Pause
    if (playing) {
        DrawRectangle((int)cx - 5, (int)cy - 7, 3, 14, COLOR_ACCENT);
        DrawRectangle((int)cx + 2, (int)cy - 7, 3, 14, COLOR_ACCENT);
    } else {
        DrawTriangle((Vector2){ cx - 4, cy - 7 }, (Vector2){ cx + 7, cy }, (Vector2){ cx - 4, cy + 7 }, COLOR_ACCENT);
    }

    // Next (>|)
    DrawTriangle((Vector2){ cx + 30, cy - 7 }, (Vector2){ cx + 30, cy + 7 }, (Vector2){ cx + 40, cy }, COLOR_TEXT_MUTED);
    DrawRectangle((int)cx + 42, (int)cy - 7, 2, 14, COLOR_TEXT_MUTED);
}

void sparkles_player_view_render(float screen_w, float screen_h) {
    check_trigger_lyrics_fetch();
    float dt = GetFrameTime();

    int cur_track_id = atomic_load(&current_track_id);

    pthread_mutex_lock(&state_mutex);
    const char *title = p_metadata.title ? p_metadata.title : (playing_filename[0] ? playing_filename : "No track loaded");
    const char *artist = p_metadata.artist ? p_metadata.artist : "Unknown Artist";
    bool has_lyrics = (ui_cache.lrc_doc != NULL && ui_cache.lrc_doc->num_lines > 0);
    bool is_searching = (strcmp(current_lyrics_backend, "Searching...") == 0);
    bool not_found = (strcmp(current_lyrics_backend, "Not Found") == 0);
    bool has_track_gain = p_metadata.has_track_gain;

    static int s_last_lrc_track_id = -1;
    if (has_lyrics && cur_track_id != s_last_lrc_track_id) {
        s_last_lrc_track_id = cur_track_id;
        for (int l = 0; l < ui_cache.lrc_doc->num_lines; l++) {
            sparkles_font_load_for_text(ui_cache.lrc_doc->lines[l].text);
        }
    }
    pthread_mutex_unlock(&state_mutex);

    float target_anim = (s_lyrics_enabled && (has_lyrics || is_searching)) ? 1.0f : 0.0f;
    s_lyrics_anim += (target_anim - s_lyrics_anim) * fminf(1.0f, dt * 7.5f);

    // Visualizer canvas
    float vis_lift = s_lyrics_anim * 45.0f;
    Rectangle vis_area = {
        24.0f,
        70.0f - vis_lift,
        screen_w - 48.0f,
        screen_h - 170.0f
    };
    sparkles_vis_render(vis_area, dt);

    // Visualizer mode badge
    float badge_alpha = sparkles_vis_get_badge_alpha();
    if (badge_alpha > 0.0f) {
        const char *vis_name = sparkles_vis_get_name(sparkles_vis_get_mode());
        const char *badge_str = TextFormat("[%s]", vis_name);
        DrawSparklesText(badge_str, (int)(screen_w - MeasureSparklesText(badge_str, FONT_SIZE_SM) - 30),
                         24, FONT_SIZE_SM, ColorAlpha(COLOR_ACCENT, badge_alpha));
    }

    // Header, floating title and artist
    Rectangle title_rect = { 30.0f, 20.0f, screen_w * 0.65f, (float)FONT_SIZE_XL };
    Rectangle artist_rect = { 30.0f, 20.0f + (float)FONT_SIZE_XL + 4.0f, screen_w * 0.65f, (float)FONT_SIZE_SM };

    DrawTextMarquee(title, title_rect, (Rectangle){0, 0, screen_w, screen_h}, FONT_SIZE_XL, COLOR_TEXT_PRIMARY, 28.0f);
    DrawTextMarquee(artist, artist_rect, (Rectangle){0, 0, screen_w, screen_h}, FONT_SIZE_SM, COLOR_TEXT_MUTED, 22.0f);

    // Lyrics line
    if (s_lyrics_anim > 0.02f) {
        float lrc_y = screen_h - 140.0f;
        float lrc_h = 36.0f;
        Rectangle lrc_area = { 30.0f, lrc_y, screen_w - 60.0f, lrc_h };

        uint32_t srate = atomic_load(&vis_srate);
        if (srate == 0) srate = 44100;
        uint32_t cur_ms = (uint32_t)(((uint64_t)atomic_load(&p_frames_consumed) * 1000ULL) / srate);

        pthread_mutex_lock(&state_mutex);
        int active_line = -1;
        if (ui_cache.lrc_doc && ui_cache.lrc_doc->num_lines > 0) {
            if (ui_cache.lrc_doc->is_synced) {
                active_line = lyric_document_get_active_line(ui_cache.lrc_doc, cur_ms);
            } else {
                active_line = 0;
            }
        }

        if (active_line != s_last_active_line) {
            if (s_cur_sentence[0] != '\0') {
                strncpy(s_prev_sentence, s_cur_sentence, sizeof(s_prev_sentence) - 1);
            }
            if (active_line >= 0 && ui_cache.lrc_doc && active_line < ui_cache.lrc_doc->num_lines) {
                const char *line_text = ui_cache.lrc_doc->lines[active_line].text;
                strncpy(s_cur_sentence, (line_text && line_text[0]) ? line_text : "...", sizeof(s_cur_sentence) - 1);
            } else if (has_lyrics) {
                strncpy(s_cur_sentence, "...", sizeof(s_cur_sentence) - 1);
            } else {
                s_cur_sentence[0] = '\0';
            }
            s_last_active_line = active_line;
            s_sentence_trans = 0.0f;
        }

        s_sentence_trans += dt * 4.8f;
        if (s_sentence_trans > 1.0f) s_sentence_trans = 1.0f;
        float ease_line = 1.0f - powf(1.0f - s_sentence_trans, 3.0f);

        BeginScissorMode((int)lrc_area.x, (int)lrc_area.y, (int)lrc_area.width, (int)lrc_area.height);

        int font_size_active = 22;
        float mid_y = lrc_area.y + (lrc_area.height - font_size_active) * 0.5f;

        if (has_lyrics && s_cur_sentence[0] != '\0') {
            bool is_dots = (strcmp(s_cur_sentence, "...") == 0);
            Color active_col = is_dots ? ColorAlpha(COLOR_TEXT_MUTED, 0.7f) : COLOR_ACCENT;

            if (s_sentence_trans < 1.0f && s_prev_sentence[0] != '\0') {
                float prev_y = mid_y - (14.0f * ease_line);
                float prev_alpha = (1.0f - ease_line) * s_lyrics_anim;
                int tw_prev = MeasureSparklesText(s_prev_sentence, font_size_active);
                Rectangle prev_box = { lrc_area.x, prev_y, lrc_area.width, (float)font_size_active };
                if (tw_prev > (int)prev_box.width) {
                    DrawTextMarquee(s_prev_sentence, prev_box, (Rectangle){0, 0, screen_w, screen_h}, font_size_active, ColorAlpha(COLOR_TEXT_MUTED, prev_alpha), 30.0f);
                } else {
                    int lx = (int)(lrc_area.x + (lrc_area.width - tw_prev) / 2);
                    DrawSparklesText(s_prev_sentence, lx, (int)prev_y, font_size_active, ColorAlpha(COLOR_TEXT_MUTED, prev_alpha));
                }
            }

            float cur_y = mid_y + 14.0f * (1.0f - ease_line);
            float cur_alpha = ease_line * s_lyrics_anim;
            int tw_cur = MeasureSparklesText(s_cur_sentence, font_size_active);
            Rectangle cur_box = { lrc_area.x, cur_y, lrc_area.width, (float)font_size_active };

            if (tw_cur > (int)cur_box.width) {
                DrawTextMarquee(s_cur_sentence, cur_box, (Rectangle){0, 0, screen_w, screen_h}, font_size_active, ColorAlpha(active_col, cur_alpha), 30.0f);
            } else {
                int lx = (int)(lrc_area.x + (lrc_area.width - tw_cur) / 2);
                DrawSparklesText(s_cur_sentence, lx, (int)cur_y, font_size_active, ColorAlpha(active_col, cur_alpha));
            }
        } else if (is_searching) {
            const char *msg = "Searching for lyrics...";
            int tw = MeasureSparklesText(msg, FONT_SIZE_SM);
            DrawSparklesText(msg, (int)(lrc_area.x + (lrc_area.width - tw) / 2), (int)mid_y, FONT_SIZE_SM, ColorAlpha(COLOR_TEXT_MUTED, s_lyrics_anim));
        }

        EndScissorMode();
        pthread_mutex_unlock(&state_mutex);
    }

    // Footer HUD, timestamp, transport and pills
    uint32_t cur_sec = atomic_load(&p_current_sec);
    uint32_t tot_sec = atomic_load(&p_total_sec);
    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%02u:%02u", cur_sec / 60, cur_sec % 60);

    // Clock
    const int clock_size = 64;
    float clock_y = screen_h - (float)clock_size - 24.0f;
    DrawSparklesText(time_str, 30, (int)clock_y, clock_size, COLOR_TEXT_PRIMARY);

    if (tot_sec > 0) {
        int tw_main = MeasureSparklesText(time_str, clock_size);
        DrawSparklesText(TextFormat("/ %02u:%02u", tot_sec / 60, tot_sec % 60),
                         34 + tw_main, (int)(clock_y + (float)clock_size * 0.38f),
                         FONT_SIZE_SM, COLOR_TEXT_MUTED);
    }

    // Transport glyphs
    PlayState st = (PlayState)atomic_load(&play_state_atomic);
    draw_minimal_transport(screen_w * 0.5f, screen_h - 38.0f, st == STATE_PLAYING);

    // Status and navigation pills
    float pill_y = screen_h - 48.0f;
    float rx = screen_w - 280.0f;

    int r_mode = atomic_load(&play_mode_repeat);
    bool shuf = atomic_load(&play_mode_shuffle);
    int rgain = atomic_load(&play_mode_rgain);
    int vol = atomic_load(&volume);

    // Navigation help (togglable)
    if (s_help_enabled) {
        DrawSparklesText("Tab: Songs  |  , : Settings  |  H: Hide Help", (int)rx - 250, (int)pill_y + 4, FONT_SIZE_XS, COLOR_TEXT_MUTED);
    }

    // [L] Lyrics pill
    Rectangle l_pill = { rx, pill_y, 28, 22 };
    Color l_indicator_col = COLOR_TEXT_DARK;
    if (playing_filepath[0] != '\0') {
        if (has_lyrics) l_indicator_col = (Color){ 80, 225, 120, 255 };
        else if (is_searching) l_indicator_col = ((int)(GetTime() * 4.0f) % 2 == 0) ? (Color){ 245, 205, 70, 255 } : (Color){ 90, 75, 25, 255 };
        else if (not_found) l_indicator_col = (Color){ 235, 75, 75, 255 };
    }
    DrawRectangleRec(l_pill, s_lyrics_enabled ? ColorAlpha(l_indicator_col, 0.22f) : (Color){ 12, 13, 16, 255 });
    DrawSparklesText("L", (int)l_pill.x + 9, (int)l_pill.y + 4, 12, s_lyrics_enabled ? l_indicator_col : COLOR_TEXT_DARK);

    // [R] Repeat pill
    Rectangle r_pill = { rx + 34, pill_y, 28, 22 };
    DrawRectangleRec(r_pill, r_mode != REPEAT_OFF ? ColorAlpha(COLOR_ACCENT, 0.2f) : (Color){ 12, 13, 16, 255 });
    DrawSparklesText(r_mode == REPEAT_ONE ? "1" : "R", (int)r_pill.x + 9, (int)r_pill.y + 4, 12, r_mode != REPEAT_OFF ? COLOR_ACCENT : COLOR_TEXT_DARK);

    // [S] Shuffle pill
    Rectangle s_pill = { rx + 68, pill_y, 28, 22 };
    DrawRectangleRec(s_pill, shuf ? ColorAlpha(COLOR_ACCENT, 0.2f) : (Color){ 12, 13, 16, 255 });
    DrawSparklesText("S", (int)s_pill.x + 9, (int)s_pill.y + 4, 12, shuf ? COLOR_ACCENT : COLOR_TEXT_DARK);

    // [RG] ReplayGain pill (falls back to Calc if track lacks metadata)
    int eff_rgain = rgain;
    if (eff_rgain == 1 && !has_track_gain) {
        eff_rgain = 2;
    }

    Rectangle rg_pill = { rx + 102, pill_y, 34, 22 };
    const char *rg_text = (eff_rgain == 1) ? "RM" : ((eff_rgain == 2) ? "RC" : "RG");
    DrawRectangleRec(rg_pill, eff_rgain != 0 ? ColorAlpha(COLOR_ACCENT, 0.2f) : (Color){ 12, 13, 16, 255 });
    DrawSparklesText(rg_text, (int)rg_pill.x + 6, (int)rg_pill.y + 4, 12, eff_rgain != 0 ? COLOR_ACCENT : COLOR_TEXT_DARK);

    // Volume
    DrawSparklesText(TextFormat("%d%%", vol), (int)rx + 144, (int)pill_y + 4, FONT_SIZE_SM, vol > 0 ? COLOR_TEXT_MUTED : COLOR_ACCENT);
}

void sparkles_player_view_input(float screen_w, float screen_h) {
    Vector2 m = GetMousePosition();
    float pill_y = screen_h - 48.0f;
    float rx = screen_w - 280.0f;

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        // Center transport click hitboxes
        float t_cx = screen_w * 0.5f;
        float t_cy = screen_h - 38.0f;

        if (CheckCollisionPointCircle(m, (Vector2){ t_cx - 40.0f, t_cy }, 18.0f)) {
            atomic_store(&current_cmd_atomic, CMD_PREV);
            return;
        }
        if (CheckCollisionPointCircle(m, (Vector2){ t_cx, t_cy }, 20.0f)) {
            if (atomic_load(&play_state_atomic) == STATE_STOPPED && playing_filepath[0] != '\0') {
                atomic_store(&current_cmd_atomic, CMD_PLAY);
            } else {
                atomic_store(&current_cmd_atomic, CMD_PAUSE);
            }
            return;
        }
        if (CheckCollisionPointCircle(m, (Vector2){ t_cx + 40.0f, t_cy }, 18.0f)) {
            atomic_store(&current_cmd_atomic, CMD_NEXT);
            return;
        }

        // Center screen click cycles visualizer mode
        if (CheckCollisionPointCircle(m, (Vector2){ screen_w * 0.5f, screen_h * 0.45f }, 90.0f)) {
            sparkles_vis_cycle();
            return;
        }

        // [L] Lyrics pill
        if (CheckCollisionPointRec(m, (Rectangle){ rx, pill_y, 28, 22 })) {
            sparkles_player_view_toggle_lyrics();
            return;
        }
        // [R] Repeat pill
        if (CheckCollisionPointRec(m, (Rectangle){ rx + 34, pill_y, 28, 22 })) {
            int r = atomic_load(&play_mode_repeat);
            atomic_store(&play_mode_repeat, (r + 1) % 3);
            return;
        }
        // [S] Shuffle pill
        if (CheckCollisionPointRec(m, (Rectangle){ rx + 68, pill_y, 28, 22 })) {
            atomic_store(&play_mode_shuffle, !atomic_load(&play_mode_shuffle));
            return;
        }
        // [RG] ReplayGain pill (skips RM if current track has no metadata)
        if (CheckCollisionPointRec(m, (Rectangle){ rx + 102, pill_y, 34, 22 })) {
            pthread_mutex_lock(&state_mutex);
            bool has_gain = p_metadata.has_track_gain;
            pthread_mutex_unlock(&state_mutex);

            int cur_rg = atomic_load(&play_mode_rgain);
            int eff_rg = (cur_rg == 1 && !has_gain) ? 2 : cur_rg;

            if (eff_rg == 0) {
                atomic_store(&play_mode_rgain, has_gain ? 1 : 2);
            } else if (eff_rg == 1) {
                atomic_store(&play_mode_rgain, 2);
            } else {
                atomic_store(&play_mode_rgain, 0);
            }
            return;
        }
        // Volume click to toggle mute
        if (CheckCollisionPointRec(m, (Rectangle){ rx + 144, pill_y, 45, 22 })) {
            int cur_vol = atomic_load(&volume);
            atomic_store(&volume, cur_vol > 0 ? 0 : 100);
            return;
        }
    }
}

// Grid tile adapter
void sparkles_player_tile_render(struct SparklesTile *tile, Rectangle b) {
    (void)tile;
    sparkles_player_view_render(b.width, b.height);
}

void sparkles_player_tile_input(struct SparklesTile *tile, Rectangle b) {
    (void)tile;
    sparkles_player_view_input(b.width, b.height);
}