#define _DEFAULT_SOURCE
#include "koni_paths.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(__ANDROID__) || defined(PLATFORM_ANDROID)
#include <android/native_activity.h>
#include <android_native_app_glue.h>
#endif

static char s_base_dir[1024] = {0};

void koni_ensure_dir(const char *path) {
    if (!path || !path[0]) return;
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static void ensure_subdirs(void) {
    char sub[1024];
    koni_get_path(sub, sizeof(sub), "playlists");
    koni_ensure_dir(sub);
    koni_get_path(sub, sizeof(sub), "peq");
    koni_ensure_dir(sub);
    koni_get_path(sub, sizeof(sub), "lyrics");
    koni_ensure_dir(sub);
}

void koni_paths_init(const char *custom_base_dir) {
    if (custom_base_dir && custom_base_dir[0]) {
        strncpy(s_base_dir, custom_base_dir, sizeof(s_base_dir) - 1);
        s_base_dir[sizeof(s_base_dir) - 1] = '\0';
        koni_ensure_dir(s_base_dir);
        ensure_subdirs();
        return;
    }

    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0]) {
        snprintf(s_base_dir, sizeof(s_base_dir), "%s/koni", xdg);
        koni_ensure_dir(s_base_dir);
        ensure_subdirs();
        return;
    }

    const char *home = getenv("HOME");
    if (home && home[0]) {
        snprintf(s_base_dir, sizeof(s_base_dir), "%s/.config/koni", home);
        koni_ensure_dir(s_base_dir);
        ensure_subdirs();
        return;
    }

    #if defined(__ANDROID__) || defined(PLATFORM_ANDROID)
    extern struct android_app* GetAndroidApp(void);
    struct android_app *app = GetAndroidApp();
    if (app && app->activity && app->activity->internalDataPath) {
        snprintf(s_base_dir, sizeof(s_base_dir), "%s", app->activity->internalDataPath);
        s_base_dir[sizeof(s_base_dir) - 1] = '\0';
        koni_ensure_dir(s_base_dir);
        ensure_subdirs();
        return;
    }
#endif

    const char *tmp = getenv("TMPDIR");
    if (tmp && tmp[0]) {
        snprintf(s_base_dir, sizeof(s_base_dir), "%s/koni", tmp);
    } else {
        strncpy(s_base_dir, "./.koni", sizeof(s_base_dir) - 1);
    }
    s_base_dir[sizeof(s_base_dir) - 1] = '\0';
    koni_ensure_dir(s_base_dir);
    ensure_subdirs();
}

const char* koni_get_base_dir(void) {
    if (!s_base_dir[0]) {
        koni_paths_init(NULL);
    }
    return s_base_dir;
}

void koni_get_path(char *out_buf, size_t sz, const char *relative_path) {
    if (!out_buf || sz == 0) return;
    const char *base = koni_get_base_dir();
    if (!relative_path || !relative_path[0]) {
        snprintf(out_buf, sz, "%s", base);
    } else {
        snprintf(out_buf, sz, "%s/%s", base, relative_path);
    }
}