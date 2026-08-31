#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600

#include "lyrics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compare_lines(const void *a, const void *b) {
    const LyricLine *la = (const LyricLine*)a;
    const LyricLine *lb = (const LyricLine*)b;
    if (la->start_ms < lb->start_ms) return -1;
    if (la->start_ms > lb->start_ms) return 1;
    return 0;
}

LyricDocument* lrc_parse_document(const char *raw_data, size_t len) {
    (void)len;
    if (!raw_data) return NULL;

    char *copy = strdup(raw_data);
    for (char *c = copy; *c; c++) if (*c == '\r') *c = ' ';

    LyricDocument *doc = lyric_document_create();
    strncpy(doc->format_name, "LRC", sizeof(doc->format_name) - 1);
    char *line = strtok(copy, "\n");
    int valid_lines = 0;

    while (line) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        char *text_start = p;
        int has_tag = 0;

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
            while (*text_start == ' ' || *text_start == '\t') text_start++;
            char *text = strdup(text_start);

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
                        doc->lines = realloc(doc->lines, sizeof(LyricLine) * doc->capacity);
                    }
                    LyricLine *ll = &doc->lines[doc->num_lines++];
                    ll->start_ms = t_ms;
                    ll->end_ms = 0;
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
        lyric_document_free(doc);
        return NULL;
    }

    doc->is_synced = true;
    qsort(doc->lines, doc->num_lines, sizeof(LyricLine), compare_lines);
    return doc;
}

extern bool lrc_fetch_remote(const LyricFetchQuery *query, char **out_raw_data, char out_method_label[32]);

static const char* lrc_exts[] = { ".lrc", NULL };

const KoniLyricsPlugin lrc_lyrics_plugin = {
    .name = "LRC",
    .supported_extensions = lrc_exts,
    .parse = lrc_parse_document,
    .fetch_remote = lrc_fetch_remote
};