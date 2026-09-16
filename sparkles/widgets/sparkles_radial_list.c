#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#include "sparkles_radial_list.h"
#include "sparkles_theme.h"
#include "state.h"
#include "modals/sparkles_context_menu.h"
#include <math.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

static bool s_open = false;
static bool s_just_opened = false;
static float s_anim = 0.0f;
static float s_scroll_idx = 0.0f;
static float s_target_scroll = 0.0f;

// Search filtering
static char s_search_buf[64] = {0};
static int s_search_len = 0;
static int s_filtered[65536];
static int s_filtered_count = 0;

static int s_hovered_filtered_idx = -1;

static bool str_contains_ci(const char *haystack, const char *needle) {
    if (!needle || !needle[0]) return true;
    if (!haystack) return false;
    return strcasestr(haystack, needle) != NULL;
}

static void update_filtered_list(void) {
    pthread_mutex_lock(&state_mutex);
    s_filtered_count = 0;
    int total = num_library_tracks;
    for (int i = 0; i < total; i++) {
        if (s_search_len == 0 ||
            str_contains_ci(library_tracks[i].title, s_search_buf) ||
            str_contains_ci(library_tracks[i].artist, s_search_buf) ||
            str_contains_ci(library_tracks[i].name, s_search_buf)) {
            s_filtered[s_filtered_count++] = i;
        }
    }
    pthread_mutex_unlock(&state_mutex);

    if (s_target_scroll > (float)(s_filtered_count - 1)) {
        s_target_scroll = (s_filtered_count > 0) ? (float)(s_filtered_count - 1) : 0.0f;
    }
    if (s_target_scroll < 0.0f) s_target_scroll = 0.0f;
}

void sparkles_radial_list_init(void) {
    s_open = false;
    s_just_opened = false;
    s_anim = 0.0f;
    s_scroll_idx = 0.0f;
    s_target_scroll = 0.0f;
    s_search_buf[0] = '\0';
    s_search_len = 0;
    s_hovered_filtered_idx = -1;
    update_filtered_list();
}

void sparkles_radial_list_open(void) {
    s_open = true;
    s_just_opened = true;
    s_search_buf[0] = '\0';
    s_search_len = 0;
    update_filtered_list();

    // Center scroll on currently playing song if found
    pthread_mutex_lock(&state_mutex);
    if (playing_filepath[0] != '\0') {
        for (int k = 0; k < s_filtered_count; k++) {
            int t = s_filtered[k];
            if (strcmp(library_tracks[t].path, playing_filepath) == 0) {
                s_target_scroll = (float)k;
                s_scroll_idx = (float)k;
                break;
            }
        }
    }
    pthread_mutex_unlock(&state_mutex);
}

void sparkles_radial_list_close(void) {
    s_open = false;
    s_search_buf[0] = '\0';
    s_search_len = 0;
}

void sparkles_radial_list_toggle(void) {
    if (s_open) sparkles_radial_list_close();
    else sparkles_radial_list_open();
}

bool sparkles_radial_list_is_open(void) { return s_open; }
bool sparkles_radial_list_is_visible(void) { return (s_open || s_anim > 0.001f); }
float sparkles_radial_list_get_anim_progress(void) { return s_anim; }

void sparkles_radial_list_update(float screen_w, float screen_h) {
    (void)screen_w; (void)screen_h;
    float dt = GetFrameTime();
    float speed = 6.0f;

    if (s_open) {
        s_anim += dt * speed;
        if (s_anim > 1.0f) s_anim = 1.0f;
    } else {
        s_anim -= dt * speed;
        if (s_anim < 0.0f) s_anim = 0.0f;
    }

    // Rotational scroll lerp
    s_scroll_idx += (s_target_scroll - s_scroll_idx) * fminf(1.0f, dt * 14.0f);
}

// Computes right-edge circle geometry passing through (W, 0) and (W, H)
static void get_arc_geometry(float W, float H, float *out_cx, float *out_cy, float *out_R, float *out_alpha_max, float *out_w_arc) {
    float W_arc = fminf(460.0f, W * 0.40f);
    if (W_arc < 300.0f) W_arc = 300.0f;

    float H_half = H * 0.5f;
    float d = (H_half * H_half - W_arc * W_arc) / (2.0f * W_arc);
    float R = d + W_arc;

    *out_cx = W + d;
    *out_cy = H_half;
    *out_R = R;
    *out_alpha_max = asinf(fminf(1.0f, H_half / R));
    *out_w_arc = W_arc;
}

bool sparkles_radial_list_handle_input(float screen_w, float screen_h) {
    if (!s_open) return false;

    // Consume the opening frame (de-bouncing)
    if (s_just_opened) {
        s_just_opened = false;
        return true;
    }

    // Do not process input while context menu is active
    if (sparkles_context_menu_is_open()) return true;

    // Pressing Tab or Escape closes the list
    if (IsKeyPressed(KEY_TAB) || IsKeyPressed(KEY_ESCAPE)) {
        sparkles_radial_list_close();
        return true;
    }

    Vector2 mouse = GetMousePosition();
    float cx, cy, R, alpha_max, W_arc;
    get_arc_geometry(screen_w, screen_h, &cx, &cy, &R, &alpha_max, &W_arc);

    // Mouse wheel scrolling
    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        s_target_scroll -= wheel * 3.0f;
        if (s_target_scroll < 0.0f) s_target_scroll = 0.0f;
        if (s_target_scroll > (float)(s_filtered_count - 1)) {
            s_target_scroll = (s_filtered_count > 0) ? (float)(s_filtered_count - 1) : 0.0f;
        }
    }

    // Keyboard search input
    int c = GetCharPressed();
    while (c > 0) {
        if (c >= 32 && c <= 126 && s_search_len < (int)sizeof(s_search_buf) - 1) {
            s_search_buf[s_search_len++] = (char)c;
            s_search_buf[s_search_len] = '\0';
            update_filtered_list();
            s_target_scroll = 0.0f;
            s_scroll_idx = 0.0f;
        }
        c = GetCharPressed();
    }

    if (IsKeyPressed(KEY_BACKSPACE) && s_search_len > 0) {
        s_search_buf[--s_search_len] = '\0';
        update_filtered_list();
        s_target_scroll = 0.0f;
        s_scroll_idx = 0.0f;
    }

    if (IsKeyPressed(KEY_UP)) {
        s_target_scroll -= 1.0f;
        if (s_target_scroll < 0.0f) s_target_scroll = 0.0f;
    }
    if (IsKeyPressed(KEY_DOWN)) {
        s_target_scroll += 1.0f;
        if (s_target_scroll > (float)(s_filtered_count - 1)) {
            s_target_scroll = (s_filtered_count > 0) ? (float)(s_filtered_count - 1) : 0.0f;
        }
    }

    // Enter plays currently hovered or top centered track
    if (IsKeyPressed(KEY_ENTER) && s_filtered_count > 0) {
        int choose = (s_hovered_filtered_idx >= 0 && s_hovered_filtered_idx < s_filtered_count)
                     ? s_hovered_filtered_idx
                     : (int)roundf(s_target_scroll);
        if (choose < 0) choose = 0;
        if (choose >= s_filtered_count) choose = s_filtered_count - 1;

        int target = s_filtered[choose];
        pthread_mutex_lock(&state_mutex);
        strncpy(playing_filepath, library_tracks[target].path, sizeof(playing_filepath) - 1);
        strncpy(playing_filename, library_tracks[target].name, 255);
        playing_file_idx = target;
        current_play_source = SOURCE_LIBRARY;
        pthread_mutex_unlock(&state_mutex);

        atomic_store(&seek_target_ms, -1);
        atomic_store(&current_cmd_atomic, CMD_PLAY);
        sparkles_radial_list_close();
        return true;
    }

    // Right-click opens context menu on hovered track
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && s_hovered_filtered_idx >= 0 && s_hovered_filtered_idx < s_filtered_count) {
        int target = s_filtered[s_hovered_filtered_idx];
        pthread_mutex_lock(&state_mutex);
        SparklesTrackItem item = {0};
        strncpy(item.path, library_tracks[target].path, sizeof(item.path) - 1);
        strncpy(item.title, library_tracks[target].title[0] ? library_tracks[target].title : library_tracks[target].name, sizeof(item.title) - 1);
        strncpy(item.artist, library_tracks[target].artist, sizeof(item.artist) - 1);
        strncpy(item.album, library_tracks[target].album, sizeof(item.album) - 1);
        item.duration_sec = library_tracks[target].duration_sec;
        pthread_mutex_unlock(&state_mutex);
        sparkles_context_menu_open(mouse, &item);
        return true;
    }

    // Left-click selects and plays song, then hides radial menu
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && s_hovered_filtered_idx >= 0 && s_hovered_filtered_idx < s_filtered_count) {
        int target = s_filtered[s_hovered_filtered_idx];
        pthread_mutex_lock(&state_mutex);
        selected_library_idx = target;
        strncpy(playing_filepath, library_tracks[target].path, sizeof(playing_filepath) - 1);
        strncpy(playing_filename, library_tracks[target].name, 255);
        playing_file_idx = target;
        current_play_source = SOURCE_LIBRARY;
        pthread_mutex_unlock(&state_mutex);

        atomic_store(&seek_target_ms, -1);
        atomic_store(&current_cmd_atomic, CMD_PLAY);
        sparkles_radial_list_close();
        return true;
    }

    // Clicking anywhere to the left of the arc closes it
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouse.x < (screen_w - W_arc - 40.0f)) {
        sparkles_radial_list_close();
        return true;
    }

    return true;
}

void sparkles_radial_list_render(float screen_w, float screen_h) {
    if (s_anim <= 0.001f) return;

    float ease = 1.0f - powf(1.0f - s_anim, 3.0f);
    float cx, cy, R, alpha_max, W_arc;
    get_arc_geometry(screen_w, screen_h, &cx, &cy, &R, &alpha_max, &W_arc);

    // Slide offset from right edge
    float slide_x = (1.0f - ease) * (W_arc + 60.0f);
    cx += slide_x;

    Vector2 mouse = GetMousePosition();

    // Arc guide lines
    const int arc_pts = 48;
    Vector2 prev_pt = {0};
    bool has_prev = false;

    for (int p = 0; p <= arc_pts; p++) {
        float frac = (float)p / (float)arc_pts;
        float a = -alpha_max + (2.0f * alpha_max * frac);
        Vector2 pt = { cx - R * cosf(a), cy + R * sinf(a) };
        if (has_prev) {
            DrawLineEx(prev_pt, pt, 1.4f, ColorAlpha(COLOR_ACCENT_DIM, 0.40f * ease));
        }
        prev_pt = pt;
        has_prev = true;
    }

    // Search query pill along top edge of arc
    if (s_search_len > 0) {
        const char *cur = ((int)(GetTime() * 2.5f) % 2 == 0) ? "_" : " ";
        const char *search_txt = TextFormat("/ %s%s", s_search_buf, cur);
        int tw = MeasureSparklesText(search_txt, FONT_SIZE_SM);
        float sx = screen_w - tw - 30.0f;
        float sy = 24.0f;
        DrawRectangle((int)sx - 8, (int)sy - 4, tw + 16, 24, ColorAlpha((Color){ 10, 11, 14, 255 }, 0.85f * ease));
        DrawRectangleLines((int)sx - 8, (int)sy - 4, tw + 16, 24, COLOR_ACCENT);
        DrawSparklesText(search_txt, (int)sx, (int)sy, FONT_SIZE_SM, COLOR_ACCENT);
    }

    // Render items
    s_hovered_filtered_idx = -1;
    float delta_alpha = 0.140f; // ~8 degrees step between songs

    pthread_mutex_lock(&state_mutex);

    for (int i = 0; i < s_filtered_count; i++) {
        float rel = (float)i - s_scroll_idx;
        float a = rel * delta_alpha;

        // Clip items beyond top and bottom corners
        if (a < -alpha_max - 0.05f || a > alpha_max + 0.05f) continue;

        float item_x = cx - R * cosf(a);
        float item_y = cy + R * sinf(a);

        int track_idx = s_filtered[i];
        bool is_playing = (playing_filepath[0] != '\0' &&
                           strcmp(library_tracks[track_idx].path, playing_filepath) == 0);

        const char *title = library_tracks[track_idx].title[0] ? library_tracks[track_idx].title : library_tracks[track_idx].name;
        const char *artist = library_tracks[track_idx].artist[0] ? library_tracks[track_idx].artist : "Unknown";
        uint32_t dur = library_tracks[track_idx].duration_sec;

        float row_w = screen_w - item_x;
        if (row_w < 120.0f) row_w = 120.0f;

        Rectangle hit_box = { item_x - 10.0f, item_y - 14.0f, row_w, 28.0f };
        bool is_hover = !sparkles_context_menu_is_open() && CheckCollisionPointRec(mouse, hit_box);
        if (is_hover) s_hovered_filtered_idx = i;

        // Radial pip on the arc rail
        Color pip_col = is_playing ? COLOR_ACCENT : (is_hover ? WHITE : ColorAlpha(COLOR_TEXT_MUTED, 0.7f));
        DrawCircleV((Vector2){ item_x, item_y }, is_hover ? 4.0f : (is_playing ? 3.5f : 2.5f), pip_col);
        if (is_hover || is_playing) {
            DrawLineEx((Vector2){ item_x, item_y }, (Vector2){ item_x + 12.0f, item_y }, 1.5f, pip_col);
        }

        // Show duration only on 9 centered items (center ±4) or hovered item
        bool show_dur = (fabsf(rel) <= 4.5f) || is_hover;
        float dur_w = show_dur ? 40.0f : 0.0f;

        // Song label layout (unconstrained tracks gain extra title width)
        float text_start_x = item_x + (is_hover ? 18.0f : 14.0f);
        float avail_title_w = screen_w - text_start_x - dur_w - 20.0f;
        if (avail_title_w < 60.0f) avail_title_w = 60.0f;

        Rectangle title_box = { text_start_x, item_y - 8.0f, avail_title_w, (float)FONT_SIZE_SM };
        Color title_col = is_playing ? COLOR_ACCENT : (is_hover ? COLOR_TEXT_PRIMARY : ColorAlpha(COLOR_TEXT_PRIMARY, 0.75f));

        const char *display_line = TextFormat("%s  ·  %s", title, artist);
        DrawTextMarquee(display_line, title_box, (Rectangle){ 0, 0, screen_w, screen_h }, FONT_SIZE_SM, title_col, 25.0f);

        // Right-aligned duration for focused tracks
        if (show_dur) {
            const char *dur_str = TextFormat("%u:%02u", dur / 60, dur % 60);
            DrawSparklesText(dur_str, (int)(screen_w - 50.0f), (int)(item_y - 7.0f), FONT_SIZE_XS, ColorAlpha(COLOR_TEXT_MUTED, 0.8f));
        }
    }

    pthread_mutex_unlock(&state_mutex);

    // Tip along bottom-right corner
    const char *hint = "[Scroll: Navigate | Click: Play | Right-Click: Menu | Tab/Esc: Hide]";
    int hint_w = MeasureSparklesText(hint, FONT_SIZE_XS);
    DrawSparklesText(hint, (int)(screen_w - hint_w - 18.0f), (int)(screen_h - 22.0f), FONT_SIZE_XS, ColorAlpha(COLOR_TEXT_DARK, ease));
}