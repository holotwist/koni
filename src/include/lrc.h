#ifndef LRC_H_INCLUDED
#define LRC_H_INCLUDED

#include <stdint.h>

typedef struct {
    uint32_t time_ms;
    char* text; // without timestamp
    char* raw_text;  // with timestamp
} LrcLine;

typedef struct {
    LrcLine* lines;
    int num_lines;
    int capacity;
} LrcDocument;

#ifdef __cplusplus
extern "C" {
#endif

// Parses a string. Returns a populated document if it contains valid LRC tags, or NULL otherwise.
LrcDocument* lrc_parse(const char* lyrics);

// Frees the LRC document and all its lines.
void lrc_free(LrcDocument* doc);

// Returns the index of the currently active line based on playback time, or -1 if none.
int lrc_get_active_line(const LrcDocument* doc, uint32_t time_ms);

#ifdef __cplusplus
}
#endif

#endif /* LRC_H_INCLUDED */