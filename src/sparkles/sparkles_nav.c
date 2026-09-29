#include "sparkles_nav.h"
#include "sparkles_theme.h"
#include <math.h>
#include <string.h>

static float s_target_x = 0.0f;
static float s_current_x = 0.0f;
static float s_target_y = 0.0f;
static float s_current_y = 0.0f;

static char s_badge_text[32] = {0};
static float s_badge_timer = 0.0f;

static void trigger_badge(const char *name) {
    strncpy(s_badge_text, name, sizeof(s_badge_text) - 1);
    s_badge_text[sizeof(s_badge_text) - 1] = '\0';
    s_badge_timer = 2.0f;
}

void sparkles_nav_init(void) {
    s_target_x = 0.0f;
    s_current_x = 0.0f;
    s_target_y = 0.0f;
    s_current_y = 0.0f;
    s_badge_text[0] = '\0';
    s_badge_timer = 0.0f;
}

void sparkles_nav_update(float dt) {
    // Exponential decay
    float speed = fminf(1.0f, dt * 14.0f);
    s_current_x += (s_target_x - s_current_x) * speed;
    s_current_y += (s_target_y - s_current_y) * speed;

    if (s_badge_timer > 0.0f) {
        s_badge_timer -= dt;
        if (s_badge_timer < 0.0f) s_badge_timer = 0.0f;
    }
}

void sparkles_nav_step_x(int delta) {
    // Delta -1 steps toward playlists (-2), +1 steps toward player (0)
    s_target_x += (float)delta;
    if (s_target_x < -2.0f) s_target_x = -2.0f;
    if (s_target_x >  0.0f) s_target_x =  0.0f;

    int page = (int)roundf(s_target_x);
    if (page == PAGE_PLAYLISTS) trigger_badge("PLAYLISTS");
    else if (page == PAGE_LIBRARY) trigger_badge("LIBRARY");
    else trigger_badge("NOW PLAYING");
}

void sparkles_nav_step_y(int delta) {
    s_target_y += (float)delta;
    if (s_target_y < 0.0f) s_target_y = 0.0f;
    if (s_target_y > 2.0f) s_target_y = 2.0f;

    int layer = (int)roundf(s_target_y);
    if (layer == LAYER_KRYSTAL) trigger_badge("KRYSTAL DSP");
    else if (layer == LAYER_EQ) trigger_badge("EQUALIZER");
    else {
        int page = (int)roundf(s_target_x);
        if (page == PAGE_PLAYLISTS) trigger_badge("PLAYLISTS");
        else if (page == PAGE_LIBRARY) trigger_badge("LIBRARY");
        else trigger_badge("NOW PLAYING");
    }
}

void sparkles_nav_set_x(int x) {
    if (x < -2) x = -2;
    if (x > 0)  x = 0;
    s_target_x = (float)x;

    if (x == PAGE_PLAYLISTS) trigger_badge("PLAYLISTS");
    else if (x == PAGE_LIBRARY) trigger_badge("LIBRARY");
    else trigger_badge("NOW PLAYING");
}

void sparkles_nav_set_y(int y) {
    if (y < 0) y = 0;
    if (y > 2) y = 2;
    s_target_y = (float)y;

    if (y == LAYER_KRYSTAL) trigger_badge("KRYSTAL DSP");
    else if (y == LAYER_EQ) trigger_badge("EQUALIZER");
}

float sparkles_nav_get_current_x(void) { return s_current_x; }
float sparkles_nav_get_current_y(void) { return s_current_y; }
int   sparkles_nav_get_target_x(void)  { return (int)roundf(s_target_x); }
int   sparkles_nav_get_target_y(void)  { return (int)roundf(s_target_y); }

bool sparkles_nav_is_eq_open(void)      { return (s_target_y == 1 || (s_target_y == 0 && s_current_y > 0.05f && s_current_y < 1.0f) || (s_target_y == 2 && s_current_y < 1.80f)); }
bool sparkles_nav_is_krystal_open(void) { return (s_target_y == 2 || s_current_y > 1.05f); }

void sparkles_nav_render_badge(float screen_w, float screen_h) {
    (void)screen_h;
    if (s_badge_timer <= 0.001f || s_badge_text[0] == '\0') return;

    float alpha = fminf(1.0f, s_badge_timer * 1.5f);
    const char *badge = TextFormat("[ %s ]", s_badge_text);
    int tw = MeasureSparklesText(badge, FONT_SIZE_SM);

    float bx = (screen_w - (float)tw) * 0.5f;
    float by = 20.0f;

    DrawRectangle((int)bx - 8, (int)by - 4, tw + 16, 26, ColorAlpha((Color){ 10, 11, 14, 255 }, 0.85f * alpha));
    DrawRectangleLines((int)bx - 8, (int)by - 4, tw + 16, 26, ColorAlpha(COLOR_ACCENT, alpha));
    DrawSparklesText(badge, (int)bx, (int)by, FONT_SIZE_SM, ColorAlpha(COLOR_ACCENT, alpha));
}