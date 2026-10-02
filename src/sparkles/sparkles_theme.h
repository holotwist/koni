#ifndef SPARKLES_THEME_H
#define SPARKLES_THEME_H

#include "raylib.h"
#include <math.h>

// Color Palette
#define COLOR_BG            (Color){ 0, 0, 0, 255 }         // Pure Pitch Black
#define COLOR_TILE_BG       (Color){ 7, 7, 9, 255 }         // Charcoal Tile
#define COLOR_TILE_BORDER   (Color){ 22, 22, 26, 255 }      // Flat border
#define COLOR_ACCENT        (Color){ 232, 152, 168, 255 }   // Blush Rose Accent
#define COLOR_ACCENT_DIM    (Color){ 130, 70, 85, 160 }     // Muted Rose
#define COLOR_TEXT_PRIMARY  (Color){ 245, 245, 247, 255 }   // Bright White
#define COLOR_TEXT_MUTED    (Color){ 140, 144, 154, 255 }   // Neutral Grey
#define COLOR_TEXT_DARK     (Color){ 55, 58, 66, 255 }      // Low Contrast Grid

// Grid Constraints (Flat Edge-to-Edge Grid)
#define GRID_COLS           12
#define GRID_ROWS           8
#define GRID_PADDING        16.0f
#define GRID_GAP            12.0f
#define TILE_ROUNDING       0.0f

// Internal corner brackets
static inline void DrawNothingCornerBrackets(Rectangle r, float len, Color col) {
    // Top-Left '┌'
    DrawLineEx((Vector2){ r.x, r.y }, (Vector2){ r.x + len, r.y }, 1.2f, col);
    DrawLineEx((Vector2){ r.x, r.y }, (Vector2){ r.x, r.y + len }, 1.2f, col);

    // Top-Right '┐'
    DrawLineEx((Vector2){ r.x + r.width, r.y }, (Vector2){ r.x + r.width - len, r.y }, 1.2f, col);
    DrawLineEx((Vector2){ r.x + r.width, r.y }, (Vector2){ r.x + r.width, r.y + len }, 1.2f, col);

    // Bottom-Left '└'
    DrawLineEx((Vector2){ r.x, r.y + r.height }, (Vector2){ r.x + len, r.y + r.height }, 1.2f, col);
    DrawLineEx((Vector2){ r.x, r.y + r.height }, (Vector2){ r.x, r.y + r.height - len }, 1.2f, col);

    // Bottom-Right '┘'
    DrawLineEx((Vector2){ r.x + r.width, r.y + r.height }, (Vector2){ r.x + r.width - len, r.y + r.height }, 1.2f, col);
    DrawLineEx((Vector2){ r.x + r.width, r.y + r.height }, (Vector2){ r.x + r.width, r.y + r.height - len }, 1.2f, col);
}

// Global UI density scale
static inline float sparkles_get_ui_scale(void) {
    float sw = (float)GetScreenWidth();
    float sh = (float)GetScreenHeight();
    if (sw <= 0.0f || sh <= 0.0f) return 1.0f;

#if defined(__ANDROID__) || defined(PLATFORM_ANDROID)
    if (sh > sw) {
        // Mobile portrait baseline, 380dp logical width
        float s = sw / 380.0f;
        return (s < 1.0f) ? 1.0f : s;
    } else {
        // Mobile landscape baseline, 540dp logical height
        float s = sh / 540.0f;
        return (s < 1.0f) ? 1.0f : s;
    }
#else
    // Desktop scale
    if (sh > 1440.0f) return 1.5f;
    return 1.0f;
#endif
}

#define UI_SCALE (sparkles_get_ui_scale())

#if defined(__ANDROID__) || defined(PLATFORM_ANDROID)
#define FONT_SIZE_XS        ((int)(12.0f * UI_SCALE))
#define FONT_SIZE_SM        ((int)(15.0f * UI_SCALE))
#define FONT_SIZE_MD        ((int)(18.0f * UI_SCALE))
#define FONT_SIZE_LG        ((int)(22.0f * UI_SCALE))
#define FONT_SIZE_XL        ((int)(26.0f * UI_SCALE))

static inline float GetEffectiveFontSize(int fontSize) {
    return (float)(int)((float)fontSize);
}
#else
// Desktop
#define FONT_SIZE_XS        16
#define FONT_SIZE_SM        16
#define FONT_SIZE_MD        16
#define FONT_SIZE_LG        32
#define FONT_SIZE_XL        32

static inline float GetEffectiveFontSize(int fontSize) {
    if (fontSize >= 48) return 64.0f;
    if (fontSize >= 24) return 32.0f;
    return 16.0f;
}
#endif

extern Font g_sparkles_font;
void sparkles_font_init(void);
void sparkles_font_scan_library(void);
void sparkles_font_load_for_text(const char *extra_text);
void sparkles_font_unload(void);

// Letter spacing proportional to font size
static inline float GetFontSpacing(float effSize) {
#if defined(__ANDROID__) || defined(PLATFORM_ANDROID)
    return fmaxf(0.5f, effSize * 0.035f);
#else
    (void)effSize;
    return 0.0f;
#endif
}

static inline int MeasureSparklesText(const char *text, int fontSize) {
    if (!text || !text[0]) return 0;
    float effSize = GetEffectiveFontSize(fontSize);
    float spacing = GetFontSpacing(effSize);
    if (g_sparkles_font.texture.id != 0) {
        return (int)ceilf(MeasureTextEx(g_sparkles_font, text, effSize, spacing).x);
    }
    return (int)ceilf(MeasureTextEx(GetFontDefault(), text, effSize, spacing).x);
}

static inline void DrawSparklesText(const char *text, int posX, int posY, int fontSize, Color color) {
    if (!text || !text[0]) return;
    float effSize = GetEffectiveFontSize(fontSize);
    float spacing = GetFontSpacing(effSize);
    if (g_sparkles_font.texture.id != 0) {
        DrawTextEx(g_sparkles_font, text, (Vector2){ (float)posX, (float)posY }, effSize, spacing, color);
    } else {
        DrawTextEx(GetFontDefault(), text, (Vector2){ (float)posX, (float)posY }, effSize, spacing, color);
    }
}

// Redirect all UI DrawText and MeasureText calls
#define MeasureText(...) MeasureSparklesText(__VA_ARGS__)
#define DrawText(...) DrawSparklesText(__VA_ARGS__)

#include "rlgl.h"

// Marquee text drawer
static inline void DrawTextMarquee(const char *text, Rectangle col, Rectangle tile, int fontSize, Color color, float speed) {
    (void)tile;
    if (!text || !text[0] || col.width <= 0) return;

    Matrix mat = rlGetMatrixTransform();
    float sx = col.x + mat.m12;
    float sy = col.y + mat.m13;

    int screen_w = GetScreenWidth();
    if (sx + col.width <= 0.0f || sx >= (float)screen_w) return;

    int tw = MeasureSparklesText(text, fontSize);
    float effH = fmaxf(col.height, (float)fontSize * 1.35f);

    if (tw <= (int)col.width) {
        BeginScissorMode((int)sx, (int)sy - 2, (int)col.width, (int)effH + 4);
        DrawSparklesText(text, (int)col.x, (int)col.y, fontSize, color);
        EndScissorMode();
        return;
    }

    float gap = 48.0f * UI_SCALE;
    float span = (float)tw + gap;
    float shift = fmodf((float)GetTime() * (speed * UI_SCALE), span);
#if !defined(__ANDROID__) && !defined(PLATFORM_ANDROID)
    shift = floorf(shift); // Snap marquee to whole pixels
#endif

    BeginScissorMode((int)sx, (int)sy - 2, (int)col.width, (int)effH + 4);
    DrawSparklesText(text, (int)(col.x - shift), (int)col.y, fontSize, color);
    DrawSparklesText(text, (int)(col.x - shift + span), (int)col.y, fontSize, color);
    EndScissorMode();
}

#endif // SPARKLES_THEME_H