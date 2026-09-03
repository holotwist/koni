#ifndef KONI_LYRICS_H
#define KONI_LYRICS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    uint32_t start_ms;
    uint32_t end_ms;
    char *text;
    char *raw_text;
} LyricLine;

typedef struct {
    LyricLine *lines;
    int num_lines;
    int capacity;
    bool is_synced;
    char format_name[16];
} LyricDocument;

typedef struct {
    char title[256];
    char artist[256];
    char album[256];
    char filepath[1024];
    char *embedded_text;
    uint32_t duration_sec;
    int track_id;
} LyricFetchQuery;

typedef struct {
    const char *name;
    const char **supported_extensions; // NULL-terminated

    // Parses raw string payload into a standardized LyricDocument
    // For all implementations, this is mandatory
    LyricDocument* (*parse)(const char *raw_data, size_t len);

    // Network/remote fetcher. If NULL, engine only queries local files/embedded tags
    // Fills out_method_label to display on the UI badge (e.g. LRCLIB)
    bool (*fetch_remote)(const LyricFetchQuery *query, char **out_raw_data, char out_method_label[32]);
} KoniLyricsPlugin;

#ifdef __cplusplus
extern "C" {
#endif

// Document utilities
LyricDocument* lyric_document_create(void);
void lyric_document_free(LyricDocument *doc);
int lyric_document_get_active_line(const LyricDocument *doc, uint32_t time_ms);

// Lyrics engine API
void lyrics_engine_init(void);
void lyrics_engine_fetch_async(const char *title, const char *artist, const char *album, uint32_t duration, const char *filepath, const char *embedded_text, int track_id);
const KoniLyricsPlugin* lyrics_find_plugin_by_ext(const char *filepath);

#ifdef __cplusplus
}
#endif

#endif // KONI_LYRICS_H