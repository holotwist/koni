#include "sparkles_vis.h"
#include "sparkles_theme.h"
#include <math.h>
#include <stdlib.h>

#define PARTICLE_COUNT 180

typedef struct {
    float angle;
    float norm_dist;
    float speed;
    float size;
    float alpha;
} OrbitParticle;

static OrbitParticle s_particles[PARTICLE_COUNT];
static bool s_inited = false;

void vis_particles_render(Rectangle b, float dt) {
    Vector2 center = { b.x + b.width * 0.5f, b.y + b.height * 0.5f };
    float max_radius = fminf(b.width, b.height) * 0.44f;
    if (max_radius < 35.0f) max_radius = 35.0f;

    float bass = 0.0f, mid = 0.0f, high = 0.0f;
    sparkles_vis_get_raw_bands(&bass, &mid, &high);

    // Initialized with normalized distance
    if (!s_inited) {
        for (int i = 0; i < PARTICLE_COUNT; i++) {
            s_particles[i].angle = ((float)rand() / (float)RAND_MAX) * 2.0f * PI;
            s_particles[i].norm_dist = 0.15f + ((float)rand() / (float)RAND_MAX) * 0.85f;
            s_particles[i].speed = 0.6f + ((float)rand() / (float)RAND_MAX) * 2.2f;
            s_particles[i].size = 1.4f + ((float)rand() / (float)RAND_MAX) * 2.4f;
            s_particles[i].alpha = 0.35f + ((float)rand() / (float)RAND_MAX) * 0.65f;
        }
        s_inited = true;
    }

    float core_glow = 16.0f + bass * 18.0f;
    float core_inner = 8.0f + bass * 9.0f;
    DrawCircleV(center, core_glow, ColorAlpha(COLOR_ACCENT, 0.25f + bass * 0.30f));
    DrawCircleV(center, core_inner, COLOR_ACCENT);

    for (int i = 0; i < PARTICLE_COUNT; i++) {
        OrbitParticle *p = &s_particles[i];

        // Orbit velocity accelerates dynamically with music energy
        p->angle += p->speed * dt * (1.2f + mid * 3.8f + bass * 2.4f);

        // Rhythmic bass expansion, pulses outward
        float reactive_dist = (p->norm_dist * max_radius) + sinf(p->angle * 2.0f) * (bass * 30.0f);
        if (reactive_dist < 12.0f) reactive_dist = 12.0f;

        Vector2 pos = {
            center.x + cosf(p->angle) * reactive_dist,
            center.y + sinf(p->angle) * reactive_dist
        };

        // Size snaps with highs and snare hits
        float p_size = p->size + high * 2.2f + bass * 1.4f;
        DrawCircleV(pos, p_size, ColorAlpha(COLOR_TEXT_PRIMARY, p->alpha));
    }
}