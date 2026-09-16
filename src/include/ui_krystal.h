#ifndef UI_KRYSTAL_H
#define UI_KRYSTAL_H

#include <stdbool.h>

void ui_krystal_init(void);
bool ui_krystal_is_active(void);
void ui_krystal_toggle(void);
void ui_krystal_close(void);

bool ui_krystal_handle_input(int ch);
void draw_krystal_panel(int y, int x, int h, int w);

#endif // UI_KRYSTAL_H