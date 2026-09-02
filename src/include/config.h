#ifndef KONI_CONFIG_H
#define KONI_CONFIG_H

#include <stdbool.h>

typedef struct {
    bool online_lyrics;
    bool online_lyrics_asked;
    char lyrics_custom_path[1024];
    char music_directories[2048];
    bool download_online_lyrics;
    bool download_online_lyrics_asked;
} KoniConfig;

extern KoniConfig app_config;

void config_init(void);
void config_save(void);

bool config_is_music_dir(const char *path);
bool config_find_parent_music_dir(const char *path, char *out_parent, size_t out_sz);
bool config_find_child_music_dir(const char *path, char *out_child, size_t out_sz);
void config_add_music_dir(const char *path);
void config_remove_music_dir(const char *path);

#endif // KONI_CONFIG_H