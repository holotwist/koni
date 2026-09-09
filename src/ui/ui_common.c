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
UICache ui_cache = { .idx = -2, .loaded_track_id = -1 };

int utf8_display_width(const char *str) {
    if (!str) return 0;
    int width = 0;
    mbstate_t state = {0};
    const char *p = str;
    while (*p != '\0') {
        if ((unsigned char)*p < 128) {
            width++;
            p++;
            continue;
        }
        wchar_t wc;
        size_t len = mbrtowc(&wc, p, MB_CUR_MAX, &state);
        if (len == (size_t)-1 || len == (size_t)-2 || len == 0) {
            memset(&state, 0, sizeof(state));
            if (len == (size_t)-1 || len == (size_t)-2) {
                width++;
                p++;
            } else {
                break;
            }
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
        if ((unsigned char)*p < 128) {
            if (current_width + 1 > target_width) break;
            current_width++;
            p++;
            byte_offset++;
            continue;
        }
        wchar_t wc;
        size_t len = mbrtowc(&wc, p, MB_CUR_MAX, &state);
        if (len == (size_t)-1 || len == (size_t)-2 || len == 0) {
            memset(&state, 0, sizeof(state));
            if (len == (size_t)-1 || len == (size_t)-2) {
                if (current_width + 1 > target_width) break;
                current_width++;
                p++;
                byte_offset++;
            } else {
                break;
            }
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
        if ((unsigned char)*p < 128) {
            current_width--;
            p++;
            byte_offset++;
            continue;
        }
        wchar_t wc;
        size_t len = mbrtowc(&wc, p, MB_CUR_MAX, &state);
        if (len == (size_t)-1 || len == (size_t)-2 || len == 0) {
            memset(&state, 0, sizeof(state));
            if (len == (size_t)-1 || len == (size_t)-2) {
                current_width--;
                p++;
                byte_offset++;
            } else {
                break;
            }
            continue;
        }
        int w = wcwidth(wc);
        if (w > 0) current_width -= w;
        p += len;
        byte_offset += len;
    }
    return byte_offset;
}

static void format_cell_truncated(char *dst, size_t dst_size, const char *text, int col_w) {
    if (!dst || dst_size == 0) return;
    memset(dst, 0, dst_size);
    if (col_w <= 0) return;

    if (!text || text[0] == '\0') {
        int pad = (col_w < (int)dst_size) ? col_w : (int)dst_size - 1;
        memset(dst, ' ', (size_t)pad);
        dst[pad] = '\0';
        return;
    }

    int text_w = utf8_display_width(text);
    if (text_w <= col_w) {
        int bytes = (int)strlen(text);
        if ((size_t)bytes >= dst_size) bytes = (int)dst_size - 1;
        memcpy(dst, text, (size_t)bytes);
        int pad = col_w - text_w;
        if (bytes + pad >= (int)dst_size) pad = (int)dst_size - 1 - bytes;
        if (pad > 0) memset(dst + bytes, ' ', (size_t)pad);
        dst[bytes + (pad > 0 ? pad : 0)] = '\0';
    } else {
        int max_target = (col_w >= 4) ? (col_w - 3) : (col_w >= 2 ? col_w - 2 : col_w);
        int body_bytes = utf8_byte_offset_for_width(text, max_target);
        if (col_w >= 4) {
            snprintf(dst, dst_size, "%.*s...", body_bytes, text);
        } else if (col_w >= 2) {
            snprintf(dst, dst_size, "%.*s..", body_bytes, text);
        } else {
            snprintf(dst, dst_size, "%.*s", body_bytes, text);
        }
        int cur_w = utf8_display_width(dst);
        int pad = col_w - cur_w;
        if (pad > 0) {
            size_t slen = strlen(dst);
            if (slen + (size_t)pad < dst_size) {
                memset(dst + slen, ' ', (size_t)pad);
                dst[slen + (size_t)pad] = '\0';
            }
        }
    }
}

static void sanitize_text(char *dst, const char *src, size_t dst_size) {
    if (!dst || dst_size == 0) return;
    if (!src) { dst[0] = '\0'; return; }

    size_t d = 0;
    const unsigned char *s = (const unsigned char *)src;

    while (*s && d + 1 < dst_size) {
        unsigned char c = *s;
        if (c < 32) {
            dst[d++] = ' ';
            s++;
        } else if (c < 0x80) { // Valid ASCII
            dst[d++] = (char)c;
            s++;
        } else if ((c >= 0xC2 && c <= 0xDF) && (s[1] >= 0x80 && s[1] <= 0xBF)) { // Valid 2-byte UTF-8
            if (d + 2 >= dst_size) break;
            dst[d++] = (char)s[0];
            dst[d++] = (char)s[1];
            s += 2;
        } else if (c == 0xE0 && (s[1] >= 0xA0 && s[1] <= 0xBF) && (s[2] >= 0x80 && s[2] <= 0xBF)) { // Valid 3-byte UTF-8
            if (d + 3 >= dst_size) break;
            dst[d++] = (char)s[0];
            dst[d++] = (char)s[1];
            dst[d++] = (char)s[2];
            s += 3;
        } else if (((c >= 0xE1 && c <= 0xEC) || c == 0xEE || c == 0xEF) &&
                   (s[1] >= 0x80 && s[1] <= 0xBF) && (s[2] >= 0x80 && s[2] <= 0xBF)) {
            if (d + 3 >= dst_size) break;
            dst[d++] = (char)s[0];
            dst[d++] = (char)s[1];
            dst[d++] = (char)s[2];
            s += 3;
        } else if (c == 0xED && (s[1] >= 0x80 && s[1] <= 0x9F) && (s[2] >= 0x80 && s[2] <= 0xBF)) {
            if (d + 3 >= dst_size) break;
            dst[d++] = (char)s[0];
            dst[d++] = (char)s[1];
            dst[d++] = (char)s[2];
            s += 3;
        } else if (c >= 0xF0 && c <= 0xF4 && (s[1] >= 0x80 && s[1] <= 0xBF) &&
                   (s[2] >= 0x80 && s[2] <= 0xBF) && (s[3] >= 0x80 && s[3] <= 0xBF)) { // Valid 4-byte UTF-8
            if (d + 4 >= dst_size) break;
            dst[d++] = (char)s[0];
            dst[d++] = (char)s[1];
            dst[d++] = (char)s[2];
            dst[d++] = (char)s[3];
            s += 4;
        } else {
            // Convert standalone ISO-8859-1 / Latin-1 byte (0x80-0xFF) to valid UTF-8
            if (d + 2 < dst_size) {
                dst[d++] = (char)(0xC0 | (c >> 6));
                dst[d++] = (char)(0x80 | (c & 0x3F));
            } else {
                dst[d++] = ' ';
            }
            s++;
        }
    }
    dst[d] = '\0';
}

void format_list_item(char* out_buf, size_t out_size, int max_w, const char* filename, KoniMetadata* meta, uint32_t duration_sec, bool is_dir, bool is_fav) {
    if (!out_buf || out_size == 0) return;
    out_buf[0] = '\0';
    if (max_w < 4) return;

    const char *fav_icon = is_dir ? "" : (is_fav ? "★ " : "  ");
    int icon_len = utf8_display_width(fav_icon);

    char clean_file[256];
    sanitize_text(clean_file, filename, sizeof(clean_file));

    if (is_dir) {
        int b = utf8_byte_offset_for_width(clean_file, max_w);
        snprintf(out_buf, out_size, "%.*s", b, clean_file);
        return;
    }

    char time_str[16] = "";
    int time_w = 0;
    if (duration_sec > 0) {
        snprintf(time_str, sizeof(time_str), "%02u:%02u", duration_sec / 60, duration_sec % 60);
        time_w = utf8_display_width(time_str);
    }

    char clean_title[256] = {0}, clean_artist[256] = {0}, clean_album[256] = {0};
    if (meta) {
        sanitize_text(clean_title, meta->title, sizeof(clean_title));
        sanitize_text(clean_artist, meta->artist, sizeof(clean_artist));
        sanitize_text(clean_album, meta->album, sizeof(clean_album));
    }

    const char *title = clean_title[0] ? clean_title : clean_file;
    int reserved_tail = (time_w > 0) ? (time_w + 1) : 0;
    int avail_w = max_w - icon_len - reserved_tail;
    if (avail_w < 5) avail_w = 5;

    bool has_artist = (clean_artist[0] != '\0');
    bool has_album  = (clean_album[0] != '\0');

    // Multi-column mode, only when screen is wide AND at least artist or album is known
    if (max_w >= 60 && (has_artist || has_album)) {
        if (has_artist && has_album) {
            // 3 Columns, Title, Artist, Album
            int total_content_w = avail_w - 2; // 2 inter-column spaces
            if (total_content_w < 15) total_content_w = 15;

            int len_t  = utf8_display_width(title);
            int len_a  = utf8_display_width(clean_artist);
            int len_al = utf8_display_width(clean_album);

            int w_t  = total_content_w * 40 / 100;
            int w_a  = total_content_w * 30 / 100;
            int w_al = total_content_w - w_t - w_a;

            // Dynamically donate surplus columns from short fields to longer ones
            int surplus_t  = (len_t < w_t)  ? (w_t - (len_t < 10 ? 10 : len_t))  : 0;
            int surplus_a  = (len_a < w_a)  ? (w_a - (len_a < 8 ? 8 : len_a))    : 0;
            int surplus_al = (len_al < w_al)? (w_al - (len_al < 8 ? 8 : len_al)) : 0;

            if (surplus_t > 0 && (len_al > w_al || len_a > w_a)) {
                int give = surplus_t;
                if (len_al > w_al && len_a > w_a) {
                    w_al += give / 2;
                    w_a  += give - (give / 2);
                } else if (len_al > w_al) {
                    w_al += give;
                } else {
                    w_a += give;
                }
                w_t -= give;
            }
            if (surplus_a > 0 && len_al > w_al) {
                int give = surplus_a / 2;
                w_al += give;
                w_a  -= give;
            }
            if (surplus_al > 0 && len_t > w_t) {
                int give = surplus_al / 2;
                w_t  += give;
                w_al -= give;
            }

            // Guarantee sum matches exactly total_content_w
            w_al = total_content_w - w_t - w_a;
            if (w_al < 6) {
                int def = 6 - w_al;
                w_al = 6;
                if (w_t > 12 + def) w_t -= def;
                else if (w_a > 10 + def) w_a -= def;
            }

            char c_title[256] = {0}, c_artist[256] = {0}, c_album[256] = {0};
            format_cell_truncated(c_title, sizeof(c_title), title, w_t);
            format_cell_truncated(c_artist, sizeof(c_artist), clean_artist, w_a);
            format_cell_truncated(c_album, sizeof(c_album), clean_album, w_al);

            if (time_w > 0) {
                snprintf(out_buf, out_size, "%s%s %s %s %s", fav_icon, c_title, c_artist, c_album, time_str);
            } else {
                snprintf(out_buf, out_size, "%s%s %s %s", fav_icon, c_title, c_artist, c_album);
            }
        } else {
            // 2 Columns, Title + either Artist or Album
            const char *sec_text = has_artist ? clean_artist : clean_album;
            int total_content_w = avail_w - 1; // 1 inter-column space
            if (total_content_w < 10) total_content_w = 10;

            int len_t = utf8_display_width(title);
            int len_s = utf8_display_width(sec_text);

            int w_t = total_content_w * 55 / 100;
            int w_s = total_content_w - w_t;

            if (len_t < w_t && len_s > w_s) {
                int surplus = w_t - (len_t < 12 ? 12 : len_t);
                w_s += surplus;
                w_t -= surplus;
            } else if (len_s < w_s && len_t > w_t) {
                int surplus = w_s - (len_s < 10 ? 10 : len_s);
                w_t += surplus;
                w_s -= surplus;
            }
            w_s = total_content_w - w_t;

            char c_title[256] = {0}, c_sec[256] = {0};
            format_cell_truncated(c_title, sizeof(c_title), title, w_t);
            format_cell_truncated(c_sec, sizeof(c_sec), sec_text, w_s);

            if (time_w > 0) {
                snprintf(out_buf, out_size, "%s%s %s %s", fav_icon, c_title, c_sec, time_str);
            } else {
                snprintf(out_buf, out_size, "%s%s %s", fav_icon, c_title, c_sec);
            }
        }
    } else {
        // Single Combined Column, Title [- Artist]
        char body[512];
        if (has_artist && has_album && max_w >= 45) {
            snprintf(body, sizeof(body), "%s - %s [%s]", title, clean_artist, clean_album);
        } else if (has_artist) {
            snprintf(body, sizeof(body), "%s - %s", title, clean_artist);
        } else {
            snprintf(body, sizeof(body), "%s", title);
        }

        char c_body[512];
        format_cell_truncated(c_body, sizeof(c_body), body, avail_w);

        if (time_w > 0) {
            snprintf(out_buf, out_size, "%s%s %s", fav_icon, c_body, time_str);
        } else {
            snprintf(out_buf, out_size, "%s%s", fav_icon, c_body);
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
    if (title && w > 6) {
        int max_title_w = w - 6;
        int t_bytes = utf8_byte_offset_for_width(title, max_title_w);
        attron(A_REVERSE);
        mvprintw(y, x + 2, " %.*s ", t_bytes, title);
        attroff(A_REVERSE);
    }
    attroff(COLOR_PAIR(color_pair));
}