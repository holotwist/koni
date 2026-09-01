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

#endif // KONI_CONFIG_H