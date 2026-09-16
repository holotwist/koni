#define _DEFAULT_SOURCE
#include "ui_eq.h"
#include "equalizer.h"
#include "peq.h"
#include "ui_modal.h"
#include "ui_common.h"
#include "ui_status.h"
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static bool s_eq_active = false;
static int s_selected_band = 0;

// PEQ navigation state
static int s_peq_row = 0;        // -1: Preamp, 0..9: Bands
static int s_peq_col = 1;        // 0: Type, 1: Freq, 2: Gain, 3: Q, 4: Status
static bool s_peq_editing = false;
static char s_edit_buf[32] = {0};
static int s_edit_len = 0;

void ui_eq_init(void) {
    s_eq_active = false;
    s_selected_band = 0;
    s_peq_row = 0;
    s_peq_col = 1;
    s_peq_editing = false;
    s_edit_buf[0] = '\0';
    s_edit_len = 0;
}

bool ui_eq_is_active(void) {
    return s_eq_active;
}

void ui_eq_toggle(void) {
    s_eq_active = !s_eq_active;
    s_peq_editing = false;
    force_redraw = true;
}

void ui_eq_close(void) {
    s_eq_active = false;
    s_peq_editing = false;
    force_redraw = true;
}

static void start_cell_editing(const char *initial) {
    s_peq_editing = true;
    strncpy(s_edit_buf, initial ? initial : "", sizeof(s_edit_buf) - 1);
    s_edit_buf[sizeof(s_edit_buf) - 1] = '\0';
    s_edit_len = (int)strlen(s_edit_buf);
}

static void commit_cell_editing(void) {
    if (s_edit_len > 0) {
        float val = (float)atof(s_edit_buf);

        if (s_peq_row == -1) {
            peq_set_preamp(val);
        } else {
            PEQBand b;
            if (peq_get_band(s_peq_row, &b)) {
                if (s_peq_col == 1) {
                    if (val < PEQ_MIN_FREQ) val = PEQ_MIN_FREQ;
                    if (val > PEQ_MAX_FREQ) val = PEQ_MAX_FREQ;
                    b.freq = val;
                } else if (s_peq_col == 2) {
                    if (val < PEQ_MIN_GAIN_DB) val = PEQ_MIN_GAIN_DB;
                    if (val > PEQ_MAX_GAIN_DB) val = PEQ_MAX_GAIN_DB;
                    b.gain_db = val;
                } else if (s_peq_col == 3) {
                    if (val < PEQ_MIN_Q) val = PEQ_MIN_Q;
                    if (val > PEQ_MAX_Q) val = PEQ_MAX_Q;
                    b.q = val;
                }
                peq_set_band(s_peq_row, &b);
            }
        }
    }
    s_peq_editing = false;
}

static bool handle_graphic_input(int ch) {
    if (ch == KEY_LEFT || ch == 'h') {
        if (s_selected_band > 0) s_selected_band--;
        return true;
    }
    if (ch == KEY_RIGHT || ch == 'l') {
        if (s_selected_band < EQ_NUM_BANDS - 1) s_selected_band++;
        return true;
    }
    if (ch == KEY_UP || ch == 'k') {
        eq_adjust_band_gain(s_selected_band, +1.0f);
        return true;
    }
    if (ch == KEY_DOWN || ch == 'j') {
        eq_adjust_band_gain(s_selected_band, -1.0f);
        return true;
    }
    if (ch == 'P') {
        ui_modal_open_eq_presets();
        return true;
    }
    if (ch == 'p') {
        eq_cycle_preset();
        ui_status_set("EQ Preset: %s", eq_get_preset_name(eq_get_current_preset()));
        return true;
    }
    if (ch == 'r' || ch == 'R' || ch == '0') {
        eq_reset_flat();
        ui_status_set("EQ: Reset to Flat");
        return true;
    }
    return false;
}

static bool handle_parametric_input(int ch) {
    if (s_peq_editing) {
        if (ch == 27) { // Escape cancels edit
            s_peq_editing = false;
            return true;
        }
        if (ch == 10) { // Enter commits
            commit_cell_editing();
            return true;
        }
        if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
            if (s_edit_len > 0) {
                s_edit_buf[--s_edit_len] = '\0';
            }
            return true;
        }
        if ((ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '+') {
            if (s_edit_len < (int)sizeof(s_edit_buf) - 1) {
                s_edit_buf[s_edit_len++] = (char)ch;
                s_edit_buf[s_edit_len] = '\0';
            }
            return true;
        }
        return true;
    }

    // Row navigation
    if (ch == KEY_UP || ch == 'k') {
        if (s_peq_row > -1) s_peq_row--;
        return true;
    }
    if (ch == KEY_DOWN || ch == 'j') {
        if (s_peq_row < PEQ_MAX_BANDS - 1) s_peq_row++;
        return true;
    }

    // Column navigation / Preamp fine-stepping
    if (ch == KEY_LEFT || ch == 'h') {
        if (s_peq_row == -1) peq_adjust_preamp(-0.10f);
        else if (s_peq_col > 0) s_peq_col--;
        return true;
    }
    if (ch == KEY_RIGHT || ch == 'l') {
        if (s_peq_row == -1) peq_adjust_preamp(+0.10f);
        else if (s_peq_col < 4) s_peq_col++;
        return true;
    }

    // Start editing or toggle/cycle
    if (ch == 10 || ch == 'e') {
        if (s_peq_row == -1) {
            char tmp[16];
            snprintf(tmp, sizeof(tmp), "%.2f", peq_get_preamp());
            start_cell_editing(tmp);
            return true;
        }

        PEQBand b;
        peq_get_band(s_peq_row, &b);

        if (s_peq_col == 0) {
            peq_cycle_band_type(s_peq_row);
        } else if (s_peq_col == 1) {
            char tmp[16];
            snprintf(tmp, sizeof(tmp), "%.1f", b.freq);
            start_cell_editing(tmp);
        } else if (s_peq_col == 2) {
            char tmp[16];
            snprintf(tmp, sizeof(tmp), "%.1f", b.gain_db);
            start_cell_editing(tmp);
        } else if (s_peq_col == 3) {
            char tmp[16];
            snprintf(tmp, sizeof(tmp), "%.2f", b.q);
            start_cell_editing(tmp);
        } else if (s_peq_col == 4) {
            peq_toggle_band_enabled(s_peq_row);
        }
        return true;
    }

    // Direct step adjustments
    if (ch == '[' || ch == '{') {
        if (s_peq_row == -1) peq_adjust_preamp(-0.5f);
        else if (s_peq_col == 1) peq_adjust_band_freq(s_peq_row, 0.9438f);
        else if (s_peq_col == 2) peq_adjust_band_gain(s_peq_row, -0.5f);
        else if (s_peq_col == 3) peq_adjust_band_q(s_peq_row, -0.10f);
        return true;
    }
    if (ch == ']' || ch == '}') {
        if (s_peq_row == -1) peq_adjust_preamp(+0.5f);
        else if (s_peq_col == 1) peq_adjust_band_freq(s_peq_row, 1.0595f);
        else if (s_peq_col == 2) peq_adjust_band_gain(s_peq_row, +0.5f);
        else if (s_peq_col == 3) peq_adjust_band_q(s_peq_row, +0.10f);
        return true;
    }
    if (ch == '-' || ch == '<' || ch == ',') {
        if (s_peq_row == -1) peq_adjust_preamp(-0.10f);
        else if (s_peq_col == 2) peq_adjust_band_gain(s_peq_row, -0.5f);
        else if (s_peq_col == 3) peq_adjust_band_q(s_peq_row, -0.10f);
        return true;
    }
    if (ch == '+' || ch == '=' || ch == '>' || ch == '.') {
        if (s_peq_row == -1) peq_adjust_preamp(+0.10f);
        else if (s_peq_col == 2) peq_adjust_band_gain(s_peq_row, +0.5f);
        else if (s_peq_col == 3) peq_adjust_band_q(s_peq_row, +0.10f);
        return true;
    }

    // Presets & File I/O
    if (ch == 'P' || ch == 'p') {
        ui_modal_open_peq_presets();
        return true;
    }
    if (ch == 'S' || ch == 's') {
        ui_modal_open_peq_save();
        return true;
    }

    // Hotkeys
    if (ch == 't' || ch == 'T') {
        if (s_peq_row >= 0) peq_cycle_band_type(s_peq_row);
        return true;
    }
    if (ch == ' ' || ch == 'x') {
        if (s_peq_row >= 0) peq_toggle_band_enabled(s_peq_row);
        return true;
    }
    if (ch == '0') {
        if (s_peq_row == -1) peq_set_preamp(0.0f);
        else if (s_peq_col == 2) peq_adjust_band_gain(s_peq_row, -peq_get_preamp());
        else if (s_peq_col == 3) peq_adjust_band_q(s_peq_row, 0.7071f);
        return true;
    }
    if (ch == 'R') {
        peq_reset();
        ui_status_set("PEQ: Reset Flat");
        return true;
    }
    if (ch == '1') {
        peq_load_harman_target(false);
        ui_status_set("PEQ: Harman Over-Ear Target Loaded");
        return true;
    }
    if (ch == '2') {
        peq_load_harman_target(true);
        ui_status_set("PEQ: Harman In-Ear (IEM) Target Loaded");
        return true;
    }

    return false;
}

bool ui_eq_handle_input(int ch) {
    if (!s_eq_active) return false;

    if (s_peq_editing) {
        if (handle_parametric_input(ch)) {
            force_redraw = true;
            return true;
        }
    }

    if (ch == 27 || ch == 'E' || ch == 'q') {
        ui_eq_close();
        return true;
    }

    if (ch == '\t' || ch == 'm' || ch == 'M') {
        eq_toggle_mode();
        s_peq_editing = false;
        ui_status_set("EQ Mode: %s", eq_get_mode() == EQ_MODE_GRAPHIC ? "Graphic (10-Band)" : "Parametric (PEQ)");
        force_redraw = true;
        return true;
    }

    if (ch == 'e') {
        eq_toggle_enabled();
        ui_status_set(eq_is_enabled() ? "EQ: Enabled" : "EQ: Bypassed");
        force_redraw = true;
        return true;
    }

    bool handled = (eq_get_mode() == EQ_MODE_GRAPHIC) ? handle_graphic_input(ch) : handle_parametric_input(ch);
    if (handled) {
        force_redraw = true;
        return true;
    }

    return true;
}

static void draw_graphic_panel(int y, int x, int h, int w) {
    bool enabled = eq_is_enabled();
    const char **labels = eq_get_freq_labels();
    int track_top = y + 4;
    int track_bottom = y + h - 4;
    int track_height = track_bottom - track_top;
    if (track_height < 5) track_height = 5;

    int zero_row = track_top + (track_height / 2);
    int col_width = (w - 6) / EQ_NUM_BANDS;
    if (col_width < 4) col_width = 4;
    int start_x = x + 3 + ((w - 6) - (col_width * EQ_NUM_BANDS)) / 2;

    if (start_x >= x + 7) {
        attron(COLOR_PAIR(2) | A_DIM);
        mvprintw(track_top, x + 1, "+12");
        mvprintw(zero_row, x + 2, " 0");
        mvprintw(track_bottom, x + 1, "-12");
        attroff(COLOR_PAIR(2) | A_DIM);
    }

    for (int b = 0; b < EQ_NUM_BANDS; b++) {
        int cx = start_x + (b * col_width) + (col_width / 2);
        float gain = eq_get_band_gain(b);
        bool is_selected = (b == s_selected_band);

        float norm = (gain - EQ_MIN_GAIN_DB) / (EQ_MAX_GAIN_DB - EQ_MIN_GAIN_DB);
        int knob_row = track_bottom - (int)(norm * (float)track_height);
        if (knob_row < track_top) knob_row = track_top;
        if (knob_row > track_bottom) knob_row = track_bottom;

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

        for (int r = track_top; r <= track_bottom; r++) {
            attron(COLOR_PAIR(2) | A_DIM);
            mvaddch(r, cx, (r == zero_row) ? ACS_PLUS : ACS_VLINE);
            attroff(COLOR_PAIR(2) | A_DIM);
        }

        if (is_selected) {
            attron(A_BOLD | COLOR_PAIR(1));
            mvprintw(knob_row, cx - 1, "[#]");
            attroff(A_BOLD | COLOR_PAIR(1));
        } else {
            attron(A_BOLD | COLOR_PAIR(enabled ? (gain > 0 ? 3 : (gain < 0 ? 5 : 2)) : 2));
            mvprintw(knob_row, cx - 1, "(o)");
            attroff(A_BOLD | COLOR_PAIR(enabled ? (gain > 0 ? 3 : (gain < 0 ? 5 : 2)) : 2));
        }

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

    attron(A_DIM | COLOR_PAIR(2));
    mvprintw(y + h - 2, x + 2, "[Tab] Parametric  [<-/->] Band  [^/v] Gain  [p] Cycle  [P] Presets  [e] On/Off  [Esc] Exit");
    attroff(A_DIM | COLOR_PAIR(2));
}

static void set_braille_dot(uint8_t *grid, int grid_w, int px_x, int px_y) {
    static const uint8_t mask_table[4][2] = {
        { 0x01, 0x08 },
        { 0x02, 0x10 },
        { 0x04, 0x20 },
        { 0x40, 0x80 }
    };
    int cx = px_x / 2;
    int cy = px_y / 4;
    grid[cy * grid_w + cx] |= mask_table[px_y % 4][px_x % 2];
}

static void draw_curve_line(uint8_t *grid, int grid_w, int grid_h, int x0, int y0, int x1, int y1) {
    int max_x = grid_w * 2;
    int max_y = grid_h * 4;
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    for (;;) {
        if (x0 >= 0 && x0 < max_x && y0 >= 0 && y0 < max_y) {
            set_braille_dot(grid, grid_w, x0, y0);
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void draw_parametric_panel(int y, int x, int h, int w) {
    int cur_y = y + 3;
    int graph_h = (h >= 24) ? 7 : (h >= 20 ? 5 : 0);

    // Full-width response graph
    if (graph_h > 0 && w >= 36) {
        int label_w = 7;
        int graph_x = x + label_w + 1;
        int graph_w = (w - 4) - label_w;
        if (graph_w < 10) graph_w = 10;

        int px_w = graph_w * 2;
        int px_h = graph_h * 4;

        uint8_t *grid = calloc((size_t)(graph_w * graph_h), sizeof(uint8_t));
        float *curve_db = malloc(sizeof(float) * px_w);

        if (grid && curve_db) {
            const float min_db = -12.0f;
            const float max_db = +12.0f;

            // Dotted 0 dB reference line
            int zero_y = (int)roundf((max_db - 0.0f) / (max_db - min_db) * (float)(px_h - 1));
            if (zero_y >= 0 && zero_y < px_h) {
                for (int gx = 0; gx < px_w; gx += 4) {
                    set_braille_dot(grid, graph_w, gx, zero_y);
                }
            }

            peq_calculate_curve(curve_db, px_w, 20.0f, 20000.0f);

            int prev_px = -1, prev_py = -1;
            for (int px = 0; px < px_w; px++) {
                int py = (int)roundf((max_db - curve_db[px]) / (max_db - min_db) * (float)(px_h - 1));
                if (py < 0) py = 0;
                if (py >= px_h) py = px_h - 1;

                if (prev_px != -1) {
                    draw_curve_line(grid, graph_w, graph_h, prev_px, prev_py, px, py);
                } else {
                    set_braille_dot(grid, graph_w, px, py);
                }
                prev_px = px;
                prev_py = py;
            }

            // Left scale labels
            attron(COLOR_PAIR(2) | A_DIM);
            mvprintw(cur_y, x + 2, "+12dB|");
            mvprintw(cur_y + (graph_h / 2), x + 2, "  0dB|");
            mvprintw(cur_y + graph_h - 1, x + 2, "-12dB|");
            attroff(COLOR_PAIR(2) | A_DIM);

            // Output Braille curve across entire available width
            for (int gy = 0; gy < graph_h; gy++) {
                move(cur_y + gy, graph_x);
                for (int gx = 0; gx < graph_w; gx++) {
                    uint8_t m = grid[gy * graph_w + gx];
                    if (m == 0) {
                        addch(' ');
                    } else {
                        uint32_t cp = 0x2800 + m;
                        char utf8[4] = {
                            (char)(0xE0 | (cp >> 12)),
                            (char)(0x80 | ((cp >> 6) & 0x3F)),
                            (char)(0x80 | (cp & 0x3F)),
                            '\0'
                        };
                        attron(COLOR_PAIR(3) | A_BOLD);
                        printw("%s", utf8);
                        attroff(COLOR_PAIR(3) | A_BOLD);
                    }
                }
            }

            // Adaptive frequency milestones along bottom
            cur_y += graph_h;
            static const struct { float freq; const char *label; } s_ticks[] = {
                { 20.0f, "20Hz" }, { 50.0f, "50" }, { 100.0f, "100" }, { 200.0f, "200" },
                { 500.0f, "500" }, { 1000.0f, "1k" }, { 2000.0f, "2k" }, { 5000.0f, "5k" },
                { 10000.0f, "10k" }, { 20000.0f, "20k" }
            };

            int last_end_x = -1;
            attron(COLOR_PAIR(2) | A_DIM);
            for (size_t t = 0; t < sizeof(s_ticks) / sizeof(s_ticks[0]); t++) {
                float norm = log10f(s_ticks[t].freq / 20.0f) / 3.0f;
                int pos_x = graph_x + (int)roundf(norm * (float)(graph_w - 1));
                int len = (int)strlen(s_ticks[t].label);

                if (pos_x + len <= graph_x + graph_w && pos_x > last_end_x + 1) {
                    mvprintw(cur_y, pos_x, "%s", s_ticks[t].label);
                    last_end_x = pos_x + len;
                }
            }
            attroff(COLOR_PAIR(2) | A_DIM);

            cur_y += 2;
        }
        if (grid) free(grid);
        if (curve_db) free(curve_db);
    }

    // Preamp row
    bool is_preamp = (s_peq_row == -1);
    float preamp = peq_get_preamp();

    mvhline(cur_y, x + 2, ' ', w - 4);
    mvprintw(cur_y, x + 3, " Preamp: ");

    if (is_preamp && s_peq_editing) {
        attron(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
        printw(" [ %s_ ] ", s_edit_buf);
        attroff(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
    } else if (is_preamp) {
        attron(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
        printw(" %+6.2f dB ", preamp);
        attroff(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
    } else {
        attron(COLOR_PAIR(2) | A_BOLD);
        printw("%+6.2f dB", preamp);
        attroff(COLOR_PAIR(2) | A_BOLD);
    }

    attron(COLOR_PAIR(2) | A_DIM);
    printw("   (Enter/e: type value, [<-/->]: 0.10dB, [[/]]: 0.50dB, 0: reset)");
    attroff(COLOR_PAIR(2) | A_DIM);

    cur_y += 2;

    // Table Header
    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(cur_y, x + 3, "Band   Type      Frequency       Gain         Q      Status");
    attroff(COLOR_PAIR(1) | A_BOLD);
    cur_y++;

    int max_bands = (y + h - 3) - cur_y;
    if (max_bands > PEQ_MAX_BANDS) max_bands = PEQ_MAX_BANDS;

    for (int b = 0; b < max_bands; b++) {
        PEQBand band;
        peq_get_band(b, &band);
        bool is_row_sel = (s_peq_row == b);
        int row_y = cur_y + b;

        mvhline(row_y, x + 2, ' ', w - 4);

        // Band number indicator
        if (is_row_sel) {
            attron(COLOR_PAIR(4) | A_BOLD);
            mvprintw(row_y, x + 3, ">%02d   ", b + 1);
            attroff(COLOR_PAIR(4) | A_BOLD);
        } else {
            attron(COLOR_PAIR(2) | A_DIM);
            mvprintw(row_y, x + 3, " %02d   ", b + 1);
            attroff(COLOR_PAIR(2) | A_DIM);
        }

        // Column 0: Filter Type
        bool sel_type = (is_row_sel && s_peq_col == 0);
        if (sel_type) attron(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
        else attron(COLOR_PAIR(2));
        printw(" %-5s ", peq_get_filter_name(band.type));
        if (sel_type) attroff(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
        else attroff(COLOR_PAIR(2));
        printw("  ");

        // Column 1: Frequency
        bool sel_freq = (is_row_sel && s_peq_col == 1);
        if (sel_freq && s_peq_editing) {
            attron(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
            printw(" [ %s_ ] ", s_edit_buf);
            attroff(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
        } else if (sel_freq) {
            attron(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
            printw(" %7.1f Hz ", band.freq);
            attroff(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
        } else {
            attron(COLOR_PAIR(2));
            printw(" %7.1f Hz ", band.freq);
            attroff(COLOR_PAIR(2));
        }
        printw("  ");

        // Column 2: Gain
        bool sel_gain = (is_row_sel && s_peq_col == 2);
        if (band.type == PEQ_FILTER_HIGH_PASS || band.type == PEQ_FILTER_LOW_PASS) {
            if (sel_gain) attron(A_REVERSE | COLOR_PAIR(2));
            printw("   ---    ");
            if (sel_gain) attroff(A_REVERSE | COLOR_PAIR(2));
        } else if (sel_gain && s_peq_editing) {
            attron(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
            printw(" [ %s_ ] ", s_edit_buf);
            attroff(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
        } else if (sel_gain) {
            attron(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
            printw(" %+6.1f dB ", band.gain_db);
            attroff(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
        } else {
            attron(COLOR_PAIR(band.gain_db > 0.0f ? 3 : (band.gain_db < 0.0f ? 10 : 2)));
            printw(" %+6.1f dB ", band.gain_db);
            attroff(COLOR_PAIR(band.gain_db > 0.0f ? 3 : (band.gain_db < 0.0f ? 10 : 2)));
        }
        printw("  ");

        // Column 3: Q Factor
        bool sel_q = (is_row_sel && s_peq_col == 3);
        if (sel_q && s_peq_editing) {
            attron(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
            printw(" [ %s_ ] ", s_edit_buf);
            attroff(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
        } else if (sel_q) {
            attron(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
            printw("  %5.2f  ", band.q);
            attroff(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
        } else {
            attron(COLOR_PAIR(2));
            printw("  %5.2f  ", band.q);
            attroff(COLOR_PAIR(2));
        }
        printw("  ");

        // Column 4: Status (ON / OFF)
        bool sel_st = (is_row_sel && s_peq_col == 4);
        if (sel_st) attron(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
        else attron(COLOR_PAIR(band.enabled ? 3 : 2));
        printw(" [%s] ", band.enabled ? "ON" : "OFF");
        if (sel_st) attroff(A_REVERSE | COLOR_PAIR(4) | A_BOLD);
        else attroff(COLOR_PAIR(band.enabled ? 3 : 2));
    }

    attron(A_DIM | COLOR_PAIR(2));
    if (s_peq_editing) {
        mvprintw(y + h - 2, x + 2, "EDITING: Type number and press [Enter] to confirm, [Esc] to cancel");
    } else {
        mvprintw(y + h - 2, x + 2, "[P] Presets/AutoEQ  [S] Save  [Enter/e] Edit  [t] Type  [Space] On/Off  [Tab] Graphic  [Esc]");
    }
    attroff(A_DIM | COLOR_PAIR(2));
}

void draw_eq_panel(int y, int x, int h, int w) {
    if (h < 8 || w < 20) return;

    bool enabled = eq_is_enabled();
    EQMode mode = eq_get_mode();

    char title[64];
    snprintf(title, sizeof(title), "Equalizer [%s] [%s]",
             enabled ? "ACTIVE" : "BYPASSED",
             mode == EQ_MODE_GRAPHIC ? "GRAPHIC" : "PARAMETRIC");
    ui_draw_box(y, x, h, w, title, enabled ? 4 : 2);

    attron(COLOR_PAIR(2));
    mvprintw(y + 1, x + 3, "Mode: ");
    attroff(COLOR_PAIR(2));
    attron(A_BOLD | COLOR_PAIR(enabled ? 3 : 2));
    printw("%-11s", mode == EQ_MODE_GRAPHIC ? "Graphic" : "Parametric");
    attroff(A_BOLD | COLOR_PAIR(enabled ? 3 : 2));

    if (mode == EQ_MODE_GRAPHIC) {
        int cur_p = eq_get_current_preset();
        attron(COLOR_PAIR(2));
        printw("  Preset: ");
        attroff(COLOR_PAIR(2));
        attron(A_BOLD | COLOR_PAIR(enabled ? 3 : 2));
        printw("%-12s", eq_get_preset_name(cur_p));
        attroff(A_BOLD | COLOR_PAIR(enabled ? 3 : 2));
    } else {
        attron(COLOR_PAIR(2) | A_DIM);
        printw("  Targets: [1] Harman Over-Ear  [2] Harman In-Ear");
        attroff(COLOR_PAIR(2) | A_DIM);
    }

    attron(COLOR_PAIR(enabled ? 4 : 2));
    mvhline(y + 2, x + 1, ACS_HLINE, w - 2);
    attroff(COLOR_PAIR(enabled ? 4 : 2));

    if (mode == EQ_MODE_GRAPHIC) {
        draw_graphic_panel(y, x, h, w);
    } else {
        draw_parametric_panel(y, x, h, w);
    }
}