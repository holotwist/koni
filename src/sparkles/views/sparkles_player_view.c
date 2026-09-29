#include "sparkles_player_view.h"
#include "visualizers/sparkles_vis.h"
#include "sparkles_theme.h"
#include "sparkles_widgets.h"
#include "input/sparkles_input.h"
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

typedef enum {
    DECK_ANIM_IDLE = 0,
    DECK_ANIM_LEFT,
    DECK_ANIM_RIGHT,
    DECK_ANIM_TAP
} DeckAnimState;

static DeckAnimState s_deck_anim = DECK_ANIM_IDLE;
static float s_deck_timer = 0.0f;
static Vector2 s_deck_press_pos = {0};
static bool s_deck_pressing = false;

static inline float v2_dist(Vector2 a, Vector2 b) {
    float dx = a.x - b.x, dy = a.y - b.y;
    return sqrtf(dx * dx + dy * dy);
}

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

static void draw_minimal_transport(float cx, float cy, bool playing) {
    DrawRectangle((int)cx - 42, (int)cy - 7, 2, 14, COLOR_TEXT_MUTED);
    DrawTriangle((Vector2){ cx - 30, cy - 7 }, (Vector2){ cx - 40, cy }, (Vector2){ cx - 30, cy + 7 }, COLOR_TEXT_MUTED);

    if (playing) {
        DrawRectangle((int)cx - 5, (int)cy - 7, 3, 14, COLOR_ACCENT);
        DrawRectangle((int)cx + 2, (int)cy - 7, 3, 14, COLOR_ACCENT);
    } else {
        DrawTriangle((Vector2){ cx - 4, cy - 7 }, (Vector2){ cx + 7, cy }, (Vector2){ cx - 4, cy + 7 }, COLOR_ACCENT);
    }

    DrawTriangle((Vector2){ cx + 30, cy - 7 }, (Vector2){ cx + 30, cy + 7 }, (Vector2){ cx + 40, cy }, COLOR_TEXT_MUTED);
    DrawRectangle((int)cx + 42, (int)cy - 7, 2, 14, COLOR_TEXT_MUTED);
}

static void trigger_deck_anim(DeckAnimState st) {
    s_deck_anim = st;
    s_deck_timer = 0.35f;
}

static void draw_deck_animation(Rectangle deck_box, float dt) {
    if (s_deck_anim == DECK_ANIM_IDLE) return;
    s_deck_timer -= dt;
    if (s_deck_timer <= 0.0f) {
        s_deck_anim = DECK_ANIM_IDLE;
        return;
    }

    float prog = (0.35f - s_deck_timer) / 0.22f;
    if (prog > 1.0f) prog = 1.0f;
    float alpha = fminf(1.0f, s_deck_timer * 4.0f);
    int cy = (int)(deck_box.y + (deck_box.height - (float)FONT_SIZE_LG) * 0.5f);
    Color col = ColorAlpha(COLOR_ACCENT, alpha);

    if (s_deck_anim == DECK_ANIM_LEFT) {
        int count = (int)(prog * 10.0f) + 1;
        if (count > 10) count = 10;
        char str[16];
        for (int i = 0; i < count; i++) str[i] = '<';
        str[count] = '\0';
        int tw = MeasureSparklesText(str, FONT_SIZE_LG);
        DrawSparklesText(str, (int)(deck_box.x + (deck_box.width - (float)tw) * 0.5f), cy, FONT_SIZE_LG, col);
    } else if (s_deck_anim == DECK_ANIM_RIGHT) {
        int count = (int)(prog * 10.0f) + 1;
        if (count > 10) count = 10;
        char str[16];
        for (int i = 0; i < count; i++) str[i] = '>';
        str[count] = '\0';
        int tw = MeasureSparklesText(str, FONT_SIZE_LG);
        DrawSparklesText(str, (int)(deck_box.x + (deck_box.width - (float)tw) * 0.5f), cy, FONT_SIZE_LG, col);
    } else if (s_deck_anim == DECK_ANIM_TAP) {
        int count = (int)(prog * 5.0f) + 1;
        if (count > 5) count = 5;
        char left_str[8], right_str[8];
        for (int i = 0; i < count; i++) {
            left_str[i] = '>';
            right_str[i] = '<';
        }
        left_str[count] = '\0';
        right_str[count] = '\0';

        int gap = (int)((1.0f - prog) * 50.0f);
        int tw_left = MeasureSparklesText(left_str, FONT_SIZE_LG);
        float mid_x = deck_box.x + deck_box.width * 0.5f;

        DrawSparklesText(left_str, (int)(mid_x - (float)gap * 0.5f - (float)tw_left), cy, FONT_SIZE_LG, col);
        DrawSparklesText(right_str, (int)(mid_x + (float)gap * 0.5f), cy, FONT_SIZE_LG, col);
    }
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

    float ui_scale = sparkles_get_ui_scale();

    // Top Header, floating title, artist
    float header_top = 16.0f * ui_scale;
    float badge_w = 0.0f;
    float badge_alpha = sparkles_vis_get_badge_alpha();
    if (badge_alpha > 0.0f) {
        const char *vis_name = sparkles_vis_get_name(sparkles_vis_get_mode());
        const char *badge_str = TextFormat("[%s]", vis_name);
        badge_w = (float)MeasureSparklesText(badge_str, FONT_SIZE_SM) + 12.0f;
        DrawSparklesText(badge_str, (int)(screen_w - badge_w - 16.0f * ui_scale),
                         (int)header_top, FONT_SIZE_SM, ColorAlpha(COLOR_ACCENT, badge_alpha));
    }

    float max_title_w = screen_w - (32.0f * ui_scale) - (badge_w > 0.0f ? badge_w : 0.0f);
    Rectangle title_rect  = { 20.0f * ui_scale, header_top, max_title_w, (float)FONT_SIZE_XL * 1.3f };
    Rectangle artist_rect = { 20.0f * ui_scale, header_top + (float)FONT_SIZE_XL * 1.3f + 4.0f, max_title_w, (float)FONT_SIZE_SM * 1.3f };

    DrawTextMarquee(title, title_rect, (Rectangle){0, 0, screen_w, screen_h}, FONT_SIZE_XL, COLOR_TEXT_PRIMARY, 28.0f);
    DrawTextMarquee(artist, artist_rect, (Rectangle){0, 0, screen_w, screen_h}, FONT_SIZE_SM, COLOR_TEXT_MUTED, 22.0f);

    // Visualizer canvas
    float header_bottom = artist_rect.y + artist_rect.height + 8.0f;
    float bottom_reserved = 190.0f * ui_scale;
    float vis_lift = s_lyrics_anim * (40.0f * ui_scale);

    Rectangle vis_area = {
        16.0f * ui_scale,
        header_bottom - vis_lift,
        screen_w - (32.0f * ui_scale),
        fmaxf(100.0f, screen_h - header_bottom - bottom_reserved)
    };
    sparkles_vis_render(vis_area, dt);

    bool is_vertical = (screen_h > screen_w);

    // Lyrics line
    if (s_lyrics_anim > 0.02f) {
        float pill_h_calc = fmaxf(40.0f, 32.0f * ui_scale);
        float bottom_pill_y = screen_h - pill_h_calc - (16.0f * ui_scale);
        float scrub_y_calc = bottom_pill_y - (32.0f * ui_scale);

        int font_size_active = is_vertical ? FONT_SIZE_SM : 20;
        float lrc_h = (float)font_size_active * 1.5f;
        float lrc_y = is_vertical ? (scrub_y_calc - lrc_h - (8.0f * ui_scale)) : (screen_h - 130.0f);
        Rectangle lrc_area = { 20.0f * ui_scale, lrc_y, screen_w - (40.0f * ui_scale), lrc_h };

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

        float mid_y = lrc_area.y + (lrc_area.height - (float)font_size_active) * 0.5f;

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

    uint32_t cur_sec = atomic_load(&p_current_sec);
    uint32_t tot_sec = atomic_load(&p_total_sec);
    int r_mode = atomic_load(&play_mode_repeat);
    bool shuf = atomic_load(&play_mode_shuffle);
    int rgain = atomic_load(&play_mode_rgain);
    int vol = atomic_load(&volume);

    int eff_rgain = rgain;
    if (eff_rgain == 1 && !has_track_gain) eff_rgain = 2;
    const char *rg_text = (eff_rgain == 1) ? "RM" : ((eff_rgain == 2) ? "RC" : "RG");

    Color l_indicator_col = COLOR_TEXT_DARK;
    if (playing_filepath[0] != '\0') {
        if (has_lyrics) l_indicator_col = (Color){ 80, 225, 120, 255 };
        else if (is_searching) l_indicator_col = ((int)(GetTime() * 4.0f) % 2 == 0) ? (Color){ 245, 205, 70, 255 } : (Color){ 90, 75, 25, 255 };
        else if (not_found) l_indicator_col = (Color){ 235, 75, 75, 255 };
    }

    if (is_vertical) {
        float bottom_margin = 16.0f * ui_scale;
        float pill_h = fmaxf(40.0f, 32.0f * ui_scale);
        float pill_y = screen_h - pill_h - bottom_margin;

        // Scrubber position
        float scrub_y = pill_y - (32.0f * ui_scale);

        // Deck box area above scrubber
        float deck_h = 72.0f * ui_scale;
        Rectangle deck_box = { 24.0f, scrub_y - deck_h - 10.0f, screen_w - 48.0f, deck_h };
        draw_deck_animation(deck_box, dt);

        // Scrubber with timestamps
        char cur_str[16], tot_str[16];
        snprintf(cur_str, sizeof(cur_str), "%02u:%02u", cur_sec / 60, cur_sec % 60);
        snprintf(tot_str, sizeof(tot_str), "%02u:%02u", tot_sec / 60, tot_sec % 60);

        int tw_cur = MeasureSparklesText(cur_str, FONT_SIZE_SM);
        int tw_tot = MeasureSparklesText(tot_str, FONT_SIZE_SM);

        DrawSparklesText(cur_str, 24, (int)(scrub_y - FONT_SIZE_SM * 0.5f), FONT_SIZE_SM, COLOR_TEXT_MUTED);
        DrawSparklesText(tot_str, (int)(screen_w - 24.0f - (float)tw_tot), (int)(scrub_y - FONT_SIZE_SM * 0.5f), FONT_SIZE_SM, COLOR_TEXT_MUTED);

        float track_x = 24.0f + (float)tw_cur + 12.0f * ui_scale;
        float track_w = screen_w - 48.0f - (float)tw_cur - (float)tw_tot - (24.0f * ui_scale);
        if (track_w > 30.0f) {
            float prog = tot_sec > 0 ? (float)cur_sec / (float)tot_sec : 0.0f;
            if (prog > 1.0f) prog = 1.0f;

            DrawLineEx((Vector2){ track_x, scrub_y }, (Vector2){ track_x + track_w, scrub_y }, 3.0f * ui_scale, (Color){ 26, 28, 36, 255 });
            DrawLineEx((Vector2){ track_x, scrub_y }, (Vector2){ track_x + track_w * prog, scrub_y }, 3.5f * ui_scale, COLOR_ACCENT);
            DrawCircle((int)(track_x + track_w * prog), (int)scrub_y, 6.0f * ui_scale, WHITE);
        }

        // Pill buttons
        float pill_w = 46.0f * ui_scale;
        float pill_w_rg = pill_w * 1.15f;
        float pill_w_vol = pill_w * 1.25f;
        float gap = 8.0f * ui_scale;
        float total_pills_w = (pill_w * 3.0f) + pill_w_rg + pill_w_vol + (gap * 4.0f);
        float px = (screen_w - total_pills_w) * 0.5f;

        float cur_pill_x = px;
        Rectangle s_pill = { cur_pill_x, pill_y, pill_w, pill_h };
        DrawRectangleRec(s_pill, shuf ? ColorAlpha(COLOR_ACCENT, 0.2f) : (Color){ 12, 13, 16, 255 });
        DrawRectangleLinesEx(s_pill, 1.0f, shuf ? COLOR_ACCENT : (Color){ 28, 30, 38, 255 });
        int tw_s = MeasureSparklesText("S", FONT_SIZE_SM);
        DrawSparklesText("S", (int)(s_pill.x + (pill_w - tw_s) / 2), (int)(s_pill.y + (pill_h - FONT_SIZE_SM) / 2), FONT_SIZE_SM, shuf ? COLOR_ACCENT : COLOR_TEXT_DARK);

        cur_pill_x += pill_w + gap;
        Rectangle r_pill = { cur_pill_x, pill_y, pill_w, pill_h };
        DrawRectangleRec(r_pill, r_mode != REPEAT_OFF ? ColorAlpha(COLOR_ACCENT, 0.2f) : (Color){ 12, 13, 16, 255 });
        DrawRectangleLinesEx(r_pill, 1.0f, r_mode != REPEAT_OFF ? COLOR_ACCENT : (Color){ 28, 30, 38, 255 });
        const char *r_txt = (r_mode == REPEAT_ONE ? "1" : "R");
        int tw_r = MeasureSparklesText(r_txt, FONT_SIZE_SM);
        DrawSparklesText(r_txt, (int)(r_pill.x + (pill_w - tw_r) / 2), (int)(r_pill.y + (pill_h - FONT_SIZE_SM) / 2), FONT_SIZE_SM, r_mode != REPEAT_OFF ? COLOR_ACCENT : COLOR_TEXT_DARK);

        cur_pill_x += pill_w + gap;
        Rectangle l_pill = { cur_pill_x, pill_y, pill_w, pill_h };
        DrawRectangleRec(l_pill, s_lyrics_enabled ? ColorAlpha(l_indicator_col, 0.22f) : (Color){ 12, 13, 16, 255 });
        DrawRectangleLinesEx(l_pill, 1.0f, s_lyrics_enabled ? l_indicator_col : (Color){ 28, 30, 38, 255 });
        int tw_l = MeasureSparklesText("L", FONT_SIZE_SM);
        DrawSparklesText("L", (int)(l_pill.x + (pill_w - tw_l) / 2), (int)(l_pill.y + (pill_h - FONT_SIZE_SM) / 2), FONT_SIZE_SM, s_lyrics_enabled ? l_indicator_col : COLOR_TEXT_DARK);

        cur_pill_x += pill_w + gap;
        Rectangle rg_pill = { cur_pill_x, pill_y, pill_w_rg, pill_h };
        DrawRectangleRec(rg_pill, eff_rgain != 0 ? ColorAlpha(COLOR_ACCENT, 0.2f) : (Color){ 12, 13, 16, 255 });
        DrawRectangleLinesEx(rg_pill, 1.0f, eff_rgain != 0 ? COLOR_ACCENT : (Color){ 28, 30, 38, 255 });
        int tw_rg = MeasureSparklesText(rg_text, FONT_SIZE_SM);
        DrawSparklesText(rg_text, (int)(rg_pill.x + (rg_pill.width - tw_rg) / 2), (int)(rg_pill.y + (pill_h - FONT_SIZE_SM) / 2), FONT_SIZE_SM, eff_rgain != 0 ? COLOR_ACCENT : COLOR_TEXT_DARK);

        cur_pill_x += pill_w_rg + gap;
        Rectangle vol_pill = { cur_pill_x, pill_y, pill_w_vol, pill_h };
        DrawRectangleRec(vol_pill, (Color){ 12, 13, 16, 255 });
        DrawRectangleLinesEx(vol_pill, 1.0f, (Color){ 28, 30, 38, 255 });
        const char *v_str = TextFormat("%d%%", vol);
        int tw_v = MeasureSparklesText(v_str, FONT_SIZE_SM);
        DrawSparklesText(v_str, (int)(vol_pill.x + (vol_pill.width - tw_v) / 2), (int)(vol_pill.y + (pill_h - FONT_SIZE_SM) / 2), FONT_SIZE_SM, vol > 0 ? COLOR_TEXT_MUTED : COLOR_ACCENT);
    } else {
        // Horizontal layout, timestamp, center transport, right pills
        char time_str[32];
        snprintf(time_str, sizeof(time_str), "%02u:%02u", cur_sec / 60, cur_sec % 60);

        const int clock_size = 64;
        float clock_y = screen_h - (float)clock_size - 24.0f;
        DrawSparklesText(time_str, 30, (int)clock_y, clock_size, COLOR_TEXT_PRIMARY);

        if (tot_sec > 0) {
            int tw_main = MeasureSparklesText(time_str, clock_size);
            DrawSparklesText(TextFormat("/ %02u:%02u", tot_sec / 60, tot_sec % 60),
                             34 + tw_main, (int)(clock_y + (float)clock_size * 0.38f),
                             FONT_SIZE_SM, COLOR_TEXT_MUTED);
        }

        PlayState st = (PlayState)atomic_load(&play_state_atomic);
        draw_minimal_transport(screen_w * 0.5f, screen_h - 38.0f, st == STATE_PLAYING);

        float pill_y = screen_h - 48.0f;
        float rx = screen_w - 280.0f;

        if (s_help_enabled) {
            DrawSparklesText("Tab: Songs  |  , : Settings  |  H: Hide Help", (int)rx - 250, (int)pill_y + 4, FONT_SIZE_XS, COLOR_TEXT_MUTED);
        }

        Rectangle l_pill = { rx, pill_y, 28, 22 };
        DrawRectangleRec(l_pill, s_lyrics_enabled ? ColorAlpha(l_indicator_col, 0.22f) : (Color){ 12, 13, 16, 255 });
        DrawSparklesText("L", (int)l_pill.x + 9, (int)l_pill.y + 4, 12, s_lyrics_enabled ? l_indicator_col : COLOR_TEXT_DARK);

        Rectangle r_pill = { rx + 34, pill_y, 28, 22 };
        DrawRectangleRec(r_pill, r_mode != REPEAT_OFF ? ColorAlpha(COLOR_ACCENT, 0.2f) : (Color){ 12, 13, 16, 255 });
        DrawSparklesText(r_mode == REPEAT_ONE ? "1" : "R", (int)r_pill.x + 9, (int)r_pill.y + 4, 12, r_mode != REPEAT_OFF ? COLOR_ACCENT : COLOR_TEXT_DARK);

        Rectangle s_pill = { rx + 68, pill_y, 28, 22 };
        DrawRectangleRec(s_pill, shuf ? ColorAlpha(COLOR_ACCENT, 0.2f) : (Color){ 12, 13, 16, 255 });
        DrawSparklesText("S", (int)s_pill.x + 9, (int)s_pill.y + 4, 12, shuf ? COLOR_ACCENT : COLOR_TEXT_DARK);

        Rectangle rg_pill = { rx + 102, pill_y, 34, 22 };
        DrawRectangleRec(rg_pill, eff_rgain != 0 ? ColorAlpha(COLOR_ACCENT, 0.2f) : (Color){ 12, 13, 16, 255 });
        DrawSparklesText(rg_text, (int)rg_pill.x + 6, (int)rg_pill.y + 4, 12, eff_rgain != 0 ? COLOR_ACCENT : COLOR_TEXT_DARK);

        DrawSparklesText(TextFormat("%d%%", vol), (int)rx + 144, (int)pill_y + 4, FONT_SIZE_SM, vol > 0 ? COLOR_TEXT_MUTED : COLOR_ACCENT);
    }
}

void sparkles_player_view_input(float screen_w, float screen_h) {
    Vector2 m = GetMousePosition();
    bool is_vertical = (screen_h > screen_w);
    float ui_scale = sparkles_get_ui_scale();

    // Visualizer long-press opens visualizer selector
    float header_bottom = 20.0f * ui_scale + (float)FONT_SIZE_XL * 1.3f + (float)FONT_SIZE_SM * 1.3f + 16.0f;
    float bottom_reserved = 190.0f * ui_scale;
    Rectangle vis_hit_area = {
        0,
        header_bottom,
        screen_w,
        fmaxf(100.0f, screen_h - header_bottom - bottom_reserved)
    };

    Vector2 lp_pos;
    if (sparkles_input_consume_long_press(vis_hit_area, &lp_pos)) {
        sparkles_input_emit_action(SPARKLES_ACTION_PICK_VIS);
        return;
    }

    if (is_vertical) {
        float bottom_margin = 16.0f * ui_scale;
        float pill_h = fmaxf(40.0f, 32.0f * ui_scale);
        float pill_y = screen_h - pill_h - bottom_margin;
        float scrub_y = pill_y - (32.0f * ui_scale);

        float deck_h = 72.0f * ui_scale;
        Rectangle deck_box = { 24.0f, scrub_y - deck_h - 10.0f, screen_w - 48.0f, deck_h };
        Rectangle scrub_area = { 24.0f, scrub_y - 12.0f * ui_scale, screen_w - 48.0f, 24.0f * ui_scale };

        static bool s_scrub_dragging = false;

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, scrub_area)) {
            s_scrub_dragging = true;
        }
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            s_scrub_dragging = false;
        }

        if (s_scrub_dragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
            uint32_t tot_sec = atomic_load(&p_total_sec);
            if (tot_sec > 0) {
                float pct = (m.x - (scrub_area.x + 30.0f * ui_scale)) / (scrub_area.width - 60.0f * ui_scale);
                if (pct < 0.0f) pct = 0.0f;
                if (pct > 1.0f) pct = 1.0f;
                atomic_store(&seek_target_ms, (int)(pct * (float)tot_sec * 1000.0f));
                atomic_store(&current_cmd_atomic, CMD_SEEK);
            }
            return;
        }

        // Invisible gesture deck tracking
        if (!s_scrub_dragging) {
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(m, deck_box)) {
                s_deck_press_pos = m;
                s_deck_pressing = true;
            }
            if (s_deck_pressing && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                s_deck_pressing = false;
                float dx = m.x - s_deck_press_pos.x;
                float dist = v2_dist(s_deck_press_pos, m);

                if (dist < 16.0f * ui_scale) {
                    if (atomic_load(&play_state_atomic) == STATE_STOPPED && playing_filepath[0] != '\0') {
                        atomic_store(&current_cmd_atomic, CMD_PLAY);
                    } else {
                        atomic_store(&current_cmd_atomic, CMD_PAUSE);
                    }
                    trigger_deck_anim(DECK_ANIM_TAP);
                    return;
                } else if (dx < -30.0f * ui_scale) {
                    atomic_store(&current_cmd_atomic, CMD_NEXT);
                    trigger_deck_anim(DECK_ANIM_LEFT);
                    return;
                } else if (dx > 30.0f * ui_scale) {
                    atomic_store(&current_cmd_atomic, CMD_PREV);
                    trigger_deck_anim(DECK_ANIM_RIGHT);
                    return;
                }
            }
        }

        // Pill buttons
        float pill_w = 46.0f * ui_scale;
        float pill_w_rg = pill_w * 1.15f;
        float pill_w_vol = pill_w * 1.25f;
        float gap = 8.0f * ui_scale;
        float total_pills_w = (pill_w * 3.0f) + pill_w_rg + pill_w_vol + (gap * 4.0f);
        float px = (screen_w - total_pills_w) * 0.5f;

        Rectangle pills_bounding = { px - 4.0f, pill_y - 4.0f, total_pills_w + 8.0f, pill_h + 8.0f };
        Vector2 tap;

        if (!s_scrub_dragging && sparkles_input_consume_tap(pills_bounding, &tap)) {
            float cur_pill_x = px;
            Rectangle s_rect = { cur_pill_x, pill_y, pill_w, pill_h };
            cur_pill_x += pill_w + gap;
            Rectangle r_rect = { cur_pill_x, pill_y, pill_w, pill_h };
            cur_pill_x += pill_w + gap;
            Rectangle l_rect = { cur_pill_x, pill_y, pill_w, pill_h };
            cur_pill_x += pill_w + gap;
            Rectangle rg_rect = { cur_pill_x, pill_y, pill_w_rg, pill_h };
            cur_pill_x += pill_w_rg + gap;
            Rectangle vol_rect = { cur_pill_x, pill_y, pill_w_vol, pill_h };

            if (CheckCollisionPointRec(tap, s_rect)) {
                atomic_store(&play_mode_shuffle, !atomic_load(&play_mode_shuffle));
                return;
            }
            if (CheckCollisionPointRec(tap, r_rect)) {
                int r = atomic_load(&play_mode_repeat);
                atomic_store(&play_mode_repeat, (r + 1) % 3);
                return;
            }
            if (CheckCollisionPointRec(tap, l_rect)) {
                sparkles_input_emit_action(SPARKLES_ACTION_TOGGLE_LYRICS);
                return;
            }
            if (CheckCollisionPointRec(tap, rg_rect)) {
                pthread_mutex_lock(&state_mutex);
                bool has_gain = p_metadata.has_track_gain;
                pthread_mutex_unlock(&state_mutex);

                int cur_rg = atomic_load(&play_mode_rgain);
                int eff_rg = (cur_rg == 1 && !has_gain) ? 2 : cur_rg;
                if (eff_rg == 0) atomic_store(&play_mode_rgain, has_gain ? 1 : 2);
                else if (eff_rg == 1) atomic_store(&play_mode_rgain, 2);
                else atomic_store(&play_mode_rgain, 0);
                return;
            }
            if (CheckCollisionPointRec(tap, vol_rect)) {
                int cur_vol = atomic_load(&volume);
                atomic_store(&volume, cur_vol > 0 ? 0 : 100);
                return;
            }
        }
    } else {
        // Horizontal widescreen desktop layout clicks
        float pill_y = screen_h - 48.0f;
        float rx = screen_w - 280.0f;

        float t_cx = screen_w * 0.5f;
        float t_cy = screen_h - 38.0f;
        Rectangle btn_prev = { t_cx - 58.0f, t_cy - 18.0f, 36.0f, 36.0f };
        Rectangle btn_play = { t_cx - 20.0f, t_cy - 20.0f, 40.0f, 40.0f };
        Rectangle btn_next = { t_cx + 22.0f, t_cy - 18.0f, 36.0f, 36.0f };

        Rectangle l_rect   = { rx,        pill_y, 28, 22 };
        Rectangle r_rect   = { rx + 34,   pill_y, 28, 22 };
        Rectangle s_rect   = { rx + 68,   pill_y, 28, 22 };
        Rectangle rg_rect  = { rx + 102,  pill_y, 34, 22 };
        Rectangle vol_rect = { rx + 144,  pill_y, 45, 22 };

        if (sparkles_input_consume_tap(btn_prev, NULL)) {
            atomic_store(&current_cmd_atomic, CMD_PREV);
            return;
        }
        if (sparkles_input_consume_tap(btn_play, NULL)) {
            if (atomic_load(&play_state_atomic) == STATE_STOPPED && playing_filepath[0] != '\0') {
                atomic_store(&current_cmd_atomic, CMD_PLAY);
            } else {
                atomic_store(&current_cmd_atomic, CMD_PAUSE);
            }
            return;
        }
        if (sparkles_input_consume_tap(btn_next, NULL)) {
            atomic_store(&current_cmd_atomic, CMD_NEXT);
            return;
        }
        if (sparkles_input_consume_tap(l_rect, NULL)) {
            sparkles_player_view_toggle_lyrics();
            return;
        }
        if (sparkles_input_consume_tap(r_rect, NULL)) {
            int r = atomic_load(&play_mode_repeat);
            atomic_store(&play_mode_repeat, (r + 1) % 3);
            return;
        }
        if (sparkles_input_consume_tap(s_rect, NULL)) {
            atomic_store(&play_mode_shuffle, !atomic_load(&play_mode_shuffle));
            return;
        }
        if (sparkles_input_consume_tap(rg_rect, NULL)) {
            pthread_mutex_lock(&state_mutex);
            bool has_gain = p_metadata.has_track_gain;
            pthread_mutex_unlock(&state_mutex);

            int cur_rg = atomic_load(&play_mode_rgain);
            int eff_rg = (cur_rg == 1 && !has_gain) ? 2 : cur_rg;
            if (eff_rg == 0) atomic_store(&play_mode_rgain, has_gain ? 1 : 2);
            else if (eff_rg == 1) atomic_store(&play_mode_rgain, 2);
            else atomic_store(&play_mode_rgain, 0);
            return;
        }
        if (sparkles_input_consume_tap(vol_rect, NULL)) {
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