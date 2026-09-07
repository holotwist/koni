#define _DEFAULT_SOURCE
#include "ui_eq.h"
#include "equalizer.h"
#include "ui_common.h"
#include "ui_status.h"
#include <ncurses.h>
#include <stdio.h>
#include <math.h>

static bool s_eq_active = false;
static int s_selected_band = 0;

void ui_eq_init(void) {
    s_eq_active = false;
    s_selected_band = 0;
}

bool ui_eq_is_active(void) {
    return s_eq_active;
}

void ui_eq_toggle(void) {
    s_eq_active = !s_eq_active;
    force_redraw = true;
}

void ui_eq_close(void) {
    s_eq_active = false;
    force_redraw = true;
}

bool ui_eq_handle_input(int ch) {
    if (!s_eq_active) return false;

    if (ch == 27 || ch == 'E' || ch == 'q') { // Esc, Shift+E, or q closes EQ view
        ui_eq_close();
        return true;
    }

    if (ch == KEY_LEFT || ch == 'h') {
        if (s_selected_band > 0) s_selected_band--;
        force_redraw = true;
        return true;
    }

    if (ch == KEY_RIGHT || ch == 'l') {
        if (s_selected_band < EQ_NUM_BANDS - 1) s_selected_band++;
        force_redraw = true;
        return true;
    }

    if (ch == KEY_UP || ch == 'k') {
        eq_adjust_band_gain(s_selected_band, +1.0f);
        force_redraw = true;
        return true;
    }

    if (ch == KEY_DOWN || ch == 'j') {
        eq_adjust_band_gain(s_selected_band, -1.0f);
        force_redraw = true;
        return true;
    }

    if (ch == 'p' || ch == 'P') {
        eq_cycle_preset();
        int cur_p = eq_get_current_preset();
        ui_status_set("EQ Preset: %s", eq_get_preset_name(cur_p));
        force_redraw = true;
        return true;
    }

    if (ch == 'r' || ch == 'R' || ch == '0') {
        eq_reset_flat();
        ui_status_set("EQ: Reset to Flat");
        force_redraw = true;
        return true;
    }

    if (ch == ' ' || ch == 'e') {
        eq_toggle_enabled();
        ui_status_set(eq_is_enabled() ? "EQ: Enabled" : "EQ: Bypassed");
        force_redraw = true;
        return true;
    }

    if (ch == '+' || ch == '=') {
        if (atomic_load(&volume) < 200) atomic_fetch_add(&volume, 5);
        force_redraw = true;
        return true;
    }

    if (ch == '-' || ch == '_') {
        if (atomic_load(&volume) > 0) atomic_fetch_sub(&volume, 5);
        force_redraw = true;
        return true;
    }

    return true; // Absorb other keys while in dedicated EQ mode
}

void draw_eq_panel(int y, int x, int h, int w) {
    if (h < 8 || w < 20) return;

    bool enabled = eq_is_enabled();
    int cur_preset = eq_get_current_preset();
    const char *preset_name = eq_get_preset_name(cur_preset);

    char title[64];
    snprintf(title, sizeof(title), "Equalizer [%s]", enabled ? "ACTIVE" : "BYPASSED");
    ui_draw_box(y, x, h, w, title, enabled ? 4 : 2);

    // Subheader
    attron(COLOR_PAIR(2));
    mvprintw(y + 1, x + 3, "Preset: ");
    attroff(COLOR_PAIR(2));
    attron(A_BOLD | COLOR_PAIR(enabled ? 3 : 2));
    printw("%-12s", preset_name);
    attroff(A_BOLD | COLOR_PAIR(enabled ? 3 : 2));
    attron(COLOR_PAIR(2) | A_DIM);
    printw("  Limiter: Soft-Knee Tanh (Active)");
    attroff(COLOR_PAIR(2) | A_DIM);

    // Separator
    attron(COLOR_PAIR(enabled ? 4 : 2));
    mvhline(y + 2, x + 1, ACS_HLINE, w - 2);
    attroff(COLOR_PAIR(enabled ? 4 : 2));

    const char **labels = eq_get_freq_labels();
    int track_top = y + 4;
    int track_bottom = y + h - 4;
    int track_height = track_bottom - track_top;
    if (track_height < 5) track_height = 5;

    int zero_row = track_top + (track_height / 2);

    int col_width = (w - 6) / EQ_NUM_BANDS;
    if (col_width < 4) col_width = 4;
    int start_x = x + 3 + ((w - 6) - (col_width * EQ_NUM_BANDS)) / 2;

    // Draw reference dB marks on the left
    if (start_x >= x + 7) {
        attron(COLOR_PAIR(2) | A_DIM);
        mvprintw(track_top, x + 1, "+12");
        mvprintw(zero_row, x + 2, " 0");
        mvprintw(track_bottom, x + 1, "-12");
        attroff(COLOR_PAIR(2) | A_DIM);
    }

    // Render 10 sliders
    for (int b = 0; b < EQ_NUM_BANDS; b++) {
        int cx = start_x + (b * col_width) + (col_width / 2);
        float gain = eq_get_band_gain(b);
        bool is_selected = (b == s_selected_band);

        // Calculate slider knob row
        float norm = (gain - EQ_MIN_GAIN_DB) / (EQ_MAX_GAIN_DB - EQ_MIN_GAIN_DB); // 0.0 to 1.0
        int knob_row = track_bottom - (int)(norm * (float)track_height);
        if (knob_row < track_top) knob_row = track_top;
        if (knob_row > track_bottom) knob_row = track_bottom;

        // Top gain readout
        char gain_str[8];
        snprintf(gain_str, sizeof(gain_str), "%+2.0fdB", gain);
        if (is_selected) {
            attron(A_BOLD | COLOR_PAIR(4) | A_REVERSE);
            mvprintw(y + 3, cx - 2, "%-5s", gain_str);
            attroff(A_BOLD | COLOR_PAIR(4) | A_REVERSE);
        } else {
            attron(COLOR_PAIR(gain > 0.0f ? 3 : (gain < 0.0f ? 10 : 2)));
            mvprintw(y + 3, cx - 2, "%-5s", gain_str);
            attroff(COLOR_PAIR(gain > 0.0f ? 3 : (gain < 0.0f ? 10 : 2)));
        }

        // Draw vertical track
        for (int r = track_top; r <= track_bottom; r++) {
            if (r == zero_row) {
                attron(COLOR_PAIR(2) | A_DIM);
                mvaddch(r, cx, ACS_PLUS);
                attroff(COLOR_PAIR(2) | A_DIM);
            } else {
                attron(COLOR_PAIR(2) | A_DIM);
                mvaddch(r, cx, ACS_VLINE);
                attroff(COLOR_PAIR(2) | A_DIM);
            }
        }

        // Draw Slider Handle Knob
        if (is_selected) {
            attron(A_BOLD | COLOR_PAIR(1));
            mvprintw(knob_row, cx - 1, "[#]");
            attroff(A_BOLD | COLOR_PAIR(1));
        } else {
            attron(A_BOLD | COLOR_PAIR(enabled ? (gain > 0 ? 3 : (gain < 0 ? 5 : 2)) : 2));
            mvprintw(knob_row, cx - 1, "(o)");
            attroff(A_BOLD | COLOR_PAIR(enabled ? (gain > 0 ? 3 : (gain < 0 ? 5 : 2)) : 2));
        }

        // Bottom frequency label
        if (is_selected) {
            attron(A_BOLD | COLOR_PAIR(1) | A_REVERSE);
            mvprintw(y + h - 3, cx - 2, " %-5s", labels[b]);
            attroff(A_BOLD | COLOR_PAIR(1) | A_REVERSE);
        } else {
            attron(COLOR_PAIR(2));
            mvprintw(y + h - 3, cx - 2, " %-5s", labels[b]);
            attroff(COLOR_PAIR(2));
        }
    }

    // Bottom Controls Help Bar
    attron(A_DIM | COLOR_PAIR(2));
    mvprintw(y + h - 2, x + 2, "[<-/->] Band  [^/v] Gain (+-1dB)  [P] Preset  [Space] On/Off  [0/R] Reset  [Esc/E] Exit");
    attroff(A_DIM | COLOR_PAIR(2));
}