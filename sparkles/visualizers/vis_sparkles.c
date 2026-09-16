#include "sparkles_vis.h"
#include "sparkles_theme.h"
#include <math.h>
#include <stdlib.h>

#define MAX_SPARKLES 96
#define MAX_RINGS 16
#define AMBIENT_SEEDS 48

typedef enum {
    SPARKLE_FLARE = 0,
    SPARKLE_DUST
} SparkleType;

typedef struct {
    Vector2 pos;
    Vector2 vel;
    float size;
    float max_size;
    float life;
    float decay;
    float rot;
    float rot_speed;
    SparkleType type;
    Color color;
} SparkleItem;

typedef struct {
    Vector2 center;
    float radius;
    float max_radius;
    float speed;
    float alpha;
    bool active;
} SparkleRing;

typedef struct {
    Vector2 norm_pos; // Normalized [0..1]
    float phase;
    float speed;
    float size;
} AmbientSeed;

static SparkleItem s_sparkles[MAX_SPARKLES];
static SparkleRing s_rings[MAX_RINGS];
static AmbientSeed s_seeds[AMBIENT_SEEDS];
static bool s_inited = false;
static float s_prev_bass = 0.0f;
static float s_prev_high = 0.0f;

static void spawn_flare(Vector2 pos, float size, Color col) {
    for (int i = 0; i < MAX_SPARKLES; i++) {
        if (s_sparkles[i].life <= 0.0f) {
            s_sparkles[i].pos = pos;
            s_sparkles[i].vel = (Vector2){
                ((float)rand() / (float)RAND_MAX - 0.5f) * 40.0f,
                ((float)rand() / (float)RAND_MAX - 0.5f) * 40.0f
            };
            s_sparkles[i].size = 0.0f;
            s_sparkles[i].max_size = size;
            s_sparkles[i].life = 1.0f;
            s_sparkles[i].decay = 1.2f + ((float)rand() / (float)RAND_MAX) * 1.0f;
            s_sparkles[i].rot = ((float)rand() / (float)RAND_MAX) * 360.0f;
            s_sparkles[i].rot_speed = ((float)rand() / (float)RAND_MAX - 0.5f) * 60.0f;
            s_sparkles[i].type = SPARKLE_FLARE;
            s_sparkles[i].color = col;
            break;
        }
    }
}

static void spawn_ring(Vector2 center, float max_r) {
    for (int i = 0; i < MAX_RINGS; i++) {
        if (!s_rings[i].active) {
            s_rings[i].center = center;
            s_rings[i].radius = 4.0f;
            s_rings[i].max_radius = max_r;
            s_rings[i].speed = max_r * 2.8f;
            s_rings[i].alpha = 1.0f;
            s_rings[i].active = true;
            break;
        }
    }
}

static void spawn_dust(Vector2 pos, int count, Color col) {
    int spawned = 0;
    for (int i = 0; i < MAX_SPARKLES && spawned < count; i++) {
        if (s_sparkles[i].life <= 0.0f) {
            float ang = ((float)rand() / (float)RAND_MAX) * 2.0f * (float)PI;
            float spd = 30.0f + ((float)rand() / (float)RAND_MAX) * 90.0f;
            s_sparkles[i].pos = pos;
            s_sparkles[i].vel = (Vector2){ cosf(ang) * spd, sinf(ang) * spd };
            s_sparkles[i].size = 1.5f + ((float)rand() / (float)RAND_MAX) * 2.0f;
            s_sparkles[i].max_size = s_sparkles[i].size;
            s_sparkles[i].life = 1.0f;
            s_sparkles[i].decay = 2.0f + ((float)rand() / (float)RAND_MAX) * 1.8f;
            s_sparkles[i].rot = 0.0f;
            s_sparkles[i].rot_speed = 0.0f;
            s_sparkles[i].type = SPARKLE_DUST;
            s_sparkles[i].color = col;
            spawned++;
        }
    }
}

// 4-pointed anamorphic diffraction flare
static void draw_diffraction_flare(Vector2 pos, float size, float rot_deg, Color col, float alpha) {
    if (size <= 1.0f || alpha <= 0.01f) return;

    float rad = rot_deg * ((float)PI / 180.0f);
    float cos_a = cosf(rad);
    float sin_a = sinf(rad);

    Color ray_col = ColorAlpha(col, alpha * 0.85f);
    Color core_glow = ColorAlpha(COLOR_ACCENT, alpha * 0.25f);
    Color white_hot = ColorAlpha(WHITE, alpha);

    // Primary horizontal needle rays
    Vector2 h1 = { pos.x - cos_a * size, pos.y - sin_a * size };
    Vector2 h2 = { pos.x + cos_a * size, pos.y + sin_a * size };
    DrawLineEx(h1, h2, 1.6f, ray_col);

    // Primary vertical needle rays
    Vector2 v1 = { pos.x + sin_a * (size * 0.85f), pos.y - cos_a * (size * 0.85f) };
    Vector2 v2 = { pos.x - sin_a * (size * 0.85f), pos.y + cos_a * (size * 0.85f) };
    DrawLineEx(v1, v2, 1.4f, ray_col);

    // Diagonal sub-needles (45 deg)
    float sub_sz = size * 0.35f;
    float rad_sub = rad + (float)PI * 0.25f;
    float cos_s = cosf(rad_sub);
    float sin_s = sinf(rad_sub);
    DrawLineEx(
        (Vector2){ pos.x - cos_s * sub_sz, pos.y - sin_s * sub_sz },
        (Vector2){ pos.x + cos_s * sub_sz, pos.y + sin_s * sub_sz },
        1.0f, ColorAlpha(ray_col, alpha * 0.45f)
    );
    DrawLineEx(
        (Vector2){ pos.x + sin_s * sub_sz, pos.y - cos_s * sub_sz },
        (Vector2){ pos.x - sin_s * sub_sz, pos.y + cos_s * sub_sz },
        1.0f, ColorAlpha(ray_col, alpha * 0.45f)
    );

    // Central diamond and white core
    DrawCircleV(pos, fmaxf(2.0f, size * 0.12f), core_glow);
    DrawCircleV(pos, 2.0f, white_hot);
}

void vis_sparkles_render(Rectangle b, float dt) {
    if (!s_inited) {
        for (int i = 0; i < MAX_SPARKLES; i++) s_sparkles[i].life = 0.0f;
        for (int i = 0; i < MAX_RINGS; i++) s_rings[i].active = false;
        for (int i = 0; i < AMBIENT_SEEDS; i++) {
            s_seeds[i].norm_pos = (Vector2){
                ((float)rand() / (float)RAND_MAX),
                ((float)rand() / (float)RAND_MAX)
            };
            s_seeds[i].phase = ((float)rand() / (float)RAND_MAX) * 2.0f * (float)PI;
            s_seeds[i].speed = 1.0f + ((float)rand() / (float)RAND_MAX) * 2.5f;
            s_seeds[i].size = 1.0f + ((float)rand() / (float)RAND_MAX) * 1.5f;
        }
        s_inited = true;
    }

    float bass = 0.0f, mid = 0.0f, high = 0.0f;
    sparkles_vis_get_bands(&bass, &mid, &high);

    float spec[32];
    sparkles_vis_get_spectrum(spec, 32);

    float bass_delta = bass - s_prev_bass;
    float high_delta = high - s_prev_high;
    s_prev_bass = bass;
    s_prev_high = high;

    // Transient detection, spawn starbursts across the full screen
    // Bass kick impact, large flares, expanding shockwave rings
    if (bass_delta > 0.18f && bass > 0.45f) {
        int bursts = 1 + (int)(bass * 3.0f);
        for (int k = 0; k < bursts; k++) {
            // Low frequencies favor middle/horizontal center with random dispersal
            Vector2 spawn_pos = {
                b.x + 40.0f + ((float)rand() / (float)RAND_MAX) * (b.width - 80.0f),
                b.y + 30.0f + ((float)rand() / (float)RAND_MAX) * (b.height - 60.0f)
            };
            float flare_size = 35.0f + bass * 45.0f;
            spawn_flare(spawn_pos, flare_size, COLOR_ACCENT);
            spawn_ring(spawn_pos, flare_size * 1.4f);
            spawn_dust(spawn_pos, 8, COLOR_TEXT_PRIMARY);
        }
    }

    // High transient snap, fast flares
    if (high_delta > 0.14f && high > 0.35f) {
        int count = 2 + (int)(high * 4.0f);
        for (int k = 0; k < count; k++) {
            // Map higher frequencies across right/upper portions of screen
            float bin_x = ((float)rand() / (float)RAND_MAX);
            Vector2 spawn_pos = {
                b.x + 30.0f + bin_x * (b.width - 60.0f),
                b.y + 20.0f + ((float)rand() / (float)RAND_MAX) * (b.height - 40.0f)
            };
            spawn_flare(spawn_pos, 18.0f + high * 25.0f, WHITE);
            spawn_dust(spawn_pos, 4, ColorAlpha(COLOR_ACCENT, 0.9f));
        }
    }

    // Continuous subtle harmony generation when energetic
    if (mid > 0.65f && ((float)rand() / (float)RAND_MAX) < 0.35f) {
        Vector2 spawn_pos = {
            b.x + 40.0f + ((float)rand() / (float)RAND_MAX) * (b.width - 80.0f),
            b.y + 40.0f + ((float)rand() / (float)RAND_MAX) * (b.height - 80.0f)
        };
        spawn_flare(spawn_pos, 14.0f + mid * 16.0f, COLOR_ACCENT_DIM);
    }

    // Render ambient drifting star seeds
    for (int i = 0; i < AMBIENT_SEEDS; i++) {
        AmbientSeed *seed = &s_seeds[i];
        seed->phase += dt * seed->speed * (1.0f + mid * 2.0f);

        Vector2 p = {
            b.x + seed->norm_pos.x * b.width,
            b.y + seed->norm_pos.y * b.height
        };

        float twinkle = (sinf(seed->phase) + 1.0f) * 0.5f;
        float alpha = (0.15f + twinkle * 0.55f) * (0.4f + bass * 0.6f);
        float sz = seed->size + (twinkle * 1.2f * high);

        DrawCircleV(p, sz, ColorAlpha(COLOR_TEXT_PRIMARY, alpha));
        if (twinkle > 0.80f) {
            DrawLineEx((Vector2){ p.x - 3, p.y }, (Vector2){ p.x + 3, p.y }, 1.0f, ColorAlpha(COLOR_ACCENT, alpha * 0.6f));
            DrawLineEx((Vector2){ p.x, p.y - 3 }, (Vector2){ p.x, p.y + 3 }, 1.0f, ColorAlpha(COLOR_ACCENT, alpha * 0.6f));
        }
    }

    // Update and render expanding shockwave rings
    for (int i = 0; i < MAX_RINGS; i++) {
        if (!s_rings[i].active) continue;
        SparkleRing *r = &s_rings[i];
        r->radius += r->speed * dt;
        float prog = r->radius / r->max_radius;
        r->alpha = 1.0f - prog;

        if (prog >= 1.0f || r->alpha <= 0.01f) {
            r->active = false;
            continue;
        }

        DrawCircleLines((int)r->center.x, (int)r->center.y, r->radius, ColorAlpha(COLOR_ACCENT, r->alpha * 0.45f));
        DrawCircleLines((int)r->center.x, (int)r->center.y, r->radius + 1.2f, ColorAlpha(WHITE, r->alpha * 0.20f));
    }

    // Update and render sparkles and particles
    for (int i = 0; i < MAX_SPARKLES; i++) {
        SparkleItem *sp = &s_sparkles[i];
        if (sp->life <= 0.0f) continue;

        sp->life -= sp->decay * dt;
        if (sp->life <= 0.0f) {
            sp->life = 0.0f;
            continue;
        }

        // Deceleration physics
        sp->pos.x += sp->vel.x * dt;
        sp->pos.y += sp->vel.y * dt;
        sp->vel.x *= 0.92f;
        sp->vel.y *= 0.92f;

        sp->rot += sp->rot_speed * dt;

        // Size snaps open then softly shrinks
        float ease_in = 1.0f - sp->life;
        float cur_size = (ease_in < 0.2f) ? (sp->max_size * (ease_in / 0.2f)) : (sp->max_size * sp->life);

        if (sp->type == SPARKLE_FLARE) {
            draw_diffraction_flare(sp->pos, cur_size, sp->rot, sp->color, sp->life);
        } else {
            // Diamond dust
            float sz = cur_size;
            Color dust_col = ColorAlpha(sp->color, sp->life * 0.85f);
            DrawLineEx((Vector2){ sp->pos.x - sz, sp->pos.y }, (Vector2){ sp->pos.x, sp->pos.y - sz }, 1.0f, dust_col);
            DrawLineEx((Vector2){ sp->pos.x, sp->pos.y - sz }, (Vector2){ sp->pos.x + sz, sp->pos.y }, 1.0f, dust_col);
            DrawLineEx((Vector2){ sp->pos.x + sz, sp->pos.y }, (Vector2){ sp->pos.x, sp->pos.y + sz }, 1.0f, dust_col);
            DrawLineEx((Vector2){ sp->pos.x, sp->pos.y + sz }, (Vector2){ sp->pos.x - sz, sp->pos.y }, 1.0f, dust_col);
            DrawCircleV(sp->pos, sz * 0.5f, dust_col);
        }
    }
}