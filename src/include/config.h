#ifndef KONI_CONFIG_H
#define KONI_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    bool online_lyrics;
    bool online_lyrics_asked;
    char *lyrics_custom_path;
    char **music_dirs;
    int num_music_dirs;
    int music_dirs_capacity;
    bool download_online_lyrics;
    bool download_online_lyrics_asked;
} KoniConfig;

extern KoniConfig app_config;

void config_init(void);
void config_save(void);
void config_cleanup(void);

bool config_is_music_dir(const char *path);
bool config_find_parent_music_dir(const char *path, char **out_parent);
bool config_find_child_music_dir(const char *path, char **out_child);
void config_add_music_dir(const char *path);
void config_remove_music_dir(const char *path);

#endif // KONI_CONFIG_H