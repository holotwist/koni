#include "lyrics.h"
#include <string.h>
#include <strings.h>

extern const KoniLyricsPlugin lrc_lyrics_plugin;
extern const KoniLyricsPlugin ttml_lyrics_plugin;

static const KoniLyricsPlugin* available_plugins[] = {
    &lrc_lyrics_plugin,
    &ttml_lyrics_plugin, // future
    NULL
};

const KoniLyricsPlugin** lyrics_get_plugins(void) {
    return available_plugins;
}

const KoniLyricsPlugin* lyrics_find_plugin_by_ext(const char *filepath) {
    if (!filepath) return NULL;
    const char *ext = strrchr(filepath, '.');
    if (!ext) return NULL;

    for (int i = 0; available_plugins[i] != NULL; i++) {
        const char **exts = available_plugins[i]->supported_extensions;
        if (!exts) continue;
        for (int j = 0; exts[j] != NULL; j++) {
            if (strcasecmp(ext, exts[j]) == 0) {
                return available_plugins[i];
            }
        }
    }
    return NULL;
}