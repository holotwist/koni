#define _DEFAULT_SOURCE
#include "sparkles_theme.h"
#include "koni_paths.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

Font g_sparkles_font = {0};
static char s_font_path[1024] = {0};

#define BASE_CP_COUNT 190
#define DYNAMIC_SLOTS 320
#define TOTAL_GLYPH_CAP (BASE_CP_COUNT + DYNAMIC_SLOTS)
#define ATLAS_BASE_SIZE 24

typedef struct {
    int codepoint;
    uint32_t last_used_frame;
} DynamicGlyph;

static int s_base_codepoints[BASE_CP_COUNT];
static DynamicGlyph s_dynamic_glyphs[DYNAMIC_SLOTS];
static int s_dynamic_count = 0;
static int s_bake_codepoints[TOTAL_GLYPH_CAP];
static uint32_t s_frame_counter = 0;

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

    koni_get_path(buf, sizeof(buf), "font.ttf");
    if (check_and_set_path(out_path, sz, buf)) return true;
    koni_get_path(buf, sizeof(buf), "unifont.otf");
    if (check_and_set_path(out_path, sz, buf)) return true;

    const char *rel_paths[] = {
        "unifont.otf", "unifont.ttf", "font.otf", "font.ttf",
        "resources/unifont.otf", "../resources/unifont.otf",
        NULL
    };
    for (int i = 0; rel_paths[i]; i++) {
        if (check_and_set_path(out_path, sz, rel_paths[i])) return true;
        if (app_dir) {
            snprintf(buf, sizeof(buf), "%s%s", app_dir, rel_paths[i]);
            if (check_and_set_path(out_path, sz, buf)) return true;
        }
    }
    return false;
}

static void init_base_set(void) {
    int idx = 0;
    for (int c = 32; c <= 126; c++) s_base_codepoints[idx++] = c;
    for (int c = 160; c <= 255; c++) s_base_codepoints[idx++] = c;
    s_base_codepoints[idx++] = 0x25B6; // Play symbol
}

static void rebuild_atlas(void) {
    if (!s_font_path[0]) return;

    int total = 0;
    for (int i = 0; i < BASE_CP_COUNT; i++) {
        s_bake_codepoints[total++] = s_base_codepoints[i];
    }
    for (int i = 0; i < s_dynamic_count; i++) {
        s_bake_codepoints[total++] = s_dynamic_glyphs[i].codepoint;
    }

    Font next_font = LoadFontEx(s_font_path, ATLAS_BASE_SIZE, s_bake_codepoints, total);
    if (next_font.texture.id != 0) {
        SetTextureFilter(next_font.texture, TEXTURE_FILTER_BILINEAR);
        if (g_sparkles_font.texture.id != 0 && g_sparkles_font.texture.id != GetFontDefault().texture.id) {
            UnloadFont(g_sparkles_font);
        }
        g_sparkles_font = next_font;
    }
}

void sparkles_font_init(void) {
    s_frame_counter = 0;
    s_dynamic_count = 0;
    memset(s_dynamic_glyphs, 0, sizeof(s_dynamic_glyphs));

    init_base_set();

    if (!find_font_file(s_font_path, sizeof(s_font_path))) {
        g_sparkles_font = GetFontDefault();
        return;
    }
    rebuild_atlas();
}

void sparkles_font_scan_library(void) {
    // Glyphs are pulled ondemand
}

void sparkles_font_load_for_text(const char *text) {
    if (!s_font_path[0] || !text || !text[0]) return;
    s_frame_counter++;

    int count = 0;
    int *cps = LoadCodepoints(text, &count);
    if (!cps || count == 0) return;

    bool needs_rebuild = false;

    for (int i = 0; i < count; i++) {
        int cp = cps[i];
        if (cp < 32) continue;

        bool is_base = false;
        for (int b = 0; b < BASE_CP_COUNT; b++) {
            if (s_base_codepoints[b] == cp) { is_base = true; break; }
        }
        if (is_base) continue;

        int found_idx = -1;
        for (int d = 0; d < s_dynamic_count; d++) {
            if (s_dynamic_glyphs[d].codepoint == cp) {
                found_idx = d;
                break;
            }
        }

        if (found_idx != -1) {
            s_dynamic_glyphs[found_idx].last_used_frame = s_frame_counter;
        } else {
            if (s_dynamic_count < DYNAMIC_SLOTS) {
                s_dynamic_glyphs[s_dynamic_count].codepoint = cp;
                s_dynamic_glyphs[s_dynamic_count].last_used_frame = s_frame_counter;
                s_dynamic_count++;
            } else {
                int oldest_idx = 0;
                uint32_t oldest_frame = s_dynamic_glyphs[0].last_used_frame;
                for (int d = 1; d < DYNAMIC_SLOTS; d++) {
                    if (s_dynamic_glyphs[d].last_used_frame < oldest_frame) {
                        oldest_frame = s_dynamic_glyphs[d].last_used_frame;
                        oldest_idx = d;
                    }
                }
                s_dynamic_glyphs[oldest_idx].codepoint = cp;
                s_dynamic_glyphs[oldest_idx].last_used_frame = s_frame_counter;
            }
            needs_rebuild = true;
        }
    }

    UnloadCodepoints(cps);

    if (needs_rebuild) {
        rebuild_atlas();
    }
}

void sparkles_font_unload(void) {
    if (g_sparkles_font.texture.id != 0 && g_sparkles_font.texture.id != GetFontDefault().texture.id) {
        UnloadFont(g_sparkles_font);
    }
    g_sparkles_font = (Font){0};
}