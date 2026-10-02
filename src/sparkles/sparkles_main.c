#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "raylib.h"
#if defined(__ANDROID__) || defined(PLATFORM_ANDROID)
#include <jni.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>
#endif
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
#include "modals/sparkles_text_prompt.h"
#include "protocols/mpris.h"
#include "input/sparkles_input.h"
#include "sparkles_nav.h"
#include "koni_paths.h"
#if !defined(__ANDROID__) && !defined(PLATFORM_ANDROID)
#include <curl/curl.h>
#endif
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static bool s_show_lyrics_modal = false;
static int s_lyrics_modal_step = 0;
static bool s_show_quit_modal = false;
static _Atomic bool s_native_ready = false;

#if defined(__ANDROID__) || defined(PLATFORM_ANDROID)
#ifdef __cplusplus
extern "C" {
#endif

JNIEXPORT void JNICALL Java_com_holotwist_koni_MainActivity_setStorageBaseNative(JNIEnv *env, jclass cls, jstring jdata, jstring jcache) {
    (void)cls;
    if (jdata) {
        const char *d = (*env)->GetStringUTFChars(env, jdata, NULL);
        if (d && d[0]) {
            setenv("HOME", d, 1);
            koni_paths_init(d);
            (*env)->ReleaseStringUTFChars(env, jdata, d);
        }
    }
    if (jcache) {
        const char *c = (*env)->GetStringUTFChars(env, jcache, NULL);
        if (c && c[0]) {
            setenv("TMPDIR", c, 1);
            (*env)->ReleaseStringUTFChars(env, jcache, c);
        }
    }
}

JNIEXPORT jboolean JNICALL Java_com_holotwist_koni_MainActivity_isNativeReady(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    return atomic_load(&s_native_ready) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_com_holotwist_koni_MainActivity_addMusicDirNative(JNIEnv *env, jclass cls, jstring jdir) {
    (void)cls;
    if (!jdir || !atomic_load(&s_native_ready)) return;
    const char *dir = (*env)->GetStringUTFChars(env, jdir, NULL);
    if (dir && dir[0]) {
        config_add_music_dir(dir);
        (*env)->ReleaseStringUTFChars(env, jdir, dir);
    }
}

JNIEXPORT void JNICALL Java_com_holotwist_koni_MainActivity_addTrackNative(
    JNIEnv *env, jclass cls, jstring jpath, jstring jtitle, jstring jartist, jstring jalbum, jint duration, jlong mtime)
{
    (void)cls;
    if (!jpath || !atomic_load(&s_native_ready)) return;

    const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
    const char *title = jtitle ? (*env)->GetStringUTFChars(env, jtitle, NULL) : NULL;
    const char *artist = jartist ? (*env)->GetStringUTFChars(env, jartist, NULL) : NULL;
    const char *album = jalbum ? (*env)->GetStringUTFChars(env, jalbum, NULL) : NULL;

    if (path && path[0]) {
        KoniMetadata meta = {
            .title = (char*)title,
            .artist = (char*)artist,
            .album = (char*)album,
            .has_track_gain = false,
            .track_gain = 0.0f
        };
        db_upsert_track(path, (time_t)mtime, &meta, (uint32_t)duration);
    }

    if (album && jalbum) (*env)->ReleaseStringUTFChars(env, jalbum, album);
    if (artist && jartist) (*env)->ReleaseStringUTFChars(env, jartist, artist);
    if (title && jtitle) (*env)->ReleaseStringUTFChars(env, jtitle, title);
    if (path) (*env)->ReleaseStringUTFChars(env, jpath, path);
}

JNIEXPORT void JNICALL Java_com_holotwist_koni_MainActivity_libraryReloadNative(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    if (atomic_load(&s_native_ready)) {
        library_reload();
        force_redraw = true;
    }
}

JNIEXPORT void JNICALL Java_com_holotwist_koni_MainActivity_triggerRescanNative(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    if (atomic_load(&s_native_ready)) {
        library_scanner_start();
    }
}

JNIEXPORT void JNICALL Java_com_holotwist_koni_MainActivity_triggerBackNative(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    sparkles_input_emit_action(SPARKLES_ACTION_BACK);
}

JNIEXPORT void JNICALL Java_com_holotwist_koni_MainActivity_saveStateNative(JNIEnv *env, jclass cls) {
    (void)env; (void)cls;
    save_state();
    config_save();
}

#ifdef __cplusplus
}
#endif
#endif

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    // Initialize persistent storage paths for Android
#if defined(__ANDROID__) || defined(PLATFORM_ANDROID)
    {
        extern struct android_app* GetAndroidApp(void);
        struct android_app *app = GetAndroidApp();
        if (app && app->activity && app->activity->internalDataPath) {
            const char *data_path = app->activity->internalDataPath;
            setenv("HOME", data_path, 1);
            koni_paths_init(data_path);
        } else {
            koni_paths_init(NULL);
        }
    }
#else
    curl_global_init(CURL_GLOBAL_DEFAULT);
    koni_paths_init(NULL);
#endif

    // Initialize configuration and database after storage path is set
    koni_codecs_init();
    config_init();
    db_init();
    playlist_mgmt_init();
    eq_init();
    krystal_init(44100);
    listening_profile_init();
    load_state();
    library_reload();
    atomic_store(&s_native_ready, true);
    library_scanner_start();

    // Start background audio decoding and DSP thread
    pthread_t audio_thread;
    pthread_create(&audio_thread, NULL, audio_thread_func, NULL);

    // Initialize MPRIS D-Bus interface
    mpris_init();

    // Initialize Raylib Window
    SetTraceLogLevel(LOG_ERROR);
#if defined(__ANDROID__) || defined(PLATFORM_ANDROID)
    SetConfigFlags(FLAG_VSYNC_HINT | FLAG_FULLSCREEN_MODE);
    InitWindow(0, 0, "Koni Sparkles");

    // Enable Android fullscreen
    {
        extern struct android_app* GetAndroidApp(void);
        struct android_app *app = GetAndroidApp();
        if (app && app->activity && app->activity->vm) {
            JavaVM *vm = app->activity->vm;
            JNIEnv *env = NULL;
            if ((*vm)->AttachCurrentThread(vm, &env, NULL) == JNI_OK && env) {
                jclass act_cls = (*env)->GetObjectClass(env, app->activity->clazz);
                jmethodID get_win = (*env)->GetMethodID(env, act_cls, "getWindow", "()Landroid/view/Window;");
                if (get_win) {
                    jobject win = (*env)->CallObjectMethod(env, app->activity->clazz, get_win);
                    if (win) {
                        jclass win_cls = (*env)->GetObjectClass(env, win);
                        jmethodID get_decor = (*env)->GetMethodID(env, win_cls, "getDecorView", "()Landroid/view/View;");
                        jobject decor = (*env)->CallObjectMethod(env, win, get_decor);
                        if (decor) {
                            jclass view_cls = (*env)->GetObjectClass(env, decor);
                            jmethodID set_flags = (*env)->GetMethodID(env, view_cls, "setSystemUiVisibility", "(I)V");
                            if (set_flags) {
                                // 256 (STABLE) | 512 (HIDE_NAV) | 1024 (FULLSCREEN) | 2 (HIDE_NAV) | 4 (FULLSCREEN) | 4096 (IMMERSIVE_STICKY)
                                (*env)->CallVoidMethod(env, decor, set_flags, 256 | 512 | 1024 | 2 | 4 | 4096);
                            }
                            (*env)->DeleteLocalRef(env, view_cls);
                            (*env)->DeleteLocalRef(env, decor);
                        }
                        (*env)->DeleteLocalRef(env, win_cls);
                        (*env)->DeleteLocalRef(env, win);
                    }
                }
                if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
                (*env)->DeleteLocalRef(env, act_cls);
                (*vm)->DetachCurrentThread(vm);
            }
        }
    }
#else
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1280, 800, "Koni Sparkles");
    SetWindowMinSize(960, 600);
#endif
    SetExitKey(KEY_NULL);
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
    sparkles_text_prompt_init();
    sparkles_input_init();
    sparkles_nav_init();

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        // Process deferred glyph additions
        // 30s LRU eviction
        sparkles_font_update();

        bool is_searching = tile_song_list_is_searching() || tile_playlists_is_searching() ||
                            sparkles_radial_list_is_open() || sparkles_input_is_text_active() ||
                            sparkles_text_prompt_is_open() || s_show_quit_modal;
        sparkles_input_set_text_focused(is_searching);
        sparkles_input_update(dt);
        float sw = (float)GetScreenWidth();
        float sh = (float)GetScreenHeight();

        // Process semantic action queue
        SparklesActionType act;
        while (sparkles_input_poll_action(&act)) {
            switch (act) {
                case SPARKLES_ACTION_BACK:
                    if (sparkles_text_prompt_is_open()) sparkles_text_prompt_close();
                    else if (s_show_quit_modal) s_show_quit_modal = false;
                    else if (sparkles_settings_is_open()) sparkles_settings_close();
                    else if (sparkles_radial_list_is_open()) sparkles_radial_list_close();
                    else if (sparkles_vis_picker_is_open()) sparkles_vis_picker_close();
                    else if (sparkles_context_menu_is_open()) sparkles_context_menu_close();
                    else if (sparkles_queue_is_open()) sparkles_queue_toggle();
                    else if (sparkles_krystal_is_arena_open()) {
                        sparkles_krystal_close_arena();
                        break;
                    }
                    else if (sparkles_nav_get_target_y() > 0) sparkles_nav_step_y(-1);
                    else if (sparkles_nav_get_target_x() < 0) sparkles_nav_step_x(+1);
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
                    // 'T' toggles between Player (0) and Library (-1)
                    if (sparkles_nav_get_target_x() == 0) sparkles_nav_set_x(-1);
                    else sparkles_nav_set_x(0);
                    break;
                case SPARKLES_ACTION_TOGGLE_QUEUE:
                    sparkles_queue_toggle();
                    break;
                case SPARKLES_ACTION_TOGGLE_EQ:
                    if (sparkles_nav_get_target_y() == 1) sparkles_nav_set_y(0);
                    else sparkles_nav_set_y(1);
                    break;
                case SPARKLES_ACTION_TOGGLE_KRYSTAL:
                    if (sparkles_nav_get_target_y() == 2) sparkles_nav_set_y(0);
                    else sparkles_nav_set_y(2);
                    break;
                case SPARKLES_ACTION_TOGGLE_HELP:
                    sparkles_player_view_toggle_help();
                    break;
                case SPARKLES_ACTION_NAV_LEFT:
                    if (sparkles_nav_get_target_y() == 2) {
                        sparkles_krystal_step_tab(-1);
                    } else {
                        sparkles_nav_step_x(-1);
                    }
                    break;
                case SPARKLES_ACTION_NAV_RIGHT:
                    if (sparkles_nav_get_target_y() == 2) {
                        sparkles_krystal_step_tab(+1);
                    } else {
                        sparkles_nav_step_x(+1);
                    }
                    break;
                case SPARKLES_ACTION_NAV_UP:
                    // Slide up, step through DSP rack (Surface -> EQ -> Krystal)
                    sparkles_nav_step_y(+1);
                    break;
                case SPARKLES_ACTION_NAV_DOWN:
                    // Slide down, step down through DSP rack (Krystal -> EQ -> Surface)
                    sparkles_nav_step_y(-1);
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
                    if (sparkles_nav_get_target_x() == 0) sparkles_radial_list_open();
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

        // Update navigation planes
        sparkles_nav_update(dt);
        bool eq_open = sparkles_nav_is_eq_open();
        bool krystal_open = sparkles_nav_is_krystal_open();

        // Always run closing/opening animations
        sparkles_settings_update(sw, sh);
        sparkles_vis_picker_update(sw, sh);
        sparkles_radial_list_update(sw, sh);
        sparkles_queue_update(sw, sh);
        sparkles_eq_update(sw, sh);
        sparkles_krystal_update(sw, sh);

        // Top-down modal input processing
        bool modal_active = !app_config.listening_profile_asked || s_show_lyrics_modal ||
                            s_show_quit_modal || sparkles_text_prompt_is_open() ||
                            sparkles_context_menu_is_open() || sparkles_queue_is_open() ||
                            sparkles_radial_list_is_open() || sparkles_vis_picker_is_open() ||
                            sparkles_settings_is_open() || eq_open || krystal_open;

        if (sparkles_text_prompt_is_open()) {
            sparkles_text_prompt_update();
        } else if (sparkles_context_menu_is_open()) {
            sparkles_context_menu_update();
        } else if (sparkles_settings_is_open()) {
            sparkles_settings_handle_input(sw, sh);
        } else if (sparkles_vis_picker_is_open()) {
            sparkles_vis_picker_handle_input(sw, sh);
        } else if (sparkles_radial_list_is_open()) {
            sparkles_radial_list_handle_input(sw, sh);
        } else if (!modal_active) {
            int active_x = sparkles_nav_get_target_x();
            int active_y = sparkles_nav_get_target_y();

            if (active_y == 0) {
                if (active_x == -2) {
                    tile_playlists_input(NULL, (Rectangle){ 0, 0, sw, sh });
                } else if (active_x == -1) {
                    tile_song_list_input(NULL, (Rectangle){ 0, 0, sw, sh });
                } else {
                    sparkles_player_view_input(sw, sh);
                }
            }
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

        // Carousel rendering pass (Pages -2, -1, 0)
        float cur_x = sparkles_nav_get_current_x();
        float cur_y = sparkles_nav_get_current_y();

        // Surface plane dimming when DSP sheets rise from below
        float surface_dim = fminf(0.70f, cur_y * 0.45f);

        // Page -2, Playlists
        float off_pl = roundf((-2.0f - cur_x) * sw);
        if (fabsf(off_pl) < sw) {
            rlPushMatrix();
            rlTranslatef(off_pl, 0, 0);
            tile_playlists_render(NULL, (Rectangle){ 0, 0, sw, sh });
            rlPopMatrix();
        }

        // Page -1, Music library
        float off_lib = roundf((-1.0f - cur_x) * sw);
        if (fabsf(off_lib) < sw) {
            rlPushMatrix();
            rlTranslatef(off_lib, 0, 0);
            tile_song_list_render(NULL, (Rectangle){ 0, 0, sw, sh });
            rlPopMatrix();
        }

        // Page 0, Player view
        float off_player = roundf((0.0f - cur_x) * sw);
        if (fabsf(off_player) < sw) {
            rlPushMatrix();
            rlTranslatef(off_player, 0, 0);
            sparkles_player_view_render(sw, sh);
            rlPopMatrix();
        }

        if (surface_dim > 0.001f) {
            DrawRectangle(0, 0, (int)sw, (int)sh, ColorAlpha(BLACK, surface_dim));
        }

        // Vertical DSP Sheets (Layer 1, EQ; Layer 2, Krystal)
        float off_eq = (1.0f - cur_y) * sh;
        if (off_eq < sh && off_eq > -sh && cur_y < 1.90f) {
            rlPushMatrix();
            rlTranslatef(0, off_eq, 0);
            sparkles_eq_render(sw, sh);
            rlPopMatrix();
        }

        float off_krystal = (2.0f - cur_y) * sh;
        if (off_krystal < sh && off_krystal > -sh) {
            rlPushMatrix();
            rlTranslatef(0, off_krystal, 0);
            sparkles_krystal_render(sw, sh);
            rlPopMatrix();
        }

        // Fading section indicator
        sparkles_nav_render_badge(sw, sh);

        // Radial arc song list
        if (sparkles_radial_list_is_visible()) {
            sparkles_radial_list_render(sw, sh);
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

        // Dynamic Right-Click context menu
        if (sparkles_context_menu_is_open()) {
            sparkles_context_menu_render(sw, sh);
        }

        // Top-anchored text prompt modal
        if (sparkles_text_prompt_is_open()) {
            sparkles_text_prompt_render(sw, sh);
        }

        float ui_scale = sparkles_get_ui_scale();

        // Listening profile permission dialog modal
        if (!app_config.listening_profile_asked) {
            DrawRectangle(0, 0, (int)sw, (int)sh, ColorAlpha(BLACK, 0.82f));

            float qw = fminf(540.0f * ui_scale, sw - 24.0f);
            float qh = fminf(240.0f * ui_scale, sh - 40.0f);
            Rectangle q_box = { (sw - qw) * 0.5f, (sh - qh) * 0.5f, qw, qh };

            DrawRectangleRec(q_box, (Color){ 8, 8, 11, 255 });
            DrawRectangleLinesEx(q_box, 1.5f, COLOR_ACCENT);
            DrawNothingCornerBrackets(q_box, 10.0f * ui_scale, COLOR_ACCENT);

            DrawText("LISTENING PROFILE", (int)q_box.x + 16, (int)q_box.y + 14, 11, COLOR_TEXT_MUTED);
            DrawText("Enable Local Listening Profile?", (int)q_box.x + 16, (int)q_box.y + 34, FONT_SIZE_MD, COLOR_TEXT_PRIMARY);
            DrawText("Koni can learn your listening habits locally\n(play counts, skip rates, and active hours)\nto power smart and weighted shuffle.\nAll data stays strictly offline on your device.",
                     (int)q_box.x + 16, (int)(q_box.y + 68 * (ui_scale > 1.4f ? 1.2f : 1.0f)), FONT_SIZE_SM, COLOR_TEXT_MUTED);

            float btn_w = 90.0f * ui_scale;
            float btn_h = fmaxf(44.0f, 36.0f * ui_scale); // Touch target standard
            Rectangle btn_yes = { q_box.x + q_box.width - (btn_w * 2.0f + 20.0f), q_box.y + q_box.height - btn_h - 14.0f, btn_w, btn_h };
            Rectangle btn_no  = { q_box.x + q_box.width - (btn_w + 10.0f),       q_box.y + q_box.height - btn_h - 14.0f, btn_w, btn_h };

            Vector2 tap;
            bool tapped = sparkles_input_consume_tap((Rectangle){0, 0, sw, sh}, &tap);
            bool pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
            Vector2 m = pressed ? GetMousePosition() : tap;

            bool hit_yes = (tapped && CheckCollisionPointRec(tap, btn_yes)) || (pressed && CheckCollisionPointRec(m, btn_yes)) || IsKeyPressed(KEY_Y);
            bool hit_no  = (tapped && CheckCollisionPointRec(tap, btn_no))  || (pressed && CheckCollisionPointRec(m, btn_no))  || IsKeyPressed(KEY_N);

            DrawRectangleRec(btn_yes, hit_yes ? ColorAlpha(COLOR_ACCENT, 0.35f) : (Color){ 18, 19, 24, 255 });
            DrawRectangleLinesEx(btn_yes, 1.5f, COLOR_ACCENT);
            int tw_y = MeasureSparklesText("Yes", FONT_SIZE_SM);
            DrawSparklesText("Yes", (int)(btn_yes.x + (btn_w - tw_y) / 2), (int)(btn_yes.y + (btn_h - FONT_SIZE_SM) / 2), FONT_SIZE_SM, COLOR_TEXT_PRIMARY);

            DrawRectangleRec(btn_no, hit_no ? ColorAlpha(COLOR_ACCENT, 0.35f) : (Color){ 18, 19, 24, 255 });
            DrawRectangleLinesEx(btn_no, 1.5f, COLOR_TEXT_MUTED);
            int tw_n = MeasureSparklesText("No", FONT_SIZE_SM);
            DrawSparklesText("No", (int)(btn_no.x + (btn_w - tw_n) / 2), (int)(btn_no.y + (btn_h - FONT_SIZE_SM) / 2), FONT_SIZE_SM, COLOR_TEXT_MUTED);

            if (hit_yes) {
                app_config.enable_listening_profile = true;
                app_config.listening_profile_asked = true;
                config_save();
            } else if (hit_no) {
                app_config.enable_listening_profile = false;
                app_config.listening_profile_asked = true;
                config_save();
            }
        }

        // Online lyrics permission dialog modal
        if (s_show_lyrics_modal) {
            DrawRectangle(0, 0, (int)sw, (int)sh, ColorAlpha(BLACK, 0.82f));

            float qw = fminf(540.0f * ui_scale, sw - 24.0f);
            float qh = fminf(220.0f * ui_scale, sh - 40.0f);
            Rectangle q_box = { (sw - qw) * 0.5f, (sh - qh) * 0.5f, qw, qh };

            DrawRectangleRec(q_box, (Color){ 8, 8, 11, 255 });
            DrawRectangleLinesEx(q_box, 1.5f, COLOR_ACCENT);
            DrawNothingCornerBrackets(q_box, 10.0f * ui_scale, COLOR_ACCENT);

            DrawText("Lyrics Setup", (int)q_box.x + 16, (int)q_box.y + 14, 11, COLOR_TEXT_MUTED);

            if (s_lyrics_modal_step == 0) {
                DrawText("Allow Online Lyrics Search?", (int)q_box.x + 16, (int)q_box.y + 34, FONT_SIZE_MD, COLOR_TEXT_PRIMARY);
                DrawText("Koni can search the internet if the song does not have\nbuilt-in lyrics or is not available locally.",
                         (int)q_box.x + 16, (int)(q_box.y + 68 * (ui_scale > 1.4f ? 1.2f : 1.0f)), FONT_SIZE_SM, COLOR_TEXT_MUTED);
            } else {
                DrawText("Save Downloaded Lyrics Offline?", (int)q_box.x + 16, (int)q_box.y + 34, FONT_SIZE_MD, COLOR_TEXT_PRIMARY);
                DrawText("Would you like to save downloaded lyrics locally\nso they can be retrieved offline later?",
                         (int)q_box.x + 16, (int)(q_box.y + 68 * (ui_scale > 1.4f ? 1.2f : 1.0f)), FONT_SIZE_SM, COLOR_TEXT_MUTED);
            }

            float btn_w = 90.0f * ui_scale;
            float btn_h = fmaxf(44.0f, 36.0f * ui_scale);
            Rectangle btn_yes = { q_box.x + q_box.width - (btn_w * 2.0f + 20.0f), q_box.y + q_box.height - btn_h - 14.0f, btn_w, btn_h };
            Rectangle btn_no  = { q_box.x + q_box.width - (btn_w + 10.0f),       q_box.y + q_box.height - btn_h - 14.0f, btn_w, btn_h };

            Vector2 tap;
            bool tapped = sparkles_input_consume_tap((Rectangle){0, 0, sw, sh}, &tap);
            bool pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
            Vector2 m = pressed ? GetMousePosition() : tap;

            bool hit_yes = (tapped && CheckCollisionPointRec(tap, btn_yes)) || (pressed && CheckCollisionPointRec(m, btn_yes)) || IsKeyPressed(KEY_Y);
            bool hit_no  = (tapped && CheckCollisionPointRec(tap, btn_no))  || (pressed && CheckCollisionPointRec(m, btn_no))  || IsKeyPressed(KEY_N);

            DrawRectangleRec(btn_yes, hit_yes ? ColorAlpha(COLOR_ACCENT, 0.35f) : (Color){ 18, 19, 24, 255 });
            DrawRectangleLinesEx(btn_yes, 1.5f, COLOR_ACCENT);
            int tw_y = MeasureSparklesText("Yes", FONT_SIZE_SM);
            DrawSparklesText("Yes", (int)(btn_yes.x + (btn_w - tw_y) / 2), (int)(btn_yes.y + (btn_h - FONT_SIZE_SM) / 2), FONT_SIZE_SM, COLOR_TEXT_PRIMARY);

            DrawRectangleRec(btn_no, hit_no ? ColorAlpha(COLOR_ACCENT, 0.35f) : (Color){ 18, 19, 24, 255 });
            DrawRectangleLinesEx(btn_no, 1.5f, COLOR_TEXT_MUTED);
            int tw_n = MeasureSparklesText("No", FONT_SIZE_SM);
            DrawSparklesText("No", (int)(btn_no.x + (btn_w - tw_n) / 2), (int)(btn_no.y + (btn_h - FONT_SIZE_SM) / 2), FONT_SIZE_SM, COLOR_TEXT_MUTED);

            if (hit_yes) {
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
            } else if (hit_no) {
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
#if !defined(__ANDROID__) && !defined(PLATFORM_ANDROID)
    curl_global_cleanup();
#endif
    sparkles_font_unload();
    CloseWindow();
    return 0;
}