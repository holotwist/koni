#include "sparkles_vis.h"
#include "sparkles_theme.h"
#include <math.h>
#include <string.h>

#define TERRAIN_ROWS 24
#define TERRAIN_BINS 56

static float s_slices[TERRAIN_ROWS][TERRAIN_BINS] = {0};
static float s_slice_timer = 0.0f;

void vis_terrain_render(Rectangle b, float dt) {
    float bins[TERRAIN_BINS];
    sparkles_vis_get_spectrum(bins, TERRAIN_BINS);

    // Push new spectrum slice ~30 times a second
    s_slice_timer += dt;
    if (s_slice_timer >= 0.033f) {
        s_slice_timer = 0.0f;
        for (int r = TERRAIN_ROWS - 1; r > 0; r--) {
            memcpy(s_slices[r], s_slices[r - 1], sizeof(float) * TERRAIN_BINS);
        }

        for (int i = 0; i < TERRAIN_BINS; i++) {
            float raw = bins[i];

            // Progressive high-frequency lift
            float freq_boost = 1.0f + sqrtf((float)i / (float)TERRAIN_BINS) * 1.85f;
            float val = raw * freq_boost;
            if (val > 1.0f) val = 1.0f;

            // Square-root curve
            val = sqrtf(val) * 0.95f;

            // Anchor only the extreme 2 points on the outer
            float anchor = 1.0f;
            if (i == 0) anchor = 0.0f;
            else if (i == 1) anchor = 0.45f;
            else if (i == TERRAIN_BINS - 2) anchor = 0.45f;
            else if (i == TERRAIN_BINS - 1) anchor = 0.0f;

            s_slices[0][i] = val * anchor;
        }
    }

    float bottom_y = b.y + b.height - 10.0f;
    float horizon_y = b.y + b.height * 0.20f;
    float max_peak_h = b.height * 0.38f;

    // Ground perspective gridlines
    for (int r = TERRAIN_ROWS - 1; r >= 0; r -= 4) {
        float norm_z = (float)r / (float)(TERRAIN_ROWS - 1);
        float y_line = horizon_y + (bottom_y - horizon_y) * powf(1.0f - norm_z, 1.4f);
        DrawLine((int)b.x, (int)y_line, (int)(b.x + b.width), (int)y_line, (Color){ 16, 18, 24, 180 });
    }

    // Render ridges from back (horizon) to front (foreground)
    for (int r = TERRAIN_ROWS - 1; r >= 0; r--) {
        float norm_z = (float)r / (float)(TERRAIN_ROWS - 1); // 1.0 = Horizon (far), 0.0 = Front
        float depth = 1.0f - norm_z;                         // 0.0 = Far, 1.0 = Front

        // Perspective baseline, foreground sits right at the bottom edge
        float y_base = horizon_y + (bottom_y - horizon_y) * powf(depth, 1.35f);

        // Foreground spans 100% of tile width; distant horizon narrows to 60%
        float row_width = b.width * (0.60f + depth * 0.40f);
        float start_x = b.x + (b.width - row_width) * 0.5f;
        float step_x = row_width / (float)(TERRAIN_BINS - 1);

        Vector2 pts[TERRAIN_BINS];
        for (int i = 0; i < TERRAIN_BINS; i++) {
            float amp = s_slices[r][i];
            float height_scale = max_peak_h * (0.28f + depth * 0.72f);
            pts[i] = (Vector2){
                start_x + i * step_x,
                y_base - (amp * height_scale)
            };
        }

        // Slender occlusion strip
        for (int i = 0; i < TERRAIN_BINS - 1; i++) {
            Vector2 p1 = pts[i];
            Vector2 p2 = pts[i + 1];
            Vector2 b1 = { p1.x, y_base + 6.0f };
            Vector2 b2 = { p2.x, y_base + 6.0f };

            DrawTriangle(p1, b1, p2, (Color){ 8, 9, 12, 245 });
            DrawTriangle(p2, b1, b2, (Color){ 8, 9, 12, 245 });
        }

        // Vector line rendering
        float alpha = 0.20f + depth * 0.80f;
        Color line_col = (r == 0) ? COLOR_ACCENT : ColorAlpha(COLOR_TEXT_PRIMARY, alpha);
        float line_thick = (r == 0) ? 2.2f : (1.0f + depth * 0.9f);

        for (int i = 0; i < TERRAIN_BINS - 1; i++) {
            DrawLineEx(pts[i], pts[i + 1], line_thick, line_col);
        }
    }
}