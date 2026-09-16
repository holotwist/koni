#include "sparkles_vis.h"
#include "sparkles_theme.h"
#include "state.h"
#include <math.h>

static float s_angle_l = 0.0f;
static float s_angle_r = 0.0f;
static float s_tape_wobble = 0.0f;

// Computes the outer tangent contact point on a reel from an external guide roller
static Vector2 get_reel_tangent(Vector2 center, float radius, Vector2 guide, bool is_left_reel) {
    float dx = guide.x - center.x;
    float dy = guide.y - center.y;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist <= radius + 1.0f) {
        return (Vector2){ center.x + (is_left_reel ? -radius : radius), center.y };
    }
    float alpha = atan2f(dy, dx);
    float beta = acosf(fminf(1.0f, radius / dist));
    float theta = is_left_reel ? (alpha + beta) : (alpha - beta);
    return (Vector2){
        center.x + radius * cosf(theta),
        center.y + radius * sinf(theta)
    };
}

void vis_reels_render(Rectangle b, float dt) {
    float bass = 0.0f, mid = 0.0f, high = 0.0f;
    sparkles_vis_get_bands(&bass, &mid, &high);

    // Sub-frame progress calculated from audio clock
    uint32_t srate = atomic_load(&vis_srate);
    if (srate == 0) srate = 44100;
    uint32_t tot_sec = atomic_load(&p_total_sec);
    uint64_t total_frames = (uint64_t)tot_sec * (uint64_t)srate;
    float progress = (total_frames > 0) ? ((float)atomic_load(&p_frames_consumed) / (float)total_frames) : 0.0f;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    PlayState st = (PlayState)atomic_load(&play_state_atomic);
    bool playing = (st == STATE_PLAYING);

    float max_r = fminf(b.width * 0.22f, b.height * 0.36f);
    if (max_r < 25.0f) max_r = 25.0f;
    float hub_r = max_r * 0.32f;

    Vector2 center_l = { b.x + b.width * 0.30f, b.y + b.height * 0.44f };
    Vector2 center_r = { b.x + b.width * 0.70f, b.y + b.height * 0.44f };

    // Cross-sectional tape area conservation, R(p) = sqrt(R_hub^2 + p * (R_max^2 - R_hub^2))
    float hub_r_sq = hub_r * hub_r;
    float max_r_sq = max_r * max_r;
    float span_sq = max_r_sq - hub_r_sq;

    float pack_r_l = sqrtf(hub_r_sq + (1.0f - progress) * span_sq);
    float pack_r_r = sqrtf(hub_r_sq + progress * span_sq);

    // Constant linear tape velocity V across the heads, omega = V / R
    float base_omega = 1.9f;
    float v_tape = base_omega * max_r;
    float omega_l = v_tape / pack_r_l;
    float omega_r = v_tape / pack_r_r;

    if (playing) {
        // Both reels rotate counter-clockwise (angles decrement in screen coordinates where +Y is down)
        s_angle_l -= dt * omega_l;
        s_angle_r -= dt * omega_r;
        s_tape_wobble += dt * (16.0f + bass * 8.0f);

        // Keep angles normalized in [0, 2*PI]
        const float two_pi = 2.0f * (float)PI;
        if (s_angle_l < 0.0f) s_angle_l += two_pi;
        if (s_angle_r < 0.0f) s_angle_r += two_pi;
    }

    // Tape head deck and capstans
    float deck_y = center_l.y + max_r + 20.0f;
    Vector2 guide_l = { center_l.x - max_r * 0.70f, deck_y };
    Vector2 guide_r = { center_r.x + max_r * 0.70f, deck_y };
    Vector2 head_pos = { (center_l.x + center_r.x) * 0.5f, deck_y + 2.0f };

    // Dynamic tangent points matching the instantaneous radius of each reel
    float jitter = playing ? (sinf(s_tape_wobble) * (bass * 2.2f)) : 0.0f;
    Vector2 tape_p1 = get_reel_tangent(center_l, pack_r_l, guide_l, true);
    Vector2 tape_p2 = { guide_l.x, guide_l.y + jitter };
    Vector2 tape_p3 = { head_pos.x, head_pos.y + jitter };
    Vector2 tape_p4 = { guide_r.x, guide_r.y + jitter };
    Vector2 tape_p5 = get_reel_tangent(center_r, pack_r_r, guide_r, false);

    DrawLineEx(tape_p1, tape_p2, 2.2f, (Color){ 45, 48, 58, 255 });
    DrawLineEx(tape_p2, tape_p3, 2.5f, (Color){ 60, 64, 76, 255 });
    DrawLineEx(tape_p3, tape_p4, 2.5f, (Color){ 60, 64, 76, 255 });
    DrawLineEx(tape_p4, tape_p5, 2.2f, (Color){ 45, 48, 58, 255 });

    // Playback magnetic tape head
    Rectangle head_box = { head_pos.x - 14.0f, head_pos.y - 8.0f, 28.0f, 16.0f };
    DrawRectangleRec(head_box, (Color){ 14, 15, 20, 255 });
    DrawRectangleLinesEx(head_box, 1.0f, (Color){ 36, 40, 52, 255 });

    // Audio reactive head contact LED
    float audio_amp = fminf(1.0f, bass * 1.5f + mid * 2.0f);
    Color head_led = ColorAlpha(COLOR_ACCENT, 0.20f + audio_amp * 0.80f);
    DrawRectangle((int)head_pos.x - 4, (int)head_pos.y - 2, 8, 4, head_led);

    // Guide rollers
    DrawCircleV(guide_l, 6.0f, (Color){ 20, 22, 28, 255 });
    DrawCircleLines((int)guide_l.x, (int)guide_l.y, 6.0f, COLOR_TEXT_MUTED);
    DrawCircleV(guide_r, 6.0f, (Color){ 20, 22, 28, 255 });
    DrawCircleLines((int)guide_r.x, (int)guide_r.y, 6.0f, COLOR_TEXT_MUTED);

    // Dual reels helper routine
    Vector2 centers[2] = { center_l, center_r };
    float pack_radii[2] = { pack_r_l, pack_r_r };
    float angles[2] = { s_angle_l, s_angle_r };

    for (int i = 0; i < 2; i++) {
        Vector2 c = centers[i];
        float pr = pack_radii[i];
        float ang = angles[i];

        // Outer rim frame
        DrawCircleLines((int)c.x, (int)c.y, max_r, (Color){ 30, 32, 40, 255 });
        DrawCircleLines((int)c.x, (int)c.y, max_r + 2.0f, (Color){ 18, 20, 24, 255 });

        // Magnetic tape ribbon mass
        DrawCircleV(c, pr, (Color){ 18, 19, 25, 255 });
        DrawCircleLines((int)c.x, (int)c.y, pr, (Color){ 42, 45, 56, 255 });

        // Faint concentric tape wrap layers
        for (float ring_r = hub_r + 4.0f; ring_r < pr - 2.0f; ring_r += 5.5f) {
            DrawCircleLines((int)c.x, (int)c.y, ring_r, (Color){ 26, 28, 36, 160 });
        }

        // Center hub
        DrawCircleV(c, hub_r, (Color){ 10, 11, 14, 255 });
        DrawCircleLines((int)c.x, (int)c.y, hub_r, COLOR_ACCENT);

        // 3-Spoke cutouts
        for (int s = 0; s < 3; s++) {
            float a = ang + s * ((2.0f * (float)PI) / 3.0f);
            Vector2 spoke_in = { c.x + cosf(a) * (hub_r + 2.0f), c.y + sinf(a) * (hub_r + 2.0f) };
            Vector2 spoke_out = { c.x + cosf(a) * (max_r - 4.0f), c.y + sinf(a) * (max_r - 4.0f) };
            DrawLineEx(spoke_in, spoke_out, 1.8f, (Color){ 60, 64, 78, 220 });

            // Weight relief hole
            Vector2 hole = { c.x + cosf(a) * (max_r * 0.65f), c.y + sinf(a) * (max_r * 0.65f) };
            DrawCircleV(hole, max_r * 0.12f, (Color){ 8, 9, 12, 255 });
            DrawCircleLines((int)hole.x, (int)hole.y, max_r * 0.12f, (Color){ 32, 35, 45, 255 });
        }

        // Center axis nut
        DrawCircleV(c, 3.5f, COLOR_ACCENT);
    }
}