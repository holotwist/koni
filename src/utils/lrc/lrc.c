#include "lrc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compare_lrc(const void* a, const void* b) {
    const LrcLine* la = (const LrcLine*)a;
    const LrcLine* lb = (const LrcLine*)b;
    if (la->time_ms < lb->time_ms) return -1;
    if (la->time_ms > lb->time_ms) return 1;
    return 0;
}

LrcDocument* lrc_parse(const char* lyrics) {
    if (!lyrics) return NULL;
    
    char* copy = strdup(lyrics);
    // Erase carriage returns
    for (char* c = copy; *c; c++) if (*c == '\r') *c = ' ';

    LrcDocument* doc = calloc(1, sizeof(LrcDocument));
    char* line = strtok(copy, "\n");
    int valid_lines = 0;
    
    while (line) {
        char* p = line;
        while (*p == ' ' || *p == '\t') p++; // Trim leading whitespace
        
        char* text_start = p;
        int has_tag = 0;
        
        // Identify if this line has valid timestamp tags (dry-run)
        while (*text_start == '[') {
            int m; float s_f; int bytes_read;
            int s_int, cs_int;
            
            if (sscanf(text_start, "[%d:%f]%n", &m, &s_f, &bytes_read) == 2) {
                has_tag = 1; text_start += bytes_read;
            } else if (sscanf(text_start, "[%d:%d:%d]%n", &m, &s_int, &cs_int, &bytes_read) == 3) {
                has_tag = 1; text_start += bytes_read;
            } else {
                break;
            }
        }
        
        if (has_tag) {
            // Trim leading spaces from the payload
            while (*text_start == ' ' || *text_start == '\t') text_start++;
            char* text = strdup(text_start);
            
            // Register all timestamps linked to this text payload (second pass)
            p = line;
            while (*p == ' ' || *p == '\t') p++;
            while (*p == '[') {
                int m; float s_f; int bytes_read;
                int s_int, cs_int;
                uint32_t t_ms = 0;
                int valid = 0;
                
                if (sscanf(p, "[%d:%f]%n", &m, &s_f, &bytes_read) == 2) {
                    t_ms = m * 60000 + (uint32_t)(s_f * 1000.0f);
                    valid = 1; p += bytes_read;
                } else if (sscanf(p, "[%d:%d:%d]%n", &m, &s_int, &cs_int, &bytes_read) == 3) {
                    t_ms = m * 60000 + s_int * 1000 + cs_int * 10;
                    valid = 1; p += bytes_read;
                } else {
                    break;
                }
                
                if (valid) {
                    if (doc->num_lines >= doc->capacity) {
                        doc->capacity = doc->capacity == 0 ? 32 : doc->capacity * 2;
                        doc->lines = realloc(doc->lines, sizeof(LrcLine) * doc->capacity);
                    }
                    LrcLine* ll = &doc->lines[doc->num_lines++];
                    ll->time_ms = t_ms;
                    ll->text = strdup(text);
                    ll->raw_text = strdup(line);
                    valid_lines++;
                }
            }
            free(text);
        }
        line = strtok(NULL, "\n");
    }
    free(copy);
    
    if (valid_lines == 0) {
        lrc_free(doc);
        return NULL;
    }
    
    // Multiple tags might appear out of chronological order line-by-line
    qsort(doc->lines, doc->num_lines, sizeof(LrcLine), compare_lrc);
    return doc;
}

void lrc_free(LrcDocument* doc) {
    if (!doc) return;
    for (int i = 0; i < doc->num_lines; i++) {
        free(doc->lines[i].raw_text);
        free(doc->lines[i].text);
    }
    free(doc->lines);
    free(doc);
}

int lrc_get_active_line(const LrcDocument* doc, uint32_t time_ms) {
    if (!doc || doc->num_lines == 0) return -1;
    int active = -1;
    for (int i = 0; i < doc->num_lines; i++) {
        if (doc->lines[i].time_ms <= time_ms) {
            active = i;
        } else {
            break;
        }
    }
    return active;
}