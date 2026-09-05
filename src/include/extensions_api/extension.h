#ifndef KONI_EXTENSION_H
#define KONI_EXTENSION_H

#include <stdint.h>
#include <stdbool.h>
#include "codec.h"

typedef enum {
    EXT_ACTIVE_ALWAYS,
    EXT_ACTIVE_ON_CODEC_CAP,
    EXT_ACTIVE_ON_FILE_EXT,
    EXT_ACTIVE_CUSTOM
} ExtActivationMode;

typedef struct KoniExtension KoniExtension;
typedef struct KoniHostContext KoniHostContext;

/* UI Panel Descriptor provided by an extension if it contributes a tab */
typedef struct {
    int  tab_id;              /* Assigned by host */
    char tab_label[24];      /* e.g., "3:tracker" */
    char shortcut_key;       /* e.g., '3' */
    void (*render)(KoniExtension *ext, int y, int x, int h, int w);
} ExtTabDescriptor;

/* Services provided by Koni host environment to extensions */
struct KoniHostContext {
    bool (*is_playing)(void);
    uint64_t (*get_playback_time_ms)(void);
    const char* (*get_current_filepath)(void);
    const KoniCodecImpl* (*get_current_codec)(void);
    KoniDecoder* (*get_current_decoder)(void);

    /* Codec interface query helper */
    void* (*query_codec_interface)(KoniInterfaceID iface_id);

    /* UI Services */
    void (*request_redraw)(void);
    void (*set_status_message)(const char *fmt, ...);
};

/* The Extension Plugin Definition */
struct KoniExtension {
    const char *id;          /* Unique key, e.g., "koni.tracker" */
    const char *name;        /* Display name */
    const char *version;     /* "1.0.0" */
    const char *author;

    /* Activation Rules */
    ExtActivationMode activation_mode;
    uint32_t required_codec_cap;     /* For EXT_ACTIVE_ON_CODEC_CAP */
    const char **target_extensions;  /* NULL-terminated, for EXT_ACTIVE_ON_FILE_EXT */
    bool (*is_active_custom)(KoniHostContext *host);

    /* UI Contributions */
    bool provides_tab;
    ExtTabDescriptor tab;

    /* Input Handling, returns true if key was handled by this extension */
    bool (*handle_key)(KoniExtension *ext, int ch, KoniHostContext *host);

    /* Lifecycle Hooks */
    bool (*init)(KoniExtension *ext, KoniHostContext *host);
    void (*shutdown)(KoniExtension *ext);

    /* Playback Event Hooks */
    void (*on_track_loaded)(KoniExtension *ext, const char *filepath, const KoniCodecImpl *codec, KoniDecoder *dec);
    void (*on_track_stopped)(KoniExtension *ext);
    void (*on_tick)(KoniExtension *ext, KoniHostContext *host);

    /* Inter-extension / Codec messaging hook */
    int (*call)(KoniExtension *ext, const char *method, void *in_data, void *out_data);
};

#ifdef __cplusplus
extern "C" {
#endif

void koni_extensions_init(void);
void koni_extensions_shutdown(void);
void koni_extensions_on_track_loaded(const char *filepath, const KoniCodecImpl *codec, KoniDecoder *dec);
void koni_extensions_on_track_stopped(void);
void koni_extensions_on_tick(void);
bool koni_extensions_handle_key(int ch);

/* Custom extension call & broadcast API */
int koni_extension_call(const char *ext_id, const char *method, void *in_data, void *out_data);
int koni_extension_broadcast(const char *event_name, void *event_data);

/* Host Context accessor */
KoniHostContext* koni_host_get_context(void);

#ifdef __cplusplus
}
#endif

#endif // KONI_EXTENSION_H