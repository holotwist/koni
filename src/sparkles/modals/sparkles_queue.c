#define _DEFAULT_SOURCE
#include "sparkles_queue.h"
#include "sparkles_theme.h"
#include "state.h"
#include "input/sparkles_input.h"
#include "rlgl.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool s_queue_open = false;
static float s_anim_progress = 0.0f;
static int s_queue_scroll = 0;

// Drag & Drop reordering state
static int s_dragged_idx = -1;
static int s_drop_target_idx = -1;

void sparkles_queue_init(void) {
    s_queue_open = false;
    s_anim_progress = 0.0f;
    s_queue_scroll = 0;
    s_dragged_idx = -1;
    s_drop_target_idx = -1;
}

void sparkles_queue_toggle(void) {
    s_queue_open = !s_queue_open;
    s_dragged_idx = -1;
    s_drop_target_idx = -1;
}

bool sparkles_queue_is_open(void) { return s_queue_open; }
bool sparkles_queue_is_visible(void) { return (s_queue_open || s_anim_progress > 0.001f); }
float sparkles_queue_get_anim_progress(void) { return s_anim_progress; }

void sparkles_queue_update(float screen_w, float screen_h) {
    (void)screen_w; (void)screen_h;
    float dt = GetFrameTime();
    float speed = 5.4f;
    if (s_queue_open) {
        s_anim_progress += dt * speed;
        if (s_anim_progress > 1.0f) s_anim_progress = 1.0f;
    } else {
        s_anim_progress -= dt * speed;
        if (s_anim_progress < 0.0f) s_anim_progress = 0.0f;
    }
}

static void reorder_queue_item(int from_idx, int to_idx) {
    pthread_mutex_lock(&state_mutex);
    if (from_idx < 0 || from_idx >= num_playlist_files ||
        to_idx < 0 || to_idx >= num_playlist_files || from_idx == to_idx) {
        pthread_mutex_unlock(&state_mutex);
        return;
    }

    PlaylistEntry temp = playlist[from_idx];
    if (from_idx < to_idx) {
        for (int i = from_idx; i < to_idx; i++) {
            playlist[i] = playlist[i + 1];
        }
    } else {
        for (int i = from_idx; i > to_idx; i--) {
            playlist[i] = playlist[i - 1];
        }
    }
    playlist[to_idx] = temp;

    // Adjust playback index if queue is currently playing
    if (current_play_source == SOURCE_QUEUE) {
        if (playing_file_idx == from_idx) {
            playing_file_idx = to_idx;
        } else if (from_idx < playing_file_idx && to_idx >= playing_file_idx) {
            playing_file_idx--;
        } else if (from_idx > playing_file_idx && to_idx <= playing_file_idx) {
            playing_file_idx++;
        }
    }
    pthread_mutex_unlock(&state_mutex);
}

static void remove_queue_item(int idx) {
    pthread_mutex_lock(&state_mutex);
    if (idx < 0 || idx >= num_playlist_files) {
        pthread_mutex_unlock(&state_mutex);
        return;
    }

    koni_metadata_free(&playlist[idx].meta);
    for (int i = idx; i < num_playlist_files - 1; i++) {
        playlist[i] = playlist[i + 1];
    }
    num_playlist_files--;

    if (current_play_source == SOURCE_QUEUE) {
        if (playing_file_idx == idx) {
            if (num_playlist_files > 0) {
                if (playing_file_idx >= num_playlist_files) playing_file_idx = 0;
                strncpy(playing_filepath, playlist[playing_file_idx].path, sizeof(playing_filepath) - 1);
                strncpy(playing_filename, playlist[playing_file_idx].name, 255);
                atomic_store(&seek_target_ms, -1);
                atomic_store(&current_cmd_atomic, CMD_PLAY);
            } else {
                atomic_store(&current_cmd_atomic, CMD_STOP);
            }
        } else if (playing_file_idx > idx) {
            playing_file_idx--;
        }
    }
    pthread_mutex_unlock(&state_mutex);
}

void sparkles_queue_render(float screen_w, float screen_h) {
    if (s_anim_progress <= 0.001f) return;

    float inv = 1.0f - s_anim_progress;
    float ease = 1.0f - (inv * inv * inv);

    float ui_scale = sparkles_get_ui_scale();
    bool is_mobile = (screen_h > screen_w);

    rlPushMatrix();

    float box_w, box_h;
    Rectangle box;

    if (is_mobile) {
        box_w = screen_w;
        box_h = screen_h * 0.72f;
        // Slide down from the top
        float cur_y = (ease - 1.0f) * box_h;
        box = (Rectangle){ 0.0f, cur_y, box_w, box_h };
    } else {
        rlTranslatef(screen_w * 0.5f, screen_h * 0.5f - 40.0f * (inv * inv), 0.0f);
        rlScalef(1.0f + 0.15f * inv, 1.0f + 0.15f * inv, 1.0f);
        rlTranslatef(-screen_w * 0.5f, -screen_h * 0.5f, 0.0f);

        box_w = fminf(760.0f * ui_scale, screen_w - 60.0f);
        box_h = fminf(600.0f * ui_scale, screen_h - 80.0f);
        box = (Rectangle){ (screen_w - box_w) * 0.5f, (screen_h - box_h) * 0.5f, box_w, box_h };
    }

    // Dimmed background
    DrawRectangle(0, 0, (int)screen_w, (int)screen_h, ColorAlpha((Color){ 3, 3, 5, 255 }, 0.70f * ease));

    Vector2 mouse = GetMousePosition();
    const SparklesPointerState *ptr = sparkles_input_get_state();
    Vector2 ptr_pos = ptr->pos;
    bool interactive = (s_anim_progress >= 0.99f);

    DrawRectangleRec(box, (Color){ 8, 9, 12, 252 });
    if (is_mobile) {
        DrawLine(0, (int)(box.y + box.height), (int)box.width, (int)(box.y + box.height), COLOR_ACCENT);
        // Bottom drag handle to push back up
        float h_w = 44.0f * ui_scale;
        DrawRectangle((int)((box.width - h_w) * 0.5f), (int)(box.y + box.height - 8.0f * ui_scale), (int)h_w, 4, ColorAlpha(COLOR_ACCENT, 0.65f));
    } else {
        DrawRectangleLinesEx(box, 1.0f, (Color){ 32, 35, 45, 255 });
        DrawNothingCornerBrackets(box, 10.0f * ui_scale, COLOR_ACCENT);
    }

    // Header bar
    float header_h = is_mobile ? (48.0f * ui_scale) : 44.0f;
    DrawRectangle((int)box.x, (int)box.y, (int)box.width, (int)header_h, (Color){ 12, 13, 17, 255 });
    DrawLine((int)box.x, (int)(box.y + header_h), (int)(box.x + box.width), (int)(box.y + header_h), (Color){ 28, 30, 38, 255 });

    pthread_mutex_lock(&state_mutex);
    int count = num_playlist_files;
    pthread_mutex_unlock(&state_mutex);

    float head_txt_y = box.y + (header_h - (float)FONT_SIZE_MD) * 0.5f;
    DrawText(TextFormat("QUEUE · %d Tracks", count), (int)(box.x + 16.0f * ui_scale), (int)head_txt_y, FONT_SIZE_MD, COLOR_TEXT_PRIMARY);

    // Clear and close buttons
    float btn_h = is_mobile ? (32.0f * ui_scale) : 24.0f;
    float btn_w_clear = is_mobile ? (62.0f * ui_scale) : 66.0f;
    float btn_w_close = is_mobile ? (58.0f * ui_scale) : 64.0f;
    float btn_gap = 8.0f * ui_scale;
    float btn_y = box.y + (header_h - btn_h) * 0.5f;

    Rectangle btn_close = { box.x + box.width - btn_w_close - 12.0f * ui_scale, btn_y, btn_w_close, btn_h };
    Rectangle btn_clear = { btn_close.x - btn_w_clear - btn_gap, btn_y, btn_w_clear, btn_h };

    bool hover_clear = interactive && CheckCollisionPointRec(mouse, btn_clear);
    bool hover_close = interactive && CheckCollisionPointRec(mouse, btn_close);

    DrawRectangleRec(btn_clear, hover_clear ? ColorAlpha(COLOR_ACCENT, 0.20f) : (Color){ 18, 20, 26, 255 });
    DrawRectangleLinesEx(btn_clear, 1.0f, hover_clear ? COLOR_ACCENT : (Color){ 36, 40, 50, 255 });
    int tw_clr = MeasureText("Clear", FONT_SIZE_SM);
    DrawText("Clear", (int)(btn_clear.x + (btn_w_clear - tw_clr) * 0.5f), (int)(btn_clear.y + (btn_h - FONT_SIZE_SM) * 0.5f), FONT_SIZE_SM, hover_clear ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED);

    DrawRectangleRec(btn_close, hover_close ? ColorAlpha(COLOR_ACCENT, 0.20f) : (Color){ 18, 20, 26, 255 });
    DrawRectangleLinesEx(btn_close, 1.0f, hover_close ? COLOR_ACCENT : (Color){ 36, 40, 50, 255 });
    int tw_esc = MeasureText("✕ Esc", FONT_SIZE_SM);
    DrawText("X Esc", (int)(btn_close.x + (btn_w_close - tw_esc) * 0.5f), (int)(btn_close.y + (btn_h - FONT_SIZE_SM) * 0.5f), FONT_SIZE_SM, hover_close ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED);

    if (interactive) {
        if (sparkles_input_consume_tap(btn_clear, NULL)) {
            pthread_mutex_lock(&state_mutex);
            for (int i = 0; i < num_playlist_files; i++) koni_metadata_free(&playlist[i].meta);
            num_playlist_files = 0;
            selected_playlist_idx = 0;
            pthread_mutex_unlock(&state_mutex);
        }
        if (sparkles_input_consume_tap(btn_close, NULL)) {
            sparkles_queue_toggle();
        }
    }

    // Dismiss if tapped outside
    Vector2 tap;
    if (interactive && sparkles_input_consume_tap((Rectangle){ 0, 0, screen_w, screen_h }, &tap)) {
        if (!CheckCollisionPointRec(tap, box)) {
            sparkles_queue_toggle();
        }
    }

    // Body dimensions
    float list_y = box.y + header_h + 6.0f * ui_scale;
    float footer_h = is_mobile ? (24.0f * ui_scale) : 32.0f;
    float list_h = box.height - header_h - footer_h - (12.0f * ui_scale);
    float row_h = is_mobile ? fmaxf(44.0f, 38.0f * ui_scale) : 34.0f;
    int max_rows = (int)(list_h / row_h);

    if (interactive && s_dragged_idx == -1) {
        float scroll = sparkles_input_get_scroll_delta(box);
        if (scroll != 0.0f) {
            s_queue_scroll += (int)roundf(scroll);
        }
    }
    if (count <= max_rows) s_queue_scroll = 0;
    else {
        if (s_queue_scroll > count - max_rows) s_queue_scroll = count - max_rows;
        if (s_queue_scroll < 0) s_queue_scroll = 0;
    }

    // Drag-and-drop pointer tracking
    if (s_dragged_idx != -1) {
        sparkles_input_consume();
        float cur_y_pos = ptr->is_down ? ptr_pos.y : mouse.y;
        int target_row = (int)floorf((cur_y_pos - list_y) / row_h);
        int candidate_idx = target_row + s_queue_scroll;
        if (candidate_idx >= 0 && candidate_idx < count) {
            s_drop_target_idx = candidate_idx;
        }

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) || ptr->just_released || !ptr->is_down) {
            if (s_drop_target_idx != -1 && s_drop_target_idx != s_dragged_idx) {
                reorder_queue_item(s_dragged_idx, s_drop_target_idx);
            }
            s_dragged_idx = -1;
            s_drop_target_idx = -1;
        }
    }

    BeginScissorMode((int)box.x, (int)list_y, (int)box.width, (int)list_h);

    if (count == 0) {
        DrawText("Queue is empty. Right-click or long-press songs to add them.", (int)(box.x + 20.0f * ui_scale), (int)(list_y + 20.0f), FONT_SIZE_SM, COLOR_TEXT_MUTED);
    }

    pthread_mutex_lock(&state_mutex);

    float handle_w = is_mobile ? (42.0f * ui_scale) : 26.0f;
    float idx_w    = 28.0f * ui_scale;
    float del_w    = is_mobile ? (38.0f * ui_scale) : 26.0f;
    float dur_w    = 44.0f * ui_scale;

    for (int i = 0; i < max_rows && (i + s_queue_scroll) < count; i++) {
        int idx = i + s_queue_scroll;
        float y = list_y + (float)i * row_h;
        Rectangle row_rect = { box.x + 8.0f * ui_scale, y, box.width - (16.0f * ui_scale), row_h - 2.0f };

        bool is_playing = (current_play_source == SOURCE_QUEUE && playing_file_idx == idx);
        bool hover_row = interactive && CheckCollisionPointRec(mouse, row_rect);

        // Delete button rect
        Rectangle btn_del = { row_rect.x + row_rect.width - del_w, y + (row_h - del_w) * 0.5f, del_w, del_w };
        bool hover_del = interactive && CheckCollisionPointRec(mouse, btn_del);

        // Drag handle rect
        Rectangle handle_r = { row_rect.x, y, handle_w, row_h };
        bool hover_handle = interactive && (CheckCollisionPointRec(mouse, handle_r) || CheckCollisionPointRec(ptr_pos, handle_r));

        if (interactive && s_dragged_idx == -1 && hover_handle && (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || ptr->just_pressed)) {
            s_dragged_idx = idx;
            s_drop_target_idx = idx;
            sparkles_input_consume();
        }

        // Row background
        if (s_dragged_idx == idx) {
            DrawRectangleRec(row_rect, (Color){ 26, 30, 42, 255 });
            DrawRectangleLinesEx(row_rect, 1.2f, COLOR_ACCENT);
        } else if (is_playing) {
            DrawRectangleRec(row_rect, (Color){ 18, 20, 26, 255 });
            DrawRectangle((int)row_rect.x, (int)row_rect.y, 3, (int)row_rect.height, COLOR_ACCENT);
        } else if (hover_row) {
            DrawRectangleRec(row_rect, (Color){ 14, 15, 20, 255 });
        }

        // Insertion line indicator
        if (s_dragged_idx != -1 && s_drop_target_idx == idx) {
            DrawLineEx((Vector2){ row_rect.x, row_rect.y }, (Vector2){ row_rect.x + row_rect.width, row_rect.y }, 2.5f, COLOR_ACCENT);
        }

        float text_y = y + (row_h - (float)FONT_SIZE_SM) * 0.5f;

        // Drag icon
        DrawText("::", (int)(handle_r.x + (handle_w - MeasureText("::", FONT_SIZE_SM)) * 0.5f), (int)text_y, FONT_SIZE_SM, hover_handle ? COLOR_ACCENT : COLOR_TEXT_DARK);

        // Track index / play icon
        float cur_col_x = handle_r.x + handle_w + 4.0f * ui_scale;
        const char *idx_label = is_playing ? "||" : TextFormat("%02d", idx + 1);
        Color idx_col = is_playing ? COLOR_ACCENT : COLOR_TEXT_MUTED;
        DrawText(idx_label, (int)cur_col_x, (int)text_y, FONT_SIZE_SM, idx_col);
        cur_col_x += idx_w;

        // Song details
        const char *title = (playlist[idx].meta.title && playlist[idx].meta.title[0]) ? playlist[idx].meta.title : playlist[idx].name;
        const char *artist = (playlist[idx].meta.artist && playlist[idx].meta.artist[0]) ? playlist[idx].meta.artist : NULL;

        float title_avail_w = (btn_del.x - dur_w - (8.0f * ui_scale)) - cur_col_x;
        if (title_avail_w < 60.0f) title_avail_w = 60.0f;

        Rectangle title_box = { cur_col_x, text_y, title_avail_w, (float)FONT_SIZE_SM * 1.3f };
        Color title_col = is_playing ? COLOR_ACCENT : (hover_row ? COLOR_TEXT_PRIMARY : ColorAlpha(COLOR_TEXT_PRIMARY, 0.85f));

        if (artist && artist[0]) {
            const char *combined = TextFormat("%s  ·  %s", title, artist);
            DrawTextMarquee(combined, title_box, box, FONT_SIZE_SM, title_col, 26.0f);
        } else {
            DrawTextMarquee(title, title_box, box, FONT_SIZE_SM, title_col, 26.0f);
        }

        // Duration
        uint32_t dur = playlist[idx].duration_sec;
        const char *dur_str = TextFormat("%u:%02u", dur / 60, dur % 60);
        float dur_x = btn_del.x - dur_w;
        DrawText(dur_str, (int)dur_x, (int)text_y, FONT_SIZE_SM, is_playing ? COLOR_ACCENT : COLOR_TEXT_MUTED);

        // Delete button
        DrawRectangleRec(btn_del, hover_del ? ColorAlpha(COLOR_ACCENT, 0.25f) : (Color){ 0, 0, 0, 0 });
        int tw_x = MeasureText("✕", FONT_SIZE_SM);
        DrawText("✕", (int)(btn_del.x + (del_w - tw_x) * 0.5f), (int)text_y, FONT_SIZE_SM, hover_del ? COLOR_ACCENT : COLOR_TEXT_MUTED);

        if (interactive && s_dragged_idx == -1 && sparkles_input_consume_tap(btn_del, NULL)) {
            remove_queue_item(idx);
            break;
        }

        // Click row to play
        Rectangle click_zone = { cur_col_x, y, btn_del.x - cur_col_x, row_h };
        if (interactive && s_dragged_idx == -1 && sparkles_input_consume_tap(click_zone, NULL)) {
            if (current_play_source != SOURCE_QUEUE && current_play_source != SOURCE_NONE) {
                base_play_source = current_play_source;
                base_playing_idx = playing_file_idx;
            }
            current_play_source = SOURCE_QUEUE;
            playing_file_idx = idx;
            strncpy(playing_filepath, playlist[idx].path, sizeof(playing_filepath) - 1);
            strncpy(playing_filename, playlist[idx].name, 255);
            atomic_store(&seek_target_ms, -1);
            atomic_store(&current_cmd_atomic, CMD_PLAY);
            break;
        }
    }

    pthread_mutex_unlock(&state_mutex);

    EndScissorMode();

    sparkles_input_block_area(box);
    rlPopMatrix();
}