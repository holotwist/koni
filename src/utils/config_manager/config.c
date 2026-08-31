#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>

KoniConfig app_config = {0};

static void ensure_dir(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    char *p = NULL;
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static void trim_string(char **str) {
    char *start = *str;
    while (*start == ' ' || *start == '\t' || *start == '"') start++;
    char *end = start + strlen(start) - 1;
    while (end > start && (*end == ' ' || *end == '\t' || *end == '"' || *end == '\n' || *end == '\r')) *end-- = '\0';
    *str = start;
}

static void parse_ini_file(const char* filepath, const char* home) {
    FILE *f = fopen(filepath, "r");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *key = strtok(line, "=");
        char *val = strtok(NULL, "\n");
        if (key && val) {
            trim_string(&key);
            trim_string(&val);
            if (strcmp(key, "lyrics.custom_path") == 0) {
                if (strncmp(val, "~/", 2) == 0 && home) {
                    snprintf(app_config.lyrics_custom_path, sizeof(app_config.lyrics_custom_path), "%s/%s", home, val + 2);
                } else {
                    strncpy(app_config.lyrics_custom_path, val, sizeof(app_config.lyrics_custom_path) - 1);
                }
            } else if (strcmp(key, "lyrics.online") == 0) {
                if (strcmp(val, "true") == 0) {
                    app_config.online_lyrics = true;
                    app_config.online_lyrics_asked = true;
                } else if (strcmp(val, "false") == 0) {
                    app_config.online_lyrics = false;
                    app_config.online_lyrics_asked = true;
                }
            } else if (strcmp(key, "lyrics.download_online") == 0) {
                if (strcmp(val, "true") == 0) {
                    app_config.download_online_lyrics = true;
                    app_config.download_online_lyrics_asked = true;
                } else if (strcmp(val, "false") == 0) {
                    app_config.download_online_lyrics = false;
                    app_config.download_online_lyrics_asked = true;
                }
            }
        }
    }
    fclose(f);
}

void config_init(void) {
    app_config.online_lyrics = false;
    app_config.online_lyrics_asked = false;
    app_config.download_online_lyrics = false;
    app_config.download_online_lyrics_asked = false;
    
    const char *home = getenv("HOME");
    if (!home) return;
    
    snprintf(app_config.lyrics_custom_path, sizeof(app_config.lyrics_custom_path), "%s/Music/lyrics", home);
    
    char config_dir[1024];
    snprintf(config_dir, sizeof(config_dir), "%s/.config/koni/", home);
    ensure_dir(config_dir);

    // Treat every INI file in the folder as a continuation of settings
    DIR *dir = opendir(config_dir);
    bool found_ini = false;
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            size_t len = strlen(entry->d_name);
            if (len > 4 && strcmp(entry->d_name + len - 4, ".ini") == 0) {
                found_ini = true;
                char filepath[1024];
                snprintf(filepath, sizeof(filepath), "%s%s", config_dir, entry->d_name);
                parse_ini_file(filepath, home);
            }
        }
        closedir(dir);
    }

    if (!found_ini) {
        char filepath[1024];
        snprintf(filepath, sizeof(filepath), "%sconfig.ini", config_dir);
        FILE *f = fopen(filepath, "w");
        if (f) {
            fprintf(f, "[Paths]\n");
            fprintf(f, "# Fallback path used if a song's local directory isn't accessible/writeable\n");
            fprintf(f, "lyrics.custom_path = \"~/Music/lyrics\"\n");
            fclose(f);
            parse_ini_file(filepath, home);
        }
    }
}

static void upsert_ini_key(const char* filepath, const char* key, const char* val) {
    FILE *f = fopen(filepath, "r");
    char **lines = NULL;
    int line_count = 0;
    int capacity = 64;
    lines = malloc(capacity * sizeof(char*));
    
    bool found = false;
    
    if (f) {
        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), f)) {
            if (line_count >= capacity) {
                capacity *= 2;
                lines = realloc(lines, capacity * sizeof(char*));
            }
            
            char temp[1024];
            strcpy(temp, buffer);
            char *k = strtok(temp, "=");
            if (k) {
                trim_string(&k);
                if (strcmp(k, key) == 0 && strchr(buffer, '=')) {
                    char new_line[1024];
                    snprintf(new_line, sizeof(new_line), "%s = %s\n", key, val);
                    lines[line_count] = strdup(new_line);
                    found = true;
                } else {
                    lines[line_count] = strdup(buffer);
                }
            } else {
                lines[line_count] = strdup(buffer);
            }
            line_count++;
        }
        fclose(f);
    }
    
    f = fopen(filepath, "w");
    if (f) {
        for (int i = 0; i < line_count; i++) {
            fputs(lines[i], f);
            free(lines[i]);
        }
        if (!found) {
            fprintf(f, "%s = %s\n", key, val);
        }
        fclose(f);
    }
    if (lines) free(lines);
}

void config_save(void) {
    const char *home = getenv("HOME");
    if (!home) return;
    
    char filepath[1024];
    snprintf(filepath, sizeof(filepath), "%s/.config/koni/config.ini", home);
    
    if (app_config.online_lyrics_asked) {
        upsert_ini_key(filepath, "lyrics.online", app_config.online_lyrics ? "true" : "false");
    }
    if (app_config.download_online_lyrics_asked) {
        upsert_ini_key(filepath, "lyrics.download_online", app_config.download_online_lyrics ? "true" : "false");
    }
}