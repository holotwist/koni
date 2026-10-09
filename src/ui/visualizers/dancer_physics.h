#ifndef DANCER_PHYSICS_H
#define DANCER_PHYSICS_H

#include <stdbool.h>

typedef struct {
    float char_base_x;
    float char_base_y;
    float floor_y;
    float scale;
    float sway;
    float total_body_y;
    float squash_x;
    float squash_y;
    float h_squash_x;
    float h_squash_y;
    float hip_tilt;
    float head_tilt;
    float hair_l;
    float hair_r;
    float ahoge;
    float l_arm_angle;
    float r_arm_angle;
    float face_y;
    float spin_scale_x;
    float smooth_l_angle;
    float smooth_r_angle;
    float smooth_l_lift;
    float smooth_r_lift;
    float smooth_l_scale_y;
    float smooth_r_scale_y;
    bool l_arm_on_hip;
    bool r_arm_on_hip;
    int face_type; // 0: neutral, 1: happy, 2: relaxed-like
    float bass;
    float mid;
    float high;
    bool beat_hit;
    bool is_airborne;
} DancerState;

void dancer_physics_init(void);
void dancer_physics_update(DancerState *out, float px_w, float px_h);

#endif // DANCER_PHYSICS_H