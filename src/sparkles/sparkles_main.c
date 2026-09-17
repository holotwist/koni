#include "raylib.h"
#include "sparkles_grid.h"
#include "sparkles_widgets.h"
#include "visualizers/sparkles_vis.h"
#include "eq/sparkles_eq.h"
#include "krystal/sparkles_krystal.h"
#include "rlgl.h"
#include "state.h"
#include "audio.h"
#include "config.h"
#include "db.h"
#include "playlist_manager.h"
#include "equalizer.h"
#include "krystal_engine.h"
#include "listening_profile.h"
#include "lyrics.h"
#include "modals/sparkles_context_menu.h"
#include "modals/sparkles_queue.h"
#include "modals/sparkles_vis_picker.h"
#include "views/sparkles_player_view.h"
#include "widgets/sparkles_radial_list.h"
#include "modals/sparkles_settings.h"
#include "protocols/mpris.h"
#include "input/sparkles_input.h"
#include <curl/curl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    SPARKLES_VIEW_PLAYER = 0,
    SPARKLES_VIEW_GRID
} SparklesViewMode;

static SparklesViewMode s_view_mode = SPARKLES_VIEW_PLAYER;
static bool s_show_lyrics_modal = false;
static int s_lyrics_modal_step = 0;
static bool s_show_quit_modal = false;

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    // Initialize Koni's backend
    curl_global_init(CURL_GLOBAL_DEFAULT);
    config_init();
    db_init();
    playlist_mgmt_init();
    eq_init();
    krystal_init(44100);
    listening_profile_init();
    load_state();
    library_reload();
    library_scanner_start();

    // Start background audio decoding & DSP thread
    pthread_t audio_thread;
    pthread_create(&audio_thread, NULL, audio_thread_func, NULL);

    // Initialize MPRIS D-Bus interface
    mpris_init();

    // Initialize Raylib Window
    SetTraceLogLevel(LOG_ERROR);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1280, 800, "Koni Sparkles");
    SetExitKey(KEY_NULL); // Prevent Raylib from force-closing the window on ESC
    SetWindowMinSize(960, 600);
    SetTargetFPS(60);

    // Initialize Visualizers, EQ, Krystal, Tile Grid, and Unicode Font
    sparkles_font_init();
    sparkles_vis_init();
    sparkles_eq_init();
    sparkles_krystal_init();
    sparkles_context_menu_init();
    sparkles_queue_init();
    sparkles_vis_picker_init();
    sparkles_radial_list_init();
    sparkles_player_view_init();
    sparkles_settings_init();
    sparkles_input_init();
    SparklesGrid grid;
    sparkles_grid_init(&grid);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        bool is_searching = tile_song_list_is_searching() || tile_playlists_is_searching() ||
                            sparkles_radial_list_is_open() || sparkles_vis_picker_is_open() ||
                            sparkles_settings_is_open() || sparkles_input_is_text_active() ||
                            s_show_quit_modal;
        sparkles_input_set_text_focused(is_searching);
        sparkles_input_update(dt);
        float sw = (float)GetScreenWidth();
        float sh = (float)GetScreenHeight();

        // Process semantic action queue
        SparklesActionType act;
        while (sparkles_input_poll_action(&act)) {
            switch (act) {
                case SPARKLES_ACTION_BACK:
                    if (s_show_quit_modal) s_show_quit_modal = false;
                    else if (sparkles_settings_is_open()) sparkles_settings_close();
                    else if (sparkles_radial_list_is_open()) sparkles_radial_list_close();
                    else if (sparkles_vis_picker_is_open()) sparkles_vis_picker_close();
                    else if (sparkles_context_menu_is_open()) sparkles_context_menu_close();
                    else if (sparkles_queue_is_open()) sparkles_queue_toggle();
                    else if (sparkles_krystal_is_open()) sparkles_krystal_toggle();
                    else if (sparkles_eq_is_open()) sparkles_eq_toggle();
                    else if (s_view_mode != SPARKLES_VIEW_PLAYER) s_view_mode = SPARKLES_VIEW_PLAYER;
                    else s_show_quit_modal = true;
                    break;
                case SPARKLES_ACTION_PLAY_PAUSE:
                    if (atomic_load(&play_state_atomic) == STATE_STOPPED && playing_filepath[0] != '\0') {
                        atomic_store(&current_cmd_atomic, CMD_PLAY);
                    } else {
                        atomic_store(&current_cmd_atomic, CMD_PAUSE);
                    }
                    break;
                case SPARKLES_ACTION_NEXT:
                    atomic_store(&current_cmd_atomic, CMD_NEXT);
                    break;
                case SPARKLES_ACTION_PREV:
                    atomic_store(&current_cmd_atomic, CMD_PREV);
                    break;
                case SPARKLES_ACTION_TOGGLE_RADIAL:
                    sparkles_radial_list_toggle();
                    break;
                case SPARKLES_ACTION_TOGGLE_SETTINGS:
                    sparkles_settings_toggle();
                    break;
                case SPARKLES_ACTION_TOGGLE_VIEW:
                    s_view_mode = (s_view_mode == SPARKLES_VIEW_PLAYER) ? SPARKLES_VIEW_GRID : SPARKLES_VIEW_PLAYER;
                    break;
                case SPARKLES_ACTION_TOGGLE_QUEUE:
                    sparkles_queue_toggle();
                    break;
                case SPARKLES_ACTION_TOGGLE_EQ:
                    if (sparkles_krystal_is_open()) sparkles_krystal_toggle();
                    sparkles_eq_toggle();
                    break;
                case SPARKLES_ACTION_TOGGLE_KRYSTAL:
                    if (sparkles_eq_is_open()) sparkles_eq_toggle();
                    sparkles_krystal_toggle();
                    break;
                case SPARKLES_ACTION_TOGGLE_HELP:
                    sparkles_player_view_toggle_help();
                    break;
                case SPARKLES_ACTION_NAV_LEFT:
                case SPARKLES_ACTION_NAV_RIGHT:
                    s_view_mode = (s_view_mode == SPARKLES_VIEW_PLAYER) ? SPARKLES_VIEW_GRID : SPARKLES_VIEW_PLAYER;
                    break;
                case SPARKLES_ACTION_NAV_UP:
                    if (!sparkles_eq_is_open() && !sparkles_krystal_is_open()) sparkles_eq_toggle();
                    else if (sparkles_eq_is_open()) { sparkles_eq_toggle(); sparkles_krystal_toggle(); }
                    break;
                case SPARKLES_ACTION_NAV_DOWN:
                    if (sparkles_krystal_is_open()) sparkles_krystal_toggle();
                    else if (sparkles_eq_is_open()) sparkles_eq_toggle();
                    break;
                case SPARKLES_ACTION_CYCLE_VIS:
                    sparkles_vis_cycle();
                    break;
                case SPARKLES_ACTION_PICK_VIS:
                    sparkles_vis_picker_toggle();
                    break;
                case SPARKLES_ACTION_LOCATE_PLAYING:
                    tile_song_list_locate_playing();
                    break;
                case SPARKLES_ACTION_TOGGLE_LYRICS:
                    if (!app_config.online_lyrics_asked) {
                        s_show_lyrics_modal = true;
                        s_lyrics_modal_step = 0;
                    } else if (app_config.online_lyrics && !app_config.download_online_lyrics_asked) {
                        s_show_lyrics_modal = true;
                        s_lyrics_modal_step = 1;
                    } else {
                        sparkles_player_view_toggle_lyrics();
                    }
                    break;
                case SPARKLES_ACTION_TOGGLE_SEARCH:
                    if (s_view_mode == SPARKLES_VIEW_PLAYER) sparkles_radial_list_open();
                    else tile_song_list_open_search();
                    break;
                default:
                    break;
            }
        }

        // Quit confirmation input
        if (s_show_quit_modal) {
            bool answered_yes = IsKeyPressed(KEY_Y) || IsKeyPressed(KEY_ENTER);
            bool answered_no  = IsKeyPressed(KEY_N);

            float qw = 420.0f;
            float qh = 160.0f;
            Rectangle q_box = { (sw - qw) * 0.5f, (sh - qh) * 0.5f, qw, qh };
            Rectangle btn_yes = { q_box.x + q_box.width - 170, q_box.y + q_box.height - 42, 68, 26 };
            Rectangle btn_no  = { q_box.x + q_box.width - 92,  q_box.y + q_box.height - 42, 68, 26 };

            Vector2 m = GetMousePosition();
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (CheckCollisionPointRec(m, btn_yes)) answered_yes = true;
                if (CheckCollisionPointRec(m, btn_no))  answered_no = true;
            }

            if (answered_yes) {
                break;
            } else if (answered_no) {
                s_show_quit_modal = false;
            }
        }

        // Process modals and popups first
        bool modal_active = !app_config.listening_profile_asked || s_show_lyrics_modal ||
                            s_show_quit_modal || sparkles_context_menu_is_open() ||
                            sparkles_queue_is_open() || sparkles_radial_list_is_open() ||
                            sparkles_vis_picker_is_open() || sparkles_settings_is_open();

        if (sparkles_settings_is_open()) {
            sparkles_settings_handle_input(sw, sh);
        }
        sparkles_settings_update(sw, sh);

        sparkles_radial_list_update(sw, sh);
        if (sparkles_radial_list_is_open()) {
            sparkles_radial_list_handle_input(sw, sh);
        }

        if (sparkles_vis_picker_is_open()) {
            modal_active = true;
            sparkles_vis_picker_handle_input(sw, sh);
        }
        if (sparkles_vis_picker_get_anim_progress() > 0.001f) {
            modal_active = true;
        }
        sparkles_vis_picker_update(sw, sh);

        if (sparkles_context_menu_update()) {
            modal_active = true;
        }

        if (sparkles_queue_is_open() || sparkles_queue_get_anim_progress() > 0.001f) {
            modal_active = true;
        }
        sparkles_queue_update(sw, sh);

        if (sparkles_eq_is_open() || sparkles_eq_get_anim_progress() > 0.001f) {
            modal_active = true;
        }
        sparkles_eq_update(sw, sh);

        if (sparkles_krystal_is_open() || sparkles_krystal_get_anim_progress() > 0.001f) {
            modal_active = true;
        }
        sparkles_krystal_update(sw, sh);

        // Dispatch input to active view if not consumed by modals
        if (s_view_mode == SPARKLES_VIEW_GRID) {
            sparkles_grid_update(&grid, sw, sh, !modal_active);
        } else if (!modal_active) {
            sparkles_player_view_input(sw, sh);
        }

        // Render frame
        BeginDrawing();
        ClearBackground(COLOR_BG);

        // Dot matrix grid
        const int dot_spacing = 24;
        for (int gx = dot_spacing / 2; gx < (int)sw; gx += dot_spacing) {
            for (int gy = dot_spacing / 2; gy < (int)sh; gy += dot_spacing) {
                DrawRectangle(gx, gy, 2, 2, (Color){ 45, 48, 58, 180 });
            }
        }

        // Calculate recession depth (old window falling deep into screen)
        float eq_anim = sparkles_eq_get_anim_progress();
        float krystal_anim = sparkles_krystal_get_anim_progress();
        float queue_anim = sparkles_queue_get_anim_progress();
        float vis_picker_anim = sparkles_vis_picker_get_anim_progress();
        float set_anim = sparkles_settings_get_anim_progress();
        float recede = fmaxf(fmaxf(fmaxf(fmaxf(eq_anim, krystal_anim), queue_anim), vis_picker_anim), set_anim);

        if (recede > 0.001f) {
            // Old window falls away into the screen (shrinks to 0.76x and drops downward)
            float depth_scale = 1.0f - (0.24f * recede);
            float depth_y = 45.0f * recede;

            rlPushMatrix();
            rlTranslatef(sw * 0.5f, sh * 0.5f + depth_y, 0.0f);
            rlScalef(depth_scale, depth_scale, 1.0f);
            rlTranslatef(-sw * 0.5f, -sh * 0.5f, 0.0f);

            if (s_view_mode == SPARKLES_VIEW_GRID) {
                sparkles_grid_render(&grid);
            } else {
                sparkles_player_view_render(sw, sh);
            }

            // Dark shadow receiving the falling window
            DrawRectangle(0, 0, (int)sw, (int)sh, ColorAlpha(BLACK, 0.80f * recede));

            rlPopMatrix();
        } else {
            if (s_view_mode == SPARKLES_VIEW_GRID) {
                sparkles_grid_render(&grid);
            } else {
                sparkles_player_view_render(sw, sh);
            }
        }

        // Radial arc song list
        if (sparkles_radial_list_is_visible()) {
            sparkles_radial_list_render(sw, sh);
        }

        // New window falling from above onto the screen
        if (sparkles_eq_is_visible()) {
            sparkles_eq_render(sw, sh);
        }

        // Falling Krystal DSP overlay over the recessed window
        if (sparkles_krystal_is_visible()) {
            sparkles_krystal_render(sw, sh);
        }

        // Falling queue overlay
        if (sparkles_queue_is_visible()) {
            sparkles_queue_render(sw, sh);
        }

        // Falling visualizer selector overlay
        if (sparkles_vis_picker_is_visible()) {
            sparkles_vis_picker_render(sw, sh);
        }

        // Falling settings overlay
        if (sparkles_settings_is_visible()) {
            sparkles_settings_render(sw, sh);
        }

        // Dynamic Right-Click context Menu
        if (sparkles_context_menu_is_open()) {
            sparkles_context_menu_render(sw, sh);
        }

        // Listening profile permission dialog modal
        if (!app_config.listening_profile_asked) {
            DrawRectangle(0, 0, (int)sw, (int)sh, ColorAlpha(BLACK, 0.78f));

            float qw = 520.0f;
            float qh = 210.0f;
            Rectangle q_box = { (sw - qw) * 0.5f, (sh - qh) * 0.5f, qw, qh };

            DrawRectangleRec(q_box, (Color){ 8, 8, 11, 255 });
            DrawRectangleLinesEx(q_box, 1.0f, COLOR_ACCENT);
            DrawNothingCornerBrackets(q_box, 8.0f, COLOR_ACCENT);

            DrawText("LISTENING PROFILE", (int)q_box.x + 20, (int)q_box.y + 16, 10, COLOR_TEXT_MUTED);
            DrawText("Enable Local Listening Profile?", (int)q_box.x + 20, (int)q_box.y + 34, FONT_SIZE_MD, COLOR_TEXT_PRIMARY);
            DrawText("Koni can learn your listening habits locally\n(play counts, skip rates, and active hours)\nto power smart and weighted shuffle.\nAll data stays strictly offline on your device.",
                     (int)q_box.x + 20, (int)q_box.y + 64, FONT_SIZE_SM, COLOR_TEXT_MUTED);

            Vector2 m = GetMousePosition();
            Rectangle btn_yes = { q_box.x + q_box.width - 170, q_box.y + q_box.height - 42, 68, 26 };
            Rectangle btn_no  = { q_box.x + q_box.width - 92,  q_box.y + q_box.height - 42, 68, 26 };

            bool hover_yes = CheckCollisionPointRec(m, btn_yes);
            bool hover_no  = CheckCollisionPointRec(m, btn_no);

            DrawRectangleRec(btn_yes, hover_yes ? ColorAlpha(COLOR_ACCENT, 0.25f) : (Color){ 18, 19, 24, 255 });
            DrawRectangleLinesEx(btn_yes, 1.0f, hover_yes ? COLOR_ACCENT : COLOR_TEXT_MUTED);
            DrawText("[Y] Yes", (int)btn_yes.x + 12, (int)btn_yes.y + 6, FONT_SIZE_SM, COLOR_TEXT_PRIMARY);

            DrawRectangleRec(btn_no, hover_no ? ColorAlpha(COLOR_ACCENT, 0.25f) : (Color){ 18, 19, 24, 255 });
            DrawRectangleLinesEx(btn_no, 1.0f, hover_no ? COLOR_ACCENT : COLOR_TEXT_MUTED);
            DrawText("[N] No", (int)btn_no.x + 14, (int)btn_no.y + 6, FONT_SIZE_SM, COLOR_TEXT_PRIMARY);
        }

        // Online lyrics permission dialog modal
        if (s_show_lyrics_modal) {
            DrawRectangle(0, 0, (int)sw, (int)sh, ColorAlpha(BLACK, 0.78f));

            float qw = 520.0f;
            float qh = 190.0f;
            Rectangle q_box = { (sw - qw) * 0.5f, (sh - qh) * 0.5f, qw, qh };

            DrawRectangleRec(q_box, (Color){ 8, 8, 11, 255 });
            DrawRectangleLinesEx(q_box, 1.0f, COLOR_ACCENT);
            DrawNothingCornerBrackets(q_box, 8.0f, COLOR_ACCENT);

            DrawText("Lyrics Setup", (int)q_box.x + 20, (int)q_box.y + 16, 10, COLOR_TEXT_MUTED);

            if (s_lyrics_modal_step == 0) {
                DrawText("Allow Online Lyrics Search?", (int)q_box.x + 20, (int)q_box.y + 34, FONT_SIZE_MD, COLOR_TEXT_PRIMARY);
                DrawText("Koni can search the internet if the song does not have\nbuilt-in lyrics or is not available locally.",
                         (int)q_box.x + 20, (int)q_box.y + 60, FONT_SIZE_SM, COLOR_TEXT_MUTED);
            } else {
                DrawText("Save Downloaded Lyrics Offline?", (int)q_box.x + 20, (int)q_box.y + 34, FONT_SIZE_MD, COLOR_TEXT_PRIMARY);
                DrawText("Would you like to save downloaded lyrics locally\nso they can be retrieved offline later?",
                         (int)q_box.x + 20, (int)q_box.y + 60, FONT_SIZE_SM, COLOR_TEXT_MUTED);
            }

            // Interactive buttons
            Vector2 m = GetMousePosition();
            Rectangle btn_yes = { q_box.x + q_box.width - 170, q_box.y + q_box.height - 42, 68, 26 };
            Rectangle btn_no  = { q_box.x + q_box.width - 92,  q_box.y + q_box.height - 42, 68, 26 };

            bool hover_yes = CheckCollisionPointRec(m, btn_yes);
            bool hover_no  = CheckCollisionPointRec(m, btn_no);

            DrawRectangleRec(btn_yes, hover_yes ? ColorAlpha(COLOR_ACCENT, 0.25f) : (Color){ 18, 19, 24, 255 });
            DrawRectangleLinesEx(btn_yes, 1.0f, hover_yes ? COLOR_ACCENT : COLOR_TEXT_MUTED);
            DrawText("[Y] Yes", (int)btn_yes.x + 12, (int)btn_yes.y + 6, FONT_SIZE_SM, COLOR_TEXT_PRIMARY);

            DrawRectangleRec(btn_no, hover_no ? ColorAlpha(COLOR_ACCENT, 0.25f) : (Color){ 18, 19, 24, 255 });
            DrawRectangleLinesEx(btn_no, 1.0f, hover_no ? COLOR_ACCENT : COLOR_TEXT_MUTED);
            DrawText("[N] No", (int)btn_no.x + 14, (int)btn_no.y + 6, FONT_SIZE_SM, COLOR_TEXT_PRIMARY);

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (hover_yes) {
                    if (s_lyrics_modal_step == 0) {
                        app_config.online_lyrics = true;
                        app_config.online_lyrics_asked = true;
                        config_save();
                        s_lyrics_modal_step = 1;
                    } else {
                        app_config.download_online_lyrics = true;
                        app_config.download_online_lyrics_asked = true;
                        config_save();
                        s_show_lyrics_modal = false;
                        pthread_mutex_lock(&state_mutex);
                        lyrics_engine_fetch_async(p_metadata.title, p_metadata.artist, p_metadata.album,
                                                  atomic_load(&p_total_sec), playing_filepath, p_metadata.lyrics,
                                                  atomic_load(&current_track_id));
                        pthread_mutex_unlock(&state_mutex);
                    }
                } else if (hover_no) {
                    if (s_lyrics_modal_step == 0) {
                        app_config.online_lyrics = false;
                        app_config.online_lyrics_asked = true;
                        config_save();
                        s_show_lyrics_modal = false;
                    } else {
                        app_config.download_online_lyrics = false;
                        app_config.download_online_lyrics_asked = true;
                        config_save();
                        s_show_lyrics_modal = false;
                    }
                }
            }
        }

        // Quit confirmation modal
        if (s_show_quit_modal) {
            DrawRectangle(0, 0, (int)sw, (int)sh, ColorAlpha(BLACK, 0.78f));

            float qw = 420.0f;
            float qh = 160.0f;
            Rectangle q_box = { (sw - qw) * 0.5f, (sh - qh) * 0.5f, qw, qh };

            DrawRectangleRec(q_box, (Color){ 8, 8, 11, 255 });
            DrawRectangleLinesEx(q_box, 1.0f, COLOR_ACCENT);
            DrawNothingCornerBrackets(q_box, 8.0f, COLOR_ACCENT);

            DrawText("QUIT", (int)q_box.x + 20, (int)q_box.y + 16, 10, COLOR_TEXT_MUTED);
            DrawText("Quit Koni?", (int)q_box.x + 20, (int)q_box.y + 34, FONT_SIZE_MD, COLOR_TEXT_PRIMARY);
            DrawText("Are you sure you want to exit the player?", (int)q_box.x + 20, (int)q_box.y + 64, FONT_SIZE_SM, COLOR_TEXT_MUTED);

            Vector2 m = GetMousePosition();
            Rectangle btn_yes = { q_box.x + q_box.width - 170, q_box.y + q_box.height - 42, 68, 26 };
            Rectangle btn_no  = { q_box.x + q_box.width - 92,  q_box.y + q_box.height - 42, 68, 26 };

            bool hover_yes = CheckCollisionPointRec(m, btn_yes);
            bool hover_no  = CheckCollisionPointRec(m, btn_no);

            DrawRectangleRec(btn_yes, hover_yes ? ColorAlpha(COLOR_ACCENT, 0.25f) : (Color){ 18, 19, 24, 255 });
            DrawRectangleLinesEx(btn_yes, 1.0f, hover_yes ? COLOR_ACCENT : COLOR_TEXT_MUTED);
            DrawText("[Y] Yes", (int)btn_yes.x + 12, (int)btn_yes.y + 6, FONT_SIZE_SM, COLOR_TEXT_PRIMARY);

            DrawRectangleRec(btn_no, hover_no ? ColorAlpha(COLOR_ACCENT, 0.25f) : (Color){ 18, 19, 24, 255 });
            DrawRectangleLinesEx(btn_no, 1.0f, hover_no ? COLOR_ACCENT : COLOR_TEXT_MUTED);
            DrawText("[N] No", (int)btn_no.x + 14, (int)btn_no.y + 6, FONT_SIZE_SM, COLOR_TEXT_PRIMARY);
        }

        EndDrawing();
    }

    // Clean teardown
    atomic_store(&current_cmd_atomic, CMD_QUIT);
    pthread_join(audio_thread, NULL);

    mpris_shutdown();

    save_state();
    config_save();
    listening_profile_shutdown();
    sparkles_input_shutdown();
    db_shutdown();
    curl_global_cleanup();
    sparkles_font_unload();
    CloseWindow();
    return 0;
}