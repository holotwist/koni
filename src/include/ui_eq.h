#ifndef UI_EQ_H
#define UI_EQ_H

#include <stdbool.h>

void ui_eq_init(void);
bool ui_eq_is_active(void);
void ui_eq_toggle(void);
void ui_eq_close(void);

bool ui_eq_handle_input(int ch);
void draw_eq_panel(int y, int x, int h, int w);

#endif // UI_EQ_H