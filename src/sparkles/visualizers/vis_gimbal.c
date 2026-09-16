#include "sparkles_vis.h"
#include "sparkles_theme.h"
#include <math.h>

#define GIMBAL_PTS 64

typedef struct {
    float x, y, z;
} GimbalVec3;

// Euler rotation state
static float s_pitch = 0.20f;
static float s_roll  = 0.15f;
static float s_yaw   = 0.0f;

// Rotation helpers
static inline GimbalVec3 rot_x(GimbalVec3 v, float a) {
    float c = cosf(a), s = sinf(a);
    return (GimbalVec3){ v.x, v.y * c - v.z * s, v.y * s + v.z * c };
}

static inline GimbalVec3 rot_y(GimbalVec3 v, float a) {
    float c = cosf(a), s = sinf(a);
    return (GimbalVec3){ v.x * c + v.z * s, v.y, -v.x * s + v.z * c };
}

static inline GimbalVec3 rot_z(GimbalVec3 v, float a) {
    float c = cosf(a), s = sinf(a);
    return (GimbalVec3){ v.x * c - v.y * s, v.x * s + v.y * c, v.z };
}

static void draw_3d_ring(Vector2 center, float radius, float angle_x, float angle_y, float angle_z,
                         Color ring_col, float thickness) {
    Vector2 prev_pt = {0};
    bool has_prev = false;

    for (int i = 0; i <= GIMBAL_PTS; i++) {
        float a = ((float)i / (float)GIMBAL_PTS) * 2.0f * (float)PI;
        GimbalVec3 p = { cosf(a) * radius, sinf(a) * radius, 0.0f };

        p = rot_x(p, angle_x);
        p = rot_y(p, angle_y);
        p = rot_z(p, angle_z);

        // Perspective projection
        const float d_cam = 4.2f;
        float z_norm = p.z / (radius * 1.6f);
        float pz = d_cam + z_norm;
        float scale = d_cam / pz;

        Vector2 scr = { center.x + p.x * scale, center.y + p.y * scale };

        if (has_prev) {
            // Front segments are brighter and thicker
            float depth_fade = 0.35f + 0.65f * (1.0f - (z_norm + 1.0f) * 0.5f);
            depth_fade = fminf(1.0f, fmaxf(0.15f, depth_fade));
            DrawLineEx(prev_pt, scr, thickness * (0.75f + depth_fade * 0.5f), ColorAlpha(ring_col, depth_fade));
        }
        prev_pt = scr;
        has_prev = true;
    }
}

void vis_gimbal_render(Rectangle b, float dt) {
    dt = fminf(dt, 0.05f);

    float bass = 0.0f, mid = 0.0f, high = 0.0f;
    sparkles_vis_get_bands(&bass, &mid, &high);

    // Dynamic rotation momentum decoupled per frequency band
    s_pitch += dt * (0.35f + bass * 1.8f);
    s_roll  += dt * (0.50f + mid  * 2.2f);
    s_yaw   += dt * (0.70f + high * 2.8f);

    Vector2 center = { b.x + b.width * 0.5f, b.y + b.height * 0.5f };
    float max_r = fminf(b.width, b.height) * 0.38f;
    if (max_r < 40.0f) max_r = 40.0f;

    // Outer fixed chassis ring
    DrawCircleLines((int)center.x, (int)center.y, max_r * 1.08f, (Color){ 25, 27, 34, 255 });
    DrawCircleLines((int)center.x, (int)center.y, max_r * 1.02f, (Color){ 35, 38, 48, 255 });

    // Chassis marks every 30 degrees
    for (int t = 0; t < 12; t++) {
        float a = ((float)t / 12.0f) * 2.0f * (float)PI;
        Vector2 t1 = { center.x + cosf(a) * (max_r * 1.02f), center.y + sinf(a) * (max_r * 1.02f) };
        Vector2 t2 = { center.x + cosf(a) * (max_r * 1.08f), center.y + sinf(a) * (max_r * 1.08f) };
        DrawLineEx(t1, t2, (t % 3 == 0) ? 1.6f : 1.0f, (t % 3 == 0) ? COLOR_ACCENT : (Color){ 60, 64, 76, 200 });
    }

    // Outer gimbal ring
    float r1 = max_r * 0.94f;
    draw_3d_ring(center, r1, s_pitch, 0.0f, 0.0f, COLOR_ACCENT, 2.0f + bass * 1.2f);

    // Middle gimbal ring
    float r2 = max_r * 0.76f;
    draw_3d_ring(center, r2, s_pitch * 0.5f, s_roll, 0.0f, (Color){ 140, 180, 255, 255 }, 1.8f);

    // Inner gimbal ring
    float r3 = max_r * 0.58f;
    draw_3d_ring(center, r3, s_pitch * 0.3f, s_roll * 0.6f, s_yaw, (Color){ 230, 210, 130, 255 }, 1.6f);

    // Central audio-reactive
    float core_r = max_r * (0.28f + bass * 0.12f);

    // Rotating latitude & longitude
    draw_3d_ring(center, core_r, s_pitch * 1.2f, s_yaw * 0.8f, s_roll * 0.4f, ColorAlpha(COLOR_TEXT_PRIMARY, 0.65f), 1.2f);
    draw_3d_ring(center, core_r * 0.65f, s_pitch * 0.8f, s_roll * 1.2f, s_yaw * 0.5f, ColorAlpha(COLOR_ACCENT_DIM, 0.55f), 1.0f);

    // Core central
    float nuc_r = 4.0f + bass * 7.0f;
    DrawCircleV(center, nuc_r * 2.0f, ColorAlpha(COLOR_ACCENT, 0.18f + bass * 0.25f));
    DrawCircleV(center, nuc_r, COLOR_ACCENT);
    DrawCircleV(center, nuc_r * 0.5f, WHITE);

    // Transient spikes
    if (high > 0.25f) {
        float spike_len = core_r * (1.2f + high * 0.8f);
        DrawLineEx((Vector2){ center.x - spike_len, center.y }, (Vector2){ center.x + spike_len, center.y }, 1.2f, ColorAlpha(WHITE, high * 0.8f));
        DrawLineEx((Vector2){ center.x, center.y - spike_len }, (Vector2){ center.x, center.y + spike_len }, 1.2f, ColorAlpha(WHITE, high * 0.8f));
    }

    // Outer pivot gimbal axis
    DrawCircleV((Vector2){ center.x - r1, center.y }, 3.5f, (Color){ 45, 48, 58, 255 });
    DrawCircleV((Vector2){ center.x + r1, center.y }, 3.5f, (Color){ 45, 48, 58, 255 });
    DrawCircleLines((int)(center.x - r1), (int)center.y, 3.5f, COLOR_ACCENT);
    DrawCircleLines((int)(center.x + r1), (int)center.y, 3.5f, COLOR_ACCENT);
}