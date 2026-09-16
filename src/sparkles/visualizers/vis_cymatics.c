#include "sparkles_vis.h"
#include "sparkles_theme.h"
#include <math.h>
#include <stdlib.h>

#define CYMATICS_PARTICLES 450

typedef struct {
    float x; // [-1.0, 1.0]
    float y; // [-1.0, 1.0]
    float vx;
    float vy;
    float size;
} CymaticsParticle;

static CymaticsParticle s_particles[CYMATICS_PARTICLES];
static bool s_inited = false;
static float s_cur_n = 2.0f;
static float s_cur_m = 3.0f;
static float s_prev_bass = 0.0f;

// 2D Chladni plate vibration function, w(x,y) = a*cos(n*pi*x/2)*cos(m*pi*y/2) - b*cos(m*pi*x/2)*cos(n*pi*y/2)
static inline float chladni_amp(float x, float y, float n, float m) {
    float pi = (float)PI;
    return cosf(n * pi * x * 0.5f) * cosf(m * pi * y * 0.5f) -
           cosf(m * pi * x * 0.5f) * cosf(n * pi * y * 0.5f);
}

void vis_cymatics_render(Rectangle b, float dt) {
    float bass = 0.0f, mid = 0.0f, high = 0.0f;
    sparkles_vis_get_bands(&bass, &mid, &high);

    if (!s_inited) {
        for (int i = 0; i < CYMATICS_PARTICLES; i++) {
            s_particles[i].x = ((float)rand() / (float)RAND_MAX) * 1.8f - 0.9f;
            s_particles[i].y = ((float)rand() / (float)RAND_MAX) * 1.8f - 0.9f;
            s_particles[i].vx = 0.0f;
            s_particles[i].vy = 0.0f;
            s_particles[i].size = 1.3f + ((float)rand() / (float)RAND_MAX) * 1.4f;
        }
        s_inited = true;
    }

    // Stable harmonic Chladni resonance table
    static const struct { float n; float m; } s_modes[] = {
        { 1.0f, 2.0f }, // Mode 0, Sub-bass oval / diamond
        { 2.0f, 2.0f }, // Mode 1, Quadrant cross
        { 2.0f, 3.0f }, // Mode 2, 4-leaf clover rosette
        { 3.0f, 3.0f }, // Mode 3, Diagonal grid lattice
        { 2.0f, 5.0f }, // Mode 4, 8-pointed star
        { 3.0f, 4.0f }, // Mode 5, Concentric rosette
        { 4.0f, 5.0f }  // Mode 6, Intricate mandala
    };

    int target_mode = 2; // Default cloverleaf
    if (bass > 0.55f) {
        target_mode = (bass > 0.75f) ? 0 : 1;
    } else if (mid > 0.45f) {
        target_mode = (mid > 0.70f) ? 4 : 3;
    } else if (high > 0.40f) {
        target_mode = (high > 0.65f) ? 6 : 5;
    }

    // Transition between modal geometries
    s_cur_n += (s_modes[target_mode].n - s_cur_n) * fminf(1.0f, dt * 2.0f);
    s_cur_m += (s_modes[target_mode].m - s_cur_m) * fminf(1.0f, dt * 2.0f);

    Vector2 center = { b.x + b.width * 0.5f, b.y + b.height * 0.5f };
    float radius = fminf(b.width, b.height) * 0.46f;
    if (radius < 35.0f) radius = 35.0f;

    // Circular Acoustic Plate Enclosure
    DrawCircleV(center, radius, (Color){ 8, 9, 12, 255 });
    DrawCircleLines((int)center.x, (int)center.y, radius, (Color){ 30, 32, 40, 255 });
    DrawCircleLines((int)center.x, (int)center.y, radius * 0.65f, (Color){ 18, 20, 26, 255 });
    DrawCircleLines((int)center.x, (int)center.y, radius * 0.32f, (Color){ 18, 20, 26, 255 });

    // Corner alignment brackets
    Rectangle plate_box = { center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f };
    DrawNothingCornerBrackets(plate_box, 8.0f, (Color){ 45, 48, 58, 180 });

    // Scatter particles only on sharp kick transients (attack phase)
    float bass_delta = bass - s_prev_bass;
    s_prev_bass = bass;
    float shockwave = (bass_delta > 0.22f && bass > 0.50f) ? (bass_delta * 1.8f) : 0.0f;

    for (int i = 0; i < CYMATICS_PARTICLES; i++) {
        CymaticsParticle *p = &s_particles[i];

        // Numerical gradient, force pushes particles toward nodal curves where |w(x,y)| = 0
        const float eps = 0.015f;
        float w_center = fabsf(chladni_amp(p->x, p->y, s_cur_n, s_cur_m));
        float w_dx = (fabsf(chladni_amp(p->x + eps, p->y, s_cur_n, s_cur_m)) - w_center) / eps;
        float w_dy = (fabsf(chladni_amp(p->x, p->y + eps, s_cur_n, s_cur_m)) - w_center) / eps;

        float drive = 2.4f;
        p->vx -= w_dx * drive * dt;
        p->vy -= w_dy * drive * dt;

        // Kick transient jolt
        if (shockwave > 0.05f) {
            p->vx += (((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f) * shockwave * 0.12f;
            p->vy += (((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f) * shockwave * 0.12f;
        }

        // Damping, allows particles to actually come to rest on the nodal lines
        p->vx *= 0.84f;
        p->vy *= 0.84f;

        p->x += p->vx;
        p->y += p->vy;

        // Circular plate boundary bounce
        float dist_sq = p->x * p->x + p->y * p->y;
        if (dist_sq > 0.90f) {
            float d = sqrtf(dist_sq);
            p->x = (p->x / d) * 0.94f;
            p->y = (p->y / d) * 0.94f;
            p->vx *= -0.4f;
            p->vy *= -0.4f;
        }

        Vector2 screen_pos = {
            center.x + p->x * radius,
            center.y + p->y * radius
        };

        // Particles directly on the nodal lines glow brightly; particles in vibrating areas are dim
        float node_proximity = 1.0f - fminf(1.0f, w_center * 2.2f);
        if (node_proximity < 0.0f) node_proximity = 0.0f;

        if (node_proximity > 0.75f) {
            // Highly focused on line, Bright white / blush core
            DrawCircleV(screen_pos, p->size + 0.8f, WHITE);
            DrawCircleV(screen_pos, p->size + 2.0f, ColorAlpha(COLOR_ACCENT, 0.25f));
        } else if (node_proximity > 0.35f) {
            // Settling near line, Rose
            DrawCircleV(screen_pos, p->size, COLOR_ACCENT);
        } else {
            // Stray particle, Dim grey
            DrawCircleV(screen_pos, p->size * 0.8f, (Color){ 45, 48, 58, 140 });
        }
    }
}