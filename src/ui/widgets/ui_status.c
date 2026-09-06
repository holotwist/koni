#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include "ui_status.h"
#include "ui_common.h"
#include <ncurses.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

static char s_status_msg[256] = {0};
static struct timespec s_expire_time = {0};
static bool s_active = false;

void ui_status_init(void) {
    s_status_msg[0] = '\0';
    s_active = false;
}

void ui_status_set(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(s_status_msg, sizeof(s_status_msg), fmt, args);
    va_end(args);

    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    // 2.5 second duration
    ts.tv_sec += 2;
    ts.tv_nsec += 500000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    s_expire_time = ts;
    s_active = true;
    force_redraw = true;
}

bool ui_status_is_active(void) {
    if (!s_active) return false;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec > s_expire_time.tv_sec ||
        (now.tv_sec == s_expire_time.tv_sec && now.tv_nsec >= s_expire_time.tv_nsec)) {
        s_active = false;
        force_redraw = true; // Redraw to restore normal player box border
        return false;
    }
    return true;
}

void ui_status_render(int player_y, int player_x, int player_h, int player_w) {
    if (!ui_status_is_active() || player_w < 10) return;

    // Draw centered on the bottom border of the player box
    int border_y = player_y + player_h - 1;
    char formatted[300];
    snprintf(formatted, sizeof(formatted), " [ %s ] ", s_status_msg);

    int msg_w = utf8_display_width(formatted);
    int max_available = player_w - 4;
    if (msg_w > max_available) {
        int bytes = utf8_byte_offset_for_width(formatted, max_available);
        formatted[bytes] = '\0';
        msg_w = max_available;
    }

    int start_x = player_x + (player_w - msg_w) / 2;
    if (start_x < player_x + 1) start_x = player_x + 1;

    attron(A_BOLD | COLOR_PAIR(4) | A_REVERSE);
    mvprintw(border_y, start_x, "%s", formatted);
    attroff(A_BOLD | COLOR_PAIR(4) | A_REVERSE);
}