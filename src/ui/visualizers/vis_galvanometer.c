#define _DEFAULT_SOURCE
#include "ui_common.h"
#include "visualizers.h"
#include "vis_renderer.h"
#include "vis_math.h"
#include "state.h"
#include <math.h>
#include <string.h>
#include <time.h>
#include <ncurses.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define VU_MIN_DB -20.0f
#define VU_MAX_DB  +3.0f

typedef struct {
    float angle;
    float velocity;
    float peak_alpha;
} MeterNeedle;

static MeterNeedle s_needle_l = { -48.0f, 0.0f, 0.0f };
static MeterNeedle s_needle_r = { -48.0f, 0.0f, 0.0f };

static inline float db_to_norm(float db) {
    if (db <= VU_MIN_DB) return 0.0f;
    if (db >= VU_MAX_DB) return 1.0f;
    return (db - VU_MIN_DB) / (VU_MAX_DB - VU_MIN_DB);
}

static void update_needle(MeterNeedle *n, float target_norm, float peak_val, float dt) {
    if (isnan(n->angle) || isinf(n->angle)) n->angle = -48.0f;
    if (isnan(n->velocity) || isinf(n->velocity)) n->velocity = 0.0f;
    if (dt > 0.05f) dt = 0.05f;
    if (dt <= 0.0001f) return;

    float target_angle = -48.0f + target_norm * 96.0f;
    float omega = 20.0f;
    float zeta = 0.72f;

    for (int step = 0; step < 2; step++) {
        float sub_dt = dt * 0.5f;
        float spring = (omega * omega) * (target_angle - n->angle);
        float damp = 2.0f * zeta * omega * n->velocity;
        n->velocity += (spring - damp) * sub_dt;
        n->angle += n->velocity * sub_dt;

        if (n->angle < -52.0f) {
            n->angle = -52.0f;
            n->velocity = -n->velocity * 0.2f;
        } else if (n->angle > 52.0f) {
            n->angle = 52.0f;
            n->velocity = -n->velocity * 0.2f;
        }
    }

    if (peak_val > 0.985f) n->peak_alpha = 1.0f;
    else {
        n->peak_alpha -= dt * 4.0f;
        if (n->peak_alpha < 0.0f) n->peak_alpha = 0.0f;
    }
}

static void draw_single_meter(uint8_t *grid, uint8_t *col_grid, int draw_w, int draw_h,
                              int px_x0, int px_y0, int px_w, int px_h,
                              const MeterNeedle *needle) {
    int pivot_x = px_x0 + (px_w / 2);
    int pivot_y = px_y0 + px_h - 2;

    float max_dx = (float)px_w * 0.5f - 3.0f;
    float R = fminf(max_dx / 0.743f, (float)px_h * 0.82f);
    if (R < 8.0f) R = 8.0f;

    float norm_zero = db_to_norm(0.0f);

    // Scale arc
    int prev_ax = -1, prev_ay = -1;
    for (int i = 0; i <= 28; i++) {
        float frac = (float)i / 28.0f;
        float a_deg = -48.0f + frac * 96.0f;
        float a_rad = a_deg * (float)(M_PI / 180.0);
        int ax = pivot_x + (int)roundf(R * sinf(a_rad));
        int ay = pivot_y - (int)roundf(R * cosf(a_rad));
        uint8_t col = (frac >= norm_zero) ? 10 : 1; // 10 = Red, 1 = Cyan

        if (prev_ax != -1) {
            draw_braille_line_colored(grid, col_grid, draw_w, draw_h, prev_ax, prev_ay, ax, ay, col);
        }
        prev_ax = ax;
        prev_ay = ay;
    }

    // Ticks
    static const float ticks[] = { -20.0f, -10.0f, -5.0f, -3.0f, 0.0f, 1.0f, 3.0f };
    for (size_t k = 0; k < sizeof(ticks) / sizeof(ticks[0]); k++) {
        float norm = db_to_norm(ticks[k]);
        float a_rad = (-48.0f + norm * 96.0f) * (float)(M_PI / 180.0);
        float sa = sinf(a_rad), ca = cosf(a_rad);
        int t1_x = pivot_x + (int)roundf((R - 3.0f) * sa);
        int t1_y = pivot_y - (int)roundf((R - 3.0f) * ca);
        int t2_x = pivot_x + (int)roundf((R + 2.0f) * sa);
        int t2_y = pivot_y - (int)roundf((R + 2.0f) * ca);
        uint8_t col = (ticks[k] >= 0.0f) ? 10 : 2;
        draw_braille_line_colored(grid, col_grid, draw_w, draw_h, t1_x, t1_y, t2_x, t2_y, col);
    }

    // Needle
    float n_rad = needle->angle * (float)(M_PI / 180.0);
    int tip_x = pivot_x + (int)roundf((R + 1.0f) * sinf(n_rad));
    int tip_y = pivot_y - (int)roundf((R + 1.0f) * cosf(n_rad));
    uint8_t needle_col = (needle->angle >= (-48.0f + norm_zero * 96.0f)) ? 10 : 2;
    draw_braille_line_colored(grid, col_grid, draw_w, draw_h, pivot_x, pivot_y, tip_x, tip_y, needle_col);

    // Draw pivot
    set_braille_pixel_colored(grid, col_grid, draw_w, draw_h, pivot_x, pivot_y, 4);
    set_braille_pixel_colored(grid, col_grid, draw_w, draw_h, pivot_x + 1, pivot_y, 4);
}

void draw_vis_galvanometer(int y, int x, int draw_w, int draw_h) {
    if (draw_w < 6 || draw_h < 4) return;

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

    uint32_t rpos = atomic_load(&p_frames_consumed);
    uint32_t srate = atomic_load(&vis_srate);
    if (srate == 0) srate = 44100;

    uint32_t window = (srate * 35u) / 1000u;
    if (window > 2048) window = 2048;
    if (window < 64)   window = 64;

    float sum_l = 0.0f, sum_r = 0.0f;
    float peak_l = 0.0f, peak_r = 0.0f;

    for (uint32_t i = 0; i < window; i++) {
        uint32_t idx = (rpos - i) & VIS_BUF_MASK;
        float l = vis_ring_l[idx];
        float r = vis_ring_r[idx];
        sum_l += l * l;
        sum_r += r * r;
        float al = fabsf(l), ar = fabsf(r);
        if (al > peak_l) peak_l = al;
        if (ar > peak_r) peak_r = ar;
    }

    PlayState st = (PlayState)atomic_load(&play_state_atomic);
    bool is_stopped = (st == STATE_STOPPED);

    float rms_l = (!is_stopped && window > 0) ? sqrtf(sum_l / (float)window) : 0.0f;
    float rms_r = (!is_stopped && window > 0) ? sqrtf(sum_r / (float)window) : 0.0f;

    float db_l = (rms_l > 1.0e-5f) ? (20.0f * log10f(rms_l) + 4.5f) : -60.0f;
    float db_r = (rms_r > 1.0e-5f) ? (20.0f * log10f(rms_r) + 4.5f) : -60.0f;

    update_needle(&s_needle_l, is_stopped ? 0.0f : db_to_norm(db_l), is_stopped ? 0.0f : peak_l, dt);
    update_needle(&s_needle_r, is_stopped ? 0.0f : db_to_norm(db_r), is_stopped ? 0.0f : peak_r, dt);

    uint8_t *grid = vis_renderer_begin(draw_w, draw_h);
    uint8_t *col_grid = vis_renderer_get_color_grid();

    int px_w = draw_w * 2;
    int px_h = draw_h * 4;

    bool dual = (draw_w >= 30);
    int m1_x0, m1_w, m2_x0, m2_w;

    if (dual) {
        int gap = 4;
        m1_w = (px_w - gap) / 2;
        m2_w = m1_w;
        m1_x0 = 0;
        m2_x0 = m1_w + gap;

        draw_single_meter(grid, col_grid, draw_w, draw_h, m1_x0, 0, m1_w, px_h, &s_needle_l);
        draw_single_meter(grid, col_grid, draw_w, draw_h, m2_x0, 0, m2_w, px_h, &s_needle_r);
    } else {
        m1_x0 = 0;
        m1_w = px_w;
        m2_x0 = 0;
        m2_w = 0;
        draw_single_meter(grid, col_grid, draw_w, draw_h, 0, 0, px_w, px_h, &s_needle_l);
    }

    vis_renderer_end(y, x, draw_w, draw_h, VIS_COLOR_CUSTOM);

    // Overlay
    if (dual) {
        int c1_x = x + 1;
        int c2_x = x + (draw_w / 2) + 1;
        int p1_x = c1_x + (draw_w / 2) - 3;
        int p2_x = x + draw_w - 2;

        attron(COLOR_PAIR(1) | A_BOLD);
        mvprintw(y, c1_x, "CH-1 LEFT");
        mvprintw(y, c2_x, "CH-2 RIGHT");
        attroff(COLOR_PAIR(1) | A_BOLD);

        // Peak indicator
        if (s_needle_l.peak_alpha > 0.25f) {
            attron(COLOR_PAIR(10) | A_BOLD);
            mvprintw(y, p1_x, "■");
            attroff(COLOR_PAIR(10) | A_BOLD);
        } else {
            attron(COLOR_PAIR(2) | A_DIM);
            mvprintw(y, p1_x, "□");
            attroff(COLOR_PAIR(2) | A_DIM);
        }

        if (s_needle_r.peak_alpha > 0.25f) {
            attron(COLOR_PAIR(10) | A_BOLD);
            mvprintw(y, p2_x, "■");
            attroff(COLOR_PAIR(10) | A_BOLD);
        } else {
            attron(COLOR_PAIR(2) | A_DIM);
            mvprintw(y, p2_x, "□");
            attroff(COLOR_PAIR(2) | A_DIM);
        }

        attron(COLOR_PAIR(2));
        if (draw_h > 2) {
            mvprintw(y + draw_h - 1, c1_x, "%+5.1f dB", db_l);
            mvprintw(y + draw_h - 1, c2_x, "%+5.1f dB", db_r);
        }
        attroff(COLOR_PAIR(2));
    } else {
        attron(COLOR_PAIR(1) | A_BOLD);
        mvprintw(y, x + 1, "CH-1 · %+5.1f dB", db_l);
        attroff(COLOR_PAIR(1) | A_BOLD);

        int p_x = x + draw_w - 2;
        if (s_needle_l.peak_alpha > 0.25f) {
            attron(COLOR_PAIR(10) | A_BOLD);
            mvprintw(y, p_x, "■");
            attroff(COLOR_PAIR(10) | A_BOLD);
        } else {
            attron(COLOR_PAIR(2) | A_DIM);
            mvprintw(y, p_x, "□");
            attroff(COLOR_PAIR(2) | A_DIM);
        }
    }
}