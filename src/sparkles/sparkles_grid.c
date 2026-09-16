#include "sparkles_grid.h"
#include "sparkles_widgets.h"
#include <math.h>

void sparkles_grid_init(SparklesGrid *grid) {
    grid->tile_count = 0;
    grid->edit_mode = false;
    grid->active_drag_id = -1;
    grid->active_resize_id = -1;

    // 12x8 Grid expanded across the full canvas
    // Top Row (gy=0, gh=4):
    //  - (0,0, 4x4) Playlists Viewer
    //  - (4,0, 4x4) Album Art & Track Info
    //  - (8,0, 4x2) Waveform Seek Bar
    //  - (8,2, 4x2) Transport Controls
    // Bottom Row (gy=4, gh=4):
    //  - (0,4, 12x4) Full-Width Song List

    // Playlists Viewer (Top Left)
    grid->tiles[grid->tile_count++] = (SparklesTile){
        .id = 1, .type = TILE_TRACK_LIST, .title = "Playlists",
        .gx = 0, .gy = 0, .gw = 4, .gh = 4,
        .render = tile_playlists_render,
        .handle_input = tile_playlists_input
    };

    // Cover Art (Top Center)
    grid->tiles[grid->tile_count++] = (SparklesTile){
        .id = 2, .type = TILE_ALBUM_ART, .title = "Album Art",
        .gx = 4, .gy = 0, .gw = 4, .gh = 4,
        .render = tile_album_art_render,
        .handle_input = tile_album_art_input
    };

    // Waveform Seek (Top Right Upper)
    grid->tiles[grid->tile_count++] = (SparklesTile){
        .id = 3, .type = TILE_WAVEFORM_SEEK, .title = "Waveform",
        .gx = 8, .gy = 0, .gw = 4, .gh = 2,
        .render = tile_waveform_render,
        .handle_input = tile_waveform_input
    };

    // Transport Controls (Top Right Lower)
    grid->tiles[grid->tile_count++] = (SparklesTile){
        .id = 4, .type = TILE_TRANSPORT, .title = "Transport",
        .gx = 8, .gy = 2, .gw = 4, .gh = 2,
        .render = tile_transport_render,
        .handle_input = tile_transport_input
    };

    // Song List / Music Library (Full-Width Bottom)
    grid->tiles[grid->tile_count++] = (SparklesTile){
        .id = 5, .type = TILE_PLAYLIST_TABLE, .title = "Song List",
        .gx = 0, .gy = 4, .gw = 12, .gh = 4,
        .render = tile_song_list_render,
        .handle_input = tile_song_list_input
    };
}

void sparkles_grid_update(SparklesGrid *grid, float screen_w, float screen_h, bool allow_input) {
    float avail_w = screen_w - (GRID_PADDING * 2.0f);
    float avail_h = screen_h - (GRID_PADDING * 2.0f);

    float cell_w = (avail_w - (GRID_GAP * (GRID_COLS - 1))) / (float)GRID_COLS;
    float cell_h = (avail_h - (GRID_GAP * (GRID_ROWS - 1))) / (float)GRID_ROWS;

    Vector2 mouse = GetMousePosition();

    // Toggle edit mode with TAB key
    if (allow_input && IsKeyPressed(KEY_TAB)) {
        grid->edit_mode = !grid->edit_mode;
    }

    for (int i = 0; i < grid->tile_count; i++) {
        SparklesTile *t = &grid->tiles[i];

        // Target bounds based on grid position
        float tx = GRID_PADDING + t->gx * (cell_w + GRID_GAP);
        float ty = GRID_PADDING + t->gy * (cell_h + GRID_GAP);
        float tw = t->gw * cell_w + (t->gw - 1) * GRID_GAP;
        float th = t->gh * cell_h + (t->gh - 1) * GRID_GAP;

        if (grid->edit_mode && allow_input) {
            Rectangle resize_handle = { t->rect.x + t->rect.width - 16, t->rect.y + t->rect.height - 16, 16, 16 };

            // Handle Resizing
            if (grid->active_resize_id == t->id) {
                float cur_w = mouse.x - t->rect.x;
                float cur_h = mouse.y - t->rect.y;
                t->gw = (int)roundf((cur_w + GRID_GAP) / (cell_w + GRID_GAP));
                t->gh = (int)roundf((cur_h + GRID_GAP) / (cell_h + GRID_GAP));
                if (t->gw < 1) t->gw = 1;
                if (t->gh < 1) t->gh = 1;
                if (t->gx + t->gw > GRID_COLS) t->gw = GRID_COLS - t->gx;
                if (t->gy + t->gh > GRID_ROWS) t->gh = GRID_ROWS - t->gy;

                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) grid->active_resize_id = -1;
            } else if (CheckCollisionPointRec(mouse, resize_handle) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && grid->active_drag_id == -1) {
                grid->active_resize_id = t->id;
            }
            // Handle Dragging
            else if (grid->active_drag_id == t->id) {
                float drag_x = mouse.x - t->drag_offset.x - GRID_PADDING;
                float drag_y = mouse.y - t->drag_offset.y - GRID_PADDING;
                t->gx = (int)roundf(drag_x / (cell_w + GRID_GAP));
                t->gy = (int)roundf(drag_y / (cell_h + GRID_GAP));
                if (t->gx < 0) t->gx = 0;
                if (t->gy < 0) t->gy = 0;
                if (t->gx + t->gw > GRID_COLS) t->gx = GRID_COLS - t->gw;
                if (t->gy + t->gh > GRID_ROWS) t->gy = GRID_ROWS - t->gh;

                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) grid->active_drag_id = -1;
            } else if (CheckCollisionPointRec(mouse, t->rect) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && grid->active_resize_id == -1) {
                grid->active_drag_id = t->id;
                t->drag_offset = (Vector2){ mouse.x - t->rect.x, mouse.y - t->rect.y };
            }
        }

        // Lerp animation toward target grid geometry
        float lerp_rate = 0.25f;
        t->rect.x += (tx - t->rect.x) * lerp_rate;
        t->rect.y += (ty - t->rect.y) * lerp_rate;
        t->rect.width += (tw - t->rect.width) * lerp_rate;
        t->rect.height += (th - t->rect.height) * lerp_rate;

        if (allow_input && !grid->edit_mode && t->handle_input) {
            t->handle_input(t, t->rect);
        }
    }
}

void sparkles_grid_render(SparklesGrid *grid) {
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    float min_x = GRID_PADDING;
    float min_y = GRID_PADDING;
    float max_x = sw - GRID_PADDING;
    float max_y = sh - GRID_PADDING;

    // Render Tiles
    for (int i = 0; i < grid->tile_count; i++) {
        SparklesTile *t = &grid->tiles[i];

        // Subtle dark tint
        DrawRectangleRec(t->rect, (Color){ 10, 11, 14, 110 });

        // Index tag
        const char *idx_tag = TextFormat("// %02d · %s", t->id, t->title);
        DrawText(idx_tag, (int)(t->rect.x + t->rect.width - MeasureText(idx_tag, 9) - 10), (int)(t->rect.y + 6), 9, (Color){ 55, 60, 72, 200 });

        // Scissor Mode, content never bleeds outside the tile
        BeginScissorMode((int)t->rect.x, (int)t->rect.y, (int)t->rect.width, (int)t->rect.height);
        if (t->render) {
            t->render(t, t->rect);
        }
        EndScissorMode();

        // Edit Mode Indicators
        if (grid->edit_mode) {
            DrawRectangleLinesEx(t->rect, 1.5f, ColorAlpha(COLOR_ACCENT, 0.75f));
            
            // Corner resize handle
            DrawTriangle(
                (Vector2){ t->rect.x + t->rect.width, t->rect.y + t->rect.height - 12 },
                (Vector2){ t->rect.x + t->rect.width - 12, t->rect.y + t->rect.height },
                (Vector2){ t->rect.x + t->rect.width, t->rect.y + t->rect.height },
                COLOR_ACCENT
            );
        }
    }

    // Gather tile corners and merge adjacent corners into single centered crosses
    typedef struct {
        float x;
        float y;
        int count;
    } MergedCross;

    MergedCross crosses[64];
    int cross_count = 0;
    float merge_threshold = GRID_GAP + 4.0f;

    for (int i = 0; i < grid->tile_count; i++) {
        SparklesTile *t = &grid->tiles[i];

        Vector2 tile_corners[4] = {
            { t->rect.x, t->rect.y },
            { t->rect.x + t->rect.width, t->rect.y },
            { t->rect.x, t->rect.y + t->rect.height },
            { t->rect.x + t->rect.width, t->rect.y + t->rect.height }
        };

        for (int c = 0; c < 4; c++) {
            Vector2 pt = tile_corners[c];
            int match_idx = -1;

            for (int k = 0; k < cross_count; k++) {
                if (fabsf(crosses[k].x - pt.x) <= merge_threshold &&
                    fabsf(crosses[k].y - pt.y) <= merge_threshold) {
                    match_idx = k;
                    break;
                }
            }

            if (match_idx >= 0) {
                crosses[match_idx].x = (crosses[match_idx].x * crosses[match_idx].count + pt.x) / (float)(crosses[match_idx].count + 1);
                crosses[match_idx].y = (crosses[match_idx].y * crosses[match_idx].count + pt.y) / (float)(crosses[match_idx].count + 1);
                crosses[match_idx].count++;
            } else if (cross_count < 64) {
                crosses[cross_count] = (MergedCross){
                    .x = pt.x,
                    .y = pt.y,
                    .count = 1
                };
                cross_count++;
            }
        }
    }

    // Render single crosses with border-aware arm clipping
    float arm = 7.0f;
    float thickness = 1.4f;
    Color static_cross_col = (Color){ 68, 72, 84, 200 };

    for (int k = 0; k < cross_count; k++) {
        float cx = crosses[k].x;
        float cy = crosses[k].y;

        bool draw_left  = (cx > min_x + 5.0f);
        bool draw_right = (cx < max_x - 5.0f);
        bool draw_up    = (cy > min_y + 5.0f);
        bool draw_down  = (cy < max_y - 5.0f);

        if (draw_left)  DrawLineEx((Vector2){ cx, cy }, (Vector2){ cx - arm, cy }, thickness, static_cross_col);
        if (draw_right) DrawLineEx((Vector2){ cx, cy }, (Vector2){ cx + arm, cy }, thickness, static_cross_col);
        if (draw_up)    DrawLineEx((Vector2){ cx, cy }, (Vector2){ cx, cy - arm }, thickness, static_cross_col);
        if (draw_down)  DrawLineEx((Vector2){ cx, cy }, (Vector2){ cx, cy + arm }, thickness, static_cross_col);
    }

    if (grid->edit_mode) {
        DrawText("[EDIT MODE: Drag tiles to move | Drag bottom-right corner to resize | TAB to finish]", 
                 20, GetScreenHeight() - 24, 12, COLOR_ACCENT);
    }
}