#include "sparkles_vis.h"
#include "sparkles_theme.h"
#include "state.h"
#include <math.h>
#include <stdlib.h>

#define TUNNEL_RINGS 18
#define TUNNEL_SIDES 6 // Hexagonal Tunnel
#define TUNNEL_PARTICLES 45

typedef struct {
    float x;
    float y;
    float z;
    float speed;
} TunnelStar;

static TunnelStar s_stars[TUNNEL_PARTICLES];
static bool s_stars_inited = false;
static float s_flight_pos = 0.0f;
static float s_twist = 0.0f;

void vis_tunnel_render(Rectangle b, float dt) {
    float bass = 0.0f, mid = 0.0f, high = 0.0f;
    sparkles_vis_get_bands(&bass, &mid, &high);

    // Tunnel speed driven by groove and kick
    float speed = 0.45f + bass * 1.8f + mid * 0.8f;
    s_flight_pos += dt * speed;
    s_twist += dt * (0.25f + high * 0.70f);

    if (!s_stars_inited) {
        for (int i = 0; i < TUNNEL_PARTICLES; i++) {
            s_stars[i].x = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
            s_stars[i].y = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
            s_stars[i].z = ((float)rand() / (float)RAND_MAX);
            s_stars[i].speed = 0.8f + ((float)rand() / (float)RAND_MAX) * 1.5f;
        }
        s_stars_inited = true;
    }

    // Vanishing point tilts subtly to stereo balance
    uint32_t rpos = atomic_load(&p_frames_consumed);
    uint32_t idx = rpos & VIS_BUF_MASK;
    float pan_tilt = (vis_ring_r[idx] - vis_ring_l[idx]) * 25.0f;

    Vector2 center = { b.x + b.width * 0.5f + pan_tilt, b.y + b.height * 0.5f };
    float max_radius = fminf(b.width, b.height) * 0.48f;
    if (max_radius < 40.0f) max_radius = 40.0f;

    Vector2 ring_verts[TUNNEL_RINGS][TUNNEL_SIDES];
    float ring_depth[TUNNEL_RINGS];

    // Compute hexagonal ring geometry
    for (int r = 0; r < TUNNEL_RINGS; r++) {
        // z: 0.0 = Far (vanishing center), 1.0 = Foreground
        float norm_idx = (float)r / (float)TUNNEL_RINGS;
        float z = fmodf(norm_idx + s_flight_pos, 1.0f);
        ring_depth[r] = z;

        // Exponential perspective expansion
        float pz = z * z * z;
        float kick_dilate = 1.0f + bass * 0.35f * (1.0f - z);
        float radius = max_radius * pz * kick_dilate;

        float ring_twist = s_twist + (1.0f - z) * 1.5f;

        for (int s = 0; s < TUNNEL_SIDES; s++) {
            float a = ring_twist + ((float)s / (float)TUNNEL_SIDES) * (2.0f * (float)PI);
            ring_verts[r][s] = (Vector2){
                center.x + cosf(a) * radius,
                center.y + sinf(a) * radius
            };
        }
    }

    // Connecting longitudinal rib lines
    for (int s = 0; s < TUNNEL_SIDES; s++) {
        for (int r = 0; r < TUNNEL_RINGS - 1; r++) {
            if (ring_depth[r + 1] > ring_depth[r]) {
                float z = ring_depth[r];
                float alpha = z * z * 0.65f;
                DrawLineEx(ring_verts[r][s], ring_verts[r + 1][s], 1.0f, ColorAlpha(COLOR_ACCENT_DIM, alpha));
            }
        }
    }

    // Polygonal rings
    for (int r = 0; r < TUNNEL_RINGS; r++) {
        float z = ring_depth[r];
        float alpha = z * z;
        if (alpha < 0.05f) continue;

        Color col = (z > 0.70f) ? COLOR_ACCENT : ColorAlpha(COLOR_TEXT_PRIMARY, alpha);
        float thick = 1.0f + z * 2.2f;

        for (int s = 0; s < TUNNEL_SIDES; s++) {
            int next_s = (s + 1) % TUNNEL_SIDES;
            DrawLineEx(ring_verts[r][s], ring_verts[r][next_s], thick, col);
        }
    }

    // Particle streaks
    for (int i = 0; i < TUNNEL_PARTICLES; i++) {
        TunnelStar *st = &s_stars[i];
        st->z += dt * speed * st->speed;
        if (st->z >= 1.0f) {
            st->z -= 1.0f;
            st->x = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
            st->y = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
        }

        float sz = st->z * st->z;
        Vector2 star_pos = {
            center.x + st->x * max_radius * sz,
            center.y + st->y * max_radius * sz
        };
        DrawCircleV(star_pos, 1.0f + sz * 1.8f, ColorAlpha(WHITE, sz * 0.85f));
    }
}