#define _DEFAULT_SOURCE
#include "sparkles_theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Font g_sparkles_font = {0};
static char s_font_path[1024] = {0};

#include "state.h"
#include "playlist_manager.h"

static int *s_active_codepoints = NULL;
static int s_active_cp_count = 0;
static int s_active_cp_cap = 0;
static bool s_cp_present[65536] = {false};

static bool check_and_set_path(char *out_path, size_t sz, const char *candidate) {
    if (candidate && FileExists(candidate)) {
        snprintf(out_path, sz, "%s", candidate);
        return true;
    }
    return false;
}

static bool find_font_file(char *out_path, size_t sz) {
    const char *app_dir = GetApplicationDirectory();
    char buf[1024];

    // User config font (~/.config/koni/font.*)
    const char *home = getenv("HOME");
    if (home) {
        snprintf(buf, sizeof(buf), "%s/.config/koni/font.ttf", home);
        if (check_and_set_path(out_path, sz, buf)) return true;
        snprintf(buf, sizeof(buf), "%s/.config/koni/font.otf", home);
        if (check_and_set_path(out_path, sz, buf)) return true;
        snprintf(buf, sizeof(buf), "%s/.config/koni/unifont.otf", home);
        if (check_and_set_path(out_path, sz, buf)) return true;
    }

    // Local resources
    const char *rel_paths[] = {
        "resources/unifont.otf",
        "../resources/unifont.otf",
        "resources/unifont.ttf",
        "../resources/unifont.ttf",
        "resources/font.otf",
        "../resources/font.otf",
        "resources/font.ttf",
        "../resources/font.ttf",
        NULL
    };
    for (int i = 0; rel_paths[i]; i++) {
        if (check_and_set_path(out_path, sz, rel_paths[i])) return true;
        if (app_dir) {
            snprintf(buf, sizeof(buf), "%s%s", app_dir, rel_paths[i]);
            if (check_and_set_path(out_path, sz, buf)) return true;
        }
    }

    // System installed Unifont
    const char *sys_paths[] = {
        "/usr/share/fonts/truetype/unifont/unifont.ttf",
        "/usr/share/fonts/TTF/unifont.ttf",
        "/usr/share/fonts/unifont/unifont.ttf",
        NULL
    };
    for (int i = 0; sys_paths[i]; i++) {
        if (check_and_set_path(out_path, sz, sys_paths[i])) return true;
    }

    return false;
}

static void add_codepoint(int cp) {
    if (cp < 32 || cp >= 65536) return;
    if (s_cp_present[cp]) return;
    s_cp_present[cp] = true;

    if (s_active_cp_count >= s_active_cp_cap) {
        s_active_cp_cap = (s_active_cp_cap == 0) ? 4096 : s_active_cp_cap * 2;
        s_active_codepoints = realloc(s_active_codepoints, sizeof(int) * s_active_cp_cap);
    }
    s_active_codepoints[s_active_cp_count++] = cp;
}

static void init_base_codepoints(void) {
    memset(s_cp_present, 0, sizeof(s_cp_present));
    s_active_cp_count = 0;

    // ASCII [32..126]
    for (int i = 32; i <= 126; i++) add_codepoint(i);

    // Latin-1 Supplement (Spanish, French, German: á, é, í, ó, ú, ñ, ¿, ¡, ç, œ) [160..255]
    for (int i = 160; i <= 255; i++) add_codepoint(i);

    // Latin Extended-A & B [256..591]
    for (int i = 256; i <= 591; i++) add_codepoint(i);

    // Vietnamese Vowels (Latin Extended Additional: 0x1EA0..0x1EF9)
    for (int i = 0x1EA0; i <= 0x1EF9; i++) add_codepoint(i);

    // Combining Diacritical Marks [0x0300..0x036F]
    for (int i = 0x0300; i <= 0x036F; i++) add_codepoint(i);

    // Cyrillic (Russian, Ukrainian, etc.) [0x0400..0x04FF]
    for (int i = 0x0400; i <= 0x04FF; i++) add_codepoint(i);

    // Greek [0x0370..0x03FF]
    for (int i = 0x0370; i <= 0x03FF; i++) add_codepoint(i);

    // Japanese Hiragana & Katakana [0x3040..0x30FF]
    for (int i = 0x3040; i <= 0x30FF; i++) add_codepoint(i);

    // General Punctuation & Quotes [0x2000..0x20CF]
    for (int i = 0x2000; i <= 0x20CF; i++) add_codepoint(i);

    // Fullwidth ASCII & Symbols
    for (int i = 0xFF00; i <= 0xFFEF; i++) add_codepoint(i);
    add_codepoint(0x2605); // ★
    add_codepoint(0x2665); // ♥
    add_codepoint(0x25B6); // ▶
}

static void reload_font_atlas(void) {
    if (!s_font_path[0]) return;

    // 2x integer multiple of 16px
    Font new_font = LoadFontEx(s_font_path, 32, s_active_codepoints, s_active_cp_count);
    if (new_font.texture.id != 0) {
        // Point filtering
        SetTextureFilter(new_font.texture, TEXTURE_FILTER_POINT);

        if (g_sparkles_font.texture.id != 0 && g_sparkles_font.texture.id != GetFontDefault().texture.id) {
            UnloadFont(g_sparkles_font);
        }
        g_sparkles_font = new_font;
    }
}

void sparkles_font_init(void) {
    if (!find_font_file(s_font_path, sizeof(s_font_path))) {
        g_sparkles_font = GetFontDefault();
        return;
    }

    init_base_codepoints();
    sparkles_font_scan_library(); // Single startup load
}

void sparkles_font_load_for_text(const char *extra_text) {
    if (!s_font_path[0] || !extra_text || !extra_text[0]) return;

    int text_cp_count = 0;
    int *text_cps = LoadCodepoints(extra_text, &text_cp_count);
    if (!text_cps || text_cp_count == 0) return;

    bool added_any = false;
    for (int i = 0; i < text_cp_count; i++) {
        int cp = text_cps[i];
        if (cp >= 32 && cp < 65536 && !s_cp_present[cp]) {
            add_codepoint(cp);
            added_any = true;
        }
    }
    UnloadCodepoints(text_cps);

    if (added_any) {
        reload_font_atlas();
    }
}

/* Scans all music library tracks and playlists
   Allows all characters exist in the atlas at startup */
void sparkles_font_scan_library(void) {
    if (!s_font_path[0]) return;
    bool added_any = false;

    pthread_mutex_lock(&state_mutex);
    for (int i = 0; i < num_library_tracks; i++) {
        const char *fields[] = {
            library_tracks[i].title,
            library_tracks[i].artist,
            library_tracks[i].album,
            library_tracks[i].name
        };
        for (int f = 0; f < 4; f++) {
            if (!fields[f] || !fields[f][0]) continue;
            int count = 0;
            int *cps = LoadCodepoints(fields[f], &count);
            if (cps) {
                for (int k = 0; k < count; k++) {
                    int cp = cps[k];
                    if (cp >= 32 && cp < 65536 && !s_cp_present[cp]) {
                        add_codepoint(cp);
                        added_any = true;
                    }
                }
                UnloadCodepoints(cps);
            }
        }
    }

    for (int i = 0; i < num_playlist_files; i++) {
        if (playlist[i].name[0]) {
            int count = 0;
            int *cps = LoadCodepoints(playlist[i].name, &count);
            if (cps) {
                for (int k = 0; k < count; k++) {
                    int cp = cps[k];
                    if (cp >= 32 && cp < 65536 && !s_cp_present[cp]) {
                        add_codepoint(cp);
                        added_any = true;
                    }
                }
                UnloadCodepoints(cps);
            }
        }
    }
    pthread_mutex_unlock(&state_mutex);

    for (int p = 0; p < playlist_mgmt_get_count(); p++) {
        const PlaylistSummary *ps = playlist_mgmt_get_summary(p);
        if (ps && ps->name[0]) {
            int count = 0;
            int *cps = LoadCodepoints(ps->name, &count);
            if (cps) {
                for (int k = 0; k < count; k++) {
                    int cp = cps[k];
                    if (cp >= 32 && cp < 65536 && !s_cp_present[cp]) {
                        add_codepoint(cp);
                        added_any = true;
                    }
                }
                UnloadCodepoints(cps);
            }
        }
    }

    if (added_any) {
        reload_font_atlas();
    }
}

void sparkles_font_unload(void) {
    if (g_sparkles_font.texture.id != 0 && g_sparkles_font.texture.id != GetFontDefault().texture.id) {
        UnloadFont(g_sparkles_font);
    }
    if (s_active_codepoints) {
        free(s_active_codepoints);
        s_active_codepoints = NULL;
    }
    s_active_cp_count = 0;
    s_active_cp_cap = 0;
}