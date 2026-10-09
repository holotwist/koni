#define _DEFAULT_SOURCE
#include "visualizers.h"
#include "vis_renderer.h"
#include "vis_math.h"
#include "dancer_physics.h"
#include <math.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef struct { float x, y; } Vec2;

// 2D affine transformation matrix [a b c; d e f; 0 0 1]
typedef struct { float a, b, c, d, e, f; } Mat2D;

static inline Mat2D mat_ident(void) {
    return (Mat2D){ 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f };
}

static inline Mat2D mat_mul(Mat2D A, Mat2D B) {
    return (Mat2D){
        A.a * B.a + A.b * B.d,
        A.a * B.b + A.b * B.e,
        A.a * B.c + A.b * B.f + A.c,
        A.d * B.a + A.e * B.d,
        A.d * B.b + A.e * B.e,
        A.d * B.c + A.e * B.f + A.f
    };
}

static inline Mat2D mat_translate(float tx, float ty) {
    return (Mat2D){ 1.0f, 0.0f, tx, 0.0f, 1.0f, ty };
}

static inline Mat2D mat_rotate(float deg) {
    float rad = deg * (float)(M_PI / 180.0);
    float c = cosf(rad), s = sinf(rad);
    return (Mat2D){ c, -s, 0.0f, s, c, 0.0f };
}

static inline Mat2D mat_scale(float sx, float sy) {
    return (Mat2D){ sx, 0.0f, 0.0f, 0.0f, sy, 0.0f };
}

static inline Vec2 mat_apply(Mat2D m, Vec2 p) {
    return (Vec2){ m.a * p.x + m.b * p.y + m.c, m.d * p.x + m.e * p.y + m.f };
}

static void draw_poly_mat(uint8_t *grid, uint8_t *col_grid, int dw, int dh,
                          Mat2D m, const Vec2 *pts, int count, bool closed, uint8_t col) {
    for (int i = 0; i < (closed ? count : count - 1); i++) {
        Vec2 p1 = mat_apply(m, pts[i]);
        Vec2 p2 = mat_apply(m, pts[(i + 1) % count]);
        draw_braille_line_colored(grid, col_grid, dw, dh, (int)p1.x, (int)p1.y, (int)p2.x, (int)p2.y, col);
    }
}

static void draw_ellipse_mat(uint8_t *grid, uint8_t *col_grid, int dw, int dh,
                             Mat2D m, Vec2 center, float rx, float ry, uint8_t col) {
    const int segs = 20;
    int prev_x = 0, prev_y = 0;
    for (int i = 0; i <= segs; i++) {
        float a = ((float)i / (float)segs) * 2.0f * (float)M_PI;
        Vec2 local = { center.x + cosf(a) * rx, center.y + sinf(a) * ry };
        Vec2 p = mat_apply(m, local);
        int px = (int)p.x, py = (int)p.y;
        if (i > 0) {
            draw_braille_line_colored(grid, col_grid, dw, dh, prev_x, prev_y, px, py, col);
        }
        prev_x = px; prev_y = py;
    }
}

static void draw_dot_mat(uint8_t *grid, uint8_t *col_grid, int dw, int dh,
                         Mat2D m, Vec2 pt, uint8_t col) {
    Vec2 p = mat_apply(m, pt);
    set_braille_pixel_colored(grid, col_grid, dw, dh, (int)p.x, (int)p.y, col);
    set_braille_pixel_colored(grid, col_grid, dw, dh, (int)p.x + 1, (int)p.y, col);
    set_braille_pixel_colored(grid, col_grid, dw, dh, (int)p.x, (int)p.y + 1, col);
    set_braille_pixel_colored(grid, col_grid, dw, dh, (int)p.x + 1, (int)p.y + 1, col);
}

// Model geometry definition
static const Vec2 s_l_leg[]   = {{-21.84f, 64.14f}, {-12.54f, 64.44f}, {-2.52f, 111.26f}, {-26.80f, 110.60f}};
static const Vec2 s_l_cuff[]  = {{-25.89f, 105.29f}, {-4.14f, 105.53f}, {-3.15f, 110.60f}, {-26.10f, 110.06f}};
static const Vec2 s_r_leg[]   = {{-3.59f, 64.32f}, {5.59f, 63.66f}, {32.47f, 109.57f}, {9.46f, 111.20f}};
static const Vec2 s_r_cuff[]  = {{8.05f, 105.42f}, {28.59f, 104.02f}, {31.32f, 108.84f}, {9.49f, 110.76f}};
static const Vec2 s_torso[]   = {{-10.66f, 0.45f}, {4.67f, 2.37f}, {4.63f, 22.40f}, {15.39f, 53.58f}, {-37.28f, 45.94f}, {-16.69f, 19.84f}};
static const Vec2 s_l_arm_s[] = {{-28.03f, 11.16f}, {-19.89f, 14.79f}, {-32.95f, 61.14f}, {-54.08f, 51.01f}};
static const Vec2 s_l_arm_h[] = {{-20.09f, 12.32f}, {-26.57f, 6.13f}, {-46.25f, 21.79f}, {-35.16f, 51.94f}, {-15.42f, 40.61f}, {-28.61f, 23.39f}};
static const Vec2 s_r_arm_s[] = {{18.77f, 11.48f}, {10.63f, 15.12f}, {23.70f, 61.48f}, {44.84f, 51.34f}};
static const Vec2 s_r_arm_h[] = {{10.82f, 11.64f}, {17.29f, 5.46f}, {36.96f, 21.11f}, {25.88f, 51.26f}, {6.15f, 39.93f}, {19.34f, 22.72f}};
static const Vec2 s_tuft_l[]  = {{-35.35f, -36.33f}, {-31.33f, 2.03f}, {-25.70f, -16.25f}};
static const Vec2 s_tuft_r[]  = {{23.09f, -15.06f}, {27.44f, 3.56f}, {34.19f, -29.67f}};
static const Vec2 s_ahoge[]   = {{6.58f, -4.22f}, {17.99f, -20.40f}, {19.67f, -12.55f}};

void draw_vis_dancer(int y, int x, int draw_w, int draw_h) {
    uint8_t *grid = vis_renderer_begin(draw_w, draw_h);
    uint8_t *col_grid = vis_renderer_get_color_grid();

    static bool s_inited = false;
    if (!s_inited) {
        dancer_physics_init();
        s_inited = true;
    }

    int px_w = draw_w * 2;
    int px_h = draw_h * 4;

    DancerState st;
    dancer_physics_update(&st, (float)px_w, (float)px_h);

    const uint8_t COL_PURPLE = 5;
    const uint8_t COL_CYAN   = 1;
    const uint8_t COL_WHITE  = 2;
    const uint8_t COL_GOLD   = 4;

    float spin_sx = (fabsf(st.spin_scale_x) < 0.05f) ? 0.05f : st.spin_scale_x;
    Mat2D m_spin = mat_mul(mat_translate(st.char_base_x, 0.0f),
                   mat_mul(mat_scale(spin_sx, 1.0f),
                           mat_translate(-st.char_base_x, 0.0f)));

    float l_floor_y = st.floor_y + st.smooth_l_lift;
    float r_floor_y = st.floor_y + st.smooth_r_lift;

    // Inverted pendulum transforms
    Mat2D m_leg_l_base = mat_mul(mat_translate(st.char_base_x - 17.3f * st.scale, l_floor_y),
                         mat_mul(mat_rotate(st.smooth_l_angle),
                         mat_mul(mat_scale(st.scale, st.scale * st.smooth_l_scale_y),
                                 mat_translate(17.3f, -111.5f))));

    Mat2D m_leg_r_base = mat_mul(mat_translate(st.char_base_x - 1.51f * st.scale, r_floor_y),
                         mat_mul(mat_rotate(st.smooth_r_angle),
                         mat_mul(mat_scale(st.scale, st.scale * st.smooth_r_scale_y),
                                 mat_translate(1.51f, -111.5f))));

    Mat2D m_leg_l = mat_mul(m_spin, m_leg_l_base);
    Mat2D m_leg_r = mat_mul(m_spin, m_leg_r_base);

    // Track leg top coordinates to torso pos
    Vec2 top_l = mat_apply(m_leg_l_base, (Vec2){-17.30f, 57.40f});
    Vec2 top_r = mat_apply(m_leg_r_base, (Vec2){-1.51f, 57.10f});
    Vec2 top_mid = { (top_l.x + top_r.x) * 0.5f, (top_l.y + top_r.y) * 0.5f };

    // Align torso hip
    Mat2D m_torso_shape = mat_mul(mat_scale(st.scale * st.squash_x, st.scale * st.squash_y),
                                  mat_rotate(st.hip_tilt));
    Vec2 hip_offset = mat_apply(m_torso_shape, (Vec2){-9.405f, 57.25f});

    float root_x = top_mid.x - hip_offset.x;
    float root_y = top_mid.y - hip_offset.y;
    Mat2D m_root = mat_mul(m_spin, mat_mul(mat_translate(root_x, root_y), m_torso_shape));

    draw_poly_mat(grid, col_grid, draw_w, draw_h, m_leg_l, s_l_leg, 4, true, COL_PURPLE);
    draw_poly_mat(grid, col_grid, draw_w, draw_h, m_leg_l, s_l_cuff, 4, true, COL_CYAN);
    draw_dot_mat(grid, col_grid, draw_w, draw_h, m_leg_l, (Vec2){-17.30f, 57.40f}, COL_CYAN);

    draw_poly_mat(grid, col_grid, draw_w, draw_h, m_leg_r, s_r_leg, 4, true, COL_PURPLE);
    draw_poly_mat(grid, col_grid, draw_w, draw_h, m_leg_r, s_r_cuff, 4, true, COL_CYAN);
    draw_dot_mat(grid, col_grid, draw_w, draw_h, m_leg_r, (Vec2){-1.51f, 57.10f}, COL_CYAN);

    // Left arm
    Mat2D m_arm_l = mat_mul(m_root,
                    mat_mul(mat_translate(-19.62f, 5.26f),
                    mat_mul(mat_rotate(st.l_arm_angle),
                            mat_translate(19.62f, -5.26f))));

    if (!st.l_arm_on_hip) {
        draw_poly_mat(grid, col_grid, draw_w, draw_h, m_arm_l, s_l_arm_s, 4, true, COL_PURPLE);
        draw_dot_mat(grid, col_grid, draw_w, draw_h, m_root, (Vec2){-19.62f, 5.26f}, COL_CYAN);
    }

    // Torso / dress
    draw_poly_mat(grid, col_grid, draw_w, draw_h, m_root, s_torso, 6, true, COL_PURPLE);
    Vec2 belt1 = mat_apply(m_root, (Vec2){-33.93f, 42.69f});
    Vec2 belt2 = mat_apply(m_root, (Vec2){13.92f, 49.48f});
    draw_braille_line_colored(grid, col_grid, draw_w, draw_h, (int)belt1.x, (int)belt1.y, (int)belt2.x, (int)belt2.y, COL_PURPLE);
    draw_dot_mat(grid, col_grid, draw_w, draw_h, m_root, (Vec2){-4.09f, 10.45f}, COL_CYAN);

    if (st.l_arm_on_hip) {
        draw_poly_mat(grid, col_grid, draw_w, draw_h, m_arm_l, s_l_arm_h, 6, true, COL_PURPLE);
        draw_dot_mat(grid, col_grid, draw_w, draw_h, m_root, (Vec2){-19.62f, 5.26f}, COL_CYAN);
    }

    // Right arm
    Mat2D m_arm_r = mat_mul(m_root,
                    mat_mul(mat_translate(10.37f, 4.58f),
                    mat_mul(mat_rotate(st.r_arm_angle),
                            mat_translate(-10.37f, -4.58f))));

    if (st.r_arm_on_hip) {
        draw_poly_mat(grid, col_grid, draw_w, draw_h, m_arm_r, s_r_arm_h, 6, true, COL_PURPLE);
    } else {
        draw_poly_mat(grid, col_grid, draw_w, draw_h, m_arm_r, s_r_arm_s, 4, true, COL_PURPLE);
    }
    draw_dot_mat(grid, col_grid, draw_w, draw_h, m_root, (Vec2){10.37f, 4.58f}, COL_CYAN);

    // Head
    Mat2D m_head = mat_mul(m_root,
                   mat_mul(mat_translate(0.0f, -5.0f),
                   mat_mul(mat_rotate(st.head_tilt),
                           mat_scale(st.h_squash_x, st.h_squash_y))));

    draw_ellipse_mat(grid, col_grid, draw_w, draw_h, m_head, (Vec2){0.03f, -38.50f}, 35.6f, 35.0f, COL_PURPLE);

    // Hair (circles)
    Mat2D m_hair_l = mat_mul(m_head,
                     mat_mul(mat_translate(-30.0f, -40.0f),
                             mat_rotate(st.hair_l)));
    draw_ellipse_mat(grid, col_grid, draw_w, draw_h, m_hair_l, (Vec2){-13.83f, -28.58f}, 16.35f, 16.50f, COL_PURPLE);

    Mat2D m_hair_r = mat_mul(m_head,
                     mat_mul(mat_translate(30.0f, -40.0f),
                             mat_rotate(st.hair_r)));
    draw_ellipse_mat(grid, col_grid, draw_w, draw_h, m_hair_r, (Vec2){19.18f, -21.38f}, 16.35f, 16.50f, COL_PURPLE);

    // Hair tufts
    draw_poly_mat(grid, col_grid, draw_w, draw_h, m_head, s_tuft_l, 3, true, COL_PURPLE);
    draw_poly_mat(grid, col_grid, draw_w, draw_h, m_head, s_tuft_r, 3, true, COL_PURPLE);

    // Ahoge
    Mat2D m_ahoge = mat_mul(m_head,
                    mat_mul(mat_translate(0.0f, -70.0f),
                            mat_rotate(st.ahoge)));
    draw_poly_mat(grid, col_grid, draw_w, draw_h, m_ahoge, s_ahoge, 3, true, COL_PURPLE);

    // Face cutouts
    Mat2D m_face = mat_mul(m_head, mat_translate(0.0f, st.face_y));

    if (st.face_type == 1) { // Happy > <
        Vec2 el1 = mat_apply(m_face, (Vec2){-22.14f, -35.60f});
        Vec2 el2 = mat_apply(m_face, (Vec2){-8.79f,  -27.99f});
        Vec2 el3 = mat_apply(m_face, (Vec2){-22.87f, -23.16f});
        draw_braille_line_colored(grid, col_grid, draw_w, draw_h, (int)el1.x, (int)el1.y, (int)el2.x, (int)el2.y, COL_WHITE);
        draw_braille_line_colored(grid, col_grid, draw_w, draw_h, (int)el2.x, (int)el2.y, (int)el3.x, (int)el3.y, COL_WHITE);

        Vec2 er1 = mat_apply(m_face, (Vec2){23.66f, -34.33f});
        Vec2 er2 = mat_apply(m_face, (Vec2){9.22f,  -27.26f});
        Vec2 er3 = mat_apply(m_face, (Vec2){23.12f, -21.10f});
        draw_braille_line_colored(grid, col_grid, draw_w, draw_h, (int)er1.x, (int)er1.y, (int)er2.x, (int)er2.y, COL_WHITE);
        draw_braille_line_colored(grid, col_grid, draw_w, draw_h, (int)er2.x, (int)er2.y, (int)er3.x, (int)er3.y, COL_WHITE);
    } else if (st.face_type == 2) { // Relaxed \ /
        Vec2 el1 = mat_apply(m_face, (Vec2){-22.48f, -27.38f});
        Vec2 el2 = mat_apply(m_face, (Vec2){-7.08f,  -32.65f});
        draw_braille_line_colored(grid, col_grid, draw_w, draw_h, (int)el1.x, (int)el1.y, (int)el2.x, (int)el2.y, COL_WHITE);

        Vec2 er1 = mat_apply(m_face, (Vec2){7.30f,  -32.29f});
        Vec2 er2 = mat_apply(m_face, (Vec2){22.32f, -27.25f});
        draw_braille_line_colored(grid, col_grid, draw_w, draw_h, (int)er1.x, (int)er1.y, (int)er2.x, (int)er2.y, COL_WHITE);
    } else { // Neutral | |
        static const Vec2 eye_l[] = {{-16.46f, -42.13f}, {-10.18f, -41.95f}, {-10.66f, -22.56f}, {-16.88f, -22.56f}};
        static const Vec2 eye_r[] = {{10.24f, -41.16f}, {16.52f, -40.98f}, {16.04f, -21.59f}, {9.82f, -21.59f}};
        draw_poly_mat(grid, col_grid, draw_w, draw_h, m_face, eye_l, 4, true, COL_WHITE);
        draw_poly_mat(grid, col_grid, draw_w, draw_h, m_face, eye_r, 4, true, COL_WHITE);
    }

    // Mouth v
    Vec2 m1 = mat_apply(m_face, (Vec2){-4.88f, -19.47f});
    Vec2 m2 = mat_apply(m_face, (Vec2){-0.55f, -15.48f});
    Vec2 m3 = mat_apply(m_face, (Vec2){4.00f,  -19.16f});
    draw_braille_line_colored(grid, col_grid, draw_w, draw_h, (int)m1.x, (int)m1.y, (int)m2.x, (int)m2.y, COL_WHITE);
    draw_braille_line_colored(grid, col_grid, draw_w, draw_h, (int)m2.x, (int)m2.y, (int)m3.x, (int)m3.y, COL_WHITE);

    // Stage floor line
    int floor_y_i = (int)st.floor_y;
    if (floor_y_i >= 0 && floor_y_i < px_h) {
        draw_braille_line_colored(grid, col_grid, draw_w, draw_h, 4, floor_y_i, px_w - 5, floor_y_i, COL_CYAN);
    }

    // Floor sparks
    if (st.beat_hit) {
        int sp_y = floor_y_i - 3;
        for (int k = -24; k <= 24; k += 8) {
            set_braille_pixel_colored(grid, col_grid, draw_w, draw_h, (int)st.char_base_x + k, sp_y - (rand() % 10), COL_GOLD);
        }
    }

    vis_renderer_end(y, x, draw_w, draw_h, VIS_COLOR_CUSTOM);
}