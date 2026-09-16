#include "sparkles_vis.h"
#include "sparkles_theme.h"
#include "state.h"
#include <math.h>

#define SCOPE_SAMPLES 280

void vis_vector_scope_render(Rectangle b, float dt) {
    (void)dt;
    Vector2 center = { b.x + b.width / 2.0f, b.y + b.height * 0.44f };
    float scale = fminf(b.width, b.height) * 0.38f;

    // Reticle Target Lines
    DrawCircleLines((int)center.x, (int)center.y, scale * 0.5f, ColorAlpha(COLOR_TEXT_DARK, 0.4f));
    DrawCircleLines((int)center.x, (int)center.y, scale, ColorAlpha(COLOR_TEXT_DARK, 0.3f));
    DrawLine((int)(center.x - scale), (int)center.y, (int)(center.x + scale), (int)center.y, ColorAlpha(COLOR_TEXT_DARK, 0.25f));
    DrawLine((int)center.x, (int)(center.y - scale), (int)center.x, (int)(center.y + scale), ColorAlpha(COLOR_TEXT_DARK, 0.25f));

    uint32_t rpos = atomic_load(&p_frames_consumed);
    Vector2 prev_pt = center;
    bool has_prev = false;

    for (int i = 0; i < SCOPE_SAMPLES; i += 2) {
        uint32_t idx = (rpos - i) & VIS_BUF_MASK;
        float l = vis_ring_l[idx];
        float r = vis_ring_r[idx];

        // Mid/Side rotation matrix
        float mid  = (l + r) * 0.7071f * 1.3f;
        float side = (l - r) * 0.7071f * 1.3f;

        Vector2 pt = {
            center.x + side * scale,
            center.y - mid  * scale
        };

        if (has_prev) {
            float fade = (float)(SCOPE_SAMPLES - i) / (float)SCOPE_SAMPLES;
            DrawLineEx(prev_pt, pt, 1.8f, ColorAlpha(COLOR_ACCENT, fade * 0.85f));
        }

        prev_pt = pt;
        has_prev = true;
    }
}