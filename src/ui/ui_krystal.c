#define _DEFAULT_SOURCE
#include "ui_krystal.h"
#include "krystal_engine.h"
#include "ui_common.h"
#include "ui_status.h"
#include <ncurses.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#define NUM_SECTIONS 6

static bool s_krystal_active = false;
static int s_selected_col = 0;
static int s_selected_row = 0;
static int s_col_scroll = 0;
static bool s_arena_active = false;

void ui_krystal_init(void) {
    s_krystal_active = false;
    s_selected_col = 0;
    s_selected_row = 0;
    s_col_scroll = 0;
    s_arena_active = false;
}

bool ui_krystal_is_active(void) {
    return s_krystal_active;
}

void ui_krystal_toggle(void) {
    s_krystal_active = !s_krystal_active;
    force_redraw = true;
}

void ui_krystal_close(void) {
    s_krystal_active = false;
    force_redraw = true;
}

static int get_max_row_for_col(int col) {
    switch (col) {
        case 0: return 4;
        case 1: return 8;
        case 2: return 8;
        case 3: return 8;
        case 4: return 8;
        case 5: return 6;
        default: return 0;
    }
}

static void adjust_active_param(float delta, bool coarse) {
    KrystalConfig cfg;
    krystal_get_config(&cfg);
    float step = coarse ? 3.0f : 1.0f;

    switch (s_selected_col) {
        case 0: // Loudness & Tilt 
            if (s_selected_row == 0) cfg.loudness.enabled = !cfg.loudness.enabled;
            else if (s_selected_row == 1) cfg.loudness.mode = (cfg.loudness.mode == LOUDNESS_MODE_DYNAMIC) ? LOUDNESS_MODE_FIXED : LOUDNESS_MODE_DYNAMIC;
            else if (s_selected_row == 2) {
                cfg.loudness.intensity += delta * 0.05f * step;
                if (cfg.loudness.intensity < 0.0f) cfg.loudness.intensity = 0.0f;
                if (cfg.loudness.intensity > 1.0f) cfg.loudness.intensity = 1.0f;
            } else if (s_selected_row == 3) {
                cfg.loudness.ref_vol += delta * 0.05f * step;
                if (cfg.loudness.ref_vol < 0.50f) cfg.loudness.ref_vol = 0.50f;
                if (cfg.loudness.ref_vol > 1.00f) cfg.loudness.ref_vol = 1.00f;
            } else if (s_selected_row == 4) {
                cfg.spectral.tilt_db += delta * 0.25f * step;
                if (cfg.spectral.tilt_db < -6.0f) cfg.spectral.tilt_db = -6.0f;
                if (cfg.spectral.tilt_db > +6.0f) cfg.spectral.tilt_db = +6.0f;
            }
            break;

        case 1: // Spatial 3D 
            if (s_selected_row == 0) cfg.spatial.enabled = !cfg.spatial.enabled;
            else if (s_selected_row == 1) {
                cfg.spatial.azimuth_deg += delta * 5.0f * step;
                if (cfg.spatial.azimuth_deg < -180.0f) cfg.spatial.azimuth_deg += 360.0f;
                if (cfg.spatial.azimuth_deg > +180.0f) cfg.spatial.azimuth_deg -= 360.0f;
            } else if (s_selected_row == 2) {
                cfg.spatial.elevation_deg += delta * 2.5f * step;
                if (cfg.spatial.elevation_deg < -45.0f) cfg.spatial.elevation_deg = -45.0f;
                if (cfg.spatial.elevation_deg > +90.0f) cfg.spatial.elevation_deg = +90.0f;
            } else if (s_selected_row == 3) {
                cfg.spatial.distance_m += delta * 0.10f * step;
                if (cfg.spatial.distance_m < 0.5f) cfg.spatial.distance_m = 0.5f;
                if (cfg.spatial.distance_m > 5.0f) cfg.spatial.distance_m = 5.0f;
            } else if (s_selected_row == 4) {
                cfg.spatial.stage_angle_deg += delta * 5.0f * step;
                if (cfg.spatial.stage_angle_deg < 20.0f) cfg.spatial.stage_angle_deg = 20.0f;
                if (cfg.spatial.stage_angle_deg > 140.0f) cfg.spatial.stage_angle_deg = 140.0f;
            } else if (s_selected_row == 5) {
                cfg.spatial.room_refl += delta * 0.05f * step;
                if (cfg.spatial.room_refl < 0.0f) cfg.spatial.room_refl = 0.0f;
                if (cfg.spatial.room_refl > 0.80f) cfg.spatial.room_refl = 0.80f;
            } else if (s_selected_row == 6) {
                cfg.spatial.center_gain_db += delta * 0.25f * step;
                if (cfg.spatial.center_gain_db < -6.0f) cfg.spatial.center_gain_db = -6.0f;
                if (cfg.spatial.center_gain_db > +6.0f) cfg.spatial.center_gain_db = +6.0f;
            } else if (s_selected_row == 7) {
                cfg.spatial.mono_cut_hz += delta * 5.0f * step;
                if (cfg.spatial.mono_cut_hz < 60.0f) cfg.spatial.mono_cut_hz = 60.0f;
                if (cfg.spatial.mono_cut_hz > 300.0f) cfg.spatial.mono_cut_hz = 300.0f;
            } else if (s_selected_row == 8) {
                cfg.spatial.safety_limit += delta * 0.05f * step;
                if (cfg.spatial.safety_limit < 0.0f) cfg.spatial.safety_limit = 0.0f;
                if (cfg.spatial.safety_limit > 0.60f) cfg.spatial.safety_limit = 0.60f;
            }
            break;

        case 2: // Bass & Sub 
            if (s_selected_row == 0) cfg.bass.enabled = !cfg.bass.enabled;
            else if (s_selected_row == 1) {
                cfg.bass.cutoff_hz += delta * 2.5f * step;
                if (cfg.bass.cutoff_hz < 40.0f) cfg.bass.cutoff_hz = 40.0f;
                if (cfg.bass.cutoff_hz > 140.0f) cfg.bass.cutoff_hz = 140.0f;
            } else if (s_selected_row == 2) {
                cfg.bass.intensity += delta * 0.05f * step;
                if (cfg.bass.intensity < 0.0f) cfg.bass.intensity = 0.0f;
                if (cfg.bass.intensity > 1.0f) cfg.bass.intensity = 1.0f;
            } else if (s_selected_row == 3) {
                cfg.bass.mix += delta * 0.05f * step;
                if (cfg.bass.mix < 0.0f) cfg.bass.mix = 0.0f;
                if (cfg.bass.mix > 1.0f) cfg.bass.mix = 1.0f;
            } else if (s_selected_row == 4) {
                cfg.bass.sub_weight += delta * 0.05f * step;
                if (cfg.bass.sub_weight < 0.0f) cfg.bass.sub_weight = 0.0f;
                if (cfg.bass.sub_weight > 1.0f) cfg.bass.sub_weight = 1.0f;
            } else if (s_selected_row == 5) {
                cfg.bass.harmonic_tone += delta * 0.05f * step;
                if (cfg.bass.harmonic_tone < 0.0f) cfg.bass.harmonic_tone = 0.0f;
                if (cfg.bass.harmonic_tone > 1.0f) cfg.bass.harmonic_tone = 1.0f;
            } else if (s_selected_row == 6) {
                cfg.bass.sub_octave += delta * 0.05f * step;
                if (cfg.bass.sub_octave < 0.0f) cfg.bass.sub_octave = 0.0f;
                if (cfg.bass.sub_octave > 1.0f) cfg.bass.sub_octave = 1.0f;
            } else if (s_selected_row == 7) {
                cfg.bass.sub_phase_deg += delta * 5.0f * step;
                if (cfg.bass.sub_phase_deg < 0.0f) cfg.bass.sub_phase_deg = 0.0f;
                if (cfg.bass.sub_phase_deg > 180.0f) cfg.bass.sub_phase_deg = 180.0f;
            } else if (s_selected_row == 8) {
                cfg.bass.rumble_hz += delta * 1.0f * step;
                if (cfg.bass.rumble_hz < 10.0f) cfg.bass.rumble_hz = 10.0f;
                if (cfg.bass.rumble_hz > 35.0f) cfg.bass.rumble_hz = 35.0f;
            }
            break;

        case 3: // Presence & Transients 
            if (s_selected_row == 0) cfg.exciter.enabled = !cfg.exciter.enabled;
            else if (s_selected_row == 1) {
                cfg.exciter.cutoff_hz += delta * 150.0f * step;
                if (cfg.exciter.cutoff_hz < 2500.0f) cfg.exciter.cutoff_hz = 2500.0f;
                if (cfg.exciter.cutoff_hz > 10000.0f) cfg.exciter.cutoff_hz = 10000.0f;
            } else if (s_selected_row == 2) {
                cfg.exciter.drive += delta * 0.05f * step;
                if (cfg.exciter.drive < 0.0f) cfg.exciter.drive = 0.0f;
                if (cfg.exciter.drive > 1.0f) cfg.exciter.drive = 1.0f;
            } else if (s_selected_row == 3) {
                cfg.exciter.mix += delta * 0.05f * step;
                if (cfg.exciter.mix < 0.0f) cfg.exciter.mix = 0.0f;
                if (cfg.exciter.mix > 1.0f) cfg.exciter.mix = 1.0f;
            } else if (s_selected_row == 4) {
                cfg.exciter.shimmer += delta * 0.05f * step;
                if (cfg.exciter.shimmer < 0.0f) cfg.exciter.shimmer = 0.0f;
                if (cfg.exciter.shimmer > 1.0f) cfg.exciter.shimmer = 1.0f;
            } else if (s_selected_row == 5) cfg.transient.enabled = !cfg.transient.enabled;
            else if (s_selected_row == 6) {
                cfg.transient.attack += delta * 0.05f * step;
                if (cfg.transient.attack < -1.0f) cfg.transient.attack = -1.0f;
                if (cfg.transient.attack > +1.0f) cfg.transient.attack = +1.0f;
            } else if (s_selected_row == 7) {
                cfg.transient.sustain += delta * 0.05f * step;
                if (cfg.transient.sustain < -1.0f) cfg.transient.sustain = -1.0f;
                if (cfg.transient.sustain > +1.0f) cfg.transient.sustain = +1.0f;
            } else if (s_selected_row == 8) cfg.transient.declip_enable = !cfg.transient.declip_enable;
            break;

        case 4: // Warmth & Dynamics 
            if (s_selected_row == 0) cfg.spectral.enabled = !cfg.spectral.enabled;
            else if (s_selected_row == 1) {
                cfg.spectral.de_harsh += delta * 0.05f * step;
                if (cfg.spectral.de_harsh < 0.0f) cfg.spectral.de_harsh = 0.0f;
                if (cfg.spectral.de_harsh > 1.0f) cfg.spectral.de_harsh = 1.0f;
            } else if (s_selected_row == 2) {
                cfg.spectral.de_boom += delta * 0.05f * step;
                if (cfg.spectral.de_boom < 0.0f) cfg.spectral.de_boom = 0.0f;
                if (cfg.spectral.de_boom > 1.0f) cfg.spectral.de_boom = 1.0f;
            } else if (s_selected_row == 3) cfg.saturator.enabled = !cfg.saturator.enabled;
            else if (s_selected_row == 4) {
                int m = (int)cfg.saturator.mode + (delta > 0 ? 1 : -1);
                if (m < 0) m = SAT_MODE_COUNT - 1;
                if (m >= SAT_MODE_COUNT) m = 0;
                cfg.saturator.mode = (KrystalSatMode)m;
            } else if (s_selected_row == 5) {
                cfg.saturator.drive += delta * 0.05f * step;
                if (cfg.saturator.drive < 0.0f) cfg.saturator.drive = 0.0f;
                if (cfg.saturator.drive > 1.0f) cfg.saturator.drive = 1.0f;
            } else if (s_selected_row == 6) {
                cfg.saturator.bias += delta * 0.05f * step;
                if (cfg.saturator.bias < -0.5f) cfg.saturator.bias = -0.5f;
                if (cfg.saturator.bias > +0.5f) cfg.saturator.bias = +0.5f;
            } else if (s_selected_row == 7) {
                cfg.saturator.mix += delta * 0.05f * step;
                if (cfg.saturator.mix < 0.0f) cfg.saturator.mix = 0.0f;
                if (cfg.saturator.mix > 1.0f) cfg.saturator.mix = 1.0f;
            } else if (s_selected_row == 8) cfg.saturator.oversample = !cfg.saturator.oversample;
            break;

        case 5: // Master & Routing 
            if (s_selected_row == 0) {
                cfg.general.pre_gain_db += delta * 0.5f * step;
                if (cfg.general.pre_gain_db < -12.0f) cfg.general.pre_gain_db = -12.0f;
                if (cfg.general.pre_gain_db > +12.0f) cfg.general.pre_gain_db = +12.0f;
            } else if (s_selected_row == 1) {
                cfg.general.balance += delta * 0.05f * step;
                if (cfg.general.balance < -1.0f) cfg.general.balance = -1.0f;
                if (cfg.general.balance > +1.0f) cfg.general.balance = +1.0f;
            } else if (s_selected_row == 2) {
                int m = (int)cfg.general.mode + (delta > 0 ? 1 : -1);
                if (m < 0) m = CHAN_MODE_COUNT - 1;
                if (m >= CHAN_MODE_COUNT) m = 0;
                cfg.general.mode = (KrystalChannelMode)m;
            } else if (s_selected_row == 3) {
                int p = (int)cfg.general.polarity + (delta > 0 ? 1 : -1);
                if (p < 0) p = POLARITY_COUNT - 1;
                if (p >= POLARITY_COUNT) p = 0;
                cfg.general.polarity = (KrystalPolarity)p;
            } else if (s_selected_row == 4) {
                cfg.general.master_mix += delta * 0.05f * step;
                if (cfg.general.master_mix < 0.0f) cfg.general.master_mix = 0.0f;
                if (cfg.general.master_mix > 1.0f) cfg.general.master_mix = 1.0f;
            } else if (s_selected_row == 5) cfg.general.auto_gain = !cfg.general.auto_gain;
            else if (s_selected_row == 6) {
                cfg.general.headroom_db += delta * 0.25f * step;
                if (cfg.general.headroom_db < -3.0f) cfg.general.headroom_db = -3.0f;
                if (cfg.general.headroom_db > 0.0f) cfg.general.headroom_db = 0.0f;
            }
            break;
    }

    krystal_set_config(&cfg);
    force_redraw = true;
}

bool ui_krystal_handle_input(int ch) {
    if (!s_krystal_active) return false;

    if (s_arena_active) {
        KrystalConfig cfg;
        krystal_get_config(&cfg);

        if (ch == 27 || ch == 10 || ch == 'q' || ch == 'M' || ch == 'm') {
            s_arena_active = false;
            force_redraw = true;
            return true;
        }

        float az_rad = cfg.spatial.azimuth_deg * ((float)M_PI / 180.0f);
        float x_pos = cfg.spatial.distance_m * sinf(az_rad);
        float y_pos = cfg.spatial.distance_m * cosf(az_rad);
        const float step = 0.15f;

        if (ch == KEY_LEFT || ch == 'h' || ch == 'a')  x_pos -= step;
        if (ch == KEY_RIGHT || ch == 'l' || ch == 'd') x_pos += step;
        if (ch == KEY_UP || ch == 'k' || ch == 'w')    y_pos += step;
        if (ch == KEY_DOWN || ch == 'j' || ch == 's')  y_pos -= step;

        if (ch == '[' || ch == '{') {
            cfg.spatial.elevation_deg -= 3.0f;
            if (cfg.spatial.elevation_deg < -45.0f) cfg.spatial.elevation_deg = -45.0f;
        }
        if (ch == ']' || ch == '}') {
            cfg.spatial.elevation_deg += 3.0f;
            if (cfg.spatial.elevation_deg > +90.0f) cfg.spatial.elevation_deg = +90.0f;
        }
        if (ch == 'r' || ch == 'R') {
            x_pos = 0.0f;
            y_pos = 1.5f;
            cfg.spatial.elevation_deg = 0.0f;
            cfg.spatial.distance_m = 1.5f;
            cfg.spatial.azimuth_deg = 0.0f;
        }

        float new_dist = sqrtf(x_pos * x_pos + y_pos * y_pos);
        if (new_dist < 0.5f) new_dist = 0.5f;
        if (new_dist > 5.0f) new_dist = 5.0f;
        float new_az = atan2f(x_pos, y_pos) * (180.0f / (float)M_PI);

        cfg.spatial.distance_m = new_dist;
        cfg.spatial.azimuth_deg = new_az;
        krystal_set_config(&cfg);
        force_redraw = true;
        return true;
    }

    if (ch == 27 || ch == 'K' || ch == 'q') {
        ui_krystal_close();
        return true;
    }

    if (ch == 'm' || ch == 'M') {
        s_arena_active = true;
        force_redraw = true;
        return true;
    }

    // Section navigation, Tab / Shift-Tab / H / L 
    if (ch == '\t' || ch == 'L') {
        s_selected_col = (s_selected_col + 1) % NUM_SECTIONS;
        int max_r = get_max_row_for_col(s_selected_col);
        if (s_selected_row > max_r) s_selected_row = max_r;
        force_redraw = true;
        return true;
    }

    if (ch == KEY_BTAB || ch == 'H') {
        s_selected_col = (s_selected_col + NUM_SECTIONS - 1) % NUM_SECTIONS;
        int max_r = get_max_row_for_col(s_selected_col);
        if (s_selected_row > max_r) s_selected_row = max_r;
        force_redraw = true;
        return true;
    }

    // Row navigation, Up / Down / k / j 
    if (ch == KEY_UP || ch == 'k') {
        if (s_selected_row > 0) s_selected_row--;
        force_redraw = true;
        return true;
    }

    if (ch == KEY_DOWN || ch == 'j') {
        int max_r = get_max_row_for_col(s_selected_col);
        if (s_selected_row < max_r) s_selected_row++;
        force_redraw = true;
        return true;
    }

    // Value adjustment 
    if (ch == KEY_LEFT || ch == 'h') {
        adjust_active_param(-1.0f, false);
        return true;
    }

    if (ch == KEY_RIGHT || ch == 'l') {
        adjust_active_param(+1.0f, false);
        return true;
    }

    if (ch == '[' || ch == '{') {
        adjust_active_param(-1.0f, true);
        return true;
    }

    if (ch == ']' || ch == '}') {
        adjust_active_param(+1.0f, true);
        return true;
    }

    if (ch == 10 || ch == 'x') {
        adjust_active_param(+1.0f, false);
        return true;
    }

    if (ch == ' ') {
        krystal_toggle_enabled();
        ui_status_set(krystal_is_enabled() ? "Krystal: ACTIVE" : "Krystal: BYPASS");
        force_redraw = true;
        return true;
    }

    if (ch == 'p' || ch == 'P') {
        krystal_cycle_profile();
        KrystalConfig cfg;
        krystal_get_config(&cfg);
        ui_status_set("Profile: %s", krystal_get_profile_name(cfg.active_profile));
        force_redraw = true;
        return true;
    }

    return true;
}

static void draw_param_row(int y, int x, int width, const char *label, const char *val_str, bool is_selected) {
    char line[64];
    int label_w = width - 8;
    if (label_w < 6) label_w = 6;
    snprintf(line, sizeof(line), " %-*.*s %6.6s ", label_w, label_w, label, val_str);

    if (is_selected) {
        attron(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
        mvprintw(y, x, "%s", line);
        attroff(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
    } else {
        bool is_on = (strcmp(val_str, "[ON]") == 0);
        bool is_off = (strcmp(val_str, "off") == 0);

        if (is_on) {
            attron(COLOR_PAIR(2));
            mvprintw(y, x, " %-*.*s ", label_w, label_w, label);
            attroff(COLOR_PAIR(2));
            attron(COLOR_PAIR(3) | A_BOLD);
            printw("%6.6s ", val_str);
            attroff(COLOR_PAIR(3) | A_BOLD);
        } else if (is_off) {
            attron(COLOR_PAIR(2));
            mvprintw(y, x, " %-*.*s ", label_w, label_w, label);
            attroff(COLOR_PAIR(2));
            attron(COLOR_PAIR(2) | A_DIM);
            printw("%6.6s ", val_str);
            attroff(COLOR_PAIR(2) | A_DIM);
        } else {
            attron(COLOR_PAIR(2));
            mvprintw(y, x, "%s", line);
            attroff(COLOR_PAIR(2));
        }
    }
}

static void draw_arena_mode(int y, int x, int h, int w, const KrystalConfig *cfg) {
    ui_draw_box(y, x, h, w, "3D SPATIAL ARENA", 4);

    int hw = w / 2;
    int xy_cx = x + (hw / 2);
    int zy_cx = x + hw + (hw / 2);
    int cy = y + (h / 2);

    int rad_max = (h / 2) - 3;
    if (rad_max < 2) rad_max = 2;

    // Separator line 
    for (int r = 1; r < h - 1; r++) {
        attron(COLOR_PAIR(2) | A_DIM);
        mvaddch(y + r, x + hw, ACS_VLINE);
        attroff(COLOR_PAIR(2) | A_DIM);
    }
    attron(COLOR_PAIR(2) | A_DIM);
    mvaddch(y, x + hw, ACS_TTEE);
    mvaddch(y + h - 1, x + hw, ACS_BTEE);

    // XY Plane (Top-Down)
    attron(COLOR_PAIR(2) | A_DIM);
    for (float a = 0.0f; a < 6.28f; a += 0.15f) {
        int px = xy_cx + (int)roundf(sinf(a) * rad_max * 2.0f);
        int py = cy - (int)roundf(cosf(a) * rad_max);
        mvaddch(py, px, '.');
    }
    mvaddch(cy - rad_max, xy_cx, 'N');
    mvaddch(cy + rad_max, xy_cx, 'S');
    mvaddch(cy, xy_cx - (rad_max * 2), 'W');
    mvaddch(cy, xy_cx + (rad_max * 2), 'E');
    attroff(COLOR_PAIR(2) | A_DIM);

    // Draw Listener (Center) 
    attron(COLOR_PAIR(3) | A_BOLD);
    mvprintw(cy, xy_cx - 1, "(O)");
    attroff(COLOR_PAIR(3) | A_BOLD);

    float az_rad = cfg->spatial.azimuth_deg * ((float)M_PI / 180.0f);
    float dist_norm = cfg->spatial.distance_m / 5.0f;
    if (dist_norm > 1.0f) dist_norm = 1.0f;

    // Draw Source Particle 
    int tx = xy_cx + (int)roundf(sinf(az_rad) * dist_norm * rad_max * 2.0f);
    int ty = cy - (int)roundf(cosf(az_rad) * dist_norm * rad_max);
    
    attron(COLOR_PAIR(4) | A_BOLD);
    mvaddch(ty, tx, '*');
    
    // Draw Virtual Stereo Speakers L / R 
    float st_half = (cfg->spatial.stage_angle_deg * 0.5f) * ((float)M_PI / 180.0f);
    int lx = xy_cx + (int)roundf(sinf(az_rad - st_half) * dist_norm * rad_max * 2.0f);
    int ly = cy - (int)roundf(cosf(az_rad - st_half) * dist_norm * rad_max);
    int rx = xy_cx + (int)roundf(sinf(az_rad + st_half) * dist_norm * rad_max * 2.0f);
    int ry = cy - (int)roundf(cosf(az_rad + st_half) * dist_norm * rad_max);

    mvaddch(ly, lx, 'L');
    mvaddch(ry, rx, 'R');
    attroff(COLOR_PAIR(4) | A_BOLD);

    // ZY Plane (Side-Profile)
    attron(COLOR_PAIR(2) | A_DIM);
    for (float a = -1.57f; a < 1.57f; a += 0.15f) {
        int px = zy_cx + (int)roundf(cosf(a) * rad_max * 2.0f);
        int py = cy - (int)roundf(sinf(a) * rad_max);
        mvaddch(py, px, '.');
    }
    for (int r = -rad_max; r <= rad_max; r++) {
        mvaddch(cy + r, zy_cx, '|');
    }
    attroff(COLOR_PAIR(2) | A_DIM);
    
    attron(COLOR_PAIR(3) | A_BOLD);
    mvaddch(cy, zy_cx, 'O');
    mvaddch(cy, zy_cx + 1, ')');
    attroff(COLOR_PAIR(3) | A_BOLD);

    float el_rad = cfg->spatial.elevation_deg * ((float)M_PI / 180.0f);
    int z_tx = zy_cx + (int)roundf(cosf(el_rad) * dist_norm * rad_max * 2.0f);
    int z_ty = cy - (int)roundf(sinf(el_rad) * dist_norm * rad_max);
    
    attron(COLOR_PAIR(4) | A_BOLD);
    mvaddch(z_ty, z_tx, '*');
    attroff(COLOR_PAIR(4) | A_BOLD);

    // Text Layout 
    attron(COLOR_PAIR(4) | A_BOLD);
    mvprintw(y + 2, x + 3, "XY SOUNDFIELD (TOP-DOWN)");
    mvprintw(y + 2, x + hw + 3, "ZY HEIGHT (SIDE-PROFILE)");
    attroff(COLOR_PAIR(4) | A_BOLD);

    attron(COLOR_PAIR(2));
    mvprintw(y + h - 3, x + 3, "Azimuth: %+.0f°", cfg->spatial.azimuth_deg);
    mvprintw(y + h - 2, x + 3, "Distance: %.2fm", cfg->spatial.distance_m);
    
    mvprintw(y + h - 3, x + hw + 3, "Elevation: %+.0f°", cfg->spatial.elevation_deg);
    mvprintw(y + h - 2, x + hw + 3, "Spread: %.0f°", cfg->spatial.stage_angle_deg);
    attroff(COLOR_PAIR(2));

    attron(A_DIM | COLOR_PAIR(2));
    mvprintw(y + h - 1, x + 2, " [↑/↓/←/→] Move  [[/]] Height  [R] Center  [Esc/M] Exit ");
    attroff(A_DIM | COLOR_PAIR(2));
}

void draw_krystal_panel(int y, int x, int h, int w) {
    if (h < 8 || w < 32) return;

    KrystalConfig cfg;
    krystal_get_config(&cfg);

    if (s_arena_active) {
        draw_arena_mode(y, x, h, w, &cfg);
        return;
    }

    KrystalTelemetry telem;
    krystal_get_telemetry(&telem);

    bool enabled = cfg.master_enabled;
    const char *prof_name = krystal_get_profile_name(cfg.active_profile);

    char box_title[64];
    snprintf(box_title, sizeof(box_title), "Krystal DSP [%s]", enabled ? "ACTIVE" : "BYPASS");
    ui_draw_box(y, x, h, w, box_title, enabled ? 4 : 2);

    mvhline(y + 1, x + 1, ' ', w - 2);
    attron(COLOR_PAIR(enabled ? 3 : 2) | A_BOLD);
    mvprintw(y + 1, x + 3, "[%s]", enabled ? "ON" : "BYPASS");
    attroff(COLOR_PAIR(enabled ? 3 : 2) | A_BOLD);

    attron(COLOR_PAIR(4) | A_BOLD);
    printw(" %s", prof_name);
    attroff(COLOR_PAIR(4) | A_BOLD);

    attron(COLOR_PAIR(2) | A_DIM);
    printw("  Mix:%.0f%%  Trim:%+.1fdB  LUFS:%.1f  Peak:%.1fdB",
           cfg.general.master_mix * 100.0f, telem.auto_trim_db,
           telem.lufs_momentary, telem.peak_dbfs);
    attroff(COLOR_PAIR(2) | A_DIM);

    mvhline(y + 2, x + 1, ACS_HLINE, w - 2);

    int avail_w = w - 4;
    int min_col_w = 17;
    int visible_cols = avail_w / min_col_w;
    if (visible_cols > NUM_SECTIONS) visible_cols = NUM_SECTIONS;
    if (visible_cols < 1) visible_cols = 1;

    int col_w = avail_w / visible_cols;

    if (s_selected_col < s_col_scroll) s_col_scroll = s_selected_col;
    if (s_selected_col >= s_col_scroll + visible_cols) s_col_scroll = s_selected_col - visible_cols + 1;

    static const char *col_titles[NUM_SECTIONS] = {
        "LOUDNESS", "SPATIAL", "BASS", "PRESENCE", "WARMTH & TONE", "ROUTING"
    };

    const char *mode_names[] = { "Stereo", "Swap", "Mono", "Side", "L-Solo", "R-Solo" };
    const char *pol_names[]  = { "Normal", "Inv-L", "Inv-R", "Inv-Both" };
    const char *sat_names[]  = { "off", "Triode", "Pentode", "Tape", "Transfm" };

    int t_y = y + h - 3;
    int avail_rows = t_y - (y + 4);
    if (avail_rows < 1) avail_rows = 1;

    int row_scroll = 0;
    if (s_selected_row >= avail_rows) {
        row_scroll = s_selected_row - avail_rows + 1;
    }

    for (int i = 0; i < visible_cols; i++) {
        int c = s_col_scroll + i;
        int cx = x + 2 + (i * col_w);
        bool is_cur_col = (c == s_selected_col);

        attron(is_cur_col ? (COLOR_PAIR(4) | A_BOLD) : (COLOR_PAIR(2) | A_DIM));
        mvprintw(y + 3, cx + 1, "%-*.*s", col_w - 2, col_w - 2, col_titles[c]);
        attroff(is_cur_col ? (COLOR_PAIR(4) | A_BOLD) : (COLOR_PAIR(2) | A_DIM));

        int max_r = get_max_row_for_col(c);
        char buf[32];

        for (int screen_r = 0; screen_r < avail_rows && (screen_r + row_scroll) <= max_r; screen_r++) {
            int r = screen_r + row_scroll;
            bool is_sel = (is_cur_col && r == s_selected_row);
            int ry = y + 4 + screen_r;

            switch (c) {
                case 0: // Loudness 
                    if (r == 0) draw_param_row(ry, cx, col_w, "Status", cfg.loudness.enabled ? "[ON]" : "off", is_sel);
                    else if (r == 1) draw_param_row(ry, cx, col_w, "Curve", (cfg.loudness.mode == LOUDNESS_MODE_DYNAMIC) ? "Dynamic" : "Fixed", is_sel);
                    else if (r == 2) { snprintf(buf, sizeof(buf), "%.0f%%", cfg.loudness.intensity * 100.0f); draw_param_row(ry, cx, col_w, "Amount", buf, is_sel); }
                    else if (r == 3) { snprintf(buf, sizeof(buf), "%.0f%%", cfg.loudness.ref_vol * 100.0f); draw_param_row(ry, cx, col_w, "Threshold", buf, is_sel); }
                    else if (r == 4) { snprintf(buf, sizeof(buf), "%+.1fdB", cfg.spectral.tilt_db); draw_param_row(ry, cx, col_w, "Spec Tilt", buf, is_sel); }
                    break;

                case 1: // Spatial 3D 
                    if (r == 0) draw_param_row(ry, cx, col_w, "Status", cfg.spatial.enabled ? "[ON]" : "off", is_sel);
                    else if (r == 1) { snprintf(buf, sizeof(buf), "%+.0f°", cfg.spatial.azimuth_deg); draw_param_row(ry, cx, col_w, "Azimuth", buf, is_sel); }
                    else if (r == 2) { snprintf(buf, sizeof(buf), "%+.0f°", cfg.spatial.elevation_deg); draw_param_row(ry, cx, col_w, "Elevation", buf, is_sel); }
                    else if (r == 3) { snprintf(buf, sizeof(buf), "%.2fm", cfg.spatial.distance_m); draw_param_row(ry, cx, col_w, "Distance", buf, is_sel); }
                    else if (r == 4) { snprintf(buf, sizeof(buf), "%.0f°", cfg.spatial.stage_angle_deg); draw_param_row(ry, cx, col_w, "Spread", buf, is_sel); }
                    else if (r == 5) { snprintf(buf, sizeof(buf), "%.0f%%", cfg.spatial.room_refl * 100.0f); draw_param_row(ry, cx, col_w, "Room Refl", buf, is_sel); }
                    else if (r == 6) { snprintf(buf, sizeof(buf), "%+.1fdB", cfg.spatial.center_gain_db); draw_param_row(ry, cx, col_w, "Center", buf, is_sel); }
                    else if (r == 7) { snprintf(buf, sizeof(buf), "%.0fHz", cfg.spatial.mono_cut_hz); draw_param_row(ry, cx, col_w, "Mono Cut", buf, is_sel); }
                    else if (r == 8) { snprintf(buf, sizeof(buf), "+%.2f", cfg.spatial.safety_limit); draw_param_row(ry, cx, col_w, "Guard", buf, is_sel); }
                    break;

                case 2: // Bass 
                    if (r == 0) draw_param_row(ry, cx, col_w, "Status", cfg.bass.enabled ? "[ON]" : "off", is_sel);
                    else if (r == 1) { snprintf(buf, sizeof(buf), "%.0fHz", cfg.bass.cutoff_hz); draw_param_row(ry, cx, col_w, "Crossover", buf, is_sel); }
                    else if (r == 2) { snprintf(buf, sizeof(buf), "%.0f%%", cfg.bass.intensity * 100.0f); draw_param_row(ry, cx, col_w, "Harmonics", buf, is_sel); }
                    else if (r == 3) { snprintf(buf, sizeof(buf), "%.0f%%", cfg.bass.mix * 100.0f); draw_param_row(ry, cx, col_w, "Harm Mix", buf, is_sel); }
                    else if (r == 4) { snprintf(buf, sizeof(buf), "%.0f%%", cfg.bass.sub_weight * 100.0f); draw_param_row(ry, cx, col_w, "Sub Level", buf, is_sel); }
                    else if (r == 5) { snprintf(buf, sizeof(buf), "%.0f%%", cfg.bass.harmonic_tone * 100.0f); draw_param_row(ry, cx, col_w, "Tone 2/3", buf, is_sel); }
                    else if (r == 6) { snprintf(buf, sizeof(buf), "%.0f%%", cfg.bass.sub_octave * 100.0f); draw_param_row(ry, cx, col_w, "Sub-Oct", buf, is_sel); }
                    else if (r == 7) { snprintf(buf, sizeof(buf), "%.0f°", cfg.bass.sub_phase_deg); draw_param_row(ry, cx, col_w, "Phase", buf, is_sel); }
                    else if (r == 8) { snprintf(buf, sizeof(buf), "%.0fHz", cfg.bass.rumble_hz); draw_param_row(ry, cx, col_w, "Highpass", buf, is_sel); }
                    break;

                case 3: // Presence 
                    if (r == 0) draw_param_row(ry, cx, col_w, "Exciter", cfg.exciter.enabled ? "[ON]" : "off", is_sel);
                    else if (r == 1) { snprintf(buf, sizeof(buf), "%.0fHz", cfg.exciter.cutoff_hz); draw_param_row(ry, cx, col_w, "Air Cut", buf, is_sel); }
                    else if (r == 2) { snprintf(buf, sizeof(buf), "%.0f%%", cfg.exciter.drive * 100.0f); draw_param_row(ry, cx, col_w, "Air Drive", buf, is_sel); }
                    else if (r == 3) { snprintf(buf, sizeof(buf), "%.0f%%", cfg.exciter.mix * 100.0f); draw_param_row(ry, cx, col_w, "Air Mix", buf, is_sel); }
                    else if (r == 4) { snprintf(buf, sizeof(buf), "%.0f%%", cfg.exciter.shimmer * 100.0f); draw_param_row(ry, cx, col_w, "Shimmer", buf, is_sel); }
                    else if (r == 5) draw_param_row(ry, cx, col_w, "Transient", cfg.transient.enabled ? "[ON]" : "off", is_sel);
                    else if (r == 6) { snprintf(buf, sizeof(buf), "%+.0f%%", cfg.transient.attack * 100.0f); draw_param_row(ry, cx, col_w, "Attack", buf, is_sel); }
                    else if (r == 7) { snprintf(buf, sizeof(buf), "%+.0f%%", cfg.transient.sustain * 100.0f); draw_param_row(ry, cx, col_w, "Sustain", buf, is_sel); }
                    else if (r == 8) draw_param_row(ry, cx, col_w, "De-Clip", cfg.transient.declip_enable ? "[ON]" : "off", is_sel);
                    break;

                case 4: // Warmth & Dynamics 
                    if (r == 0) draw_param_row(ry, cx, col_w, "Tamer", cfg.spectral.enabled ? "[ON]" : "off", is_sel);
                    else if (r == 1) { snprintf(buf, sizeof(buf), "%.0f%%", cfg.spectral.de_harsh * 100.0f); draw_param_row(ry, cx, col_w, "De-Harsh", buf, is_sel); }
                    else if (r == 2) { snprintf(buf, sizeof(buf), "%.0f%%", cfg.spectral.de_boom * 100.0f); draw_param_row(ry, cx, col_w, "De-Boom", buf, is_sel); }
                    else if (r == 3) draw_param_row(ry, cx, col_w, "Saturator", cfg.saturator.enabled ? "[ON]" : "off", is_sel);
                    else if (r == 4) draw_param_row(ry, cx, col_w, "Sat Type", sat_names[(int)cfg.saturator.mode], is_sel);
                    else if (r == 5) { snprintf(buf, sizeof(buf), "%.0f%%", cfg.saturator.drive * 100.0f); draw_param_row(ry, cx, col_w, "Drive", buf, is_sel); }
                    else if (r == 6) { snprintf(buf, sizeof(buf), "%+.0f%%", cfg.saturator.bias * 100.0f); draw_param_row(ry, cx, col_w, "Bias", buf, is_sel); }
                    else if (r == 7) { snprintf(buf, sizeof(buf), "%.0f%%", cfg.saturator.mix * 100.0f); draw_param_row(ry, cx, col_w, "Sat Mix", buf, is_sel); }
                    else if (r == 8) draw_param_row(ry, cx, col_w, "Anti-Alias", cfg.saturator.oversample ? "[2x]" : "[1x]", is_sel);
                    break;

                case 5: // Master & Routing 
                    if (r == 0) { snprintf(buf, sizeof(buf), "%+.1fdB", cfg.general.pre_gain_db); draw_param_row(ry, cx, col_w, "Pre-Gain", buf, is_sel); }
                    else if (r == 1) { snprintf(buf, sizeof(buf), "%+.0f%%", cfg.general.balance * 100.0f); draw_param_row(ry, cx, col_w, "Balance", buf, is_sel); }
                    else if (r == 2) draw_param_row(ry, cx, col_w, "Routing", mode_names[(int)cfg.general.mode], is_sel);
                    else if (r == 3) draw_param_row(ry, cx, col_w, "Polarity", pol_names[(int)cfg.general.polarity], is_sel);
                    else if (r == 4) { snprintf(buf, sizeof(buf), "%.0f%%", cfg.general.master_mix * 100.0f); draw_param_row(ry, cx, col_w, "Mix", buf, is_sel); }
                    else if (r == 5) draw_param_row(ry, cx, col_w, "Auto-Gain", cfg.general.auto_gain ? "[ON]" : "off", is_sel);
                    else if (r == 6) { snprintf(buf, sizeof(buf), "%.1fdB", cfg.general.headroom_db); draw_param_row(ry, cx, col_w, "Ceiling", buf, is_sel); }
                    break;
            }
        }
    }

    if (s_col_scroll > 0) {
        attron(COLOR_PAIR(4) | A_BOLD);
        mvaddch(y + 3, x + 1, '<');
        attroff(COLOR_PAIR(4) | A_BOLD);
    }
    if (s_col_scroll + visible_cols < NUM_SECTIONS) {
        attron(COLOR_PAIR(4) | A_BOLD);
        mvaddch(y + 3, x + w - 2, '>');
        attroff(COLOR_PAIR(4) | A_BOLD);
    }

    mvhline(t_y, x + 1, ACS_HLINE, w - 2);
    t_y++;
    mvhline(t_y, x + 1, ' ', w - 2);

    float corr = telem.phase_correlation;
    int phase_bar_w = 16;
    int center_p = phase_bar_w / 2;
    float norm_c = (corr + 1.0f) * 0.5f;
    int pin = (int)(norm_c * (float)phase_bar_w);
    if (pin < 0) pin = 0;
    if (pin >= phase_bar_w) pin = phase_bar_w - 1;

    attron(COLOR_PAIR(2));
    mvprintw(t_y, x + 3, "Phase [");
    for (int p = 0; p < phase_bar_w; p++) {
        if (p == pin) {
            attron(A_BOLD | COLOR_PAIR(corr > 0.30f ? 3 : (corr >= 0.0f ? 4 : 10)));
            addch('|');
            attroff(A_BOLD | COLOR_PAIR(corr > 0.30f ? 3 : (corr >= 0.0f ? 4 : 10)));
        } else if (p == center_p) {
            addch(':');
        } else {
            addch('-');
        }
    }
    printw("] %+.2f", corr);

    printw("  Crest: %.1fdB  Tamer: %+.1f(H) %+.1f(B)",
           telem.crest_factor_db, telem.de_harsh_cut_db, telem.de_boom_cut_db);
    attroff(COLOR_PAIR(2));

    int f_y = y + h - 1;
    attron(COLOR_PAIR(2) | A_DIM);
    mvprintw(f_y, x + 2, " [Tab/H/L] Sec  [↑/↓] Row  [←/→] Adj  [M] 3D Arena  [Space] On/Off  [Esc] Exit ");
    attroff(COLOR_PAIR(2) | A_DIM);
}