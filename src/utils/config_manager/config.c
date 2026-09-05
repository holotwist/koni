#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600

#include "config.h"
#include "ui_keybinds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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
    int section = 0; // 0=none, 1=paths, 2=lyrics, 3=keybindings

    while ((linelen = getline(&line, &linecap, f)) > 0) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '[') {
            if (strncasecmp(p, "[keybindings]", 13) == 0 || strncasecmp(p, "[keys]", 6) == 0) {
                section = 3;
            } else if (strncasecmp(p, "[paths]", 7) == 0) {
                section = 1;
            } else if (strncasecmp(p, "[lyrics]", 8) == 0) {
                section = 2;
            } else {
                section = 0;
            }
            continue;
        }

        char *eq = strchr(line, '=');
        if (eq) {
            *eq = '\0';
            char *key = line;
            char *val = eq + 1;
            trim_string(&key);
            trim_string(&val);

            if (strcmp(key, "music_directories") == 0 || strcmp(key, "music_dir") == 0 ||
                (section == 1 && (strcmp(key, "directories") == 0 || strcmp(key, "music") == 0))) {
                // Clear default dirs if custom ones are specified in the ini
                for (int i = 0; i < app_config.num_music_dirs; i++) free(app_config.music_dirs[i]);
                app_config.num_music_dirs = 0;

                char *saveptr = NULL;
                char *dir_tok = strtok_r(val, ",;", &saveptr);
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
                            char *norm = normalize_path_dup(expanded);
                            if (norm) {
                                if (app_config.num_music_dirs >= app_config.music_dirs_capacity) {
                                    app_config.music_dirs_capacity = app_config.music_dirs_capacity == 0 ? 8 : app_config.music_dirs_capacity * 2;
                                    app_config.music_dirs = realloc(app_config.music_dirs, sizeof(char*) * app_config.music_dirs_capacity);
                                }
                                app_config.music_dirs[app_config.num_music_dirs++] = norm;
                            }
                            free(expanded);
                        }
                    }
                    dir_tok = strtok_r(NULL, ",;", &saveptr);
                }
            } else if (strcmp(key, "lyrics.custom_path") == 0 ||
                       (section == 2 && (strcmp(key, "custom_path") == 0 || strcmp(key, "path") == 0)) ||
                       (section == 1 && strcmp(key, "lyrics") == 0)) {
                if (app_config.lyrics_custom_path) free(app_config.lyrics_custom_path);
                if (strncmp(val, "~/", 2) == 0 && home) {
                    if (asprintf(&app_config.lyrics_custom_path, "%s/%s", home, val + 2) < 0) {
                        app_config.lyrics_custom_path = NULL;
                    }
                } else {
                    app_config.lyrics_custom_path = strdup(val);
                }
            } else if (strcmp(key, "lyrics.online") == 0 || (section == 2 && strcmp(key, "online") == 0)) {
                if (strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0 || strcasecmp(val, "yes") == 0) {
                    app_config.online_lyrics = true;
                    app_config.online_lyrics_asked = true;
                } else if (strcasecmp(val, "false") == 0 || strcmp(val, "0") == 0 || strcasecmp(val, "no") == 0) {
                    app_config.online_lyrics = false;
                    app_config.online_lyrics_asked = true;
                }
            } else if (strcmp(key, "lyrics.download_online") == 0 ||
                       (section == 2 && (strcmp(key, "download_online") == 0 || strcmp(key, "download") == 0))) {
                if (strcasecmp(val, "true") == 0 || strcmp(val, "1") == 0 || strcasecmp(val, "yes") == 0) {
                    app_config.download_online_lyrics = true;
                    app_config.download_online_lyrics_asked = true;
                } else if (strcasecmp(val, "false") == 0 || strcmp(val, "0") == 0 || strcasecmp(val, "no") == 0) {
                    app_config.download_online_lyrics = false;
                    app_config.download_online_lyrics_asked = true;
                }
            } else if (section == 3 || strncasecmp(key, "key.", 4) == 0 || strncasecmp(key, "keybind.", 8) == 0) {
                const char *action_k = key;
                if (strncasecmp(action_k, "key.", 4) == 0) action_k += 4;
                else if (strncasecmp(action_k, "keybind.", 8) == 0) action_k += 8;
                ui_keybinds_set(action_k, val);
            }
        }
    }
    fclose(f);
}

static void ensure_keybindings_in_file(const char *filepath) {
    int def_count = 0;
    const KeybindDefault *defs = ui_keybinds_get_defaults(&def_count);
    if (!defs || def_count == 0) return;

    FILE *f = fopen(filepath, "r");
    if (!f) return;

    char **lines = NULL;
    int line_count = 0;
    int capacity = 128;
    lines = malloc(sizeof(char*) * capacity);
    if (!lines) { fclose(f); return; }

    char buffer[1024];
    int kb_section_idx = -1;
    int next_section_idx = -1;
    int current_section = 0; // 3 = keybindings
    bool *found = calloc((size_t)def_count, sizeof(bool));

    while (fgets(buffer, sizeof(buffer), f)) {
        if (line_count >= capacity) {
            capacity *= 2;
            char **new_lines = realloc(lines, sizeof(char*) * capacity);
            if (!new_lines) break;
            lines = new_lines;
        }

        char *p = buffer;
        while (*p == ' ' || *p == '\t') p++;

        if (*p == '[') {
            if (strncasecmp(p, "[keybindings]", 13) == 0 || strncasecmp(p, "[keys]", 6) == 0) {
                current_section = 3;
                if (kb_section_idx == -1) kb_section_idx = line_count;
            } else {
                if (current_section == 3 && next_section_idx == -1) {
                    next_section_idx = line_count;
                }
                current_section = 0;
            }
        } else if (strchr(buffer, '=')) {
            char temp[1024];
            strncpy(temp, buffer, sizeof(temp) - 1);
            temp[sizeof(temp) - 1] = '\0';
            char *eq = strchr(temp, '=');
            if (eq) {
                *eq = '\0';
                char *k = temp;
                trim_string(&k);

                if (current_section == 3 || strncasecmp(k, "key.", 4) == 0 || strncasecmp(k, "keybind.", 8) == 0) {
                    if (strncasecmp(k, "key.", 4) == 0) k += 4;
                    else if (strncasecmp(k, "keybind.", 8) == 0) k += 8;

                    for (int i = 0; i < def_count; i++) {
                        if (strcasecmp(defs[i].name, k) == 0) {
                            found[i] = true;
                            break;
                        }
                    }
                }
            }
        }

        lines[line_count++] = strdup(buffer);
    }
    fclose(f);

    int missing_count = 0;
    for (int i = 0; i < def_count; i++) {
        if (!found[i]) missing_count++;
    }

    // Nothing missing: preserve file exactly as it is
    if (missing_count == 0) {
        for (int i = 0; i < line_count; i++) free(lines[i]);
        free(lines);
        free(found);
        return;
    }

    f = fopen(filepath, "w");
    if (!f) {
        for (int i = 0; i < line_count; i++) free(lines[i]);
        free(lines);
        free(found);
        return;
    }

    int insert_idx = (next_section_idx != -1) ? next_section_idx : line_count;

    for (int i = 0; i < line_count; i++) {
        if (kb_section_idx != -1 && i == insert_idx) {
            for (int d = 0; d < def_count; d++) {
                if (!found[d]) {
                    fprintf(f, "%s = %s\n", defs[d].name, defs[d].default_keys);
                }
            }
        }
        fputs(lines[i], f);
        free(lines[i]);
    }
    free(lines);

    if (kb_section_idx == -1) {
        if (line_count > 0) fprintf(f, "\n");
        fprintf(f, "[Keybindings]\n");
        for (int d = 0; d < def_count; d++) {
            if (!found[d]) {
                fprintf(f, "%s = %s\n", defs[d].name, defs[d].default_keys);
            }
        }
    } else if (insert_idx == line_count) {
        for (int d = 0; d < def_count; d++) {
            if (!found[d]) {
                fprintf(f, "%s = %s\n", defs[d].name, defs[d].default_keys);
            }
        }
    }

    fclose(f);
    free(found);
}

void config_init(void) {
    ui_keybinds_init();
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
    
    char config_dir[1024];
    snprintf(config_dir, sizeof(config_dir), "%s/.config/koni/", home);
    ensure_dir(config_dir);

    char primary_ini[1024];
    snprintf(primary_ini, sizeof(primary_ini), "%sconfig.ini", config_dir);

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

    // If no directories were defined in INI configs, register the default ~/Music
    if (app_config.num_music_dirs == 0) {
        char *def_music = NULL;
        if (asprintf(&def_music, "%s/Music", home) >= 0 && def_music) {
            config_add_music_dir(def_music);
            free(def_music);
        }
    }

    if (!found_ini) {
        FILE *f = fopen(primary_ini, "w");
        if (f) {
            fprintf(f, "[Paths]\n");
            fprintf(f, "# Fallback path used if a song's local directory isn't accessible/writeable\n");
            fprintf(f, "lyrics.custom_path = \"~/Music/lyrics\"\n");
            fclose(f);
            parse_ini_file(primary_ini, home);
        }
    }

    // Auto-populate missing keybindings without modifying existing user entries
    ensure_keybindings_in_file(primary_ini);
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
            strncpy(temp, buffer, sizeof(temp) - 1);
            temp[sizeof(temp) - 1] = '\0';
            char *eq = strchr(temp, '=');
            if (eq && strchr(buffer, '=')) {
                *eq = '\0';
                char *k = temp;
                trim_string(&k);
                if (strcmp(k, key) == 0) {
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