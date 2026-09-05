#include "extension.h"
#include "ui_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ncurses.h>

static int s_channel_offset = 0; // Horizontal scroll for units

static void tracker_render(KoniExtension *ext, int y, int x, int h, int w) {
    (void)ext;
    if (h < 5 || w < 20) return;

    pthread_mutex_lock(&state_mutex);

    KoniHostContext *host = koni_host_get_context();
    KoniTrackerInterface *tracker = (KoniTrackerInterface*)host->query_codec_interface(KONI_IFACE_TRACKER);
    KoniDecoder *dec = host->get_current_decoder();

    if (!tracker || !dec) {
        pthread_mutex_unlock(&state_mutex);
        mvprintw(y + 2, x + 4, "No active tracker engine for this format.");
        return;
    }

    uint32_t total_channels = tracker->get_num_channels(dec);
    uint32_t cur_row = tracker->get_current_row(dec);
    uint32_t total_rows = tracker->get_total_rows(dec);
    uint32_t bpm = tracker->get_bpm(dec);

    // Number of channels that fit on screen (6 chars for "Row  |" + 13 chars per channel)
    int channel_col_width = 13;
    int visible_channels = (w - 8) / channel_col_width;
    if (visible_channels < 1) visible_channels = 1;

    // Keep channel scroll offset in bounds
    if (s_channel_offset < 0) s_channel_offset = 0;
    if (total_channels > 0 && s_channel_offset >= (int)total_channels) {
        s_channel_offset = (int)total_channels - 1;
    }

    // Top status line
    int end_ch = s_channel_offset + visible_channels;
    if (end_ch > (int)total_channels) end_ch = (int)total_channels;
    attron(COLOR_PAIR(4) | A_BOLD);
    mvprintw(y, x + 2, " BPM:%-3u Row:%04u/%04u [Units %02d-%02d of %02u] (</> or [/]:scroll) ",
             bpm, cur_row, total_rows, s_channel_offset,
             end_ch > 0 ? end_ch - 1 : 0, total_channels);
    attroff(COLOR_PAIR(4) | A_BOLD);

    // Column Headers (exactly 6 chars for row, exactly 13 chars per unit)
    int header_y = y + 1;
    attron(COLOR_PAIR(1) | A_BOLD);
    mvprintw(header_y, x + 1, "Row  |");
    for (int c = 0; c < visible_channels; c++) {
        uint32_t ch_idx = (uint32_t)s_channel_offset + c;
        if (ch_idx < total_channels) {
            printw("  Unit %02u   |", ch_idx);
        }
    }
    attroff(COLOR_PAIR(1) | A_BOLD);

    // Pattern rows centered on playback row
    int list_h = h - 2;
    int center_offset = list_h / 2;

    for (int i = 0; i < list_h; i++) {
        int draw_y = y + 2 + i;
        int row_idx = (int)cur_row - center_offset + i;
        bool is_cur_row = (row_idx == (int)cur_row);
        bool is_beat = (row_idx >= 0 && (row_idx % 4 == 0));

        mvhline(draw_y, x + 1, ' ', w - 2);

        // Out-of-bounds rows before song start or after song end
        if (row_idx < 0 || (uint32_t)row_idx >= total_rows) {
            attron(COLOR_PAIR(2) | A_DIM);
            mvprintw(draw_y, x + 1, ".... |");
            for (int c = 0; c < visible_channels; c++) {
                uint32_t ch_idx = (uint32_t)s_channel_offset + c;
                if (ch_idx >= total_channels) break;
                printw(" ... .. ... |");
            }
            attroff(COLOR_PAIR(2) | A_DIM);
            continue;
        }

        // Row number (exactly 6 characters: "%04u |")
        if (is_cur_row) attron(COLOR_PAIR(4) | A_REVERSE | A_BOLD);
        else if (is_beat) attron(COLOR_PAIR(1) | A_BOLD);
        else attron(COLOR_PAIR(2));

        mvprintw(draw_y, x + 1, "%04u |", (uint32_t)row_idx);

        if (is_cur_row) attroff(COLOR_PAIR(4) | A_REVERSE | A_BOLD);
        else if (is_beat) attroff(COLOR_PAIR(1) | A_BOLD);
        else attroff(COLOR_PAIR(2));

        // Draw cells for each visible channel (each cell is strictly 13 characters)
        for (int c = 0; c < visible_channels; c++) {
            uint32_t ch_idx = (uint32_t)s_channel_offset + c;
            if (ch_idx >= total_channels) break;

            char note[4] = "...";
            uint8_t inst = 0, vol = 0;
            tracker->get_cell_data(dec, (uint32_t)row_idx, ch_idx, note, &inst, &vol);
            bool has_note = (strcmp(note, "...") != 0 && strcmp(note, "---") != 0);

            if (is_cur_row) {
                attron(COLOR_PAIR(4) | A_REVERSE | A_BOLD);
            } else if (has_note) {
                attron(COLOR_PAIR(3) | A_BOLD); // Highlight active notes in green
            } else {
                attron(COLOR_PAIR(2) | A_DIM);
            }

            if (has_note && inst > 0 && vol > 0) {
                printw(" %s %02u %03u |", note, inst, vol);
            } else if (has_note && inst > 0) {
                printw(" %s %02u ... |", note, inst);
            } else if (has_note && vol > 0) {
                printw(" %s .. %03u |", note, vol);
            } else if (has_note) {
                printw(" %s .. ... |", note);
            } else {
                printw(" ... .. ... |");
            }

            if (is_cur_row) {
                attroff(COLOR_PAIR(4) | A_REVERSE | A_BOLD);
            } else if (has_note) {
                attroff(COLOR_PAIR(3) | A_BOLD);
            } else {
                attroff(COLOR_PAIR(2) | A_DIM);
            }
        }
    }

    pthread_mutex_unlock(&state_mutex);
}

static bool tracker_handle_key(KoniExtension *ext, int ch, KoniHostContext *host) {
    // Horizontal unit navigation when tracker tab is focused
    if (active_tab == ext->tab.tab_id) {
        if (ch == '<' || ch == ',' || ch == '[') {
            if (s_channel_offset > 0) {
                s_channel_offset--;
                force_redraw = true;
                return true;
            }
        } else if (ch == '>' || ch == '.' || ch == ']') {
            pthread_mutex_lock(&state_mutex);
            KoniTrackerInterface *tracker = (KoniTrackerInterface*)host->query_codec_interface(KONI_IFACE_TRACKER);
            KoniDecoder *dec = host->get_current_decoder();
            uint32_t total_channels = (tracker && dec) ? tracker->get_num_channels(dec) : 0;
            if (total_channels > 0 && s_channel_offset + 1 < (int)total_channels) {
                s_channel_offset++;
                force_redraw = true;
            }
            pthread_mutex_unlock(&state_mutex);
            return true;
        }
    }
    return false;
}

KoniExtension tracker_extension = {
    .id = "koni.tracker",
    .name = "PxTone Tracker View",
    .version = "1.0.0",
    .author = "holotwist",
    .activation_mode = EXT_ACTIVE_ON_CODEC_CAP,
    .required_codec_cap = KONI_CODEC_CAP_TRACKER,
    .provides_tab = true,
    .tab = {
        .tab_name = "tracker",
        .render = tracker_render
    },
    .handle_key = tracker_handle_key,
    .init = NULL,
    .shutdown = NULL,
    .on_track_loaded = NULL,
    .on_track_stopped = NULL,
    .on_tick = NULL,
    .call = NULL
};