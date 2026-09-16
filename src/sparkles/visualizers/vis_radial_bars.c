#include "sparkles_vis.h"
#include "sparkles_theme.h"
#include <math.h>

#define RADIAL_BINS 64

void vis_radial_bars_render(Rectangle b, float dt) {
    (void)dt;
    Vector2 center = { b.x + b.width / 2.0f, b.y + b.height * 0.44f };
    float inner_radius = fminf(b.width, b.height) * 0.18f;
    float max_bar_len  = fminf(b.width, b.height) * 0.22f;

    float bins[RADIAL_BINS];
    sparkles_vis_get_spectrum(bins, RADIAL_BINS);

    // Inner center
    DrawCircleV(center, inner_radius * 0.92f, (Color){ 22, 23, 27, 255 });
    DrawCircleLines((int)center.x, (int)center.y, inner_radius * 0.92f, COLOR_TILE_BORDER);

    // Radiating equalizer bars
    for (int i = 0; i < RADIAL_BINS; i++) {
        float angle = ((float)i / (float)RADIAL_BINS) * (2.0f * PI) - (PI / 2.0f);
        float bar_len = bins[i] * max_bar_len;
        if (bar_len < 2.0f) bar_len = 2.0f;

        Vector2 start = {
            center.x + cosf(angle) * inner_radius,
            center.y + sinf(angle) * inner_radius
        };
        Vector2 end = {
            center.x + cosf(angle) * (inner_radius + bar_len),
            center.y + sinf(angle) * (inner_radius + bar_len)
        };

        Color tip_color = ColorAlpha(COLOR_ACCENT, 0.6f + bins[i] * 0.4f);
        DrawLineEx(start, end, 3.0f, tip_color);
        DrawCircleV(end, 2.0f, COLOR_TEXT_PRIMARY);
    }
}