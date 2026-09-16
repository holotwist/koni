#ifndef UI_SEARCH_H
#define UI_SEARCH_H

#include <stdbool.h>
#include "state.h"

// Search API
void ui_search_open(void);
void ui_search_close(void);
bool ui_search_is_active(void);
const char* ui_search_get_query(void);

// Returns true if the input was fully consumed by the search widget
bool ui_search_handle_input(int ch, BrowserTab tab);

// Renders the search bar line if active. Returns lines consumed (1 if active, 0 if inactive).
int  ui_search_render_bar(int y, int x, int w);

// Retrieves matching index mapping for the given tab
int  ui_search_get_filtered_indices(BrowserTab tab, int *out_indices, int max_count);

int* ui_search_get_selected_ptr(void);
int* ui_search_get_scroll_ptr(void);

#endif // UI_SEARCH_H