#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#define _XOPEN_SOURCE 600

#include "ui_common.h"
#include <wchar.h>
#include <stdlib.h>
#include <string.h>

unsigned long ui_frame_counter = 0;
bool vis_needs_full_redraw = false;
int ui_last_selected_idx = -1;
int ui_last_playlist_idx = -1;
char current_lyrics_backend[32] = "";
UICache ui_cache = { .idx = -2, .header_loaded_for_idx = -2 };

int utf8_display_width(const char *str) {
    if (!str) return 0;
    int width = 0;
    mbstate_t state = {0};
    const char *p = str;
    while (*p != '\0') {
        if ((unsigned char)*p < 128) { // Fast-path for ASCII
            width++;
            p++;
            continue;
        }
        wchar_t wc;
        size_t len = mbrtowc(&wc, p, MB_CUR_MAX, &state);
        if (len == (size_t)-1 || len == (size_t)-2 || len == 0) {
            if (len == (size_t)-1 || len == (size_t)-2) p++;
            else break;
            continue;
        }
        int w = wcwidth(wc);
        if (w > 0) width += w;
        p += len;
    }
    return width;
}

int utf8_byte_offset_for_width(const char *str, int target_width) {
    if (!str) return 0;
    int current_width = 0;
    int byte_offset = 0;
    mbstate_t state = {0};
    const char *p = str;
    while (*p != '\0') {
        if ((unsigned char)*p < 128) { // Fast-path for ASCII
            if (current_width + 1 > target_width) break;
            current_width++;
            p++;
            byte_offset++;
            continue;
        }
        wchar_t wc;
        size_t len = mbrtowc(&wc, p, MB_CUR_MAX, &state);
        if (len == (size_t)-1 || len == (size_t)-2 || len == 0) {
            if (len == (size_t)-1 || len == (size_t)-2) { p++; byte_offset++; }
            else break;
            continue;
        }
        int w = wcwidth(wc);
        if (w < 0) w = 0;
        if (current_width + w > target_width) break;
        current_width += w;
        p += len;
        byte_offset += len;
    }
    return byte_offset;
}

int utf8_byte_offset_for_suffix(const char *str, int target_width) {
    if (!str) return 0;
    int current_width = utf8_display_width(str);
    if (current_width <= target_width) return 0;
    
    int byte_offset = 0;
    mbstate_t state = {0};
    const char *p = str;
    
    while (*p != '\0' && current_width > target_width) {
        if ((unsigned char)*p < 128) { // Fast-path for ASCII
            current_width--;
            p++;
            byte_offset++;
            continue;
        }
        wchar_t wc;
        size_t len = mbrtowc(&wc, p, MB_CUR_MAX, &state);
        if (len == (size_t)-1 || len == (size_t)-2 || len == 0) {
            if (len == (size_t)-1 || len == (size_t)-2) { p++; byte_offset++; }
            else break;
            continue;
        }
        int w = wcwidth(wc);
        if (w > 0) current_width -= w;
        p += len;
        byte_offset += len;
    }
    return byte_offset;
}

void format_list_item(char* out_buf, size_t out_size, int max_w, const char* filename, KoniMetadata* meta, uint32_t duration_sec, bool is_dir, bool is_fav) {
    const char *fav_icon = is_dir ? "" : (is_fav ? "★ " : "  ");
    int icon_len = utf8_display_width(fav_icon);
    int eff_max_w = max_w - icon_len;
    if (eff_max_w < 1) eff_max_w = 1;

    if (is_dir) {
        snprintf(out_buf, out_size, "%s", filename);
        return;
    }
    
    char time_str[16] = "";
    if (duration_sec > 0) {
        snprintf(time_str, sizeof(time_str), "%02u:%02u", duration_sec / 60, duration_sec % 60);
    }
    
    if (max_w >= 80 && meta && (meta->title || meta->artist || meta->album)) {
        const char* title = meta->title && strlen(meta->title) ? meta->title : filename;
        const char* artist = meta->artist && strlen(meta->artist) ? meta->artist : "";
        const char* album = meta->album && strlen(meta->album) ? meta->album : "";
        
        int w_title = max_w * 35 / 100;
        int w_artist = max_w * 25 / 100;
        int w_album = max_w * 25 / 100;
        
        char t_buf[256] = {0}; char a_buf[256] = {0}; char al_buf[256] = {0};
        
        int t_bytes = utf8_byte_offset_for_width(title, w_title - 2);
        snprintf(t_buf, sizeof(t_buf), "%.*s", t_bytes, title);
        
        int a_bytes = utf8_byte_offset_for_width(artist, w_artist - 2);
        snprintf(a_buf, sizeof(a_buf), "%.*s", a_bytes, artist);
        
        int al_bytes = utf8_byte_offset_for_width(album, w_album - 2);
        snprintf(al_buf, sizeof(al_buf), "%.*s", al_bytes, album);
        
        int t_pad = w_title - utf8_display_width(t_buf); if(t_pad<0) t_pad=0;
        int a_pad = w_artist - utf8_display_width(a_buf); if(a_pad<0) a_pad=0;
        int al_pad = w_album - utf8_display_width(al_buf); if(al_pad<0) al_pad=0;
        
        snprintf(out_buf, out_size, "%s%s%*s %s%*s %s%*s %s", 
            fav_icon, t_buf, t_pad, "", a_buf, a_pad, "", al_buf, al_pad, "", time_str);
    } else if (meta && (meta->title || meta->artist)) {
        const char* title = meta->title && strlen(meta->title) ? meta->title : filename;
        char body[512];
        if (meta->artist && strlen(meta->artist)) {
            snprintf(body, sizeof(body), "%s - %s", title, meta->artist);
        } else {
            snprintf(body, sizeof(body), "%s", title);
        }
        if (duration_sec > 0) {
            snprintf(out_buf, out_size, "%s%s (%s)", fav_icon, body, time_str);
        } else {
            snprintf(out_buf, out_size, "%s%s", fav_icon, body);
        }
    } else {
        if (duration_sec > 0) {
            snprintf(out_buf, out_size, "%s%s (%s)", fav_icon, filename, time_str);
        } else {
            snprintf(out_buf, out_size, "%s%s", fav_icon, filename);
        }
    }
}

void ui_draw_box(int y, int x, int h, int w, const char* title, int color_pair) {
    attron(COLOR_PAIR(color_pair));
    mvhline(y, x+1, ACS_HLINE, w-2);
    mvhline(y+h-1, x+1, ACS_HLINE, w-2);
    mvvline(y+1, x, ACS_VLINE, h-2);
    mvvline(y+1, x+w-1, ACS_VLINE, h-2);
    mvaddch(y, x, ACS_ULCORNER);
    mvaddch(y, x+w-1, ACS_URCORNER);
    mvaddch(y+h-1, x, ACS_LLCORNER);
    mvaddch(y+h-1, x+w-1, ACS_LRCORNER);
    if (title) { attron(A_REVERSE); mvprintw(y, x + 2, " %s ", title); attroff(A_REVERSE); }
    attroff(COLOR_PAIR(color_pair));
}