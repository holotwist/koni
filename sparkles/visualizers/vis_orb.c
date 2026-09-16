#include "sparkles_vis.h"
#include "sparkles_theme.h"
#include <math.h>

#define ORB_POINTS 72

void vis_orb_render(Rectangle b, float dt) {
    (void)dt;
    // Centered in the visualizer bounds
    Vector2 center = { b.x + b.width * 0.5f, b.y + b.height * 0.5f };
    float base_radius = fminf(b.width, b.height) * 0.34f;

    float bass = 0.0f, mid = 0.0f, high = 0.0f;
    sparkles_vis_get_bands(&bass, &mid, &high);

    static float time = 0.0f;
    time += GetFrameTime() * (1.2f + bass * 2.8f);

    // Compute fluid organic ripple vertices
    Vector2 pts[ORB_POINTS + 1];
    for (int i = 0; i < ORB_POINTS; i++) {
        float angle = ((float)i / (float)ORB_POINTS) * (2.0f * PI);

        float ripple = sinf(angle * 3.0f + time * 2.2f) * (bass * 16.0f)
                     + cosf(angle * 5.0f - time * 1.8f) * (mid * 10.0f)
                     + sinf(angle * 7.0f + time * 3.0f) * (high * 6.0f);

        float r = base_radius + ripple;
        pts[i] = (Vector2){
            center.x + cosf(angle) * r,
            center.y + sinf(angle) * r
        };
    }
    pts[ORB_POINTS] = pts[0];

    // Solid dark body
    for (int i = 0; i < ORB_POINTS; i++) {
        DrawTriangle(center, pts[i], pts[i + 1], (Color){ 22, 23, 27, 255 });
    }

    // Clean outer rim
    for (int i = 0; i < ORB_POINTS; i++) {
        DrawLineEx(pts[i], pts[i + 1], 2.5f, ColorAlpha(COLOR_ACCENT, 0.6f + high * 0.4f));
    }
}