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

static char* normalize_path_dup(const char *in) {
    if (!in) return NULL;
    char *out = strdup(in);
    size_t len = strlen(out);
    while (len > 1 && out[len - 1] == '/') {
        out[len - 1] = '\0';
        len--;
    }
    return out;
}

static void parse_ini_file(const char* filepath, const char* home) {
    FILE *f = fopen(filepath, "r");
    if (!f) return;
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    while ((linelen = getline(&line, &linecap, f)) > 0) {
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            char *key = line;
            char *val = eq + 1;
            trim_string(&key);
            trim_string(&val);

            if (strcmp(key, "music_directories") == 0 || strcmp(key, "music_dir") == 0) {
                // Clear default dirs if custom ones are specified in the ini
                for (int i = 0; i < app_config.num_music_dirs; i++) free(app_config.music_dirs[i]);
                app_config.num_music_dirs = 0;

                char *dir_tok = strtok(val, ",;");
                while (dir_tok) {
                    while (*dir_tok == ' ' || *dir_tok == '\t') dir_tok++;
                    char *end = dir_tok + strlen(dir_tok) - 1;
                    while (end > dir_tok && (*end == ' ' || *end == '\t')) *end-- = '\0';

                    if (dir_tok[0] != '\0') {
                        char *expanded = NULL;
                        if (strncmp(dir_tok, "~/", 2) == 0 && home) {
                            if (asprintf(&expanded, "%s/%s", home, dir_tok + 2) < 0) expanded = NULL;
                        } else {
                            expanded = strdup(dir_tok);
                        }
                        if (expanded) {
                            config_add_music_dir(expanded);
                            free(expanded);
                        }
                    }
                    dir_tok = strtok(NULL, ",;");
                }
            } else if (strcmp(key, "lyrics.custom_path") == 0) {
                if (app_config.lyrics_custom_path) free(app_config.lyrics_custom_path);
                if (strncmp(val, "~/", 2) == 0 && home) {
                    if (asprintf(&app_config.lyrics_custom_path, "%s/%s", home, val + 2) < 0) {
                        app_config.lyrics_custom_path = NULL;
                    }
                } else {
                    app_config.lyrics_custom_path = strdup(val);
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
    app_config.music_dirs = NULL;
    app_config.num_music_dirs = 0;
    app_config.music_dirs_capacity = 0;
    
    const char *home = getenv("HOME");
    if (!home) return;

    if (asprintf(&app_config.lyrics_custom_path, "%s/Music/lyrics", home) < 0) {
        app_config.lyrics_custom_path = NULL;
    }

    char *def_music = NULL;
    if (asprintf(&def_music, "%s/Music", home) >= 0 && def_music) {
        config_add_music_dir(def_music);
        free(def_music);
    }
    
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

bool config_is_music_dir(const char *path) {
    if (!path || !path[0]) return false;
    char *norm_target = normalize_path_dup(path);
    if (!norm_target) return false;

    bool found = false;
    for (int i = 0; i < app_config.num_music_dirs; i++) {
        char *norm_entry = normalize_path_dup(app_config.music_dirs[i]);
        if (norm_entry) {
            if (strcmp(norm_target, norm_entry) == 0) found = true;
            free(norm_entry);
            if (found) break;
        }
    }
    free(norm_target);
    return found;
}

bool config_find_parent_music_dir(const char *path, char **out_parent) {
    if (!path || !path[0]) return false;
    char *norm_target = normalize_path_dup(path);
    if (!norm_target) return false;

    bool found = false;
    for (int i = 0; i < app_config.num_music_dirs; i++) {
        char *norm_entry = normalize_path_dup(app_config.music_dirs[i]);
        if (norm_entry) {
            size_t entry_len = strlen(norm_entry);
            if (strncmp(norm_target, norm_entry, entry_len) == 0 && norm_target[entry_len] == '/') {
                if (out_parent) *out_parent = strdup(norm_entry);
                found = true;
            }
            free(norm_entry);
            if (found) break;
        }
    }
    free(norm_target);
    return found;
}

bool config_find_child_music_dir(const char *path, char **out_child) {
    if (!path || !path[0]) return false;
    char *norm_target = normalize_path_dup(path);
    if (!norm_target) return false;
    size_t target_len = strlen(norm_target);

    bool found = false;
    for (int i = 0; i < app_config.num_music_dirs; i++) {
        char *norm_entry = normalize_path_dup(app_config.music_dirs[i]);
        if (norm_entry) {
            if (strncmp(norm_entry, norm_target, target_len) == 0 && norm_entry[target_len] == '/') {
                if (out_child) *out_child = strdup(norm_entry);
                found = true;
            }
            free(norm_entry);
            if (found) break;
        }
    }
    free(norm_target);
    return found;
}

void config_add_music_dir(const char *path) {
    if (!path || !path[0]) return;
    char *norm_target = normalize_path_dup(path);
    if (!norm_target) return;
    size_t target_len = strlen(norm_target);

    // Remove any identical or redundant child subdirectories
    int write_idx = 0;
    for (int i = 0; i < app_config.num_music_dirs; i++) {
        char *norm_entry = normalize_path_dup(app_config.music_dirs[i]);
        if (norm_entry) {
            bool is_child_or_same = (strcmp(norm_entry, norm_target) == 0) ||
                                   (strncmp(norm_entry, norm_target, target_len) == 0 && norm_entry[target_len] == '/');
            free(norm_entry);
            if (is_child_or_same) {
                free(app_config.music_dirs[i]);
                continue;
            }
        }
        app_config.music_dirs[write_idx++] = app_config.music_dirs[i];
    }
    app_config.num_music_dirs = write_idx;

    if (app_config.num_music_dirs >= app_config.music_dirs_capacity) {
        app_config.music_dirs_capacity = app_config.music_dirs_capacity == 0 ? 8 : app_config.music_dirs_capacity * 2;
        app_config.music_dirs = realloc(app_config.music_dirs, sizeof(char*) * app_config.music_dirs_capacity);
    }
    app_config.music_dirs[app_config.num_music_dirs++] = norm_target;
    config_save();
}

void config_remove_music_dir(const char *path) {
    if (!path || !path[0]) return;
    char *norm_target = normalize_path_dup(path);
    if (!norm_target) return;

    int write_idx = 0;
    for (int i = 0; i < app_config.num_music_dirs; i++) {
        char *norm_entry = normalize_path_dup(app_config.music_dirs[i]);
        if (norm_entry) {
            bool matches = (strcmp(norm_entry, norm_target) == 0);
            free(norm_entry);
            if (matches) {
                free(app_config.music_dirs[i]);
                continue;
            }
        }
        app_config.music_dirs[write_idx++] = app_config.music_dirs[i];
    }
    app_config.num_music_dirs = write_idx;
    free(norm_target);
    config_save();
}

void config_save(void) {
    const char *home = getenv("HOME");
    if (!home) return;
    
    char *filepath = NULL;
    if (asprintf(&filepath, "%s/.config/koni/config.ini", home) < 0 || !filepath) return;

    // Dynamically serialize music_directories into an unbounded string
    size_t total_len = 0;
    for (int i = 0; i < app_config.num_music_dirs; i++) {
        total_len += strlen(app_config.music_dirs[i]) + 2;
    }
    char *serialized = calloc(1, total_len + 1);
    if (serialized) {
        for (int i = 0; i < app_config.num_music_dirs; i++) {
            if (i > 0) strcat(serialized, ", ");
            strcat(serialized, app_config.music_dirs[i]);
        }
        upsert_ini_key(filepath, "music_directories", serialized);
        free(serialized);
    }

    if (app_config.lyrics_custom_path) {
        upsert_ini_key(filepath, "lyrics.custom_path", app_config.lyrics_custom_path);
    }

    if (app_config.online_lyrics_asked) {
        upsert_ini_key(filepath, "lyrics.online", app_config.online_lyrics ? "true" : "false");
    }
    if (app_config.download_online_lyrics_asked) {
        upsert_ini_key(filepath, "lyrics.download_online", app_config.download_online_lyrics ? "true" : "false");
    }

    free(filepath);
}

void config_cleanup(void) {
    if (app_config.lyrics_custom_path) {
        free(app_config.lyrics_custom_path);
        app_config.lyrics_custom_path = NULL;
    }
    if (app_config.music_dirs) {
        for (int i = 0; i < app_config.num_music_dirs; i++) free(app_config.music_dirs[i]);
        free(app_config.music_dirs);
        app_config.music_dirs = NULL;
        app_config.num_music_dirs = 0;
        app_config.music_dirs_capacity = 0;
    }
}