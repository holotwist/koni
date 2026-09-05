#include "extension.h"
#include "ext_registry.h"
#include "state.h"
#include "ui_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* Forward declaration of extensions, preparing for a tracker tab */
// extern KoniExtension tracker_extension;

static const KoniExtension *registered_extensions[] = {
    // &tracker_extension,
    NULL
};

/* Host Context Implementation */
static bool host_is_playing(void) {
    return atomic_load(&play_state_atomic) == STATE_PLAYING;
}

static uint64_t host_get_playback_time_ms(void) {
    uint32_t srate = atomic_load(&vis_srate);
    if (srate == 0) srate = 44100;
    return ((uint64_t)atomic_load(&p_frames_consumed) * 1000ULL) / srate;
}

static const char* host_get_current_filepath(void) {
    return playing_filepath;
}

static const KoniCodecImpl* host_get_current_codec(void) {
    return active_codec;
}

static KoniDecoder* host_get_current_decoder(void) {
    return active_decoder;
}

static void* host_query_codec_interface(KoniInterfaceID iface_id) {
    if (active_codec && active_codec->get_interface && active_decoder) {
        return active_codec->get_interface(active_decoder, iface_id);
    }
    return NULL;
}

static void host_request_redraw(void) {
    force_redraw = true;
}

static void host_set_status_message(const char *fmt, ...) {
    (void)fmt;
    // Reserved for status line rendering
}

static KoniHostContext g_host_ctx = {
    .is_playing = host_is_playing,
    .get_playback_time_ms = host_get_playback_time_ms,
    .get_current_filepath = host_get_current_filepath,
    .get_current_codec = host_get_current_codec,
    .get_current_decoder = host_get_current_decoder,
    .query_codec_interface = host_query_codec_interface,
    .request_redraw = host_request_redraw,
    .set_status_message = host_set_status_message
};

KoniHostContext* koni_host_get_context(void) {
    return &g_host_ctx;
}

const KoniExtension** koni_extensions_get_all(void) {
    return registered_extensions;
}

static bool is_extension_active(const KoniExtension *ext) {
    if (!ext) return false;

    switch (ext->activation_mode) {
        case EXT_ACTIVE_ALWAYS:
            return true;
            
        case EXT_ACTIVE_ON_CODEC_CAP:
            if (active_codec && (active_codec->capabilities & ext->required_codec_cap)) {
                return true;
            }
            return false;

        case EXT_ACTIVE_ON_FILE_EXT:
            if (!playing_filepath[0] || !ext->target_extensions) return false;
            const char *dot = strrchr(playing_filepath, '.');
            if (!dot) return false;
            for (int i = 0; ext->target_extensions[i]; i++) {
                if (strcasecmp(dot, ext->target_extensions[i]) == 0) return true;
            }
            return false;

        case EXT_ACTIVE_CUSTOM:
            if (ext->is_active_custom) return ext->is_active_custom(&g_host_ctx);
            return false;
    }
    return false;
}

int koni_extensions_get_active(KoniExtension **out_active, int max_count) {
    int count = 0;
    for (int i = 0; registered_extensions[i] && count < max_count; i++) {
        if (is_extension_active(registered_extensions[i])) {
            out_active[count++] = (KoniExtension*)registered_extensions[i];
        }
    }
    return count;
}

int koni_extensions_get_active_tabs(ExtTabDescriptor **out_tabs, KoniExtension **out_exts, int max_count) {
    int count = 0;
    for (int i = 0; registered_extensions[i] && count < max_count; i++) {
        KoniExtension *ext = (KoniExtension*)registered_extensions[i];
        if (ext->provides_tab && is_extension_active(ext)) {
            if (out_tabs) out_tabs[count] = &ext->tab;
            if (out_exts) out_exts[count] = ext;
            count++;
        }
    }
    return count;
}

void koni_extensions_init(void) {
    for (int i = 0; registered_extensions[i]; i++) {
        KoniExtension *ext = (KoniExtension*)registered_extensions[i];
        if (ext->init) ext->init(ext, &g_host_ctx);
    }
}

void koni_extensions_shutdown(void) {
    for (int i = 0; registered_extensions[i]; i++) {
        KoniExtension *ext = (KoniExtension*)registered_extensions[i];
        if (ext->shutdown) ext->shutdown(ext);
    }
}

void koni_extensions_on_track_loaded(const char *filepath, const KoniCodecImpl *codec, KoniDecoder *dec) {
    for (int i = 0; registered_extensions[i]; i++) {
        KoniExtension *ext = (KoniExtension*)registered_extensions[i];
        if (ext->on_track_loaded) ext->on_track_loaded(ext, filepath, codec, dec);
    }
}

void koni_extensions_on_track_stopped(void) {
    for (int i = 0; registered_extensions[i]; i++) {
        KoniExtension *ext = (KoniExtension*)registered_extensions[i];
        if (ext->on_track_stopped) ext->on_track_stopped(ext);
    }
}

void koni_extensions_on_tick(void) {
    for (int i = 0; registered_extensions[i]; i++) {
        KoniExtension *ext = (KoniExtension*)registered_extensions[i];
        if (is_extension_active(ext) && ext->on_tick) {
            ext->on_tick(ext, &g_host_ctx);
        }
    }
}

bool koni_extensions_handle_key(int ch) {
    for (int i = 0; registered_extensions[i]; i++) {
        KoniExtension *ext = (KoniExtension*)registered_extensions[i];
        if (is_extension_active(ext) && ext->handle_key) {
            if (ext->handle_key(ext, ch, &g_host_ctx)) return true;
        }
    }
    return false;
}

int koni_extension_call(const char *ext_id, const char *method, void *in_data, void *out_data) {
    if (!ext_id || !method) return -1;
    for (int i = 0; registered_extensions[i]; i++) {
        KoniExtension *ext = (KoniExtension*)registered_extensions[i];
        if (strcmp(ext->id, ext_id) == 0) {
            if (ext->call) return ext->call(ext, method, in_data, out_data);
            return -2;
        }
    }
    return -3; // Not found
}

int koni_extension_broadcast(const char *event_name, void *event_data) {
    int handled_count = 0;
    for (int i = 0; registered_extensions[i]; i++) {
        KoniExtension *ext = (KoniExtension*)registered_extensions[i];
        if (ext->call) {
            if (ext->call(ext, event_name, event_data, NULL) == 0) handled_count++;
        }
    }
    return handled_count;
}