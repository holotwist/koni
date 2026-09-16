#define _DEFAULT_SOURCE
#include "sparkles_vis.h"
#include "sparkles_theme.h"
#include "voyager_dna.h"
#include "state.h"
#include "rlgl.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Koni Character Palette
#define COLOR_KONI_PURPLE (Color){ 128, 0, 128, 255 }
#define COLOR_CYAN   (Color){ 0, 240, 255, 255 }
#define COLOR_CUTOUT      (Color){ 0, 0, 0, 255 } 

#define NUM_SPOTLIGHTS 4
#define MAX_SPARKS 180

typedef struct {
    Vector2 pos;
    Vector2 vel;
    float size;
    float life;
    float decay;
    Color color;
    bool has_gravity;
} StageSpark;

// Second-Order Spring Physics (Mass-Spring-Damper)
typedef struct {
    float val;
    float vel;
    float tension;
    float damping;
} SpringObj;

static inline void update_spring(SpringObj *s, float target, float dt) {
    float force = -s->tension * (s->val - target) - s->damping * s->vel;
    s->vel += force * dt;
    s->val += s->vel * dt;
}

// Elastic properties (tension, damping)
static SpringObj s_spr_body_y    = {0.0f, 0.0f, 300.0f, 18.0f};
static SpringObj s_spr_squash    = {1.0f, 0.0f, 250.0f, 14.0f};
static SpringObj s_spr_sway      = {0.0f, 0.0f, 150.0f, 12.0f};
// Lower tension and damping so the hair feels like loose jello/springs
static SpringObj s_spr_hair_l    = {0.0f, 0.0f, 50.0f, 3.5f};
static SpringObj s_spr_hair_r    = {0.0f, 0.0f, 50.0f, 3.5f};
static SpringObj s_spr_arm_l     = {0.0f, 0.0f, 180.0f, 12.0f};
static SpringObj s_spr_arm_r     = {0.0f, 0.0f, 180.0f, 12.0f};
static SpringObj s_spr_face_y    = {0.0f, 0.0f, 240.0f, 22.0f};
// Antenna spring that snaps back to center (0.0f)
static SpringObj s_spr_ahoge     = {0.0f, 0.0f, 220.0f, 11.0f};
static SpringObj s_spr_spin      = {0.0f, 0.0f, 300.0f, 14.0f};

static float s_dance_side = 1.0f;
static float s_face_timer = 0.0f;
static float s_face_cooldown = 0.0f;
static float s_spin_cooldown = 0.0f;
static float s_buildup_timer = 0.0f;
static float s_buildup_cooldown = 0.0f;
static float s_headbang_blend = 0.0f;
static float s_prev_bass = 0.0f;
static float s_prev_high = 0.0f;

static float s_rapid_hit_energy = 0.0f;
static float s_time_since_beat = 0.0f;
static float s_time_since_nod = 1.0f;
static int s_beat_count = 0;

typedef enum {
    FACE_NEUTRAL = 0,
    FACE_HAPPY,
    FACE_VIBE
} KoniFace;

typedef enum {
    DANCE_IDLE,
    DANCE_GROOVE_RIGHT,
    DANCE_GROOVE_LEFT,
    DANCE_DOUBLE_HIP,
    DANCE_SWAY_LOW,
    DANCE_BUILDUP,
    DANCE_HYPE
} DanceMode;

static DanceMode s_current_dance = DANCE_IDLE;
static float s_spin_target = 0.0f;
static bool s_was_drop = false;

// Leg smoothers
static float s_smooth_l_angle = 0.0f;
static float s_smooth_r_angle = 0.0f;
static float s_smooth_l_lift  = 0.0f;
static float s_smooth_r_lift  = 0.0f;
static float s_smooth_l_scale_y = 1.0f;
static float s_smooth_r_scale_y = 1.0f;
static float s_jump_tuck_side = 1.0f;
static int   s_jump_type = 0;

// Dynamic tempo (chunk-based variable BPM & autocorrelation) tracking state
#define DANCER_FLUX_BINS 160
static float s_flux_history[DANCER_FLUX_BINS] = {0};
static int   s_flux_idx = 0;
static float s_flux_timer = 0.0f;
static float s_estimated_bpm = 120.0f;
static float s_smoothed_bpm = 120.0f;
static char s_last_path[1024] = {0};

// Song energy tracking
static float s_phrase_energy = 0.35f;
static float s_baseline_energy = 0.35f;
static float s_smoothed_energy = 0.35f;
static bool  s_is_climax = false;

typedef struct {
    float angle;
    float r, g, b;
    float alpha;
} Spotlight;

static StageSpark s_sparks[MAX_SPARKS];
static Spotlight  s_spots[NUM_SPOTLIGHTS];
static bool s_inited = false;

// Helper functions
static inline Vector2 rot_pt(Vector2 p, Vector2 pivot, float angle_rad) {
    float s = sinf(angle_rad);
    float c = cosf(angle_rad);
    float dx = p.x - pivot.x;
    float dy = p.y - pivot.y;
    return (Vector2){ pivot.x + dx * c - dy * s, pivot.y + dx * s + dy * c };
}

static void DrawPolyStroke(const Vector2* pts, int count, float thick, Color c) {
    for (int i = 0; i < count; i++) {
        DrawLineEx(pts[i], pts[(i + 1) % count], thick, c);
    }
}

static void DrawTriangleSafe(Vector2 v1, Vector2 v2, Vector2 v3, Color col) {
    // In screen coordinates (Y down), counter-clockwise winding requires
    // (x2 - x1)*(y3 - y1) - (y2 - y1)*(x3 - x1) < 0
    // If clockwise, swap v2 and v3
    float cross = (v2.x - v1.x) * (v3.y - v1.y) - (v2.y - v1.y) * (v3.x - v1.x);
    if (cross > 0.0f) {
        DrawTriangle(v1, v3, v2, col);
    } else {
        DrawTriangle(v1, v2, v3, col);
    }
}

static void DrawQuadFill(Vector2 p1, Vector2 p2, Vector2 p3, Vector2 p4, Color c) {
    DrawTriangleSafe(p1, p4, p2, c);
    DrawTriangleSafe(p2, p4, p3, c);
}

static void spawn_spark(Vector2 pos, Vector2 vel, float size, float decay, Color col, bool gravity) {
    for (int i = 0; i < MAX_SPARKS; i++) {
        if (s_sparks[i].life <= 0.0f) {
            s_sparks[i].pos = pos;
            s_sparks[i].vel = vel;
            s_sparks[i].size = size;
            s_sparks[i].life = 1.0f;
            s_sparks[i].decay = decay;
            s_sparks[i].color = col;
            s_sparks[i].has_gravity = gravity;
            break;
        }
    }
}

static void trigger_floor_pyro(float cx, float floor_y, float power, Color col) {
    int count = 6 + (int)(power * 8.0f);
    float origins[2] = { cx - 42.0f, cx + 42.0f };
    for (int side = 0; side < 2; side++) {
        for (int k = 0; k < count; k++) {
            float vx = ((float)(rand() % 100) - 50.0f) * 1.6f;
            float vy = -180.0f - ((float)(rand() % 150)) * (0.8f + power * 0.6f);
            float sz = 2.0f + ((float)(rand() % 100) / 100.0f) * 2.5f;
            float decay = 1.8f + ((float)(rand() % 100) / 100.0f) * 1.4f;
            spawn_spark((Vector2){ origins[side], floor_y - 2.0f }, (Vector2){ vx, vy }, sz, decay, col, true);
        }
    }
}

static void trigger_aerial_firework(Vector2 origin, Color col) {
    int count = 8 + (rand() % 6);
    for (int k = 0; k < count; k++) {
        float ang = ((float)k / (float)count) * 2.0f * (float)PI + ((float)(rand() % 100) / 200.0f);
        float spd = 70.0f + (float)(rand() % 110);
        float sz = 2.4f + ((float)(rand() % 100) / 100.0f) * 2.0f;
        float decay = 2.0f + ((float)(rand() % 100) / 100.0f) * 1.4f;
        spawn_spark(origin, (Vector2){ cosf(ang) * spd, sinf(ang) * spd }, sz, decay, col, false);
    }
}

static void init_stage(void) {
    for (int i = 0; i < MAX_SPARKS; i++) s_sparks[i].life = 0.0f;
    for (int i = 0; i < NUM_SPOTLIGHTS; i++) {
        s_spots[i].angle = 0.0f;
        s_spots[i].r = 80.0f;
        s_spots[i].g = 160.0f;
        s_spots[i].b = 240.0f;
        s_spots[i].alpha = 0.05f;
    }
    s_inited = true;
}

void vis_dancer_render(Rectangle b, float dt) {
    if (!s_inited) init_stage();

    // Clamp to prevent NaN/Infinity
    if (dt > 0.05f) dt = 0.05f;
    if (dt <= 0.0001f) return;

    if (isnan(s_spr_squash.val) || isinf(s_spr_squash.val)) {
        s_spr_body_y.val = s_spr_body_y.vel = 0.0f;
        s_spr_squash.val = 1.0f; s_spr_squash.vel = 0.0f;
        s_spr_sway.val = s_spr_sway.vel = 0.0f;
        s_spr_hair_l.val = s_spr_hair_l.vel = 0.0f;
        s_spr_hair_r.val = s_spr_hair_r.vel = 0.0f;
        s_spr_arm_l.val = s_spr_arm_l.vel = 0.0f;
        s_spr_arm_r.val = s_spr_arm_r.vel = 0.0f;
        s_spr_face_y.val = s_spr_face_y.vel = 0.0f;
        s_spr_ahoge.val = s_spr_ahoge.vel = 0.0f;
        s_spr_spin.val = s_spr_spin.vel = 0.0f;
    }

    PlayState st = (PlayState)atomic_load(&play_state_atomic);
    bool is_playing = (st == STATE_PLAYING);

    // Detect track change and check for tracker BPM metadata
    pthread_mutex_lock(&state_mutex);
    uint32_t tot_sec = atomic_load(&p_total_sec);
    uint32_t cur_sec = atomic_load(&p_current_sec);

    if (strncmp(s_last_path, playing_filepath, sizeof(s_last_path)) != 0) {
        strncpy(s_last_path, playing_filepath, sizeof(s_last_path) - 1);
        s_estimated_bpm = 120.0f;
        s_smoothed_bpm = 120.0f;
        s_time_since_beat = 0.0f;
        s_time_since_nod = 1.0f;
        s_face_timer = 0.0f;
        s_face_cooldown = 0.0f;
        s_spin_cooldown = 0.0f;
        s_rapid_hit_energy = 0.0f;
        s_was_drop = false;
        s_current_dance = DANCE_IDLE;
        s_buildup_timer = 0.0f;
        s_buildup_cooldown = 0.0f;
        s_headbang_blend = 0.0f;
        s_phrase_energy = 0.15f;
        s_baseline_energy = 0.15f;
        s_smoothed_energy = 0.15f;
        s_is_climax = false;
        memset(s_flux_history, 0, sizeof(s_flux_history));
        s_flux_idx = 0;
        s_flux_timer = 0.0f;

        if (active_codec && active_codec->get_interface && active_decoder) {
            KoniTrackerInterface *tracker = (KoniTrackerInterface*)active_codec->get_interface(active_decoder, KONI_IFACE_TRACKER);
            if (tracker) {
                uint32_t bpm = tracker->get_bpm(active_decoder);
                if (bpm >= 45 && bpm <= 300) {
                    s_estimated_bpm = (float)bpm;
                    s_smoothed_bpm = (float)bpm;
                }
            }
        }

        voyager_dna_check_update(s_last_path, tot_sec);
    }
    pthread_mutex_unlock(&state_mutex);

    float bass = 0.0f, mid = 0.0f, high = 0.0f;
    sparkles_vis_get_bands(&bass, &mid, &high);

    float bass_delta = bass - s_prev_bass;
    float high_delta = high - s_prev_high;
    s_prev_bass = bass;
    s_prev_high = high;

    float kick_drive = (bass_delta > 0.0f ? bass_delta * 1.6f : 0.0f) + (bass * 0.28f);
    float snare_drive = (high_delta > 0.0f ? high_delta * 1.4f : 0.0f) + (high * 0.22f);

    // Song Energy Analysis
    float inst_power = 0.30f * bass + 0.50f * mid + 0.20f * high + (kick_drive * 0.15f);

    // Dual-window tracking, ~1.5s phrase window vs. ~15s macro baseline
    s_phrase_energy += (inst_power - s_phrase_energy) * fminf(1.0f, dt * 1.8f);
    s_baseline_energy += (inst_power - s_baseline_energy) * fminf(1.0f, dt * 0.09f);

    float rel_ratio = (s_baseline_energy > 0.025f) ? (s_phrase_energy / s_baseline_energy) : 1.0f;

    // Query voyager background (16 sectors across full track)
    int sector_idx = (tot_sec > 0) ? (int)(((uint64_t)cur_sec * VOYAGER_SECTOR_COUNT) / tot_sec) : 0;
    if (sector_idx < 0) sector_idx = 0;
    if (sector_idx >= VOYAGER_SECTOR_COUNT) sector_idx = VOYAGER_SECTOR_COUNT - 1;

    float current_energy = 0.0f;
    if (voyager_dna_is_ready()) {
        VoyagerSectorDNA dna = voyager_dna_get_sector(sector_idx);
        current_energy = dna.energy * 0.60f + fminf(1.0f, s_phrase_energy * rel_ratio) * 0.40f;
    } else {
        current_energy = fminf(1.0f, s_phrase_energy * (rel_ratio > 1.15f ? 1.25f : 0.85f));
    }
    s_smoothed_energy += (current_energy - s_smoothed_energy) * fminf(1.0f, dt * 2.8f);

    // Song section classification
    if (!s_is_climax && (s_smoothed_energy >= 0.64f || (rel_ratio >= 1.30f && s_phrase_energy > 0.38f))) {
        s_is_climax = true;
    } else if (s_is_climax && (s_smoothed_energy < 0.48f && rel_ratio < 1.05f)) {
        s_is_climax = false;
    }

    // Chunk-Based variable BPM autocorrelation (2.5-second rolling window @ 60Hz)
    s_flux_timer += dt;
    float onset_flux = (bass_delta > 0.0f ? bass_delta * 2.0f : 0.0f) +
                       (high_delta > 0.0f ? high_delta * 1.2f : 0.0f) + (bass * 0.3f);

    while (s_flux_timer >= 0.01667f) {
        s_flux_timer -= 0.01667f;
        s_flux_history[s_flux_idx] = onset_flux;
        s_flux_idx = (s_flux_idx + 1) % DANCER_FLUX_BINS;
    }

    if (is_playing) {
        // Autocorrelation across musical lag range, ~65 to 195 BPM (lags 18 to 55 at 60Hz)
        int search_len = 85;
        float best_corr = 0.0f;
        int best_lag = 0;
        float sum_corr = 0.0f;
        int lag_count = 0;

        for (int lag = 18; lag <= 55; lag++) {
            float corr = 0.0f;
            for (int k = 0; k < search_len; k++) {
                int i1 = (s_flux_idx - 1 - k + DANCER_FLUX_BINS) % DANCER_FLUX_BINS;
                int i2 = (s_flux_idx - 1 - k - lag + DANCER_FLUX_BINS) % DANCER_FLUX_BINS;
                corr += s_flux_history[i1] * s_flux_history[i2];
            }
            sum_corr += corr;
            lag_count++;
            if (corr > best_corr) {
                best_corr = corr;
                best_lag = lag;
            }
        }

        // Peak confidence test
        float avg_corr = (lag_count > 0) ? (sum_corr / (float)lag_count) : 0.01f;
        if (best_lag > 0 && best_corr > avg_corr * 1.25f && best_corr > 0.08f) {
            float chunk_bpm = 3600.0f / (float)best_lag;
            // Octave fold, align into natural dance tempo range [75, 175]
            while (chunk_bpm < 75.0f) chunk_bpm *= 2.0f;
            while (chunk_bpm > 175.0f) chunk_bpm *= 0.5f;
            s_estimated_bpm += (chunk_bpm - s_estimated_bpm) * fminf(1.0f, dt * 3.2f);
        }
    }

    if (s_estimated_bpm < 70.0f) s_estimated_bpm = 70.0f;
    if (s_estimated_bpm > 185.0f) s_estimated_bpm = 185.0f;
    s_smoothed_bpm += (s_estimated_bpm - s_smoothed_bpm) * fminf(1.0f, dt * 2.5f);

    // Sensibility beat and nod triggering
    s_time_since_beat += dt;

    bool beat_hit = false;
    if (is_playing && s_time_since_beat >= 0.12f) {
        // Lowered thresholds to catch mid-heavy tracks without sub-bass
        if (kick_drive > 0.26f || snare_drive > 0.30f || (bass_delta > 0.07f && bass > 0.22f) || (mid > 0.65f && high_delta > 0.15f)) {
            beat_hit = true;
            s_time_since_beat = 0.0f;
        }
    }

    if (s_face_cooldown > 0.0f) s_face_cooldown -= dt;
    if (s_face_timer > 0.0f) s_face_timer -= dt;
    if (s_spin_cooldown > 0.0f) s_spin_cooldown -= dt;
    if (s_buildup_cooldown > 0.0f) s_buildup_cooldown -= dt;

    if (s_current_dance == DANCE_BUILDUP) {
        s_buildup_timer += dt;
        if (s_buildup_timer > 5.0f) {
            s_buildup_cooldown = 14.0f;
        }
    } else {
        s_buildup_timer = 0.0f;
    }

    s_time_since_nod += dt;

    float min_nod_interval = fmaxf(0.28f, (60.0f / s_smoothed_bpm) * 0.70f);

    if (beat_hit) {
        s_beat_count++;
        s_rapid_hit_energy += 1.0f;
        if (s_rapid_hit_energy >= 2.2f && s_face_cooldown <= 0.0f) {
            s_face_timer = 0.65f;
            s_face_cooldown = 2.0f;
        }
    }

    bool is_buildup = (rel_ratio > 1.45f && s_phrase_energy > 0.45f && s_buildup_cooldown <= 0.0f && s_buildup_timer < 5.0f);

    // State machine
    if (!is_playing) {
        s_current_dance = DANCE_IDLE;
    } else if (s_is_climax) {
        s_current_dance = DANCE_HYPE;
    } else if (is_buildup) {
        s_current_dance = DANCE_BUILDUP;
    } else if (s_smoothed_energy < 0.20f) {
        s_current_dance = DANCE_IDLE;
    } else {
        int routine_step = (s_beat_count / 8) % 4;
        if (routine_step == 0) s_current_dance = DANCE_GROOVE_RIGHT;
        else if (routine_step == 1) s_current_dance = DANCE_GROOVE_LEFT;
        else if (routine_step == 2) s_current_dance = DANCE_DOUBLE_HIP;
        else s_current_dance = DANCE_SWAY_LOW;
    }

    // 360 pirouette / drop jump
    bool is_drop = (rel_ratio > 1.45f && kick_drive > 0.40f && s_phrase_energy > 0.38f);
    bool is_flourish = (beat_hit && (s_beat_count % 32 == 0) && high_delta > 0.20f && s_current_dance >= DANCE_GROOVE_RIGHT);
    bool can_spin = (s_spin_cooldown <= 0.0f) && (fabsf(s_spr_spin.val - s_spin_target) < 0.25f);

    if ((is_drop || is_flourish) && can_spin && !s_was_drop) {
        s_jump_type = rand() % 3;
        s_jump_tuck_side = s_dance_side;
        if (is_drop) {
            s_spr_body_y.vel -= 580.0f;
            s_spr_squash.vel += 4.0f;
        }
        s_spin_target += 2.0f;
        s_spin_cooldown = 6.0f;
        s_was_drop = true;
    } else if (!is_drop && !is_flourish) {
        s_was_drop = false;
    }

    if (beat_hit) {
        if (s_time_since_nod >= min_nod_interval) {
            s_time_since_nod = 0.0f;
            float nod_mult = 0.85f + (s_smoothed_energy * 0.65f);
            s_dance_side *= -1.0f;

            float bounce_force = 140.0f * nod_mult;
            if (s_current_dance == DANCE_HYPE) bounce_force = 240.0f * nod_mult;
            else if (s_current_dance == DANCE_IDLE) bounce_force = 40.0f;
            else if (s_current_dance == DANCE_BUILDUP) bounce_force = 70.0f;

            s_spr_body_y.vel += bounce_force * 0.7f;
            s_spr_squash.vel -= bounce_force * 0.005f;

            // Headbang, amplified head bounce
            float head_force = bounce_force * 0.4f;
            if (s_current_dance == DANCE_HYPE || kick_drive > 0.38f) {
                head_force = bounce_force * 1.5f;
            }
            s_spr_face_y.vel += head_force;

            if (s_current_dance == DANCE_HYPE) {
                s_spr_arm_l.vel += bounce_force * 0.8f;
                s_spr_arm_r.vel -= bounce_force * 0.8f;
            } else {
                s_spr_arm_l.vel -= bounce_force * 0.2f;
                s_spr_arm_r.vel -= bounce_force * 0.2f;
            }

            s_spr_hair_l.vel += bounce_force * 1.5f;
            s_spr_hair_r.vel += bounce_force * 1.5f;
            s_spr_ahoge.vel += s_dance_side * bounce_force * 0.45f;
        }
    }

    s_rapid_hit_energy -= dt * 3.2f;
    if (s_rapid_hit_energy < 0.0f) s_rapid_hit_energy = 0.0f;

    if (!is_playing) s_dance_side = 1.0f;

    static float s_idle_time = 0.0f;
    s_idle_time += dt * (is_playing ? (s_smoothed_bpm / 80.0f) : 1.5f);

    update_spring(&s_spr_spin, s_spin_target, dt);

    float idle_sway = sinf(s_idle_time * 1.5f) * 2.0f;
    float idle_squash = 1.0f + sinf(s_idle_time * 3.0f + (float)M_PI) * 0.015f;

    float target_sway = idle_sway;
    if (s_current_dance == DANCE_HYPE) {
        target_sway += s_dance_side * (16.0f + s_smoothed_energy * 12.0f);
    } else if (s_current_dance == DANCE_SWAY_LOW) {
        target_sway += s_dance_side * (15.0f + s_smoothed_energy * 12.0f);
    } else if (s_current_dance == DANCE_GROOVE_LEFT || s_current_dance == DANCE_GROOVE_RIGHT || s_current_dance == DANCE_DOUBLE_HIP) {
        target_sway += s_dance_side * (12.0f + s_smoothed_energy * 10.0f);
    } else if (s_current_dance == DANCE_BUILDUP) {
        target_sway = sinf(s_idle_time * 25.0f) * 1.5f;
    }
    update_spring(&s_spr_sway, target_sway, dt);

    float target_squash = idle_squash;
    if (s_current_dance == DANCE_BUILDUP) target_squash = 0.85f;
    update_spring(&s_spr_body_y, 0.0f, dt);
    update_spring(&s_spr_squash, target_squash, dt);
    update_spring(&s_spr_face_y, 0.0f, dt);

    float sway = s_spr_sway.val;
    float hip_tilt = (s_current_dance == DANCE_DOUBLE_HIP) ? (sway * 0.65f) : (sway * 0.45f);
    float head_tilt = -hip_tilt * 0.65f + s_spr_sway.vel * 0.04f;

    // Smooth headbang tilt blend
    float target_hb = (s_current_dance == DANCE_HYPE) ? 1.0f : 0.0f;
    s_headbang_blend += (target_hb - s_headbang_blend) * fminf(1.0f, dt * 6.0f);
    head_tilt += s_spr_face_y.val * 1.6f * s_headbang_blend;

    float world_head_rot = hip_tilt + head_tilt;

    float hair_float_l = sinf(s_idle_time * 2.2f) * 8.0f;
    float hair_float_r = sinf(s_idle_time * 2.2f + 1.5f) * 8.0f;
    update_spring(&s_spr_hair_l, -world_head_rot + hair_float_l, dt);
    update_spring(&s_spr_hair_r, -world_head_rot + hair_float_r, dt);

    s_spr_hair_l.vel -= s_spr_sway.vel * 3.5f * dt;
    s_spr_hair_r.vel -= s_spr_sway.vel * 3.5f * dt;

    float target_arm_l = 0.0f, target_arm_r = 0.0f;
    if (s_current_dance == DANCE_HYPE) {
        target_arm_l = 75.0f + sinf(s_idle_time * 3.0f) * 6.0f;
        target_arm_r = -75.0f - sinf(s_idle_time * 3.0f) * 6.0f;
    } else if (s_current_dance == DANCE_BUILDUP) {
        target_arm_l = -15.0f;
        target_arm_r = 15.0f;
    } else if (s_current_dance == DANCE_SWAY_LOW) {
        target_arm_l = s_spr_sway.val * 1.5f + sinf(s_idle_time * 2.0f) * 3.0f;
        target_arm_r = -s_spr_sway.val * 1.5f + sinf(s_idle_time * 2.0f + 1.0f) * 3.0f;
    } else {
        target_arm_l = s_spr_sway.val * 2.2f + sinf(s_idle_time * 2.0f) * 4.0f;
        target_arm_r = -s_spr_sway.val * 2.2f + sinf(s_idle_time * 2.0f + 1.0f) * 4.0f;
        if (s_current_dance == DANCE_GROOVE_LEFT || s_current_dance == DANCE_DOUBLE_HIP) target_arm_l = 0.0f;
        if (s_current_dance == DANCE_GROOVE_RIGHT || s_current_dance == DANCE_DOUBLE_HIP) target_arm_r = 0.0f;
    }
    update_spring(&s_spr_arm_l, target_arm_l, dt);
    update_spring(&s_spr_arm_r, target_arm_r, dt);

    if (s_spr_face_y.val < -1.8f) s_spr_face_y.val = -1.8f;
    if (s_spr_face_y.val > 6.8f) s_spr_face_y.val = 6.8f;

    float rad_tilt = hip_tilt * ((float)PI / 180.0f);
    float natural_hip_dip = (1.0f - cosf(rad_tilt)) * 48.0f;

    float hop_amp = 4.0f + (is_playing ? (s_smoothed_energy * 6.0f) : 0.0f);
    float idle_hop = -fabsf(sinf(s_idle_time * 2.0f)) * hop_amp;

    float total_body_y = s_spr_body_y.val + natural_hip_dip + idle_hop;

    float squash_y = s_spr_squash.val;
    if (squash_y < 0.7f) squash_y = 0.7f;
    if (squash_y > 1.3f) squash_y = 1.3f;
    float squash_x = 1.0f / squash_y;

    float l_arm_angle = s_spr_arm_l.val;
    float r_arm_angle = s_spr_arm_r.val;

    // Stage and motorized lightning
    float scale = fminf(b.width, b.height) / 360.0f;
    if (scale < 0.5f) scale = 0.5f;

    float char_base_x = b.x + b.width * 0.5f;
    float char_base_y = b.y + b.height * 0.5f;
    float floor_y = char_base_y + 111.5f * scale;
    float stage_floor = floor_y;
    float top_y = b.y + 4.0f;

    // Top ceiling truss rail
    DrawLineEx((Vector2){ b.x, top_y }, (Vector2){ b.x + b.width, top_y }, 2.0f, (Color){ 28, 30, 38, 255 });

    for (int i = 0; i < NUM_SPOTLIGHTS; i++) {
        float fx = b.x + b.width * (0.16f + 0.226f * (float)i);
        float target_angle = 0.0f;
        Color target_color = (Color){ 80, 160, 240, 255 };
        float target_alpha = 0.05f;

        if (s_current_dance == DANCE_BUILDUP) {
            float dx = char_base_x - fx;
            float dy = stage_floor - top_y;
            target_angle = atan2f(dx, dy) * (180.0f / (float)PI);
            target_color = WHITE;
            target_alpha = 0.16f + sinf(s_idle_time * 24.0f) * 0.06f;
        } else if (s_current_dance == DANCE_HYPE) {
            target_angle = sinf(s_idle_time * 4.5f + (float)i * 1.1f) * 42.0f;
            target_color = (i % 2 == 0) ? COLOR_KONI_PURPLE : COLOR_CYAN;
            target_alpha = 0.14f + bass * 0.12f;
        } else if (s_current_dance == DANCE_SWAY_LOW) {
            target_angle = sinf(s_idle_time * 1.8f + (float)i * 0.4f) * 22.0f;
            target_color = (i % 2 == 0) ? (Color){ 140, 160, 240, 255 } : (Color){ 190, 140, 230, 255 };
            target_alpha = 0.09f + mid * 0.05f;
        } else if (s_current_dance == DANCE_GROOVE_LEFT || s_current_dance == DANCE_GROOVE_RIGHT) {
            float dir = (i % 2 == 0) ? 1.0f : -1.0f;
            target_angle = dir * sinf(s_idle_time * 2.2f) * 28.0f;
            target_color = (i % 2 == 0) ? COLOR_CYAN : COLOR_KONI_PURPLE;
            target_alpha = 0.09f + mid * 0.04f;
        } else {
            target_angle = sinf(s_idle_time * 0.8f + (float)i * 0.9f) * 14.0f;
            target_color = (Color){ 80, 160, 240, 255 };
            target_alpha = 0.05f;
        }

        // Motorized head angle slew
        s_spots[i].angle += (target_angle - s_spots[i].angle) * fminf(1.0f, dt * 5.2f);

        // Color cross-fade (~300ms dimmer transition)
        float color_blend = fminf(1.0f, dt * 3.5f);
        s_spots[i].r += ((float)target_color.r - s_spots[i].r) * color_blend;
        s_spots[i].g += ((float)target_color.g - s_spots[i].g) * color_blend;
        s_spots[i].b += ((float)target_color.b - s_spots[i].b) * color_blend;
        s_spots[i].alpha += (target_alpha - s_spots[i].alpha) * color_blend;

        Color current_beam_col = {
            (unsigned char)fminf(255.0f, fmaxf(0.0f, s_spots[i].r)),
            (unsigned char)fminf(255.0f, fmaxf(0.0f, s_spots[i].g)),
            (unsigned char)fminf(255.0f, fmaxf(0.0f, s_spots[i].b)),
            255
        };

        float rad = s_spots[i].angle * ((float)PI / 180.0f);
        float dist_y = stage_floor - top_y;
        float gx = fx + tanf(rad) * dist_y;

        float top_w = 10.0f * scale;
        float bot_w = (65.0f + bass * 40.0f) * scale;

        // Volumetric beam quad
        Vector2 b1 = { fx - top_w * 0.5f, top_y };
        Vector2 b2 = { fx + top_w * 0.5f, top_y };
        Vector2 b3 = { gx + bot_w * 0.5f, stage_floor };
        Vector2 b4 = { gx - bot_w * 0.5f, stage_floor };

        Color col_beam = ColorAlpha(current_beam_col, s_spots[i].alpha);
        DrawQuadFill(b1, b2, b3, b4, col_beam);

        // Floor reflection
        DrawEllipse((int)gx, (int)stage_floor, bot_w * 0.55f, 7.0f * scale, ColorAlpha(current_beam_col, s_spots[i].alpha * 1.5f));

        // Ceiling fixture housing
        DrawRectangle((int)(fx - 10.0f), (int)top_y, 20, 6, (Color){ 18, 19, 24, 255 });
        DrawCircle((int)fx, (int)top_y + 6, 3.0f, current_beam_col);
    }

    // Stage pyros and micro-fireworks
    if (is_playing) {
        // Floor pyro fountains trigger on kicks
        if (beat_hit) {
            Color pyro_col = (s_current_dance == DANCE_HYPE) ? WHITE : COLOR_KONI_PURPLE;
            trigger_floor_pyro(char_base_x, stage_floor, s_smoothed_energy, pyro_col);
        }

        // Aerial micro-fireworks trigger on snare/hi-hat transients
        if (high_delta > 0.12f && high > 0.32f) {
            Vector2 f_origin = {
                b.x + b.width * 0.2f + ((float)(rand() % 100) / 100.0f) * (b.width * 0.6f),
                b.y + b.height * 0.14f + ((float)(rand() % 100) / 100.0f) * (b.height * 0.30f)
            };
            Color burst_col = (rand() % 2 == 0) ? COLOR_CYAN : COLOR_KONI_PURPLE;
            trigger_aerial_firework(f_origin, burst_col);
        }

        // Random fireworks in climax
        if (s_current_dance == DANCE_HYPE && ((rand() % 100) < 14)) {
            Vector2 f_origin = {
                b.x + ((float)(rand() % 100) / 100.0f) * b.width,
                b.y + b.height * 0.12f + ((float)(rand() % 100) / 100.0f) * (b.height * 0.32f)
            };
            trigger_aerial_firework(f_origin, WHITE);
        }
    }

    // Update and draw active sparks
    for (int i = 0; i < MAX_SPARKS; i++) {
        StageSpark *p = &s_sparks[i];
        if (p->life <= 0.0f) continue;

        p->pos.x += p->vel.x * dt;
        p->pos.y += p->vel.y * dt;
        p->life -= p->decay * dt;

        if (p->has_gravity) {
            p->vel.y += 360.0f * dt; // Gravity arc for floor pyro
            p->vel.x *= 0.98f;
        } else {
            p->vel.x *= 0.91f; // High air resistance for sky starbursts
            p->vel.y *= 0.91f;
        }

        if (p->life > 0.0f) {
            Color col = ColorAlpha(p->color, p->life);
            float sz = p->size * (0.5f + p->life * 0.5f);
            // Geometric diamonds
            DrawRectangle((int)(p->pos.x - sz * 0.5f), (int)(p->pos.y - sz * 0.5f), (int)sz, (int)sz, col);
        }
    }

    // Character vector rendering

    // Scale X-flip handles pirouette spins
    float spin_scale_x = cosf(s_spr_spin.val * (float)PI);
    if (fabsf(spin_scale_x) < 0.01f) spin_scale_x = 0.01f; // Prevent completely disappearing at dead center

    rlPushMatrix();
    rlTranslatef(char_base_x, 0.0f, 0.0f);
    rlScalef(spin_scale_x, 1.0f, 1.0f);
    rlTranslatef(-char_base_x, 0.0f, 0.0f);

    // Leg kinematics
    float target_l_angle = 0.0f;
    float target_r_angle = 0.0f;
    float target_l_lift  = 0.0f;
    float target_r_lift  = 0.0f;
    float target_l_scale_y = 1.0f;
    float target_r_scale_y = 1.0f;

    bool is_airborne = (total_body_y < -10.0f);

    if (is_airborne) {
        float airborne_lift = total_body_y + 10.0f;
        target_l_lift = airborne_lift;
        target_r_lift = airborne_lift;

        if (s_jump_type == 0) {
            if (s_jump_tuck_side > 0) {
                target_l_lift += -16.0f * scale;
                target_l_angle = 18.0f;
                target_l_scale_y = 0.80f;
                target_r_angle = -10.0f;
                target_r_scale_y = 1.05f;
            } else {
                target_r_lift += -16.0f * scale;
                target_r_angle = -18.0f;
                target_r_scale_y = 0.80f;
                target_l_angle = 10.0f;
                target_l_scale_y = 1.05f;
            }
        } else if (s_jump_type == 1) {
            target_l_angle = -22.0f;
            target_r_angle = 22.0f;
            target_l_scale_y = 1.08f;
            target_r_scale_y = 1.08f;
        } else {
            target_l_lift += -10.0f * scale;
            target_r_lift += -10.0f * scale;
            target_l_angle = 8.0f * s_jump_tuck_side;
            target_r_angle = 8.0f * s_jump_tuck_side;
            target_l_scale_y = 0.85f;
            target_r_scale_y = 0.85f;
        }
    } else if (is_playing) {
        if (sway > 0.0f) {
            target_r_angle = sway * 0.35f;
            target_l_angle = sway * 0.85f;
        } else {
            target_l_angle = sway * 0.35f;
            target_r_angle = sway * 0.85f;
        }

        float knee_flex = (1.0f - squash_y) * 26.0f;
        if (knee_flex < 0.0f) knee_flex = 0.0f;
        target_l_angle -= knee_flex;
        target_r_angle += knee_flex;

        if (s_current_dance == DANCE_GROOVE_LEFT || s_current_dance == DANCE_GROOVE_RIGHT || s_current_dance == DANCE_DOUBLE_HIP) {
            float tap_cycle = sinf(s_idle_time * 3.0f);
            if (tap_cycle > 0.45f) {
                float tap = (tap_cycle - 0.45f) / 0.55f;
                target_l_lift = -tap * 6.0f * scale;
                target_l_angle += tap * 8.0f;
            } else if (tap_cycle < -0.45f) {
                float tap = (-tap_cycle - 0.45f) / 0.55f;
                target_r_lift = -tap * 6.0f * scale;
                target_r_angle -= tap * 8.0f;
            }
        }
    } else {
        target_l_angle = -2.5f;
        target_r_angle = 2.5f;
    }

    // Continuous interpolation
    float leg_blend = fminf(1.0f, dt * 18.0f);
    s_smooth_l_angle   += (target_l_angle   - s_smooth_l_angle)   * leg_blend;
    s_smooth_r_angle   += (target_r_angle   - s_smooth_r_angle)   * leg_blend;
    s_smooth_l_lift    += (target_l_lift    - s_smooth_l_lift)    * leg_blend;
    s_smooth_r_lift    += (target_r_lift    - s_smooth_r_lift)    * leg_blend;
    s_smooth_l_scale_y += (target_l_scale_y - s_smooth_l_scale_y) * leg_blend;
    s_smooth_r_scale_y += (target_r_scale_y - s_smooth_r_scale_y) * leg_blend;

    float l_floor_y = floor_y + s_smooth_l_lift;
    float r_floor_y = floor_y + s_smooth_r_lift;

    // Left leg
    rlPushMatrix();
    rlTranslatef(char_base_x - 17.3f * scale, l_floor_y, 0.0f);
    rlRotatef(s_smooth_l_angle, 0, 0, 1);
    rlScalef(scale, scale * s_smooth_l_scale_y, 1.0f);
    rlTranslatef(17.3f, -111.5f, 0.0f);
    Vector2 l_leg_pts[] = {{-21.84f, 64.14f}, {-12.54f, 64.44f}, {-2.52f, 111.26f}, {-26.80f, 110.60f}};
    DrawQuadFill(l_leg_pts[0], l_leg_pts[1], l_leg_pts[2], l_leg_pts[3], COLOR_CUTOUT);
    DrawPolyStroke(l_leg_pts, 4, 2.1f, COLOR_KONI_PURPLE);
    DrawQuadFill((Vector2){-25.89f, 105.29f}, (Vector2){-4.14f, 105.53f}, (Vector2){-3.15f, 110.60f}, (Vector2){-26.10f, 110.06f}, COLOR_KONI_PURPLE);
    DrawEllipse(-17.30f, 57.40f, 2.8f, 2.7f, COLOR_KONI_PURPLE); // L Joint Dot
    rlPopMatrix();

    // Right leg
    rlPushMatrix();
    rlTranslatef(char_base_x - 1.51f * scale, r_floor_y, 0.0f);
    rlRotatef(s_smooth_r_angle, 0, 0, 1);
    rlScalef(scale, scale * s_smooth_r_scale_y, 1.0f);
    rlTranslatef(1.51f, -111.5f, 0.0f);
    Vector2 r_leg_pts[] = {{-3.59f, 64.32f}, {5.59f, 63.66f}, {32.47f, 109.57f}, {9.46f, 111.20f}};
    DrawQuadFill(r_leg_pts[0], r_leg_pts[1], r_leg_pts[2], r_leg_pts[3], COLOR_CUTOUT);
    DrawPolyStroke(r_leg_pts, 4, 2.1f, COLOR_KONI_PURPLE);
    DrawQuadFill((Vector2){8.05f, 105.42f}, (Vector2){28.59f, 104.02f}, (Vector2){31.32f, 108.84f}, (Vector2){9.49f, 110.76f}, COLOR_KONI_PURPLE);
    DrawEllipse(-1.51f, 57.10f, 2.8f, 2.7f, COLOR_KONI_PURPLE); // R Joint Dot
    rlPopMatrix();

    // Root frame, hips sway, dip, bounce, squash
    rlPushMatrix();
    rlTranslatef(char_base_x + sway, char_base_y + total_body_y, 0.0f);
    rlScalef(scale * squash_x, scale * squash_y, 1.0f);
    
    rlRotatef(hip_tilt, 0, 0, 1);

    bool l_arm_on_hip = (s_current_dance == DANCE_GROOVE_LEFT || s_current_dance == DANCE_DOUBLE_HIP);
    bool r_arm_on_hip = (s_current_dance == DANCE_GROOVE_RIGHT || s_current_dance == DANCE_DOUBLE_HIP);

    // Left arm straight (behind dress)
    if (!l_arm_on_hip) {
        rlPushMatrix();
        rlTranslatef(-19.62f, 5.26f, 0.0f);
        rlRotatef(l_arm_angle, 0, 0, 1);
        rlTranslatef(19.62f, -5.26f, 0.0f);
        Vector2 l_arm_pts[] = {{-28.03f, 11.16f}, {-19.89f, 14.79f}, {-32.95f, 61.14f}, {-54.08f, 51.01f}};
        DrawQuadFill(l_arm_pts[0], l_arm_pts[1], l_arm_pts[2], l_arm_pts[3], COLOR_CUTOUT);
        DrawPolyStroke(l_arm_pts, 4, 2.1f, COLOR_KONI_PURPLE);
        DrawQuadFill((Vector2){-50.66f, 46.74f}, (Vector2){-31.88f, 55.37f}, (Vector2){-33.39f, 60.50f}, (Vector2){-53.26f, 50.78f}, COLOR_KONI_PURPLE);
        rlPopMatrix();
        DrawEllipse(-19.62f, 5.26f, 2.7f, 2.8f, COLOR_KONI_PURPLE);
    }

    // Torso/dress
    Vector2 torso_pts[] = {{-10.66f, 0.45f}, {4.67f, 2.37f}, {4.63f, 22.40f}, {15.39f, 53.58f}, {-37.28f, 45.94f}, {-16.69f, 19.84f}};
    DrawQuadFill(torso_pts[0], torso_pts[1], torso_pts[2], torso_pts[5], COLOR_CUTOUT);
    DrawQuadFill(torso_pts[5], torso_pts[2], torso_pts[3], torso_pts[4], COLOR_CUTOUT);
    DrawPolyStroke(torso_pts, 6, 2.1f, COLOR_KONI_PURPLE);
    DrawLineEx((Vector2){-33.93f, 42.69f}, (Vector2){13.92f, 49.48f}, 2.1f, COLOR_KONI_PURPLE);
    DrawEllipse(-4.09f, 10.45f, 3.58f, 3.57f, COLOR_KONI_PURPLE);

    // Left arm on hip (over dress)
    if (l_arm_on_hip) {
        rlPushMatrix();
        rlTranslatef(-19.62f, 5.26f, 0.0f);
        rlRotatef(l_arm_angle, 0, 0, 1);
        rlTranslatef(19.62f, -5.26f, 0.0f);
        Vector2 l_arm_pts_bent[] = {{-20.09f, 12.32f}, {-26.57f, 6.13f}, {-46.25f, 21.79f}, {-35.16f, 51.94f}, {-15.42f, 40.61f}, {-28.61f, 23.39f}};
        DrawQuadFill(l_arm_pts_bent[0], l_arm_pts_bent[1], l_arm_pts_bent[2], l_arm_pts_bent[5], COLOR_CUTOUT);
        DrawQuadFill(l_arm_pts_bent[5], l_arm_pts_bent[2], l_arm_pts_bent[3], l_arm_pts_bent[4], COLOR_CUTOUT);
        DrawPolyStroke(l_arm_pts_bent, 6, 2.1f, COLOR_KONI_PURPLE);
        DrawQuadFill((Vector2){-19.14f, 36.07f}, (Vector2){-36.63f, 46.19f}, (Vector2){-34.76f, 51.17f}, (Vector2){-16.09f, 40.14f}, COLOR_KONI_PURPLE);
        DrawQuadFill((Vector2){-22.32f, 14.38f}, (Vector2){-28.78f, 8.46f}, (Vector2){-26.24f, 6.25f}, (Vector2){-20.56f, 11.78f}, COLOR_KONI_PURPLE);
        rlPopMatrix();
        DrawEllipse(-19.62f, 5.26f, 2.7f, 2.8f, COLOR_KONI_PURPLE);
    }

    // Right arm (over dress)
    rlPushMatrix();
    rlTranslatef(10.37f, 4.58f, 0.0f);
    rlRotatef(r_arm_angle, 0, 0, 1);
    rlTranslatef(-10.37f, -4.58f, 0.0f);
    if (r_arm_on_hip) {
        Vector2 r_arm_pts_bent[] = {{10.82f, 11.64f}, {17.29f, 5.46f}, {36.96f, 21.11f}, {25.88f, 51.26f}, {6.15f, 39.93f}, {19.34f, 22.72f}};
        DrawQuadFill(r_arm_pts_bent[0], r_arm_pts_bent[1], r_arm_pts_bent[2], r_arm_pts_bent[5], COLOR_CUTOUT);
        DrawQuadFill(r_arm_pts_bent[5], r_arm_pts_bent[2], r_arm_pts_bent[3], r_arm_pts_bent[4], COLOR_CUTOUT);
        DrawPolyStroke(r_arm_pts_bent, 6, 2.1f, COLOR_KONI_PURPLE);
        DrawQuadFill((Vector2){9.88f, 35.38f}, (Vector2){27.37f, 45.49f}, (Vector2){25.50f, 50.47f}, (Vector2){6.84f, 39.45f}, COLOR_KONI_PURPLE);
        DrawQuadFill((Vector2){13.03f, 14.70f}, (Vector2){19.49f, 8.78f}, (Vector2){16.95f, 6.57f}, (Vector2){11.27f, 12.10f}, COLOR_KONI_PURPLE);
    } else {
        Vector2 r_arm_pts[] = {{18.77f, 11.48f}, {10.63f, 15.12f}, {23.70f, 61.48f}, {44.84f, 51.34f}};
        DrawQuadFill(r_arm_pts[0], r_arm_pts[1], r_arm_pts[2], r_arm_pts[3], COLOR_CUTOUT);
        DrawPolyStroke(r_arm_pts, 4, 2.1f, COLOR_KONI_PURPLE);
        DrawQuadFill((Vector2){41.39f, 47.07f}, (Vector2){22.60f, 55.71f}, (Vector2){24.11f, 60.85f}, (Vector2){43.99f, 51.12f}, COLOR_KONI_PURPLE);
        DrawQuadFill((Vector2){20.10f, 14.62f}, (Vector2){11.98f, 18.85f}, (Vector2){11.38f, 15.01f}, (Vector2){18.77f, 12.06f}, COLOR_KONI_PURPLE);
    }
    rlPopMatrix();
    DrawEllipse(10.37f, 4.58f, 2.7f, 2.8f, COLOR_KONI_PURPLE);

    // Head and hair, pivoting subtly on the neck axis (invisible axis)
    rlPushMatrix();
    rlTranslatef(0.0f, -5.0f, 0.0f); // Neck axis
    rlRotatef(head_tilt, 0, 0, 1);    // Tilt along the sway angle

    float h_squash_y = 1.0f / sqrtf(squash_y);
    float h_squash_x = 1.0f / h_squash_y;
    rlScalef(h_squash_x, h_squash_y, 1.0f);

    DrawEllipse(0.03f, -38.50f, 35.6f, 35.0f, COLOR_KONI_PURPLE); // Main Head Circle

    // Left circle tail (inertial sway via spring)
    rlPushMatrix();
    rlTranslatef(-30.0f, -40.0f, 0.0f);
    rlRotatef(s_spr_hair_l.val, 0, 0, 1);
    DrawEllipse(-13.83f, -28.58f, 16.35f, 16.50f, COLOR_KONI_PURPLE);
    rlPopMatrix();

    // Right circle tail (inertial sway via spring)
    rlPushMatrix();
    rlTranslatef(30.0f, -40.0f, 0.0f);
    rlRotatef(s_spr_hair_r.val, 0, 0, 1);
    DrawEllipse(19.18f, -21.38f, 16.35f, 16.50f, COLOR_KONI_PURPLE);
    rlPopMatrix();

    // Tufts
    DrawTriangleSafe((Vector2){-35.35f, -36.33f}, (Vector2){-31.33f, 2.03f}, (Vector2){-25.70f, -16.25f}, COLOR_KONI_PURPLE);
    DrawTriangleSafe((Vector2){23.09f, -15.06f}, (Vector2){27.44f, 3.56f}, (Vector2){34.19f, -29.67f}, COLOR_KONI_PURPLE);

    // Ahoge
    rlPushMatrix();
    rlTranslatef(0.0f, -70.0f, 0.0f);
    rlRotatef(s_spr_body_y.vel * -0.1f + high * 15.0f, 0, 0, 1);
    DrawTriangleSafe((Vector2){6.58f, -4.22f}, (Vector2){17.99f, -20.40f}, (Vector2){19.67f, -12.55f}, COLOR_KONI_PURPLE);
    rlPopMatrix();

    // Face cutouts (nodding inside the head along Y)
    rlPushMatrix();
    rlTranslatef(0.0f, s_spr_face_y.val, 0.0f);

    KoniFace active_face = (s_face_timer > 0.0f || s_current_dance == DANCE_HYPE) ? FACE_HAPPY : (s_current_dance >= DANCE_GROOVE_RIGHT ? FACE_VIBE : FACE_NEUTRAL);

    switch (active_face) {
        case FACE_HAPPY:
            // > < Eyes
            DrawLineEx((Vector2){-22.14f, -35.60f}, (Vector2){-8.79f, -27.99f}, 2.6f, COLOR_CUTOUT);
            DrawLineEx((Vector2){-8.79f, -27.99f}, (Vector2){-22.87f, -23.16f}, 2.6f, COLOR_CUTOUT);
            DrawLineEx((Vector2){23.66f, -34.33f}, (Vector2){9.22f, -27.26f}, 2.6f, COLOR_CUTOUT);
            DrawLineEx((Vector2){9.22f, -27.26f}, (Vector2){23.12f, -21.10f}, 2.6f, COLOR_CUTOUT);
            // v mouth
            DrawLineEx((Vector2){-4.88f, -19.47f}, (Vector2){-0.55f, -15.48f}, 1.6f, COLOR_CUTOUT);
            DrawLineEx((Vector2){-0.55f, -15.48f}, (Vector2){4.00f, -19.16f}, 1.6f, COLOR_CUTOUT);
            break;

        case FACE_VIBE:
            // \ / eyes
            DrawLineEx((Vector2){-22.48f, -27.38f}, (Vector2){-7.08f, -32.65f}, 2.6f, COLOR_CUTOUT);
            DrawLineEx((Vector2){7.30f, -32.29f}, (Vector2){22.32f, -27.25f}, 2.6f, COLOR_CUTOUT);
            // v mouth
            DrawLineEx((Vector2){-4.88f, -19.47f}, (Vector2){-0.55f, -15.48f}, 1.6f, COLOR_CUTOUT);
            DrawLineEx((Vector2){-0.55f, -15.48f}, (Vector2){4.00f, -19.16f}, 1.6f, COLOR_CUTOUT);
            break;

        case FACE_NEUTRAL:
        default:
            // | | eyes
            DrawQuadFill((Vector2){-16.46f, -42.13f}, (Vector2){-10.18f, -41.95f}, (Vector2){-10.66f, -22.56f}, (Vector2){-16.88f, -22.56f}, COLOR_CUTOUT);
            DrawQuadFill((Vector2){10.24f, -41.16f}, (Vector2){16.52f, -40.98f}, (Vector2){16.04f, -21.59f}, (Vector2){9.82f, -21.59f}, COLOR_CUTOUT);
            // v mouth
            DrawLineEx((Vector2){-4.88f, -19.47f}, (Vector2){-0.55f, -15.48f}, 1.6f, COLOR_CUTOUT);
            DrawLineEx((Vector2){-0.55f, -15.48f}, (Vector2){4.00f, -19.16f}, 1.6f, COLOR_CUTOUT);
            break;
    }

    rlPopMatrix(); // Pop face translation matrix
    rlPopMatrix(); // Pop head/neck matrix
    rlPopMatrix(); // Pop root hip/torso matrix
    rlPopMatrix(); // Pop master spin matrix

    // Watermark and artist attribution
    const char *prefix_str = "original character Koni, style based on work by ";
    const char *user_str = "@takawoyu";

    int pre_w = MeasureSparklesText(prefix_str, 10);
    int user_w = MeasureSparklesText(user_str, 10);
    int total_w = pre_w + user_w;

    float tag_x = b.x + (b.width - total_w) * 0.5f;
    float tag_y = b.y + b.height - 18.0f;

    DrawSparklesText(prefix_str, (int)tag_x, (int)tag_y, 10, ColorAlpha(COLOR_TEXT_DARK, 0.75f));

    Rectangle user_rect = { tag_x + pre_w, tag_y - 2.0f, (float)user_w, 14.0f };
    Vector2 mouse = GetMousePosition();
    bool is_hover = CheckCollisionPointRec(mouse, user_rect);

    Color user_col = is_hover ? COLOR_CYAN : ColorAlpha(COLOR_TEXT_MUTED, 0.85f);
    DrawSparklesText(user_str, (int)(tag_x + pre_w), (int)tag_y, 10, user_col);

    if (is_hover) {
        DrawLineEx((Vector2){ tag_x + pre_w, tag_y + 11.0f },
                   (Vector2){ tag_x + total_w, tag_y + 11.0f }, 1.0f, COLOR_CYAN);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            OpenURL("https://x.com/takawoyu");
        }
    }
}