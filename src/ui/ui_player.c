#include "ui_common.h"
#include "ui_animations.h"
#include <string.h>
#include <math.h>

void draw_player_panel(int y, int x, int h, int w) {
    if (h < 3 || w < 10) return;
    ui_draw_box(y, x, h, w, "player", 1);
    
    uint32_t cur_sec = atomic_load(&p_current_sec);
    uint32_t tot_sec = atomic_load(&p_total_sec);
    
    int r_mode = atomic_load(&play_mode_repeat);
    bool shuf = atomic_load(&play_mode_shuffle);
    int rgain = (int)atomic_load(&play_mode_rgain);
    int vol = atomic_load(&volume);
    
    // Title and indicators
    int line1_y = y + 1;
    int ind_w = 32; // Space needed for indicators
    
    if (w > ind_w + 15) {
        // Right-aligned indicators
        int curr_x = x + w - 12;
        mvprintw(line1_y, curr_x, " Vol:%3d%%", vol);
        
        curr_x -= 9;
        int display_rgain = rgain;
        // If Meta mode, but the song does not have metadata for ReplayGain, just change to Calc
        if (rgain == 1 && !ui_cache.meta.has_track_gain) display_rgain = 2;
        
        if (rgain != 0) attron(A_REVERSE | COLOR_PAIR(5)); else attron(A_DIM | COLOR_PAIR(2));
        if (display_rgain == 1) mvprintw(line1_y, curr_x, " RG:Meta ");
        else if (display_rgain == 2) mvprintw(line1_y, curr_x, " RG:Calc ");
        else mvprintw(line1_y, curr_x, " RG:Off  ");
        if (rgain != 0) attroff(A_REVERSE | COLOR_PAIR(5)); else attroff(A_DIM | COLOR_PAIR(2));
        
        curr_x -= 5;
        if (r_mode != REPEAT_OFF) attron(A_REVERSE | COLOR_PAIR(5)); else attron(A_DIM | COLOR_PAIR(2));
        mvprintw(line1_y, curr_x, (r_mode == REPEAT_ONE) ? " 1 " : " R ");
        if (r_mode != REPEAT_OFF) attroff(A_REVERSE | COLOR_PAIR(5)); else attroff(A_DIM | COLOR_PAIR(2));

        curr_x -= 5;
        if (shuf) attron(A_REVERSE | COLOR_PAIR(5)); else attron(A_DIM | COLOR_PAIR(2));
        mvprintw(line1_y, curr_x, " S ");
        if (shuf) attroff(A_REVERSE | COLOR_PAIR(5)); else attroff(A_DIM | COLOR_PAIR(2));
        
        // Left-aligned Title
        const char* prefix = "Playing: ";
        attron(A_DIM | COLOR_PAIR(2));
        mvprintw(line1_y, x + 2, "%s", prefix);
        attroff(A_DIM | COLOR_PAIR(2));
        
        int title_start = x + 2 + strlen(prefix);
        int max_title_w = (curr_x - 1) - title_start;
        
        if (max_title_w > 0) {
            char title_buf[512] = {0};
            if (ui_cache.meta.title && ui_cache.meta.artist && strlen(ui_cache.meta.title) > 0 && strlen(ui_cache.meta.artist) > 0) {
                snprintf(title_buf, sizeof(title_buf), "%s by %s", ui_cache.meta.title, ui_cache.meta.artist);
            } else if (ui_cache.meta.title && strlen(ui_cache.meta.title) > 0) {
                snprintf(title_buf, sizeof(title_buf), "%s", ui_cache.meta.title);
            } else if (playing_file_idx >= 0) {
                snprintf(title_buf, sizeof(title_buf), "%s", ui_cache.filename);
            } else {
                snprintf(title_buf, sizeof(title_buf), "<No Song Selected>");
            }
            
            char disp_buf[1024] = {0};
            int title_w = utf8_display_width(title_buf); // Safe to call once per frame
            get_marquee_text(title_buf, title_w, max_title_w, ui_frame_counter, disp_buf, sizeof(disp_buf));
            
            attron(A_BOLD | COLOR_PAIR(2));
            mvprintw(line1_y, title_start, "%s", disp_buf);
            attroff(A_BOLD | COLOR_PAIR(2));
        }
    }
    
    // Progress bar
    if (h >= 4) {
        int prog_y = y + 2;
        char time_str[32];
        snprintf(time_str, sizeof(time_str), "%02u:%02u / %02u:%02u", cur_sec / 60, cur_sec % 60, tot_sec / 60, tot_sec % 60);
        
        int time_len = 15; // Width of "00:00 / 00:00"
        mvprintw(prog_y, x + 2, "%s", time_str);
        
        int bar_start = x + 2 + time_len + 1;
        int bar_w = (x + w - 2) - bar_start;
        
        if (bar_w > 2) {
            float progress = (tot_sec > 0) ? ((float)cur_sec / (float)tot_sec) : 0.0f;
            if (progress > 1.0f) progress = 1.0f;
            if (progress < 0.0f) progress = 0.0f;
            
            int inner_w = bar_w - 2; // Subtract space for [ and ]
            int filled_w = (int)(progress * inner_w);
            
            attron(COLOR_PAIR(2));
            mvprintw(prog_y, bar_start, "[");
            
            attron(COLOR_PAIR(5)); // Magenta highlight
            for (int i = 0; i < filled_w; i++) {
                mvprintw(prog_y, bar_start + 1 + i, "\xE2\x96\x88"); // Filled block (█)
            }
            attroff(COLOR_PAIR(5));
            
            attron(COLOR_PAIR(2) | A_DIM);
            for (int i = filled_w; i < inner_w; i++) {
                mvprintw(prog_y, bar_start + 1 + i, "\xE2\x96\x91"); // Dimmed empty block (░)
            }
            attroff(A_DIM);
            
            mvprintw(prog_y, bar_start + 1 + inner_w, "]");
            attroff(COLOR_PAIR(2));
        }
    }
    
    // VU-meter
    if (h >= 5 && w >= 22) {
        int vu_y = y + 3;
        int center_x = x + (w / 2);
        
        uint32_t srate = atomic_load(&vis_srate);
        if (srate == 0) srate = 44100;
        uint32_t vu_window = (srate * 50u) / 1000u; // 50ms window
        float peak_l = 0.0f, peak_r = 0.0f;
        // Step by 2
        for (uint32_t i = 0; i < vu_window; i += 2) {
            if (i > ui_cache.smooth_rpos) break;
            uint32_t idx = (ui_cache.smooth_rpos - i) & VIS_BUF_MASK;
            float l = vis_ring_l[idx]; if (l < 0.0f) l = -l;
            float r = vis_ring_r[idx]; if (r < 0.0f) r = -r;
            if (l > peak_l) peak_l = l;
            if (r > peak_r) peak_r = r;
        }

        int clip_l = (peak_l > 1.0f);
        int clip_r = (peak_r > 1.0f);
        
        static int clip_hold_l = 0; static int clip_hold_r = 0;
        if (clip_l) clip_hold_l = 20; else if (clip_hold_l > 0) clip_hold_l--;
        if (clip_r) clip_hold_r = 20; else if (clip_hold_r > 0) clip_hold_r--;

        float db_l = (peak_l < 0.001f) ? -60.0f : 20.0f * log10f(peak_l);
        float db_r = (peak_r < 0.001f) ? -60.0f : 20.0f * log10f(peak_r);
        peak_l = (db_l + 40.0f) / 40.0f; peak_r = (db_r + 40.0f) / 40.0f;
        if (peak_l < 0.0f) peak_l = 0.0f;
        if (peak_l > 1.0f) peak_l = 1.0f;
        if (peak_r < 0.0f) peak_r = 0.0f;
        if (peak_r > 1.0f) peak_r = 1.0f;

        static float smooth_peak_l = 0.0f; static float smooth_peak_r = 0.0f;
        if (peak_l > smooth_peak_l) smooth_peak_l = peak_l; else { smooth_peak_l -= 0.03f; if (smooth_peak_l < 0.0f) smooth_peak_l = 0.0f; }
        if (peak_r > smooth_peak_r) smooth_peak_r = peak_r; else { smooth_peak_r -= 0.03f; if (smooth_peak_r < 0.0f) smooth_peak_r = 0.0f; }

        int bar_len = (w - 10) / 2;
        if (bar_len > 24) bar_len = 24;
        if (bar_len < 5) bar_len = 5;

        int val_l = (int)(smooth_peak_l * bar_len); if (val_l > bar_len) val_l = bar_len;
        int val_r = (int)(smooth_peak_r * bar_len); if (val_r > bar_len) val_r = bar_len;

        attron(COLOR_PAIR(2));
        mvaddstr(vu_y, center_x, "|");
        mvaddstr(vu_y, center_x - 1 - bar_len - 2, "L");
        mvaddstr(vu_y, center_x + 1 + bar_len + 2, "R");
        attroff(COLOR_PAIR(2));

        for (int i = 0; i < bar_len; i++) {
            int color; float pct = (float)i / bar_len;
            if (pct < 0.6f) color = 3; else if (pct < 0.85f) color = 4; else color = 10;
            
            attron(COLOR_PAIR(color));
            mvaddstr(vu_y, center_x - 1 - i, (i < val_l) ? "\xE2\x96\x88" : " ");
            mvaddstr(vu_y, center_x + 1 + i, (i < val_r) ? "\xE2\x96\x88" : " ");
            attroff(COLOR_PAIR(color));
        }

        if (clip_hold_l > 0) attron(COLOR_PAIR(10) | A_REVERSE);
        mvaddstr(vu_y, center_x - 1 - bar_len, " ");
        if (clip_hold_l > 0) attroff(COLOR_PAIR(10) | A_REVERSE);

        if (clip_hold_r > 0) attron(COLOR_PAIR(10) | A_REVERSE);
        mvaddstr(vu_y, center_x + 1 + bar_len, " ");
        if (clip_hold_r > 0) attroff(COLOR_PAIR(10) | A_REVERSE);
    }
}