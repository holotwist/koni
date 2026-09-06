#ifndef UI_STATUS_H
#define UI_STATUS_H

#include <stdbool.h>

void ui_status_init(void);
void ui_status_set(const char *fmt, ...);
void ui_status_render(int player_y, int player_x, int player_h, int player_w);
bool ui_status_is_active(void);

#endif // UI_STATUS_H