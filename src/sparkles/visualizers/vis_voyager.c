#include "sparkles_vis.h"
#include "sparkles_theme.h"
#include "voyager_dna.h"
#include "state.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define NODES_PER_SECTOR 64
#define MAX_ACTIVE_SECTORS 32
#define MAX_TOTAL_NODES (MAX_ACTIVE_SECTORS * NODES_PER_SECTOR)
#define MAX_EDGES 5
#define MAX_PULSES 32
#define VIEW_DIST 1400.0f
#define HISTORY_LEN 24

typedef struct { float x, y, z; } SynVec3;

typedef struct {
    SynVec3 pos;
    int neighbors[MAX_EDGES];
    int neighbor_count;
    float pulse_energy;
    uint32_t step_id; // Monotonically increasing note counter
    int sector_id;
    int local_idx;
    char tag[16];
    float spawn_scale;
} SynNode;

typedef struct {
    int from, to;
    float t, speed, intensity;
    bool active;
} SynPulse;

static SynNode s_nodes[MAX_TOTAL_NODES];
static SynPulse s_pulses[MAX_PULSES];
static int s_built_sectors = 0;

static char s_active_filepath[1024] = {0}; // Track change detection
static bool s_inited = false;

// Discrete Runner & Voyage State
static int s_curr_node = 0;
static int s_target_node = 1;
static float s_tracer_t = 1.0f; // 1.0 = Parked on s_curr_node, 0..1 = Actively leaping
static bool s_is_leaping = false;
static float s_leap_duration = 0.075f;
static float s_idle_timer = 0.0f;
static float s_tracer_glow = 1.0f;
static uint32_t s_history[HISTORY_LEN] = {0};
static int s_history_head = 0;

// Sprouter vector state
static SynVec3 s_last_sprout_center = { 0, 0, 0 };
static SynVec3 s_last_sprout_dir = { 0, 0, 1 };

// Seek and jump warp state
static uint32_t s_last_play_sec = 0;
static float s_warp_fx = 0.0f;

// Real-time melodic / vocal / chiptune onset analyzer
static uint32_t s_last_rpos = 0;
static float s_env_fast = 0.0f;
static float s_env_slow = 0.0f;
static float s_prev_flux = 0.0f;
static float s_time_since_onset = 0.0f;
static float s_bp_s1 = 0.0f, s_bp_s2 = 0.0f;
static float s_prev_mono = 0.0f;
static uint32_t s_filter_srate = 0;
static float s_bp_b0 = 0.0f, s_bp_b1 = 0.0f, s_bp_b2 = 0.0f, s_bp_a1 = 0.0f, s_bp_a2 = 0.0f;

// Third-person trailing camera
static SynVec3 s_cam_pos = { 0, 160, -300 };
static SynVec3 s_cam_target = { 0, 0, 0 };
static SynVec3 s_cam_forward = { 0, 0, 1 };
static float s_cam_pause_yaw = 0.0f;

// Vector helpers
static inline SynVec3 vec3_add(SynVec3 a, SynVec3 b) { return (SynVec3){ a.x + b.x, a.y + b.y, a.z + b.z }; }
static inline SynVec3 vec3_sub(SynVec3 a, SynVec3 b) { return (SynVec3){ a.x - b.x, a.y - b.y, a.z - b.z }; }
static inline SynVec3 vec3_scale(SynVec3 a, float s) { return (SynVec3){ a.x * s, a.y * s, a.z * s }; }
static inline float vec3_dist_sq(SynVec3 a, SynVec3 b) { float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z; return dx * dx + dy * dy + dz * dz; }
static inline SynVec3 vec3_lerp(SynVec3 a, SynVec3 b, float t) { return (SynVec3){ a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t }; }
static inline float vec3_dot(SynVec3 a, SynVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

static inline SynVec3 vec3_norm(SynVec3 v) {
    float l = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (l < 1e-6f) return (SynVec3){ 0, 0, 1 };
    return (SynVec3){ v.x / l, v.y / l, v.z / l };
}

static inline SynVec3 vec3_cross(SynVec3 a, SynVec3 b) {
    return (SynVec3){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}

// Orthonormal look-at projection
static bool project_point(SynVec3 p, Rectangle b, SynVec3 eye, SynVec3 target, Vector2 *out_pt, float *out_depth) {
    SynVec3 F = vec3_norm(vec3_sub(target, eye));
    SynVec3 world_up = { 0.0f, 1.0f, 0.0f };
    if (fabsf(vec3_dot(F, world_up)) > 0.98f) world_up = (SynVec3){ 0.0f, 0.0f, 1.0f };

    SynVec3 R = vec3_norm(vec3_cross(F, world_up));
    SynVec3 U = vec3_cross(R, F);

    SynVec3 rel = vec3_sub(p, eye);
    float cam_z = vec3_dot(rel, F);
    if (cam_z < 20.0f) return false;

    float cam_x = vec3_dot(rel, R);
    float cam_y = vec3_dot(rel, U);

    float fov_scale = fminf(b.width, b.height) * 0.95f;
    Vector2 center = { b.x + b.width * 0.5f, b.y + b.height * 0.5f };

    out_pt->x = center.x + (cam_x / cam_z) * fov_scale;
    out_pt->y = center.y - (cam_y / cam_z) * fov_scale;
    *out_depth = cam_z;
    return true;
}

// Procedurally sprouts an infinite forward spire
static void sprout_galaxy_sector(int s_idx) {
    VoyagerSectorDNA dna = voyager_dna_get_sector(s_idx % VOYAGER_SECTOR_COUNT);

    SynVec3 center = s_last_sprout_center;
    SynVec3 dir = s_last_sprout_dir;

    float deflect = dna.chaos * 2.0f;
    SynVec3 rand_dir = {
        ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f,
        ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f,
        ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f
    };
    SynVec3 next_dir = vec3_norm(vec3_add(dir, vec3_scale(rand_dir, deflect)));
    float travel_dist = 220.0f + dna.energy * 380.0f;
    SynVec3 next_center = vec3_add(center, vec3_scale(next_dir, travel_dist));

    s_last_sprout_center = next_center;
    s_last_sprout_dir = next_dir;

    int start_step = s_idx * NODES_PER_SECTOR;
    int end_step = start_step + NODES_PER_SECTOR;

    float scatter_radius = 12.0f + (dna.chaos * 280.0f);
    int allowed_bifurcations = 1 + (int)(dna.chaos * (MAX_EDGES - 1));

    for (int step = start_step; step < end_step; step++) {
        int l_idx = step - start_step;
        int slot = step % MAX_TOTAL_NODES;
        SynNode *n = &s_nodes[slot];
        n->step_id = (uint32_t)step;
        n->sector_id = s_idx;
        n->local_idx = l_idx;
        n->neighbor_count = 0;
        n->pulse_energy = 0.0f;
        n->spawn_scale = 0.0f;
        snprintf(n->tag, sizeof(n->tag), "S%02d-%02d", (s_idx % 99) + 1, l_idx);

        float progress = (float)l_idx / (float)NODES_PER_SECTOR;
        SynVec3 spine_pos = vec3_lerp(center, next_center, progress);

        float u = ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
        float v = ((float)rand() / (float)RAND_MAX) * 2.0f * (float)PI;
        float r = powf(((float)rand() / (float)RAND_MAX), 0.5f) * scatter_radius;

        if (l_idx % 5 == 0) r *= 0.05f;

        n->pos = (SynVec3){
            spine_pos.x + r * sqrtf(1.0f - u * u) * cosf(v),
            spine_pos.y + r * u,
            spine_pos.z + r * sqrtf(1.0f - u * u) * sinf(v)
        };
    }

    // Connect nodes forward along the spine and add forward bifurcations
    for (int step = start_step; step < end_step; step++) {
        int slot = step % MAX_TOTAL_NODES;
        if (step < end_step - 1) {
            int next_slot = (step + 1) % MAX_TOTAL_NODES;
            s_nodes[slot].neighbors[s_nodes[slot].neighbor_count++] = next_slot;
        }

        for (int next_s = step + 2; next_s < end_step; next_s++) {
            if (s_nodes[slot].neighbor_count >= allowed_bifurcations) break;
            int next_slot = next_s % MAX_TOTAL_NODES;
            float d2 = vec3_dist_sq(s_nodes[slot].pos, s_nodes[next_slot].pos);
            if (d2 < 180.0f * 180.0f) {
                s_nodes[slot].neighbors[s_nodes[slot].neighbor_count++] = next_slot;
            }
        }
    }

    // Bridge the previous sector's tip node to the first node of this new sector
    if (s_idx > 0) {
        int prev_tip_slot = (start_step - 1) % MAX_TOTAL_NODES;
        int first_slot = start_step % MAX_TOTAL_NODES;
        if (s_nodes[prev_tip_slot].neighbor_count < MAX_EDGES) {
            s_nodes[prev_tip_slot].neighbors[s_nodes[prev_tip_slot].neighbor_count++] = first_slot;
        }
    }
}

static void spawn_pulse(int from, int to, float speed, float intensity) {
    for (int i = 0; i < MAX_PULSES; i++) {
        if (!s_pulses[i].active) {
            s_pulses[i].from = from;
            s_pulses[i].to = to;
            s_pulses[i].t = 0.0f;
            s_pulses[i].speed = speed;
            s_pulses[i].intensity = intensity;
            s_pulses[i].active = true;
            break;
        }
    }
}

static bool is_in_history(uint32_t step_id) {
    for (int i = 0; i < HISTORY_LEN; i++) {
        if (s_history[i] == step_id) return true;
    }
    return false;
}

// Initializes the 1.6kHz bandpass filter
static void init_melodic_filter(uint32_t srate) {
    if (srate == 0) srate = 44100;
    s_filter_srate = srate;
    float fs = (float)srate;
    float fc = 1600.0f;
    float q = 0.70f;
    float w0 = 2.0f * (float)PI * (fc / fs);
    float alpha = sinf(w0) / (2.0f * q);
    float cos_w = cosf(w0);
    float a0 = 1.0f + alpha;

    s_bp_b0 = alpha / a0;
    s_bp_b1 = 0.0f;
    s_bp_b2 = -alpha / a0;
    s_bp_a1 = (-2.0f * cos_w) / a0;
    s_bp_a2 = (1.0f - alpha) / a0;
}

// Scans physical audio buffer for notes, vocal syllables, and high pitch (chiptune-like) onsets
static int detect_audio_onsets(uint32_t cur_rpos, uint32_t srate) {
    if (srate != s_filter_srate) {
        init_melodic_filter(srate);
    }

    if (s_last_rpos == 0 || llabs((int64_t)cur_rpos - (int64_t)s_last_rpos) > (int64_t)srate) {
        s_last_rpos = (cur_rpos >= 512) ? (cur_rpos - 512) : 0;
    }

    uint32_t num_samples = cur_rpos - s_last_rpos;
    if (num_samples > 4096) {
        s_last_rpos = cur_rpos - 4096;
        num_samples = 4096;
    }
    if (num_samples == 0) return 0;

    int onsets_count = 0;
    float dt_sample = 1.0f / (float)srate;
    float att_fast = 1.0f - expf(-dt_sample / 0.002f);
    float rel_fast = 1.0f - expf(-dt_sample / 0.022f);
    float att_slow = 1.0f - expf(-dt_sample / 0.025f);
    float rel_slow = 1.0f - expf(-dt_sample / 0.160f);

    for (uint32_t s = 0; s < num_samples; s++) {
        uint32_t idx = (s_last_rpos + s) & VIS_BUF_MASK;
        float mono = (vis_ring_l[idx] + vis_ring_r[idx]) * 0.5f;

        // Bandpass filter isolating melodic formants (350Hz - 4.5kHz)
        float y = s_bp_b0 * mono + s_bp_s1;
        s_bp_s1 = s_bp_b1 * mono - s_bp_a1 * y + s_bp_s2;
        s_bp_s2 = s_bp_b2 * mono - s_bp_a2 * y;

        // High-frequency edge differentiator
        float hf = mono - s_prev_mono;
        s_prev_mono = mono;

        float sample_energy = (y * y) + 0.35f * (hf * hf);

        s_env_fast += (sample_energy > s_env_fast ? att_fast : rel_fast) * (sample_energy - s_env_fast);
        s_env_slow += (sample_energy > s_env_slow ? att_slow : rel_slow) * (sample_energy - s_env_slow);

        s_time_since_onset += dt_sample;

        // Normalized relative flux (Weber-Fechner logarithmic onset)
        float flux = (s_env_fast - s_env_slow) / (s_env_slow + 0.00035f);
        if (flux < 0.0f) flux = 0.0f;

        // Peak detection (refractory period of 28ms allows up to 35 notes/sec)
        const float threshold = 0.35f;
        if (s_prev_flux > threshold && flux < s_prev_flux && s_time_since_onset >= 0.028f) {
            onsets_count++;
            s_time_since_onset = 0.0f;
        }
        s_prev_flux = flux;
    }

    s_last_rpos = cur_rpos;
    return onsets_count;
}

void vis_voyager_render(Rectangle b, float dt) {
    pthread_mutex_lock(&state_mutex);
    char cur_path[1024] = {0};
    strncpy(cur_path, playing_filepath, sizeof(cur_path) - 1);
    uint32_t tot_sec = atomic_load(&p_total_sec);
    uint32_t cur_sec = atomic_load(&p_current_sec);
    uint32_t cur_rpos = atomic_load(&p_frames_consumed);
    uint32_t srate = atomic_load(&vis_srate);
    PlayState play_state = (PlayState)atomic_load(&play_state_atomic);
    pthread_mutex_unlock(&state_mutex);

    if (srate == 0) srate = 44100;

    // Track change detection & complete reset
    if (strcmp(s_active_filepath, cur_path) != 0 || !s_inited) {
        strncpy(s_active_filepath, cur_path, sizeof(s_active_filepath) - 1);
        
        voyager_dna_init();
        voyager_dna_check_update(cur_path, tot_sec);

        memset(s_nodes, 0, sizeof(s_nodes));
        s_built_sectors = 0;
        
        unsigned int h = 5381;
        for (const char *p = cur_path; *p; p++) h = ((h << 5) + h) + (unsigned char)*p;
        srand(h);

        s_last_sprout_center = (SynVec3){0, 0, 0};
        s_last_sprout_dir    = (SynVec3){0, 0, 1};

        // Sprout first 4 sectors ahead
        while (s_built_sectors < 4) {
            sprout_galaxy_sector(s_built_sectors);
            s_built_sectors++;
        }

        s_curr_node = 0;
        s_target_node = s_nodes[0].neighbor_count > 0 ? s_nodes[0].neighbors[0] : 1;
        s_tracer_t = 1.0f; // Start parked on node 0
        s_is_leaping = false;
        s_leap_duration = 0.075f;
        s_idle_timer = 0.0f;
        s_tracer_glow = 1.0f;
        s_last_play_sec = cur_sec;
        s_warp_fx = 0.0f;

        s_last_rpos = cur_rpos;
        s_env_fast = 0.0f;
        s_env_slow = 0.0f;
        s_prev_flux = 0.0f;
        s_time_since_onset = 0.0f;
        s_bp_s1 = 0.0f;
        s_bp_s2 = 0.0f;
        s_prev_mono = 0.0f;
        init_melodic_filter(srate);

        s_cam_target = s_nodes[0].pos;
        s_cam_pos = (SynVec3){ s_nodes[0].pos.x, s_nodes[0].pos.y + 160.0f, s_nodes[0].pos.z - 280.0f };
        s_cam_forward = (SynVec3){ 0, 0, 1 };
        s_cam_pause_yaw = 0.0f;

        for (int i = 0; i < HISTORY_LEN; i++) s_history[i] = 0xFFFFFFFF;
        s_inited = true;
    }

    // Sprout forward, runner always has 4 sectors ahead
    uint32_t runner_sector = s_nodes[s_curr_node].step_id / NODES_PER_SECTOR;
    while (s_built_sectors < (int)runner_sector + 4) {
        sprout_galaxy_sector(s_built_sectors);
        s_built_sectors++;
    }

    int active_node_count = (s_built_sectors * NODES_PER_SECTOR < MAX_TOTAL_NODES) 
                            ? (s_built_sectors * NODES_PER_SECTOR) 
                            : MAX_TOTAL_NODES;

    for (int i = 0; i < active_node_count; i++) {
        if (s_nodes[i].spawn_scale < 1.0f) {
            s_nodes[i].spawn_scale += dt * 2.0f;
            if (s_nodes[i].spawn_scale > 1.0f) s_nodes[i].spawn_scale = 1.0f;
        }
    }

    float bass = 0.0f, mid = 0.0f, high = 0.0f;
    sparkles_vis_get_bands(&bass, &mid, &high);

    bool is_paused = (play_state == STATE_PAUSED || play_state == STATE_STOPPED);

    // Seek / timeline scrubbing -> Jump warp
    int sec_delta = (int)cur_sec - (int)s_last_play_sec;
    if (abs(sec_delta) > 2) {
        float prog = (tot_sec > 0) ? ((float)cur_sec / (float)tot_sec) : 0.0f;
        float total_est_sectors = (tot_sec > 0) ? fmaxf(16.0f, (float)tot_sec / 15.0f) : 16.0f;
        int target_sec = (int)(prog * total_est_sectors);

        memset(s_nodes, 0, sizeof(s_nodes));
        s_built_sectors = target_sec;

        unsigned int h = 5381;
        for (const char *p = cur_path; *p; p++) h = ((h << 5) + h) + (unsigned char)*p;
        srand(h + (unsigned int)target_sec * 1013);

        s_last_sprout_center = (SynVec3){ (float)target_sec * 120.0f, (float)target_sec * 40.0f, (float)target_sec * 320.0f };
        s_last_sprout_dir = (SynVec3){ 0, 0, 1 };

        while (s_built_sectors < target_sec + 4) {
            sprout_galaxy_sector(s_built_sectors);
            s_built_sectors++;
        }

        int target_slot = (target_sec * NODES_PER_SECTOR) % MAX_TOTAL_NODES;
        s_curr_node = target_slot;
        s_target_node = s_nodes[s_curr_node].neighbor_count > 0 ? s_nodes[s_curr_node].neighbors[0] : (s_curr_node + 1) % MAX_TOTAL_NODES;
        s_tracer_t = 1.0f;
        s_is_leaping = false;
        s_idle_timer = 0.0f;
        s_nodes[s_curr_node].pulse_energy = 1.0f;
        s_warp_fx = 1.0f;
        s_last_rpos = cur_rpos;
    }
    s_last_play_sec = cur_sec;

    if (s_warp_fx > 0.0f) {
        s_warp_fx -= dt * 2.8f;
        if (s_warp_fx < 0.0f) s_warp_fx = 0.0f;
    }

    // Detect onsets in audio stream
    int onsets_count = is_paused ? 0 : detect_audio_onsets(cur_rpos, srate);

    // Runner motion physics, discrete note/syllable hopping
    if (is_paused) {
        s_tracer_glow = fmaxf(0.12f, s_tracer_glow - dt * 1.5f);
        s_is_leaping = false;
    } else {
        s_tracer_glow = fminf(1.0f, s_tracer_glow + dt * 4.0f);

        if (onsets_count > 0) {
            s_idle_timer = 0.0f;

            for (int oc = 0; oc < onsets_count; oc++) {
                // If already mid-leap, complete instantly so rapid notes never drop
                if (s_is_leaping) {
                    s_curr_node = s_target_node;
                    s_nodes[s_curr_node].pulse_energy = 1.0f;
                    s_history[s_history_head] = s_nodes[s_curr_node].step_id;
                    s_history_head = (s_history_head + 1) % HISTORY_LEN;

                    int n_count = s_nodes[s_curr_node].neighbor_count;
                    if (high > 0.25f) {
                        for (int k = 0; k < n_count; k++) {
                            int nb = s_nodes[s_curr_node].neighbors[k];
                            if (!is_in_history(s_nodes[nb].step_id)) {
                                spawn_pulse(s_curr_node, nb, 3.5f + high * 4.0f, 0.95f);
                            }
                        }
                    }
                }

                // Pick next node strictly forward in time/space
                int n_count = s_nodes[s_curr_node].neighbor_count;
                int forward_choices[MAX_EDGES];
                int forward_count = 0;
                for (int k = 0; k < n_count; k++) {
                    int nb = s_nodes[s_curr_node].neighbors[k];
                    if (s_nodes[nb].step_id > s_nodes[s_curr_node].step_id && !is_in_history(s_nodes[nb].step_id)) {
                        forward_choices[forward_count++] = nb;
                    }
                }

                if (forward_count > 0) {
                    s_target_node = forward_choices[rand() % forward_count];
                } else if (n_count > 0) {
                    s_target_node = s_nodes[s_curr_node].neighbors[rand() % n_count];
                } else {
                    s_target_node = (s_curr_node + 1) % MAX_TOTAL_NODES;
                }

                // Launch snappy leap to the next dot
                s_tracer_t = 0.0f;
                s_is_leaping = true;
                s_leap_duration = 0.075f;
            }
        }

        if (s_is_leaping) {
            s_tracer_t += dt / s_leap_duration;
            if (s_tracer_t >= 1.0f) {
                // Landed on destination dot
                s_tracer_t = 1.0f;
                s_is_leaping = false;
                s_curr_node = s_target_node;
                s_nodes[s_curr_node].pulse_energy = 1.0f;
                s_history[s_history_head] = s_nodes[s_curr_node].step_id;
                s_history_head = (s_history_head + 1) % HISTORY_LEN;

                int n_count = s_nodes[s_curr_node].neighbor_count;
                if (high > 0.25f) {
                    for (int k = 0; k < n_count; k++) {
                        int nb = s_nodes[s_curr_node].neighbors[k];
                        if (!is_in_history(s_nodes[nb].step_id)) {
                            spawn_pulse(s_curr_node, nb, 3.5f + high * 4.0f, 0.95f);
                        }
                    }
                }
            }
        } else {
            // Stationary on current dot
            s_idle_timer += dt;

            // Ambient fallback, gentle glide if no discrete notes play for >1.4s
            if (s_idle_timer > 1.4f) {
                s_idle_timer = 0.0f;
                int n_count = s_nodes[s_curr_node].neighbor_count;
                int forward_choices[MAX_EDGES];
                int forward_count = 0;
                for (int k = 0; k < n_count; k++) {
                    int nb = s_nodes[s_curr_node].neighbors[k];
                    if (s_nodes[nb].step_id > s_nodes[s_curr_node].step_id && !is_in_history(s_nodes[nb].step_id)) {
                        forward_choices[forward_count++] = nb;
                    }
                }
                if (forward_count > 0) s_target_node = forward_choices[rand() % forward_count];
                else if (n_count > 0) s_target_node = s_nodes[s_curr_node].neighbors[rand() % n_count];
                else s_target_node = (s_curr_node + 1) % MAX_TOTAL_NODES;

                s_tracer_t = 0.0f;
                s_is_leaping = true;
                s_leap_duration = 0.55f;
            }
        }
    }

    // Particle 3d position
    SynVec3 tracer_3d;
    if (s_is_leaping) {
        float ease = 1.0f - powf(1.0f - s_tracer_t, 3.0f); // Cubic ease-out
        tracer_3d = vec3_lerp(s_nodes[s_curr_node].pos, s_nodes[s_target_node].pos, ease);
    } else {
        tracer_3d = s_nodes[s_curr_node].pos;
        if (!is_paused && bass > 0.08f) {
            float throb = sinf((float)GetTime() * 24.0f) * (bass * 2.5f);
            tracer_3d.y += throb;
        }
    }

    // Node energy decay
    for (int i = 0; i < active_node_count; i++) {
        if (s_nodes[i].pulse_energy > 0.0f) {
            s_nodes[i].pulse_energy -= dt * 2.5f;
            if (s_nodes[i].pulse_energy < 0.0f) s_nodes[i].pulse_energy = 0.0f;
        }
    }

    // Branch pulses decay
    for (int i = 0; i < MAX_PULSES; i++) {
        if (!s_pulses[i].active) continue;
        SynVec3 p3d = vec3_lerp(s_nodes[s_pulses[i].from].pos, s_nodes[s_pulses[i].to].pos, s_pulses[i].t);
        Vector2 s_pt;
        float depth;
        if (project_point(p3d, b, s_cam_pos, s_cam_target, &s_pt, &depth)) {
            DrawCircleV(s_pt, 2.5f + bass * 2.0f, ColorAlpha(COLOR_ACCENT, (1.0f - s_pulses[i].t) * s_tracer_glow));
        }
    }

    // Third-person camera tracking
    float cam_lag = fminf(1.0f, dt * (s_warp_fx > 0.0f ? 15.0f : (s_is_leaping ? 8.0f : 3.5f)));
    s_cam_target = vec3_lerp(s_cam_target, tracer_3d, cam_lag);

    if (s_is_leaping) {
        SynVec3 inst_dir = vec3_norm(vec3_sub(s_nodes[s_target_node].pos, s_nodes[s_curr_node].pos));
        s_cam_forward = vec3_norm(vec3_lerp(s_cam_forward, inst_dir, fminf(1.0f, dt * 7.0f)));
    }

    float follow_dist = 320.0f - (bass * 50.0f) - (mid * 40.0f);
    float follow_height = 140.0f - (mid * 30.0f);

    if (!is_paused) {
        SynVec3 desired_pos = {
            s_cam_target.x - s_cam_forward.x * follow_dist,
            s_cam_target.y + follow_height,
            s_cam_target.z - s_cam_forward.z * follow_dist
        };
        s_cam_pos = vec3_lerp(s_cam_pos, desired_pos, fminf(1.0f, dt * (s_is_leaping ? 7.0f : 4.0f)));
    } else {
        s_cam_pause_yaw += dt * 0.15f;
        SynVec3 orbit_pos = {
            s_cam_target.x + sinf(s_cam_pause_yaw) * follow_dist,
            s_cam_target.y + follow_height,
            s_cam_target.z + cosf(s_cam_pause_yaw) * follow_dist
        };
        s_cam_pos = vec3_lerp(s_cam_pos, orbit_pos, fminf(1.0f, dt * 2.5f));
    }

    // View-sphere culling across active window
    Vector2 screen_pts[MAX_TOTAL_NODES];
    float depths[MAX_TOTAL_NODES];
    bool in_view[MAX_TOTAL_NODES];
    float view_d2 = VIEW_DIST * VIEW_DIST;

    for (int i = 0; i < active_node_count; i++) {
        in_view[i] = false;
        if (vec3_dist_sq(s_nodes[i].pos, s_cam_target) > view_d2) continue;
        in_view[i] = project_point(s_nodes[i].pos, b, s_cam_pos, s_cam_target, &screen_pts[i], &depths[i]);
    }

    // Render edges (guarding against recycled slots across the ring buffer)
    for (int i = 0; i < active_node_count; i++) {
        if (!in_view[i] || s_nodes[i].spawn_scale <= 0.01f) continue;

        for (int k = 0; k < s_nodes[i].neighbor_count; k++) {
            int j = s_nodes[i].neighbors[k];
            if (!in_view[j] || s_nodes[j].step_id <= s_nodes[i].step_id || (s_nodes[j].step_id - s_nodes[i].step_id) > 128) continue;

            float avg_depth = (depths[i] + depths[j]) * 0.5f;
            float norm_depth = 1.0f - (avg_depth / (VIEW_DIST * 0.9f));
            if (norm_depth < 0.05f) norm_depth = 0.05f;
            if (norm_depth > 1.0f)  norm_depth = 1.0f;

            bool is_main_edge = ((s_curr_node == i && s_target_node == j) || (s_curr_node == j && s_target_node == i));

            if (is_main_edge && !is_paused) {
                // Bass is shiny, active path illuminates brilliantly with the bass
                float edge_thick = 1.5f + (bass * 7.0f);
                float edge_alpha = s_is_leaping ? (0.35f + (bass * 0.65f)) : (0.20f + (bass * 0.50f));
                Color act_col = ColorAlpha(WHITE, edge_alpha);
                DrawLineEx(screen_pts[i], screen_pts[j], edge_thick, act_col);
            } else {
                float alpha = norm_depth * 0.35f * s_nodes[i].spawn_scale;
                DrawLineEx(screen_pts[i], screen_pts[j], 1.2f, ColorAlpha(COLOR_ACCENT_DIM, alpha));
            }
        }
    }

    // Render branch pulses
    for (int i = 0; i < MAX_PULSES; i++) {
        if (!s_pulses[i].active) continue;
        SynVec3 p3d = vec3_lerp(s_nodes[s_pulses[i].from].pos, s_nodes[s_pulses[i].to].pos, s_pulses[i].t);
        Vector2 s_pt;
        float depth;
        if (project_point(p3d, b, s_cam_pos, s_cam_target, &s_pt, &depth)) {
            DrawCircleV(s_pt, 2.5f + bass * 2.0f, ColorAlpha(COLOR_ACCENT, (1.0f - s_pulses[i].t) * s_tracer_glow));
        }
    }

    // Render runner (Dead-center in third person)
    Vector2 tr_screen;
    float tr_depth;
    if (project_point(tracer_3d, b, s_cam_pos, s_cam_target, &tr_screen, &tr_depth)) {
        Color core_col = is_paused ? ColorAlpha(COLOR_TEXT_MUTED, 0.4f) : COLOR_ACCENT;

        // Bass is shiny, the runner explodes with intense light on bass hits
        float core_r = (4.0f + bass * 18.0f) * s_tracer_glow;
        float halo_r = core_r + (8.0f + bass * 40.0f) * s_tracer_glow;

        DrawCircleV(tr_screen, halo_r, ColorAlpha(WHITE, 0.15f + bass * 0.50f));
        DrawCircleV(tr_screen, core_r, core_col);
        DrawCircleV(tr_screen, core_r * 0.5f, WHITE);

        if (!is_paused && high > 0.20f) {
            float spike_len = core_r + 6.0f + (high * 40.0f);
            float spike_alpha = high * 0.9f;
            DrawLineEx((Vector2){tr_screen.x - spike_len, tr_screen.y}, (Vector2){tr_screen.x + spike_len, tr_screen.y}, 2.0f, ColorAlpha(WHITE, spike_alpha));
            DrawLineEx((Vector2){tr_screen.x, tr_screen.y - spike_len}, (Vector2){tr_screen.x, tr_screen.y + spike_len}, 2.0f, ColorAlpha(WHITE, spike_alpha));
        }

        if (!is_paused) {
            const char *tag = s_is_leaping ? TextFormat("LEAP -> %s", s_nodes[s_target_node].tag)
                                            : TextFormat("GOTO [%s]", s_nodes[s_curr_node].tag);
            DrawSparklesText(tag, (int)tr_screen.x + 14, (int)tr_screen.y - 10, FONT_SIZE_XS, COLOR_TEXT_PRIMARY);
        } else {
            DrawSparklesText("STATIC", (int)tr_screen.x + 12, (int)tr_screen.y - 8, 11, COLOR_TEXT_MUTED);
        }
    }

    // Render nodes
    for (int i = 0; i < active_node_count; i++) {
        if (!in_view[i] || s_nodes[i].spawn_scale <= 0.01f) continue;

        float norm_depth = 1.0f - (depths[i] / (VIEW_DIST * 0.9f));
        if (norm_depth < 0.10f) norm_depth = 0.10f;
        if (norm_depth > 1.0f)  norm_depth = 1.0f;

        float en = s_nodes[i].pulse_energy;
        
        // Active nodes glow brilliantly with the bass
        float active_flare = (en > 0.05f) ? (bass * 8.0f) : 0.0f;
        float r = (2.2f + en * 6.5f + active_flare) * norm_depth * s_nodes[i].spawn_scale;

        Color col = (en > 0.05f) ? ColorAlpha(WHITE, 0.8f + bass * 0.2f) : ColorAlpha(COLOR_ACCENT_DIM, norm_depth * 0.5f);
        DrawCircleV(screen_pts[i], r, col);

        if (en > 0.35f) {
            DrawCircleLines((int)screen_pts[i].x, (int)screen_pts[i].y, r + 4.0f + bass * 6.0f, ColorAlpha(COLOR_ACCENT, en * (0.4f + bass * 0.6f)));
            DrawSparklesText(s_nodes[i].tag, (int)screen_pts[i].x + 10, (int)screen_pts[i].y - 8, 11, COLOR_TEXT_PRIMARY);
        }
    }

    // Jump FX
    if (s_warp_fx > 0.01f) {
        float cx = b.x + b.width * 0.5f;
        float cy = b.y + b.height * 0.5f;
        for (int w = 0; w < 24; w++) {
            float a = ((float)w / 24.0f) * 2.0f * (float)PI;
            float r1 = 40.0f * (1.0f - s_warp_fx);
            float r2 = fminf(b.width, b.height) * 0.65f * s_warp_fx;
            DrawLineEx(
                (Vector2){ cx + cosf(a) * r1, cy + sinf(a) * r1 },
                (Vector2){ cx + cosf(a) * r2, cy + sinf(a) * r2 },
                1.8f, ColorAlpha(WHITE, s_warp_fx * 0.7f)
            );
        }
    }
}