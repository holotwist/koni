#ifndef UI_INPUT_H
#define UI_INPUT_H

#include <stdbool.h>

// Processes a keystroke. Returns false if the player should exit.
bool ui_handle_input(int ch);

#endif // UI_INPUT_H