#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include "sparkles_settings.h"
#include "sparkles_theme.h"
#include "config.h"
#include "db.h"
#include "state.h"
#include "listening_profile.h"
#include "sparkles_text_prompt.h"
#include "rlgl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    SETTINGS_TAB_LIBRARY = 0,
    SETTINGS_TAB_LYRICS,
    SETTINGS_TAB_PLAYBACK,
    SETTINGS_TAB_COUNT
} SettingsTab;

static bool s_open = false;
static float s_anim = 0.0f;
static SettingsTab s_active_tab = SETTINGS_TAB_LIBRARY;

// Rebuild and profile reset confirmation states
static bool s_rebuild_confirm = false;
static float s_rebuild_confirm_timer = 0.0f;
static bool s_profile_confirm = false;
static float s_profile_confirm_timer = 0.0f;

static void on_add_folder_submitted(const char *path, void *ud) {
    (void)ud;
    if (!path || !path[0]) return;
    char expanded[1024];
    if (path[0] == '~' && path[1] == '/') {
        const char *home = getenv("HOME");
        snprintf(expanded, sizeof(expanded), "%s/%s", home ? home : "", path + 2);
    } else {
        strncpy(expanded, path, sizeof(expanded) - 1);
        expanded[sizeof(expanded) - 1] = '\0';
    }
    config_add_music_dir(expanded);
    library_scanner_start();
}

void sparkles_settings_init(void) {
    s_open = false;
    s_anim = 0.0f;
    s_active_tab = SETTINGS_TAB_LIBRARY;
    s_rebuild_confirm = false;
    s_rebuild_confirm_timer = 0.0f;
}

void sparkles_settings_open(void) {
    s_open = true;
    s_rebuild_confirm = false;
}

void sparkles_settings_close(void) {
    s_open = false;
    s_rebuild_confirm = false;
}

void sparkles_settings_toggle(void) {
    if (s_open) sparkles_settings_close();
    else sparkles_settings_open();
}

bool sparkles_settings_is_open(void) { return s_open; }
bool sparkles_settings_is_visible(void) { return (s_open || s_anim > 0.001f); }
float sparkles_settings_get_anim_progress(void) { return s_anim; }

void sparkles_settings_update(float screen_w, float screen_h) {
    (void)screen_w; (void)screen_h;
    float dt = GetFrameTime();
    float speed = 5.4f;

    if (s_open) {
        s_anim += dt * speed;
        if (s_anim > 1.0f) s_anim = 1.0f;
    } else {
        s_anim -= dt * speed;
        if (s_anim < 0.0f) s_anim = 0.0f;
    }

    if (s_rebuild_confirm) {
        s_rebuild_confirm_timer -= dt;
        if (s_rebuild_confirm_timer <= 0.0f) s_rebuild_confirm = false;
    }
    if (s_profile_confirm) {
        s_profile_confirm_timer -= dt;
        if (s_profile_confirm_timer <= 0.0f) s_profile_confirm = false;
    }
}

static bool DrawBtn(Rectangle bounds, const char *text, bool active) {
    Vector2 m = GetMousePosition();
    bool hover = CheckCollisionPointRec(m, bounds);
    bool clicked = hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    Color bg = active ? ColorAlpha(COLOR_ACCENT, 0.18f) : (hover ? (Color){18, 19, 23, 255} : (Color){0, 0, 0, 0});
    Color fg = active ? COLOR_ACCENT : (hover ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED);

    DrawRectangleRec(bounds, bg);
    if (active) {
        DrawRectangle((int)bounds.x, (int)(bounds.y + bounds.height - 2), (int)bounds.width, 2, COLOR_ACCENT);
    }

    int tw = MeasureText(text, FONT_SIZE_SM);
    DrawText(text, (int)(bounds.x + (bounds.width - tw) / 2), (int)(bounds.y + (bounds.height - FONT_SIZE_SM) / 2), FONT_SIZE_SM, fg);
    return clicked;
}

static bool DrawToggle(Rectangle bounds, const char *label, bool *val) {
    Vector2 mouse = GetMousePosition();
    bool hover = CheckCollisionPointRec(mouse, bounds);

    DrawText(label, (int)bounds.x + 12, (int)bounds.y + (int)(bounds.height - 14) / 2, FONT_SIZE_SM, hover ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED);

    Rectangle btn = { bounds.x + bounds.width - 76, bounds.y + (bounds.height - 22) / 2, 64, 22 };
    bool clicked = CheckCollisionPointRec(mouse, btn) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (clicked) *val = !(*val);

    Color st_col = *val ? COLOR_ACCENT : (Color){ 30, 32, 38, 255 };
    DrawRectangleRec(btn, st_col);
    const char *st_txt = *val ? "ON" : "OFF";
    int tw = MeasureText(st_txt, 12);
    DrawText(st_txt, (int)(btn.x + (btn.width - tw) / 2), (int)(btn.y + 5), 12, *val ? (Color){ 10, 10, 12, 255 } : COLOR_TEXT_MUTED);

    return clicked;
}

bool sparkles_settings_handle_input(float screen_w, float screen_h) {
    if (!s_open || s_anim < 0.95f) return false;

    if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_COMMA)) {
        sparkles_settings_close();
        return true;
    }

    float box_w = fminf(780.0f, screen_w - 60.0f);
    float box_h = fminf(540.0f, screen_h - 60.0f);
    Rectangle box = { (screen_w - box_w) * 0.5f, (screen_h - box_h) * 0.5f, box_w, box_h };

    Vector2 mouse = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(mouse, box)) {
        sparkles_settings_close();
        return true;
    }

    return true;
}

void sparkles_settings_render(float screen_w, float screen_h) {
    if (s_anim <= 0.001f) return;

    float inv = 1.0f - s_anim;
    float ease = 1.0f - (inv * inv * inv);

    rlPushMatrix();
    rlTranslatef(screen_w * 0.5f, screen_h * 0.5f - 40.0f * (inv * inv), 0.0f);
    rlScalef(1.0f + 0.15f * inv, 1.0f + 0.15f * inv, 1.0f);
    rlTranslatef(-screen_w * 0.5f, -screen_h * 0.5f, 0.0f);

    DrawRectangle(0, 0, (int)screen_w, (int)screen_h, ColorAlpha((Color){ 3, 3, 5, 255 }, 0.70f * ease));

    float box_w = fminf(780.0f, screen_w - 60.0f);
    float box_h = fminf(540.0f, screen_h - 60.0f);
    Rectangle box = { (screen_w - box_w) * 0.5f, (screen_h - box_h) * 0.5f, box_w, box_h };

    Vector2 mouse = GetMousePosition();
    bool interactive = (s_anim >= 0.99f);

    DrawRectangleRec(box, (Color){ 8, 9, 12, 250 });
    DrawRectangleLinesEx(box, 1.0f, (Color){ 32, 35, 45, 255 });
    DrawNothingCornerBrackets(box, 10.0f, COLOR_ACCENT);

    // Header
    float header_h = 44.0f;
    DrawRectangle((int)box.x, (int)box.y, (int)box.width, (int)header_h, (Color){ 12, 13, 17, 255 });
    DrawLine((int)box.x, (int)(box.y + header_h), (int)(box.x + box.width), (int)(box.y + header_h), (Color){ 28, 30, 38, 255 });

    DrawText("SETTINGS AND PREFERENCES", (int)box.x + 18, (int)box.y + 14, FONT_SIZE_MD, COLOR_TEXT_PRIMARY);

    Rectangle btn_close = { box.x + box.width - 80, box.y + 10, 64, 24 };
    bool hover_close = interactive && CheckCollisionPointRec(mouse, btn_close);
    DrawRectangleRec(btn_close, hover_close ? ColorAlpha(COLOR_ACCENT, 0.20f) : (Color){ 18, 20, 26, 255 });
    DrawRectangleLinesEx(btn_close, 1.0f, hover_close ? COLOR_ACCENT : (Color){ 36, 40, 50, 255 });
    DrawText("✕ Esc", (int)btn_close.x + 10, (int)btn_close.y + 5, FONT_SIZE_SM, hover_close ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED);

    if (interactive && hover_close && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        sparkles_settings_close();
    }

    // Sidebar & Main Layout
    float sidebar_w = 200.0f;
    float footer_h = 32.0f;
    Rectangle side_rect = { box.x, box.y + header_h, sidebar_w, box.height - header_h - footer_h };
    Rectangle main_rect = { box.x + sidebar_w, box.y + header_h, box.width - sidebar_w, box.height - header_h - footer_h };

    DrawRectangleRec(side_rect, (Color){ 10, 11, 15, 255 });
    DrawLine((int)(side_rect.x + side_rect.width), (int)side_rect.y,
             (int)(side_rect.x + side_rect.width), (int)(side_rect.y + side_rect.height), (Color){ 28, 30, 38, 255 });

    const char *tabs[] = { "Library & Paths", "Lyrics & Privacy", "Audio & Playback" };
    for (int t = 0; t < SETTINGS_TAB_COUNT; t++) {
        Rectangle tab_btn = { side_rect.x, side_rect.y + 14.0f + t * 32.0f, side_rect.width, 30.0f };
        bool selected = (s_active_tab == (SettingsTab)t);
        bool hover = interactive && CheckCollisionPointRec(mouse, tab_btn);

        if (selected) {
            DrawRectangleRec(tab_btn, (Color){ 18, 20, 28, 255 });
            DrawRectangle((int)tab_btn.x, (int)tab_btn.y, 3, (int)tab_btn.height, COLOR_ACCENT);
        } else if (hover) {
            DrawRectangleRec(tab_btn, (Color){ 14, 15, 20, 255 });
        }

        DrawText(tabs[t], (int)tab_btn.x + 18, (int)tab_btn.y + 7, FONT_SIZE_SM, selected ? COLOR_ACCENT : (hover ? COLOR_TEXT_PRIMARY : COLOR_TEXT_MUTED));

        if (interactive && hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            s_active_tab = (SettingsTab)t;
        }
    }

    // Content pane
    float cy = main_rect.y + 20.0f;
    float row_w = main_rect.width - 40.0f;

    switch (s_active_tab) {
        case SETTINGS_TAB_LIBRARY: {
            DrawText("SCANNED MUSIC DIRECTORIES", (int)main_rect.x + 18, (int)cy, 11, COLOR_TEXT_MUTED);
            cy += 20.0f;

            for (int i = 0; i < app_config.num_music_dirs; i++) {
                Rectangle dir_box = { main_rect.x + 18, cy, row_w - 30.0f, 26.0f };
                Rectangle del_btn = { main_rect.x + 18 + row_w - 24.0f, cy, 24.0f, 26.0f };

                DrawRectangleRec(dir_box, (Color){ 12, 13, 17, 255 });
                DrawRectangleLinesEx(dir_box, 1.0f, (Color){ 24, 26, 34, 255 });
                DrawText(app_config.music_dirs[i], (int)dir_box.x + 8, (int)dir_box.y + 6, FONT_SIZE_SM, COLOR_TEXT_PRIMARY);

                bool hover_del = interactive && CheckCollisionPointRec(mouse, del_btn);
                DrawRectangleRec(del_btn, hover_del ? ColorAlpha(COLOR_ACCENT, 0.25f) : (Color){ 16, 17, 22, 255 });
                DrawRectangleLinesEx(del_btn, 1.0f, hover_del ? COLOR_ACCENT : (Color){ 28, 30, 38, 255 });
                DrawText("✕", (int)del_btn.x + 7, (int)del_btn.y + 5, 11, hover_del ? COLOR_ACCENT : COLOR_TEXT_MUTED);

                if (interactive && hover_del && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    config_remove_music_dir(app_config.music_dirs[i]);
                    library_scanner_start();
                    break;
                }
                cy += 30.0f;
            }

            Rectangle add_btn = { main_rect.x + 18, cy, 140.0f, 26.0f };
            if (interactive && DrawBtn(add_btn, "+ Add Folder", false)) {
                sparkles_text_prompt_open(&(SparklesTextPromptConfig){
                    .tag = "MUSIC FOLDER",
                    .prompt = "Enter Directory Path",
                    .submit_label = "Add",
                    .initial_text = "~/Music",
                    .max_len = 512,
                    .on_submit = on_add_folder_submitted
                });
            }

            cy += 48.0f;
            DrawLine((int)main_rect.x + 18, (int)cy, (int)(main_rect.x + 18 + row_w), (int)cy, (Color){ 28, 30, 38, 255 });
            cy += 14.0f;

            DrawText("DATABASE MAINTENANCE", (int)main_rect.x + 18, (int)cy, 11, COLOR_TEXT_MUTED);
            cy += 20.0f;

            // Rescan library button
            bool scanning = library_scanner_is_running();
            Rectangle rescan_btn = { main_rect.x + 18, cy, 160.0f, 28.0f };
            if (interactive && DrawBtn(rescan_btn, scanning ? "Scanning..." : "↺ Rescan Library", false)) {
                if (!scanning) library_scanner_start();
            }

            // Clean and rebuild Database button
            Rectangle rebuild_btn = { main_rect.x + 190, cy, 200.0f, 28.0f };
            const char *rebuild_lbl = s_rebuild_confirm ? "Click to Confirm Rebuild!" : "⚠ Rebuild Database";
            if (interactive && DrawBtn(rebuild_btn, rebuild_lbl, s_rebuild_confirm)) {
                if (!s_rebuild_confirm) {
                    s_rebuild_confirm = true;
                    s_rebuild_confirm_timer = 4.0f;
                } else {
                    db_rebuild_library();
                    s_rebuild_confirm = false;
                }
            }
            break;
        }

        case SETTINGS_TAB_LYRICS: {
            DrawText("LYRICS PROVIDER SETTINGS", (int)main_rect.x + 18, (int)cy, 11, COLOR_TEXT_MUTED);
            cy += 16.0f;

            if (DrawToggle((Rectangle){ main_rect.x + 18, cy, row_w, 32.0f }, "Allow Online Lyrics Fetching", &app_config.online_lyrics)) {
                app_config.online_lyrics_asked = true;
                config_save();
            }
            cy += 34.0f;

            if (DrawToggle((Rectangle){ main_rect.x + 18, cy, row_w, 32.0f }, "Save Downloaded Lyrics Offline", &app_config.download_online_lyrics)) {
                app_config.download_online_lyrics_asked = true;
                config_save();
            }
            cy += 44.0f;

            DrawText("CUSTOM LYRICS DIRECTORY", (int)main_rect.x + 18, (int)cy, 11, COLOR_TEXT_MUTED);
            cy += 18.0f;

            const char *cur_lrc_path = app_config.lyrics_custom_path ? app_config.lyrics_custom_path : "~/Music/lyrics";
            Rectangle lrc_path_box = { main_rect.x + 18, cy, row_w, 26.0f };
            DrawRectangleRec(lrc_path_box, (Color){ 12, 13, 17, 255 });
            DrawRectangleLinesEx(lrc_path_box, 1.0f, (Color){ 24, 26, 34, 255 });
            DrawText(cur_lrc_path, (int)lrc_path_box.x + 8, (int)lrc_path_box.y + 6, FONT_SIZE_SM, COLOR_TEXT_PRIMARY);
            break;
        }

        case SETTINGS_TAB_PLAYBACK: {
            DrawText("PLAYBACK ENGINE SETTINGS", (int)main_rect.x + 18, (int)cy, 11, COLOR_TEXT_MUTED);
            cy += 18.0f;

            // Shuffle algorithm selector
            ShuffleAlgorithm cur_alg = (ShuffleAlgorithm)atomic_load(&shuffle_algorithm);
            const char *alg_names[] = { "Pure Random", "Fisher-Yates Deck", "Artist-Spaced Deck", "Weighted (Profile)" };
            DrawText("Shuffle Algorithm", (int)main_rect.x + 18, (int)cy + 6, FONT_SIZE_SM, COLOR_TEXT_PRIMARY);

            Rectangle alg_btn = { main_rect.x + main_rect.width - 200, cy, 160, 26 };
            if (interactive && DrawBtn(alg_btn, alg_names[(int)cur_alg], false)) {
                ShuffleAlgorithm next_alg = (ShuffleAlgorithm)(((int)cur_alg + 1) % SHUFFLE_ALG_COUNT);
                atomic_store(&shuffle_algorithm, next_alg);
            }
            cy += 36.0f;

            // Listening profile toggle
            if (DrawToggle((Rectangle){ main_rect.x + 18, cy, row_w, 30.0f }, "Listening Profile Learning", &app_config.enable_listening_profile)) {
                app_config.listening_profile_asked = true;
                config_save();
            }
            cy += 36.0f;

            // Reset listening profile button
            Rectangle prf_btn = { main_rect.x + 18, cy, 200, 26 };
            const char *prf_lbl = s_profile_confirm ? "Click to Confirm Reset" : "↺ Reset Profile DB";
            if (interactive && DrawBtn(prf_btn, prf_lbl, s_profile_confirm)) {
                if (!s_profile_confirm) {
                    s_profile_confirm = true;
                    s_profile_confirm_timer = 4.0f;
                } else {
                    listening_profile_reset_db();
                    s_profile_confirm = false;
                }
            }
            cy += 38.0f;

            // ReplayGain mode selector
            int cur_rg = atomic_load(&play_mode_rgain);
            const char *rg_names[] = { "OFF", "Track Metadata", "Real-time RMS" };
            DrawText("ReplayGain Loudness", (int)main_rect.x + 18, (int)cy + 6, FONT_SIZE_SM, COLOR_TEXT_PRIMARY);

            Rectangle rg_btn = { main_rect.x + main_rect.width - 200, cy, 160, 26 };
            if (interactive && DrawBtn(rg_btn, rg_names[cur_rg], false)) {
                atomic_store(&play_mode_rgain, (cur_rg + 1) % 3);
            }
            cy += 38.0f;

            // Hardware playback specs
            uint32_t srate = atomic_load(&vis_srate);
            const char *dev_txt = TextFormat("%u Hz · Stereo 32-bit Float", srate ? srate : 44100);
            int dev_w = MeasureText(dev_txt, FONT_SIZE_SM);
            DrawText("Active Output Device", (int)main_rect.x + 18, (int)cy + 6, FONT_SIZE_SM, COLOR_TEXT_PRIMARY);
            DrawText(dev_txt, (int)(main_rect.x + main_rect.width - dev_w - 40), (int)cy + 6, FONT_SIZE_SM, COLOR_TEXT_MUTED);
            break;
        }

        default:
            break;
    }

    // Footer
    DrawText("[Esc / ,] Close Settings  |  Click to toggle / select",
             (int)box.x + 18, (int)(box.y + box.height - 24), FONT_SIZE_XS, COLOR_TEXT_DARK);

    rlPopMatrix();
}