#include "ui_common.h"
#include <string.h>
#include <stdio.h>

void draw_lrc_overlay(int y, int x, int h, int w) {
    if (h < 3 || w < 10) return;
    
    // Draw a clean box
    ui_draw_box(y, x, h, w, NULL, 2); 
    
    // Prevents ghosting
    mvhline(y + 1, x + 1, ' ', w - 2);

    if (!ui_cache.lrc_doc) return;

    uint32_t srate = atomic_load(&vis_srate);
    if (srate == 0) return;

    uint32_t current_ms = (uint32_t)(((uint64_t)ui_cache.smooth_rpos * 1000) / srate);
    int active_line_idx = lyric_document_get_active_line(ui_cache.lrc_doc, current_ms);

    if (active_line_idx >= 0) {
        const char* text = ui_cache.lrc_doc->lines[active_line_idx].text;
        int txt_w = utf8_display_width(text);
        int max_w = w - 2;
        
        int start_x = x + 1 + (max_w - txt_w) / 2;
        if (start_x < x + 1) start_x = x + 1;
        
        char buf[1024];
        int bytes = utf8_byte_offset_for_width(text, max_w);
        snprintf(buf, sizeof(buf), "%.*s", bytes, text);
        
        // Emphasize the lyrics with yellow/bold
        attron(A_BOLD | COLOR_PAIR(4));
        mvprintw(y + 1, start_x, "%s", buf);
        attroff(A_BOLD | COLOR_PAIR(4));
    }
}