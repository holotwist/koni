#define _DEFAULT_SOURCE
#include "sparkles_theme.h"
#include "koni_paths.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

Font g_sparkles_font = {0};
static char s_font_path[1024] = {0};

#define BASE_CP_MAX 256
#define PENDING_MAX 256
#define GLYPH_TTL_FRAMES (30 * 60) // 30s at 60 FPS

#if defined(__ANDROID__) || defined(PLATFORM_ANDROID)
#define ATLAS_BASE_SIZE 24
#define DYNAMIC_SLOTS   2048
#define HIGH_WATERMARK  1024
#else
// 64px 4x supersampling with 16/32/64px mip levels
#define ATLAS_BASE_SIZE 64
#define DYNAMIC_SLOTS   768
#define HIGH_WATERMARK  512
#endif

#define TOTAL_GLYPH_CAP (BASE_CP_MAX + DYNAMIC_SLOTS)

typedef struct {
    int codepoint;
    uint32_t last_used_frame;
} DynamicGlyph;

static int s_base_codepoints[BASE_CP_MAX];
static int s_base_count = 0;

static DynamicGlyph s_dynamic_glyphs[DYNAMIC_SLOTS];
static int s_dynamic_count = 0;

static int s_pending_codepoints[PENDING_MAX];
static int s_pending_count = 0;

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
    s_base_count = 0;
    // Standard ASCII
    for (int c = 32; c <= 126; c++) s_base_codepoints[s_base_count++] = c;
    // Latin-1 Supplement (includes °, ·)
    for (int c = 160; c <= 255; c++) s_base_codepoints[s_base_count++] = c;

    // Essential UI glyphs
    static const int ui_symbols[] = {
        0x25B6, // ▶ Play
        0x2605, // ★ Star filled
        0x2606, // ☆ Star outline
        0x25BE, // ▾ Down triangle
        0x2715, // ✕ Close X
        0x2922, // ⤢ Arena arrow
        0x2190, // ← Left
        0x2191, // ↑ Up
        0x2192, // → Right
        0x2193, // ↓ Down
        0x2026, // … Ellipsis
        0x2014, // — Em dash
        0x2018, // ‘
        0x2019, // ’
        0x201C, // “
        0x201D  // ”
    };
    for (size_t i = 0; i < sizeof(ui_symbols)/sizeof(ui_symbols[0]); i++) {
        if (s_base_count < BASE_CP_MAX) s_base_codepoints[s_base_count++] = ui_symbols[i];
    }
}

static inline bool is_base_codepoint(int cp) {
    if ((cp >= 32 && cp <= 126) || (cp >= 160 && cp <= 255)) return true;
    for (int i = 191; i < s_base_count; i++) {
        if (s_base_codepoints[i] == cp) return true;
    }
    return false;
}

static inline int find_dynamic_glyph(int cp) {
    for (int i = 0; i < s_dynamic_count; i++) {
        if (s_dynamic_glyphs[i].codepoint == cp) return i;
    }
    return -1;
}

static void rebuild_atlas(void) {
    if (!s_font_path[0]) return;

    int total = 0;
    for (int i = 0; i < s_base_count; i++) {
        s_bake_codepoints[total++] = s_base_codepoints[i];
    }
    for (int i = 0; i < s_dynamic_count; i++) {
        s_bake_codepoints[total++] = s_dynamic_glyphs[i].codepoint;
    }

    Font next_font = LoadFontEx(s_font_path, ATLAS_BASE_SIZE, s_bake_codepoints, total);
    if (next_font.texture.id != 0) {
#if defined(__ANDROID__) || defined(PLATFORM_ANDROID)
        SetTextureFilter(next_font.texture, TEXTURE_FILTER_BILINEAR);
#else
        // Generate mipmaps
        GenTextureMipmaps(&next_font.texture);
        SetTextureFilter(next_font.texture, TEXTURE_FILTER_TRILINEAR);
#endif
        if (g_sparkles_font.texture.id != 0 && g_sparkles_font.texture.id != GetFontDefault().texture.id) {
            UnloadFont(g_sparkles_font);
        }
        g_sparkles_font = next_font;
    }
}

void sparkles_font_touch_text(const char *text) {
    if (!text || !text[0]) return;

    // Fast skip for ASCII
    bool has_non_ascii = false;
    for (const unsigned char *p = (const unsigned char*)text; *p; p++) {
        if (*p >= 128) { has_non_ascii = true; break; }
    }
    if (!has_non_ascii) return;

    const char *ptr = text;
    while (*ptr) {
        int cpSize = 0;
        int cp = GetCodepointNext(ptr, &cpSize);
        ptr += cpSize;

        if (cp < 128 || is_base_codepoint(cp)) continue;

        int idx = find_dynamic_glyph(cp);
        if (idx != -1) {
            s_dynamic_glyphs[idx].last_used_frame = s_frame_counter;
        } else {
            // Queue for next frame bake
            bool already_pending = false;
            for (int k = 0; k < s_pending_count; k++) {
                if (s_pending_codepoints[k] == cp) { already_pending = true; break; }
            }
            if (!already_pending && s_pending_count < PENDING_MAX) {
                s_pending_codepoints[s_pending_count++] = cp;
            }
        }
    }
}

void sparkles_font_update(void) {
    s_frame_counter++;
    bool needs_rebuild = false;

    // LRU eviction when cache exceeds high watermark
    if (s_dynamic_count > HIGH_WATERMARK) {
        int write_idx = 0;
        for (int i = 0; i < s_dynamic_count; i++) {
            if (s_frame_counter - s_dynamic_glyphs[i].last_used_frame <= GLYPH_TTL_FRAMES) {
                s_dynamic_glyphs[write_idx++] = s_dynamic_glyphs[i];
            }
        }
        if (write_idx != s_dynamic_count) {
            s_dynamic_count = write_idx;
            needs_rebuild = true;
        }
    }

    // Append intercepted on-screen glyphs
    if (s_pending_count > 0) {
        for (int k = 0; k < s_pending_count; k++) {
            int cp = s_pending_codepoints[k];
            if (find_dynamic_glyph(cp) == -1) {
                if (s_dynamic_count < DYNAMIC_SLOTS) {
                    s_dynamic_glyphs[s_dynamic_count].codepoint = cp;
                    s_dynamic_glyphs[s_dynamic_count].last_used_frame = s_frame_counter;
                    s_dynamic_count++;
                    needs_rebuild = true;
                }
            }
        }
        s_pending_count = 0;
    }

    if (needs_rebuild) {
        rebuild_atlas();
    }
}

void sparkles_font_init(void) {
    s_frame_counter = 0;
    s_dynamic_count = 0;
    s_pending_count = 0;
    memset(s_dynamic_glyphs, 0, sizeof(s_dynamic_glyphs));

    init_base_set();

    if (!find_font_file(s_font_path, sizeof(s_font_path))) {
        g_sparkles_font = GetFontDefault();
        return;
    }
    rebuild_atlas();
}

void sparkles_font_scan_library(void) {
    // Intercepted on-demand
}

void sparkles_font_load_for_text(const char *text) {
    sparkles_font_touch_text(text);
}

void sparkles_font_unload(void) {
    if (g_sparkles_font.texture.id != 0 && g_sparkles_font.texture.id != GetFontDefault().texture.id) {
        UnloadFont(g_sparkles_font);
    }
    g_sparkles_font = (Font){0};
}