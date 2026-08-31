#include "lyrics.h"
#include <stddef.h>

static LyricDocument* ttml_parse_placeholder(const char *raw_data, size_t len) {
    (void)raw_data;
    (void)len;
    // TODO: Implement TTML/XML parsing logic
    // For now just placeholder, template for future lyric providers
    return NULL;
}

static const char* ttml_exts[] = { ".ttml", ".xml", NULL };

const KoniLyricsPlugin ttml_lyrics_plugin = {
    .name = "TTML",
    .supported_extensions = ttml_exts,
    .parse = ttml_parse_placeholder,
    .fetch_remote = NULL // If NULL, means only Local and Embedded
};