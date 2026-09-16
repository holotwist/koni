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

// Typography scale
#define FONT_SIZE_XS        15
#define FONT_SIZE_SM        17
#define FONT_SIZE_MD        20
#define FONT_SIZE_LG        25
#define FONT_SIZE_XL        30

extern Font g_sparkles_font;
void sparkles_font_init(void);
void sparkles_font_scan_library(void);
void sparkles_font_load_for_text(const char *extra_text);
void sparkles_font_unload(void);

// Global size booster, lifts tiny labels, snaps large numbers to integers to avoid antialiasing blur
static inline float GetEffectiveFontSize(int fontSize) {
    if (fontSize <= 9)  return 14.0f;
    if (fontSize <= 11) return 15.0f;
    if (fontSize <= 13) return 16.0f;
    if (fontSize >= 24) return (float)fontSize; // Keep large headers/clocks at exact integer sizes
    return (float)(int)((float)fontSize * 1.08f);
}

static inline int MeasureSparklesText(const char *text, int fontSize) {
    if (!text || !text[0]) return 0;
    float effSize = GetEffectiveFontSize(fontSize);
    if (g_sparkles_font.texture.id != 0) {
        return (int)MeasureTextEx(g_sparkles_font, text, effSize, 0.0f).x;
    }
    return (int)MeasureTextEx(GetFontDefault(), text, effSize, effSize / 10.0f).x;
}

static inline void DrawSparklesText(const char *text, int posX, int posY, int fontSize, Color color) {
    if (!text || !text[0]) return;
    float effSize = GetEffectiveFontSize(fontSize);
    if (g_sparkles_font.texture.id != 0) {
        DrawTextEx(g_sparkles_font, text, (Vector2){ (float)posX, (float)posY }, effSize, 0.0f, color);
    } else {
        DrawTextEx(GetFontDefault(), text, (Vector2){ (float)posX, (float)posY }, effSize, effSize / 10.0f, color);
    }
}

// Redirect all UI DrawText and MeasureText calls
#define MeasureText(...) MeasureSparklesText(__VA_ARGS__)
#define DrawText(...) DrawSparklesText(__VA_ARGS__)

// Marquee text drawer for overflowing labels
static inline void DrawTextMarquee(const char *text, Rectangle col, Rectangle tile, int fontSize, Color color, float speed) {
    if (!text || !text[0] || col.width <= 0) return;
    int tw = MeasureSparklesText(text, fontSize);

    // If text fits, scissor to col boundary
    if (tw <= (int)col.width) {
        BeginScissorMode((int)col.x, (int)col.y, (int)col.width, (int)col.height + 2);
        DrawSparklesText(text, (int)col.x, (int)col.y, fontSize, color);
        BeginScissorMode((int)tile.x, (int)tile.y, (int)tile.width, (int)tile.height);
        return;
    }

    float gap = 32.0f;
    float span = (float)tw + gap;
    float shift = fmodf((float)GetTime() * speed, span);

    BeginScissorMode((int)col.x, (int)col.y, (int)col.width, (int)col.height + 2);
    DrawSparklesText(text, (int)(col.x - shift), (int)col.y, fontSize, color);
    DrawSparklesText(text, (int)(col.x - shift + span), (int)col.y, fontSize, color);
    BeginScissorMode((int)tile.x, (int)tile.y, (int)tile.width, (int)tile.height);
}

#endif // SPARKLES_THEME_H