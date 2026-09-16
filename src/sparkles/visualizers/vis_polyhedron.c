#include "sparkles_vis.h"
#include "sparkles_theme.h"
#include <math.h>

#define ICO_VERTS 12
#define ICO_EDGES 30

// Vector math helpers
typedef struct { float x, y, z; } Vec3;

static const Vec3 s_base_verts[ICO_VERTS] = {
    { -0.525731f,  0.850651f,  0.0f },
    {  0.525731f,  0.850651f,  0.0f },
    { -0.525731f, -0.850651f,  0.0f },
    {  0.525731f, -0.850651f,  0.0f },
    {  0.0f, -0.525731f,  0.850651f },
    {  0.0f,  0.525731f,  0.850651f },
    {  0.0f, -0.525731f, -0.850651f },
    {  0.0f,  0.525731f, -0.850651f },
    {  0.850651f,  0.0f, -0.525731f },
    {  0.850651f,  0.0f,  0.525731f },
    { -0.850651f,  0.0f, -0.525731f },
    { -0.850651f,  0.0f,  0.525731f }
};

static const int s_ico_edges[ICO_EDGES][2] = {
    {0, 1},  {0, 5},  {0, 7},  {0, 10}, {0, 11},
    {1, 5},  {1, 7},  {1, 8},  {1, 9},
    {2, 3},  {2, 4},  {2, 6},  {2, 10}, {2, 11},
    {3, 4},  {3, 6},  {3, 8},  {3, 9},
    {4, 5},  {4, 9},  {4, 11},
    {5, 9},  {5, 11},
    {6, 7},  {6, 8},  {6, 10},
    {7, 8},  {7, 10},
    {8, 9},
    {10, 11}
};

static float s_rot_x = 0.2f;
static float s_rot_y = 0.0f;
static float s_rot_z = 0.0f;

void vis_polyhedron_render(Rectangle b, float dt) {
    float bass = 0.0f, mid = 0.0f, high = 0.0f;
    sparkles_vis_get_bands(&bass, &mid, &high);

    float bins[ICO_VERTS];
    sparkles_vis_get_spectrum(bins, ICO_VERTS);

    // Rotation drifts
    s_rot_x += dt * (0.45f + mid * 0.90f);
    s_rot_y += dt * (0.65f + bass * 1.40f);
    s_rot_z += dt * (0.30f + high * 0.80f);

    float cx = cosf(s_rot_x), sx = sinf(s_rot_x);
    float cy = cosf(s_rot_y), sy = sinf(s_rot_y);
    float cz = cosf(s_rot_z), sz = sinf(s_rot_z);

    Vector2 center = { b.x + b.width * 0.5f, b.y + b.height * 0.5f };
    float base_radius = fminf(b.width, b.height) * 0.36f;
    if (base_radius < 35.0f) base_radius = 35.0f;

    Vector2 proj_outer[ICO_VERTS];
    Vector2 proj_inner[ICO_VERTS];
    float depth_outer[ICO_VERTS];
    float depth_inner[ICO_VERTS];

    for (int i = 0; i < ICO_VERTS; i++) {
        // Each vertex shoots outward to its frequency bin
        float spike = 1.0f + bins[i] * 0.65f + bass * 0.35f;

        Vec3 v = s_base_verts[i];
        Vec3 v_out = { v.x * spike, v.y * spike, v.z * spike };
        Vec3 v_in  = { v.x * 0.45f, v.y * 0.45f, v.z * 0.45f };

        // Euler rotation matrix
        #define ROTATE(in, out) { \
            float x1 = in.x; \
            float y1 = in.y * cx - in.z * sx; \
            float z1 = in.y * sx + in.z * cx; \
            float x2 = x1 * cy + z1 * sy; \
            float y2 = y1; \
            float z2 = -x1 * sy + z1 * cy; \
            out.x = x2 * cz - y2 * sz; \
            out.y = x2 * sz + y2 * cz; \
            out.z = z2; \
        }

        Vec3 r_out, r_in;
        ROTATE(v_out, r_out);
        ROTATE(v_in, r_in);
        #undef ROTATE

        // Perspective projection
        const float dist_cam = 2.8f;
        float pz_out = dist_cam + r_out.z;
        float pz_in  = dist_cam + r_in.z;

        depth_outer[i] = r_out.z;
        depth_inner[i] = r_in.z;

        proj_outer[i] = (Vector2){
            center.x + (r_out.x / pz_out) * base_radius * 2.4f,
            center.y + (r_out.y / pz_out) * base_radius * 2.4f
        };

        proj_inner[i] = (Vector2){
            center.x + (r_in.x / pz_in) * base_radius * 2.4f,
            center.y + (r_in.y / pz_in) * base_radius * 2.4f
        };
    }

    // Inner core
    for (int e = 0; e < ICO_EDGES; e++) {
        int v1 = s_ico_edges[e][0];
        int v2 = s_ico_edges[e][1];
        float avg_z = (depth_inner[v1] + depth_inner[v2]) * 0.5f;
        float alpha = 0.20f + (avg_z + 1.0f) * 0.25f;
        DrawLineEx(proj_inner[v1], proj_inner[v2], 1.0f, ColorAlpha(COLOR_ACCENT_DIM, alpha));
    }

    // Radial spokes connecting inner core to outer spikes
    for (int i = 0; i < ICO_VERTS; i++) {
        DrawLineEx(proj_inner[i], proj_outer[i], 1.0f, (Color){ 36, 40, 52, 160 });
    }

    // Draw outer icosahedron
    for (int e = 0; e < ICO_EDGES; e++) {
        int v1 = s_ico_edges[e][0];
        int v2 = s_ico_edges[e][1];
        float avg_z = (depth_outer[v1] + depth_outer[v2]) * 0.5f;

        // Front-facing lines are brighter
        float norm_z = (avg_z + 1.4f) / 2.8f;
        if (norm_z < 0.15f) norm_z = 0.15f;
        if (norm_z > 1.0f)  norm_z = 1.0f;

        Color edge_col = (norm_z > 0.65f) ? COLOR_ACCENT : ColorAlpha(COLOR_TEXT_PRIMARY, norm_z);
        float thick = 1.0f + norm_z * 1.5f;

        DrawLineEx(proj_outer[v1], proj_outer[v2], thick, edge_col);
    }

    // Draw vertex nodes
    for (int i = 0; i < ICO_VERTS; i++) {
        float norm_z = (depth_outer[i] + 1.4f) / 2.8f;
        if (norm_z < 0.2f) norm_z = 0.2f;

        float node_r = (2.0f + bins[i] * 3.5f) * norm_z;
        DrawCircleV(proj_outer[i], node_r * 2.0f, ColorAlpha(COLOR_ACCENT, 0.18f));
        DrawCircleV(proj_outer[i], node_r, WHITE);
    }
}