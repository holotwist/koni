#define _DEFAULT_SOURCE
#include "sparkles_queue.h"
#include "sparkles_theme.h"
#include "state.h"
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
static float s_drag_mouse_offset_y = 0.0f;
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

    rlPushMatrix();
    rlTranslatef(screen_w * 0.5f, screen_h * 0.5f - 40.0f * (inv * inv), 0.0f);
    rlScalef(1.0f + 0.15f * inv, 1.0f + 0.15f * inv, 1.0f);
    rlTranslatef(-screen_w * 0.5f, -screen_h * 0.5f, 0.0f);

    // Dimmed background
    DrawRectangle(0, 0, (int)screen_w, (int)screen_h, ColorAlpha((Color){ 3, 3, 5, 255 }, 0.70f * ease));

    float box_w = fminf(760.0f, screen_w - 60.0f);
    float box_h = fminf(600.0f, screen_h - 80.0f);
    Rectangle box = { (screen_w - box_w) * 0.5f, (screen_h - box_h) * 0.5f, box_w, box_h };

    Vector2 mouse = GetMousePosition();
    bool interactive = (s_anim_progress >= 0.99f);

    DrawRectangleRec(box, (Color){ 8, 9, 12, 250 });
    DrawRectangleLinesEx(box, 1.0f, (Color){ 32, 35, 45, 255 });
    DrawNothingCornerBrackets(box, 10.0f, COLOR_ACCENT);

    // Header bar
    float header_h = 44.0f;
    DrawRectangle((int)box.x, (int)box.y, (int)box.width, (int)header_h, (Color){ 12, 13, 17, 255 });
    DrawLine((int)box.x, (int)(box.y + header_h), (int)(box.x + box.width), (int)(box.y + header_h), (Color){ 28, 30, 38, 255 });

    pthread_mutex_lock(&state_mutex);
    int count = num_playlist_files;
    pthread_mutex_unlock(&state_mutex);

    DrawText(TextFormat("PLAYBACK QUEUE  ·  %d Tracks", count), (int)box.x + 18, (int)box.y + 14, FONT_SIZE_MD, COLOR_TEXT_PRIMARY);

    // Clear and close buttons
    Rectangle btn_clear = { box.x + box.width - 160, box.y + 10, 70, 24 };
    Rectangle btn_close = { box.x + box.width - 80, box.y + 10, 64, 24 };

    bool hover_clear = interactive && CheckCollisionPointRec(mouse, btn_clear);
    bool hover_close = interactive && CheckCollisionPointRec(mouse, btn_close);

    DrawRectangleRec(btn_clear, hover_clear ? ColorAlpha(COLOR_ACCENT, 0.20f) : (Color){ 18, 20, 26, 255 });
    DrawRectangleLinesEx(btn_clear, 1.0f, hover_clear ? COLOR_ACCENT : (Color){ 36, 40, 50, 255 });
    DrawText("Clear", (int)btn_clear.x + 16, (int)btn_clear.y + 5, FONT_SIZE_SM, hover_clear ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED);

    DrawRectangleRec(btn_close, hover_close ? ColorAlpha(COLOR_ACCENT, 0.20f) : (Color){ 18, 20, 26, 255 });
    DrawRectangleLinesEx(btn_close, 1.0f, hover_close ? COLOR_ACCENT : (Color){ 36, 40, 50, 255 });
    DrawText("✕ Esc", (int)btn_close.x + 10, (int)btn_close.y + 5, FONT_SIZE_SM, hover_close ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED);

    if (hover_clear && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        pthread_mutex_lock(&state_mutex);
        for (int i = 0; i < num_playlist_files; i++) koni_metadata_free(&playlist[i].meta);
        num_playlist_files = 0;
        selected_playlist_idx = 0;
        pthread_mutex_unlock(&state_mutex);
    }
    if (hover_close && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        sparkles_queue_toggle();
    }

    // Body area
    float list_y = box.y + header_h + 10.0f;
    float list_h = box.height - header_h - 40.0f;
    float row_h = 32.0f;
    int max_rows = (int)(list_h / row_h);

    if (interactive && CheckCollisionPointRec(mouse, (Rectangle){ box.x, list_y, box.width, list_h })) {
        s_queue_scroll -= (int)GetMouseWheelMove() * 3;
        if (s_queue_scroll < 0) s_queue_scroll = 0;
        if (s_queue_scroll > count - max_rows) s_queue_scroll = count - max_rows;
        if (s_queue_scroll < 0) s_queue_scroll = 0;
    }

    BeginScissorMode((int)box.x, (int)list_y, (int)box.width, (int)list_h);

    if (count == 0) {
        DrawText("Queue is empty. Right-click on songs to add them.", (int)box.x + 24, (int)list_y + 24, FONT_SIZE_SM, COLOR_TEXT_MUTED);
    }

    pthread_mutex_lock(&state_mutex);

    // Drop insertion logic
    if (s_dragged_idx != -1 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (s_drop_target_idx != -1 && s_drop_target_idx != s_dragged_idx) {
            reorder_queue_item(s_dragged_idx, s_drop_target_idx);
        }
        s_dragged_idx = -1;
        s_drop_target_idx = -1;
    }

    for (int i = 0; i < max_rows && (i + s_queue_scroll) < count; i++) {
        int idx = i + s_queue_scroll;
        float y = list_y + i * row_h;
        Rectangle row_rect = { box.x + 12.0f, y, box.width - 24.0f, row_h - 2.0f };

        bool is_playing = (current_play_source == SOURCE_QUEUE && playing_file_idx == idx);
        bool hover_row = interactive && CheckCollisionPointRec(mouse, row_rect);

        // Delete button
        Rectangle btn_del = { row_rect.x + row_rect.width - 32.0f, y + 4.0f, 22.0f, 22.0f };
        bool hover_del = interactive && CheckCollisionPointRec(mouse, btn_del);

        // Drag handle
        Rectangle handle_r = { row_rect.x + 6.0f, y + 4.0f, 20.0f, 22.0f };
        bool hover_handle = interactive && CheckCollisionPointRec(mouse, handle_r);

        if (hover_handle && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            s_dragged_idx = idx;
            s_drag_mouse_offset_y = mouse.y - y;
        }

        if (s_dragged_idx != -1 && hover_row) {
            s_drop_target_idx = idx;
        }

        if (s_dragged_idx == idx) {
            DrawRectangleRec(row_rect, (Color){ 20, 22, 30, 160 });
            DrawRectangleLinesEx(row_rect, 1.0f, COLOR_ACCENT);
        } else if (is_playing) {
            DrawRectangleRec(row_rect, (Color){ 18, 20, 26, 255 });
            DrawRectangle((int)row_rect.x, (int)row_rect.y, 3, (int)row_rect.height, COLOR_ACCENT);
        } else if (hover_row) {
            DrawRectangleRec(row_rect, (Color){ 14, 15, 20, 255 });
        }

        // Drop line indicator
        if (s_dragged_idx != -1 && s_drop_target_idx == idx) {
            DrawLineEx((Vector2){ row_rect.x, row_rect.y }, (Vector2){ row_rect.x + row_rect.width, row_rect.y }, 2.0f, COLOR_ACCENT);
        }

        // Handle dots
        DrawText("::", (int)handle_r.x + 4, (int)handle_r.y + 3, FONT_SIZE_SM, hover_handle ? COLOR_ACCENT : COLOR_TEXT_DARK);

        // Index
        DrawText(TextFormat("%02d", idx + 1), (int)row_rect.x + 32, (int)y + 6, FONT_SIZE_SM, is_playing ? COLOR_ACCENT : COLOR_TEXT_MUTED);

        // Song Title
        const char *display_title = (playlist[idx].meta.title && playlist[idx].meta.title[0]) ? playlist[idx].meta.title : playlist[idx].name;
        DrawText(display_title, (int)row_rect.x + 64, (int)y + 6, FONT_SIZE_SM, is_playing ? COLOR_ACCENT : COLOR_TEXT_PRIMARY);

        // Duration
        uint32_t dur = playlist[idx].duration_sec;
        DrawText(TextFormat("%u:%02u", dur / 60, dur % 60), (int)row_rect.x + row_rect.width - 80, (int)y + 6, FONT_SIZE_SM, COLOR_TEXT_MUTED);

        // Delete
        DrawRectangleRec(btn_del, hover_del ? ColorAlpha(COLOR_ACCENT, 0.25f) : (Color){ 0, 0, 0, 0 });
        DrawText("✕", (int)btn_del.x + 6, (int)btn_del.y + 4, 11, hover_del ? COLOR_ACCENT : COLOR_TEXT_MUTED);

        if (interactive && hover_del && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            remove_queue_item(idx);
            break;
        }

        // Left-click to play immediately from queue
        if (interactive && hover_row && !hover_del && !hover_handle && s_dragged_idx == -1 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
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

    // Bottom help tip
    DrawText("Drag '::' to reorder  |  Click to play  |  Queue holds playback priority", 
             (int)box.x + 18, (int)(box.y + box.height - 24), 10, COLOR_TEXT_DARK);

    rlPopMatrix();
}