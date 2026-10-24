#define _DEFAULT_SOURCE
#include "ui_common.h"
#include "visualizers.h"
#include "vis_renderer.h"
#include "vis_math.h"
#include "state.h"
#include <math.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct { float x, y; } Vec2;

static float s_angle_l = 0.0f;
static float s_angle_r = 0.0f;
static float s_tape_wobble = 0.0f;

static Vec2 get_reel_tangent(Vec2 center, float radius, Vec2 guide, bool is_left_reel) {
    float dx = guide.x - center.x;
    float dy = guide.y - center.y;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist <= radius + 1.0f) {
        return (Vec2){ center.x + (is_left_reel ? -radius : radius), center.y };
    }
    float alpha = atan2f(dy, dx);
    float beta = acosf(fminf(1.0f, radius / dist));
    float theta = is_left_reel ? (alpha + beta) : (alpha - beta);
    return (Vec2){
        center.x + radius * cosf(theta),
        center.y + radius * sinf(theta)
    };
}

static void draw_braille_circle(uint8_t *grid, uint8_t *col_grid, int dw, int dh,
                                float cx, float cy, float r, uint8_t col, int steps) {
    if (r <= 0.5f) return;
    if (steps < 12) steps = 12;
    int prev_x = -1, prev_y = -1;
    for (int i = 0; i <= steps; i++) {
        float a = ((float)i / (float)steps) * 2.0f * (float)M_PI;
        int px = (int)roundf(cx + r * cosf(a));
        int py = (int)roundf(cy + r * sinf(a));
        if (prev_x != -1) {
            draw_braille_line_colored(grid, col_grid, dw, dh, prev_x, prev_y, px, py, col);
        }
        prev_x = px;
        prev_y = py;
    }
}

void draw_vis_reels(int y, int x, int draw_w, int draw_h) {
    if (draw_w < 10 || draw_h < 4) return;

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    static struct timespec s_last_ts = {0};
    float dt = 0.025f;
    if (s_last_ts.tv_sec != 0) {
        dt = (float)(ts.tv_sec - s_last_ts.tv_sec) + (float)(ts.tv_nsec - s_last_ts.tv_nsec) * 1e-9f;
        if (dt < 0.001f) dt = 0.001f;
        if (dt > 0.05f)  dt = 0.05f;
    }
    s_last_ts = ts;

    // Sample audio from ring buffer
    uint32_t rpos = atomic_load(&p_frames_consumed);
    float bass = 0.0f, mid = 0.0f;
    for (int i = 0; i < 64; i++) {
        uint32_t idx = (rpos - i * 4) & VIS_BUF_MASK;
        float s = fabsf(vis_ring_l[idx] + vis_ring_r[idx]) * 0.5f;
        if (i < 20) bass += s;
        else mid += s;
    }
    bass = fminf(1.0f, (bass / 20.0f) * 2.5f);
    mid  = fminf(1.0f, (mid / 44.0f) * 2.5f);

    uint32_t srate = atomic_load(&vis_srate);
    if (srate == 0) srate = 44100;
    uint32_t tot_sec = atomic_load(&p_total_sec);
    uint64_t total_frames = (uint64_t)tot_sec * (uint64_t)srate;
    uint32_t cur_frames = ui_cache.smooth_rpos > 0 ? ui_cache.smooth_rpos : atomic_load(&p_frames_consumed);
    float progress = (total_frames > 0) ? ((float)cur_frames / (float)total_frames) : 0.0f;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    PlayState st = (PlayState)atomic_load(&play_state_atomic);
    bool playing = (st == STATE_PLAYING);

    int px_w = draw_w * 2;
    int px_h = draw_h * 4;

    Vec2 center_l = { (float)px_w * 0.30f, (float)px_h * 0.44f };
    Vec2 center_r = { (float)px_w * 0.70f, (float)px_h * 0.44f };

    float max_r = fminf((center_r.x - center_l.x) * 0.44f, (float)px_h * 0.36f);
    if (max_r < 10.0f) max_r = 10.0f;
    float hub_r = max_r * 0.32f;
    if (hub_r < 3.0f) hub_r = 3.0f;

    // Cross-sectional area conservation
    float hub_sq = hub_r * hub_r;
    float span_sq = (max_r * max_r) - hub_sq;
    float pack_r_l = sqrtf(hub_sq + (1.0f - progress) * span_sq);
    float pack_r_r = sqrtf(hub_sq + progress * span_sq);

    // omega = V / R
    float base_omega = 1.9f;
    float v_tape = base_omega * max_r;
    float omega_l = v_tape / pack_r_l;
    float omega_r = v_tape / pack_r_r;

    if (playing) {
        s_angle_l -= dt * omega_l;
        s_angle_r -= dt * omega_r;
        s_tape_wobble += dt * (16.0f + bass * 8.0f);

        const float two_pi = 2.0f * (float)M_PI;
        if (s_angle_l < 0.0f) s_angle_l += two_pi;
        if (s_angle_r < 0.0f) s_angle_r += two_pi;
    }

    uint8_t *grid = vis_renderer_begin(draw_w, draw_h);
    uint8_t *col_grid = vis_renderer_get_color_grid();

    Vec2 centers[2] = { center_l, center_r };
    float pack_radii[2] = { pack_r_l, pack_r_r };
    float angles[2] = { s_angle_l, s_angle_r };

    // Dual reels
    for (int i = 0; i < 2; i++) {
        Vec2 c = centers[i];
        float pr = pack_radii[i];
        float ang = angles[i];

        // Outer frame
        draw_braille_circle(grid, col_grid, draw_w, draw_h, c.x, c.y, max_r, 1, 28);

        // Ribbon mass
        draw_braille_circle(grid, col_grid, draw_w, draw_h, c.x, c.y, pr, 2, 24);

        // Winding layers
        for (float wr = hub_r + 2.0f; wr < pr - 1.5f; wr += 3.5f) {
            draw_braille_circle(grid, col_grid, draw_w, draw_h, c.x, c.y, wr, 6, 18);
        }

        // Center hub
        draw_braille_circle(grid, col_grid, draw_w, draw_h, c.x, c.y, hub_r, 4, 16);

        // Cutouts
        for (int s = 0; s < 3; s++) {
            float a = ang + (float)s * (2.0f * (float)M_PI / 3.0f);
            float ca = cosf(a), sa = sinf(a);
            int x1 = (int)roundf(c.x + ca * (hub_r + 1.0f));
            int y1 = (int)roundf(c.y + sa * (hub_r + 1.0f));
            int x2 = (int)roundf(c.x + ca * (max_r - 1.0f));
            int y2 = (int)roundf(c.y + sa * (max_r - 1.0f));
            draw_braille_line_colored(grid, col_grid, draw_w, draw_h, x1, y1, x2, y2, 1);
        }

        // Dots (axis)
        set_braille_pixel_colored(grid, col_grid, draw_w, draw_h, (int)c.x, (int)c.y, 4);
        set_braille_pixel_colored(grid, col_grid, draw_w, draw_h, (int)c.x + 1, (int)c.y, 4);
    }

    // Tape deck
    float deck_y = center_l.y + max_r + 4.0f;
    if (deck_y > (float)px_h - 4.0f) deck_y = (float)px_h - 4.0f;

    Vec2 guide_l = { center_l.x - max_r * 0.65f, deck_y };
    Vec2 guide_r = { center_r.x + max_r * 0.65f, deck_y };
    Vec2 head_pos = { (center_l.x + center_r.x) * 0.5f, deck_y };

    // Wobble
    float jitter = playing ? (sinf(s_tape_wobble) * (bass * 2.2f)) : 0.0f;
    Vec2 t1 = get_reel_tangent(center_l, pack_r_l, guide_l, true);
    Vec2 t2 = { guide_l.x, guide_l.y + jitter };
    Vec2 t3 = { head_pos.x, head_pos.y + jitter };
    Vec2 t4 = { guide_r.x, guide_r.y + jitter };
    Vec2 t5 = get_reel_tangent(center_r, pack_r_r, guide_r, false);

    // Ribbon
    draw_braille_line_colored(grid, col_grid, draw_w, draw_h, (int)t1.x, (int)t1.y, (int)t2.x, (int)t2.y, 2);
    draw_braille_line_colored(grid, col_grid, draw_w, draw_h, (int)t2.x, (int)t2.y, (int)t3.x, (int)t3.y, 2);
    draw_braille_line_colored(grid, col_grid, draw_w, draw_h, (int)t3.x, (int)t3.y, (int)t4.x, (int)t4.y, 2);
    draw_braille_line_colored(grid, col_grid, draw_w, draw_h, (int)t4.x, (int)t4.y, (int)t5.x, (int)t5.y, 2);

    // Rollers
    draw_braille_circle(grid, col_grid, draw_w, draw_h, guide_l.x, guide_l.y, 2.5f, 4, 10);
    draw_braille_circle(grid, col_grid, draw_w, draw_h, guide_r.x, guide_r.y, 2.5f, 4, 10);

    // Playback head
    int hx = (int)head_pos.x;
    int hy = (int)(head_pos.y + jitter);
    draw_braille_line_colored(grid, col_grid, draw_w, draw_h, hx - 3, hy - 2, hx + 3, hy - 2, 4);
    draw_braille_line_colored(grid, col_grid, draw_w, draw_h, hx - 3, hy + 2, hx + 3, hy + 2, 4);
    draw_braille_line_colored(grid, col_grid, draw_w, draw_h, hx - 3, hy - 2, hx - 3, hy + 2, 4);
    draw_braille_line_colored(grid, col_grid, draw_w, draw_h, hx + 3, hy - 2, hx + 3, hy + 2, 4);

    // Reactive light
    float audio_amp = fminf(1.0f, bass * 1.5f + mid * 2.0f);
    uint8_t led_col = (playing && audio_amp > 0.35f) ? 10 : 4;
    set_braille_pixel_colored(grid, col_grid, draw_w, draw_h, hx, hy, led_col);

    vis_renderer_end(y, x, draw_w, draw_h, VIS_COLOR_CUSTOM);
}