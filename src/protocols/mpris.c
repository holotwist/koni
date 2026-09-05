#include "protocols/mpris.h"
#include "ui_common.h"
#include "state.h"
#include <dbus/dbus.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static pthread_t mpris_thread;
static DBusConnection *dbus_conn = NULL;
static bool mpris_running = false;

static void sanitize_utf8(char *dst, const char *src, size_t dst_size) {
    if (!dst || dst_size == 0) return;
    if (!src) { dst[0] = '\0'; return; }

    size_t di = 0;
    const unsigned char *s = (const unsigned char *)src;

    while (*s && di + 1 < dst_size) {
        unsigned char c = *s;
        if (c < 0x80) { // ASCII
            dst[di++] = (char)c;
            s++;
        } else if ((c >= 0xC2 && c <= 0xDF) && (s[1] >= 0x80 && s[1] <= 0xBF)) { // 2-byte
            if (di + 2 >= dst_size) break;
            dst[di++] = (char)s[0];
            dst[di++] = (char)s[1];
            s += 2;
        } else if (c == 0xE0 && (s[1] >= 0xA0 && s[1] <= 0xBF) && (s[2] >= 0x80 && s[2] <= 0xBF)) { // 3-byte E0
            if (di + 3 >= dst_size) break;
            dst[di++] = (char)s[0];
            dst[di++] = (char)s[1];
            dst[di++] = (char)s[2];
            s += 3;
        } else if (((c >= 0xE1 && c <= 0xEC) || c == 0xEE || c == 0xEF) &&
                   (s[1] >= 0x80 && s[1] <= 0xBF) && (s[2] >= 0x80 && s[2] <= 0xBF)) { // 3-byte general
            if (di + 3 >= dst_size) break;
            dst[di++] = (char)s[0];
            dst[di++] = (char)s[1];
            dst[di++] = (char)s[2];
            s += 3;
        } else if (c == 0xED && (s[1] >= 0x80 && s[1] <= 0x9F) && (s[2] >= 0x80 && s[2] <= 0xBF)) { // 3-byte ED (exclude surrogates)
            if (di + 3 >= dst_size) break;
            dst[di++] = (char)s[0];
            dst[di++] = (char)s[1];
            dst[di++] = (char)s[2];
            s += 3;
        } else if (c == 0xF0 && (s[1] >= 0x90 && s[1] <= 0xBF) &&
                   (s[2] >= 0x80 && s[2] <= 0xBF) && (s[3] >= 0x80 && s[3] <= 0xBF)) { // 4-byte F0
            if (di + 4 >= dst_size) break;
            dst[di++] = (char)s[0];
            dst[di++] = (char)s[1];
            dst[di++] = (char)s[2];
            dst[di++] = (char)s[3];
            s += 4;
        } else if ((c >= 0xF1 && c <= 0xF3) && (s[1] >= 0x80 && s[1] <= 0xBF) &&
                   (s[2] >= 0x80 && s[2] <= 0xBF) && (s[3] >= 0x80 && s[3] <= 0xBF)) { // 4-byte F1-F3
            if (di + 4 >= dst_size) break;
            dst[di++] = (char)s[0];
            dst[di++] = (char)s[1];
            dst[di++] = (char)s[2];
            dst[di++] = (char)s[3];
            s += 4;
        } else if (c == 0xF4 && (s[1] >= 0x80 && s[1] <= 0x8F) &&
                   (s[2] >= 0x80 && s[2] <= 0xBF) && (s[3] >= 0x80 && s[3] <= 0xBF)) { // 4-byte F4
            if (di + 4 >= dst_size) break;
            dst[di++] = (char)s[0];
            dst[di++] = (char)s[1];
            dst[di++] = (char)s[2];
            dst[di++] = (char)s[3];
            s += 4;
        } else {
            // Replace invalid byte with '?'
            dst[di++] = '?';
            s++;
        }
    }
    dst[di] = '\0';
}

static void append_variant_string(DBusMessageIter *iter, const char *val)
{
    char clean[1024];
    sanitize_utf8(clean, val, sizeof(clean));
    const char *clean_p = clean;

    DBusMessageIter variant;
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "s", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &clean_p);
    dbus_message_iter_close_container(iter, &variant);
}

static void append_variant_object_path(DBusMessageIter *iter, const char *val)
{
    DBusMessageIter variant;
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "o", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_OBJECT_PATH, &val);
    dbus_message_iter_close_container(iter, &variant);
}

static void append_variant_double(DBusMessageIter *iter, double val)
{
    DBusMessageIter variant;
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "d", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_DOUBLE, &val);
    dbus_message_iter_close_container(iter, &variant);
}

static void append_variant_int64(DBusMessageIter *iter, int64_t val)
{
    DBusMessageIter variant;
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "x", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_INT64, &val);
    dbus_message_iter_close_container(iter, &variant);
}

static void append_metadata_variant(DBusMessageIter *iter)
{
    DBusMessageIter variant, dict, dict_entry;
    dbus_message_iter_open_container(iter, DBUS_TYPE_VARIANT, "a{sv}", &variant);
    dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "{sv}", &dict);

    pthread_mutex_lock(&state_mutex);

    int track_id = atomic_load(&current_track_id);
    if (track_id > 0 && playing_file_idx >= 0)
    {
        char track_path[128];
        snprintf(track_path, sizeof(track_path), "/org/mpris/MediaPlayer2/Track/%d", track_id);
        const char *track_id_key = "mpris:trackid";
        const char *track_id_val = track_path;

        dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
        dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &track_id_key);
        append_variant_object_path(&dict_entry, track_id_val);
        dbus_message_iter_close_container(&dict, &dict_entry);

        uint32_t length_sec = atomic_load(&p_total_sec);
        if (length_sec > 0)
        {
            dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
            const char *length_key = "mpris:length";
            int64_t length_us = (int64_t)length_sec * 1000000LL;
            dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &length_key);
            append_variant_int64(&dict_entry, length_us);
            dbus_message_iter_close_container(&dict, &dict_entry);
        }

        if (p_metadata.title && strlen(p_metadata.title) > 0)
        {
            dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
            const char *title_key = "xesam:title";
            dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title_key);
            append_variant_string(&dict_entry, p_metadata.title);
            dbus_message_iter_close_container(&dict, &dict_entry);
        }
        else
        {
            char title_no_ext[256];
            strncpy(title_no_ext, playing_filename, sizeof(title_no_ext) - 1);
            title_no_ext[sizeof(title_no_ext) - 1] = '\0';
            
            char *dot = strrchr(title_no_ext, '.');
            if (dot && dot != title_no_ext) {
                *dot = '\0';
            }
            
            dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
            const char *title_key = "xesam:title";
            dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &title_key);
            append_variant_string(&dict_entry, title_no_ext);
            dbus_message_iter_close_container(&dict, &dict_entry);
        }

        if (p_metadata.artist && strlen(p_metadata.artist) > 0)
        {
            dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
            const char *artist_key = "xesam:artist";
            dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &artist_key);
            DBusMessageIter artist_var, artist_arr;
            dbus_message_iter_open_container(&dict_entry, DBUS_TYPE_VARIANT, "as", &artist_var);
            dbus_message_iter_open_container(&artist_var, DBUS_TYPE_ARRAY, "s", &artist_arr);
            char clean_artist[512];
            sanitize_utf8(clean_artist, p_metadata.artist, sizeof(clean_artist));
            const char *clean_artist_p = clean_artist;
            dbus_message_iter_append_basic(&artist_arr, DBUS_TYPE_STRING, &clean_artist_p);
            dbus_message_iter_close_container(&artist_var, &artist_arr);
            dbus_message_iter_close_container(&dict_entry, &artist_var);
            dbus_message_iter_close_container(&dict, &dict_entry);
        }

        if (p_metadata.album && strlen(p_metadata.album) > 0)
        {
            dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
            const char *album_key = "xesam:album";
            dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &album_key);
            append_variant_string(&dict_entry, p_metadata.album);
            dbus_message_iter_close_container(&dict, &dict_entry);
        }
        
        if (p_metadata.art_url && strlen(p_metadata.art_url) > 0)
        {
            dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
            const char *art_key = "mpris:artUrl";
            dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &art_key);
            append_variant_string(&dict_entry, p_metadata.art_url);
            dbus_message_iter_close_container(&dict, &dict_entry);
        }
    }
    else
    {
        dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
        const char *track_id_key = "mpris:trackid";
        const char *track_id_val = "/org/mpris/MediaPlayer2/TrackList/NoTrack";
        dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &track_id_key);
        append_variant_object_path(&dict_entry, track_id_val);
        dbus_message_iter_close_container(&dict, &dict_entry);
    }

    pthread_mutex_unlock(&state_mutex);

    dbus_message_iter_close_container(&variant, &dict);
    dbus_message_iter_close_container(iter, &variant);
}

static DBusHandlerResult mpris_handle_methods(DBusConnection *conn, DBusMessage *msg, void *user_data)
{
    (void)user_data;
    const char *path = dbus_message_get_path(msg);
    if (!path || strcmp(path, "/org/mpris/MediaPlayer2") != 0)
    {
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }

    if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Introspectable", "Introspect"))
    {
        const char *xml =
            "<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\"\n"
            "\"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
            "<node>\n"
            "  <interface name=\"org.freedesktop.DBus.Introspectable\">\n"
            "    <method name=\"Introspect\">\n"
            "      <arg name=\"xml_data\" type=\"s\" direction=\"out\"/>\n"
            "    </method>\n"
            "  </interface>\n"
            "  <interface name=\"org.freedesktop.DBus.Properties\">\n"
            "    <method name=\"Get\">\n"
            "      <arg name=\"interface_name\" type=\"s\" direction=\"in\"/>\n"
            "      <arg name=\"property_name\" type=\"s\" direction=\"in\"/>\n"
            "      <arg name=\"value\" type=\"v\" direction=\"out\"/>\n"
            "    </method>\n"
            "    <method name=\"GetAll\">\n"
            "      <arg name=\"interface_name\" type=\"s\" direction=\"in\"/>\n"
            "      <arg name=\"properties\" type=\"a{sv}\" direction=\"out\"/>\n"
            "    </method>\n"
            "    <method name=\"Set\">\n"
            "      <arg name=\"interface_name\" type=\"s\" direction=\"in\"/>\n"
            "      <arg name=\"property_name\" type=\"s\" direction=\"in\"/>\n"
            "      <arg name=\"value\" type=\"v\" direction=\"in\"/>\n"
            "    </method>\n"
            "    <signal name=\"PropertiesChanged\">\n"
            "      <arg name=\"interface_name\" type=\"s\"/>\n"
            "      <arg name=\"changed_properties\" type=\"a{sv}\"/>\n"
            "      <arg name=\"invalidated_properties\" type=\"as\"/>\n"
            "    </signal>\n"
            "  </interface>\n"
            "  <interface name=\"org.mpris.MediaPlayer2\">\n"
            "    <property name=\"CanQuit\" type=\"b\" access=\"read\"/>\n"
            "    <property name=\"CanRaise\" type=\"b\" access=\"read\"/>\n"
            "    <property name=\"HasTrackList\" type=\"b\" access=\"read\"/>\n"
            "    <property name=\"Identity\" type=\"s\" access=\"read\"/>\n"
            "    <property name=\"CanSetFullscreen\" type=\"b\" access=\"read\"/>\n"
            "    <property name=\"SupportedUriSchemes\" type=\"as\" access=\"read\"/>\n"
            "    <property name=\"SupportedMimeTypes\" type=\"as\" access=\"read\"/>\n"
            "  </interface>\n"
            "  <interface name=\"org.mpris.MediaPlayer2.Player\">\n"
            "    <method name=\"Next\"/>\n"
            "    <method name=\"Previous\"/>\n"
            "    <method name=\"Pause\"/>\n"
            "    <method name=\"PlayPause\"/>\n"
            "    <method name=\"Stop\"/>\n"
            "    <method name=\"Play\"/>\n"
            "    <method name=\"Seek\">\n"
            "      <arg name=\"Offset\" type=\"x\" direction=\"in\"/>\n"
            "    </method>\n"
            "    <method name=\"SetPosition\">\n"
            "      <arg name=\"TrackId\" type=\"o\" direction=\"in\"/>\n"
            "      <arg name=\"Position\" type=\"x\" direction=\"in\"/>\n"
            "    </method>\n"
            "    <signal name=\"Seeked\">\n"
            "      <arg name=\"Position\" type=\"x\"/>\n"
            "    </signal>\n"
            "    <property name=\"PlaybackStatus\" type=\"s\" access=\"read\"/>\n"
            "    <property name=\"LoopStatus\" type=\"s\" access=\"readwrite\"/>\n"
            "    <property name=\"Rate\" type=\"d\" access=\"readwrite\"/>\n"
            "    <property name=\"Shuffle\" type=\"b\" access=\"readwrite\"/>\n"
            "    <property name=\"Metadata\" type=\"a{sv}\" access=\"read\"/>\n"
            "    <property name=\"Volume\" type=\"d\" access=\"readwrite\"/>\n"
            "    <property name=\"Position\" type=\"x\" access=\"read\"/>\n"
            "    <property name=\"MinimumRate\" type=\"d\" access=\"read\"/>\n"
            "    <property name=\"MaximumRate\" type=\"d\" access=\"read\"/>\n"
            "    <property name=\"CanControl\" type=\"b\" access=\"read\"/>\n"
            "    <property name=\"CanPlay\" type=\"b\" access=\"read\"/>\n"
            "    <property name=\"CanPause\" type=\"b\" access=\"read\"/>\n"
            "    <property name=\"CanGoNext\" type=\"b\" access=\"read\"/>\n"
            "    <property name=\"CanGoPrevious\" type=\"b\" access=\"read\"/>\n"
            "    <property name=\"CanSeek\" type=\"b\" access=\"read\"/>\n"
            "  </interface>\n"
            "</node>\n";

        DBusMessage *reply = dbus_message_new_method_return(msg);
        if (reply)
        {
            dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
            dbus_connection_send(conn, reply, NULL);
            dbus_message_unref(reply);
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (dbus_message_is_method_call(msg, "org.mpris.MediaPlayer2.Player", "PlayPause"))
    {
        if (atomic_load(&play_state_atomic) == STATE_STOPPED && playing_file_idx >= 0)
        {
            atomic_store(&current_cmd_atomic, CMD_PLAY);
        }
        else
        {
            atomic_store(&current_cmd_atomic, CMD_PAUSE);
        }
    }
    else if (dbus_message_is_method_call(msg, "org.mpris.MediaPlayer2.Player", "Play"))
    {
        if (atomic_load(&play_state_atomic) == STATE_STOPPED && playing_file_idx >= 0)
        {
            atomic_store(&current_cmd_atomic, CMD_PLAY);
        }
        else if (atomic_load(&play_state_atomic) != STATE_PLAYING)
        {
            atomic_store(&current_cmd_atomic, CMD_PAUSE);
        }
    }
    else if (dbus_message_is_method_call(msg, "org.mpris.MediaPlayer2.Player", "Pause"))
    {
        if (atomic_load(&play_state_atomic) == STATE_PLAYING)
        {
            atomic_store(&current_cmd_atomic, CMD_PAUSE);
        }
    }
    else if (dbus_message_is_method_call(msg, "org.mpris.MediaPlayer2.Player", "Stop"))
    {
        atomic_store(&current_cmd_atomic, CMD_STOP);
    }
    else if (dbus_message_is_method_call(msg, "org.mpris.MediaPlayer2.Player", "Next"))
    {
        atomic_store(&current_cmd_atomic, CMD_NEXT);
    }
    else if (dbus_message_is_method_call(msg, "org.mpris.MediaPlayer2.Player", "Previous"))
    {
        atomic_store(&current_cmd_atomic, CMD_PREV);
    }
    else if (dbus_message_is_method_call(msg, "org.mpris.MediaPlayer2.Player", "Seek"))
    {
        dbus_int64_t offset_us;
        if (dbus_message_get_args(msg, NULL, DBUS_TYPE_INT64, &offset_us, DBUS_TYPE_INVALID))
        {
            int base_ms = (atomic_load(&current_cmd_atomic) == CMD_SEEK) ? atomic_load(&seek_target_ms) : (atomic_load(&p_current_sec) * 1000);
            int t_ms = base_ms + (offset_us / 1000LL);
            int tot_ms = atomic_load(&p_total_sec) * 1000;
            if (t_ms > tot_ms) t_ms = tot_ms - 1000;
            if (t_ms < 0) t_ms = 0;
            atomic_store(&seek_target_ms, t_ms);
            atomic_store(&current_cmd_atomic, CMD_SEEK);
        }
    }
    else if (dbus_message_is_method_call(msg, "org.mpris.MediaPlayer2.Player", "SetPosition"))
    {
        const char *track_id;
        dbus_int64_t pos_us;
        if (dbus_message_get_args(msg, NULL, DBUS_TYPE_OBJECT_PATH, &track_id, DBUS_TYPE_INT64, &pos_us, DBUS_TYPE_INVALID))
        {
            int t_ms = pos_us / 1000LL;
            int tot_ms = atomic_load(&p_total_sec) * 1000;
            if (t_ms > tot_ms) t_ms = tot_ms - 1000;
            if (t_ms < 0) t_ms = 0;
            atomic_store(&seek_target_ms, t_ms);
            atomic_store(&current_cmd_atomic, CMD_SEEK);
        }
    }
    else if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "Get"))
    {
        DBusMessageIter args;
        if (!dbus_message_iter_init(msg, &args))
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

        const char *interface;
        dbus_message_iter_get_basic(&args, &interface);
        dbus_message_iter_next(&args);

        if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

        const char *prop;
        dbus_message_iter_get_basic(&args, &prop);

        DBusMessage *reply = dbus_message_new_method_return(msg);
        if (!reply)
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        DBusMessageIter reply_args;
        dbus_message_iter_init_append(reply, &reply_args);

        if (strcmp(interface, "org.mpris.MediaPlayer2.Player") == 0)
        {
            if (strcmp(prop, "PlaybackStatus") == 0)
            {
                PlayState st = atomic_load(&play_state_atomic);
                const char *status = (st == STATE_PLAYING) ? "Playing" : (st == STATE_PAUSED ? "Paused" : "Stopped");
                append_variant_string(&reply_args, status);
            }
            else if (strcmp(prop, "Metadata") == 0)
            {
                append_metadata_variant(&reply_args);
            }
            else if (strcmp(prop, "Volume") == 0)
            {
                double vol = (double)atomic_load(&volume) / 100.0;
                append_variant_double(&reply_args, vol);
            }
            else if (strcmp(prop, "Rate") == 0 || strcmp(prop, "MinimumRate") == 0 || strcmp(prop, "MaximumRate") == 0)
            {
                append_variant_double(&reply_args, 1.0);
            }
            else if (strcmp(prop, "Position") == 0)
            {
                uint32_t srate = atomic_load(&vis_srate);
                if (srate == 0) srate = 44100;
                int64_t pos_us = ((int64_t)atomic_load(&p_frames_consumed) * 1000000LL) / srate;
                append_variant_int64(&reply_args, pos_us);
            }
            else if (strcmp(prop, "LoopStatus") == 0)
            {
                int r_mode = atomic_load(&play_mode_repeat);
                const char *ls = (r_mode == 2) ? "Track" : ((r_mode == 1) ? "Playlist" : "None");
                append_variant_string(&reply_args, ls);
            }
            else if (strcmp(prop, "Shuffle") == 0)
            {
                DBusMessageIter variant;
                dbus_message_iter_open_container(&reply_args, DBUS_TYPE_VARIANT, "b", &variant);
                dbus_bool_t val = atomic_load(&play_mode_shuffle) ? TRUE : FALSE;
                dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &val);
                dbus_message_iter_close_container(&reply_args, &variant);
            }
            else if (strcmp(prop, "CanControl") == 0 || strcmp(prop, "CanPlay") == 0 ||
                     strcmp(prop, "CanPause") == 0 || strcmp(prop, "CanGoNext") == 0 ||
                     strcmp(prop, "CanGoPrevious") == 0 || strcmp(prop, "CanSeek") == 0)
            {
                DBusMessageIter variant;
                dbus_message_iter_open_container(&reply_args, DBUS_TYPE_VARIANT, "b", &variant);
                dbus_bool_t val = TRUE;
                dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &val);
                dbus_message_iter_close_container(&reply_args, &variant);
            }
            else
            {
                dbus_message_unref(reply);
                return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
            }
        }
        else if (strcmp(interface, "org.mpris.MediaPlayer2") == 0)
        {
            if (strcmp(prop, "Identity") == 0)
            {
                append_variant_string(&reply_args, "Koni");
            }
            else if (strcmp(prop, "CanQuit") == 0 || strcmp(prop, "CanSetFullscreen") == 0 || 
                     strcmp(prop, "CanRaise") == 0 || strcmp(prop, "HasTrackList") == 0)
            {
                DBusMessageIter variant;
                dbus_message_iter_open_container(&reply_args, DBUS_TYPE_VARIANT, "b", &variant);
                dbus_bool_t val = FALSE;
                dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &val);
                dbus_message_iter_close_container(&reply_args, &variant);
            }
            else if (strcmp(prop, "SupportedUriSchemes") == 0)
            {
                DBusMessageIter variant, as_array;
                dbus_message_iter_open_container(&reply_args, DBUS_TYPE_VARIANT, "as", &variant);
                dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "s", &as_array);
                const char* scheme = "file";
                dbus_message_iter_append_basic(&as_array, DBUS_TYPE_STRING, &scheme);
                dbus_message_iter_close_container(&variant, &as_array);
                dbus_message_iter_close_container(&reply_args, &variant);
            }
            else if (strcmp(prop, "SupportedMimeTypes") == 0)
            {
                DBusMessageIter variant, as_array;
                dbus_message_iter_open_container(&reply_args, DBUS_TYPE_VARIANT, "as", &variant);
                dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "s", &as_array);
                dbus_message_iter_close_container(&variant, &as_array);
                dbus_message_iter_close_container(&reply_args, &variant);
            }
            else
            {
                dbus_message_unref(reply);
                return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
            }
        }
        else
        {
            dbus_message_unref(reply);
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        }

        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    else if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "GetAll"))
    {
        DBusMessageIter args;
        if (!dbus_message_iter_init(msg, &args))
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

        const char *interface;
        dbus_message_iter_get_basic(&args, &interface);

        DBusMessage *reply = dbus_message_new_method_return(msg);
        if (!reply)
            return DBUS_HANDLER_RESULT_NEED_MEMORY;
        DBusMessageIter reply_args, array, dict_entry;
        dbus_message_iter_init_append(reply, &reply_args);
        dbus_message_iter_open_container(&reply_args, DBUS_TYPE_ARRAY, "{sv}", &array);

        if (strcmp(interface, "org.mpris.MediaPlayer2.Player") == 0)
        {
            // PlaybackStatus
            dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
            const char *ps_key = "PlaybackStatus";
            dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &ps_key);
            PlayState st = atomic_load(&play_state_atomic);
            const char *status = (st == STATE_PLAYING) ? "Playing" : (st == STATE_PAUSED ? "Paused" : "Stopped");
            append_variant_string(&dict_entry, status);
            dbus_message_iter_close_container(&array, &dict_entry);

            // Metadata
            dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
            const char *meta_key = "Metadata";
            dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &meta_key);
            append_metadata_variant(&dict_entry);
            dbus_message_iter_close_container(&array, &dict_entry);

            // Volume
            dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
            const char *vol_key = "Volume";
            dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &vol_key);
            DBusMessageIter variant;
            dbus_message_iter_open_container(&dict_entry, DBUS_TYPE_VARIANT, "d", &variant);
            double vol = (double)atomic_load(&volume) / 100.0;
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_DOUBLE, &vol);
            dbus_message_iter_close_container(&dict_entry, &variant);
            dbus_message_iter_close_container(&array, &dict_entry);

            // Rate properties
            const char *rate_props[] = {"Rate", "MinimumRate", "MaximumRate"};
            for (int i = 0; i < 3; i++) {
                dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
                dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &rate_props[i]);
                dbus_message_iter_open_container(&dict_entry, DBUS_TYPE_VARIANT, "d", &variant);
                double r_val = 1.0;
                dbus_message_iter_append_basic(&variant, DBUS_TYPE_DOUBLE, &r_val);
                dbus_message_iter_close_container(&dict_entry, &variant);
                dbus_message_iter_close_container(&array, &dict_entry);
            }

            // Capability Booleans
            const char *bool_props[] = {"CanControl", "CanPlay", "CanPause", "CanGoNext", "CanGoPrevious", "CanSeek"};
            for (int i = 0; i < 6; i++) {
                dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
                dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &bool_props[i]);
                dbus_message_iter_open_container(&dict_entry, DBUS_TYPE_VARIANT, "b", &variant);
                dbus_bool_t b_val = TRUE;
                dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &b_val);
                dbus_message_iter_close_container(&dict_entry, &variant);
                dbus_message_iter_close_container(&array, &dict_entry);
            }

            // LoopStatus
            dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
            const char *ls_key = "LoopStatus";
            dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &ls_key);
            int r_mode = atomic_load(&play_mode_repeat);
            const char *ls = (r_mode == 2) ? "Track" : ((r_mode == 1) ? "Playlist" : "None");
            append_variant_string(&dict_entry, ls);
            dbus_message_iter_close_container(&array, &dict_entry);

            // Shuffle
            dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
            const char *shuf_key = "Shuffle";
            dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &shuf_key);
            dbus_message_iter_open_container(&dict_entry, DBUS_TYPE_VARIANT, "b", &variant);
            dbus_bool_t shuf_val = atomic_load(&play_mode_shuffle) ? TRUE : FALSE;
            dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &shuf_val);
            dbus_message_iter_close_container(&dict_entry, &variant);
            dbus_message_iter_close_container(&array, &dict_entry);
        }
        else if (strcmp(interface, "org.mpris.MediaPlayer2") == 0)
        {
            // Identity
            dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
            const char *id_key = "Identity";
            dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &id_key);
            append_variant_string(&dict_entry, "Koni");
            dbus_message_iter_close_container(&array, &dict_entry);
            
            // Required root capabilities
            const char *root_bools[] = {"CanQuit", "CanSetFullscreen", "CanRaise", "HasTrackList"};
            for (int i = 0; i < 4; i++) {
                dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
                dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &root_bools[i]);
                DBusMessageIter variant;
                dbus_message_iter_open_container(&dict_entry, DBUS_TYPE_VARIANT, "b", &variant);
                dbus_bool_t b_val = FALSE;
                dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &b_val);
                dbus_message_iter_close_container(&dict_entry, &variant);
                dbus_message_iter_close_container(&array, &dict_entry);
            }

            // SupportedUriSchemes
            dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
            const char *uri_key = "SupportedUriSchemes";
            dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &uri_key);
            {
                DBusMessageIter variant, as_array;
                dbus_message_iter_open_container(&dict_entry, DBUS_TYPE_VARIANT, "as", &variant);
                dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "s", &as_array);
                const char* scheme = "file";
                dbus_message_iter_append_basic(&as_array, DBUS_TYPE_STRING, &scheme);
                dbus_message_iter_close_container(&variant, &as_array);
                dbus_message_iter_close_container(&dict_entry, &variant);
            }
            dbus_message_iter_close_container(&array, &dict_entry);

            // SupportedMimeTypes
            dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
            const char *mime_key = "SupportedMimeTypes";
            dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &mime_key);
            {
                DBusMessageIter variant, as_array;
                dbus_message_iter_open_container(&dict_entry, DBUS_TYPE_VARIANT, "as", &variant);
                dbus_message_iter_open_container(&variant, DBUS_TYPE_ARRAY, "s", &as_array);
                dbus_message_iter_close_container(&variant, &as_array);
                dbus_message_iter_close_container(&dict_entry, &variant);
            }
            dbus_message_iter_close_container(&array, &dict_entry);
        }

        dbus_message_iter_close_container(&reply_args, &array);
        dbus_connection_send(conn, reply, NULL);
        dbus_message_unref(reply);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    else if (dbus_message_is_method_call(msg, "org.freedesktop.DBus.Properties", "Set"))
    {
        DBusMessageIter args;
        if (!dbus_message_iter_init(msg, &args))
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

        const char *interface;
        dbus_message_iter_get_basic(&args, &interface);
        dbus_message_iter_next(&args);

        if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_STRING)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

        const char *prop;
        dbus_message_iter_get_basic(&args, &prop);
        dbus_message_iter_next(&args);

        if (dbus_message_iter_get_arg_type(&args) != DBUS_TYPE_VARIANT)
            return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
        DBusMessageIter variant;
        dbus_message_iter_recurse(&args, &variant);

        if (strcmp(interface, "org.mpris.MediaPlayer2.Player") == 0)
        {
            if (strcmp(prop, "LoopStatus") == 0 && dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRING)
            {
                const char *val;
                dbus_message_iter_get_basic(&variant, &val);
                if (strcmp(val, "None") == 0)
                    atomic_store(&play_mode_repeat, 0);
                else if (strcmp(val, "Playlist") == 0)
                    atomic_store(&play_mode_repeat, 1);
                else if (strcmp(val, "Track") == 0)
                    atomic_store(&play_mode_repeat, 2);
                force_redraw = true;
            }
            else if (strcmp(prop, "Shuffle") == 0 && dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_BOOLEAN)
            {
                dbus_bool_t val;
                dbus_message_iter_get_basic(&variant, &val);
                atomic_store(&play_mode_shuffle, val ? 1 : 0);
                force_redraw = true;
            }
            else if (strcmp(prop, "Volume") == 0 && dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_DOUBLE)
            {
                double val;
                dbus_message_iter_get_basic(&variant, &val);
                int v = (int)(val * 100.0);
                if (v < 0) v = 0;
                if (v > 200) v = 200;
                atomic_store(&volume, v);
                force_redraw = true;
            }
        }

        DBusMessage *reply = dbus_message_new_method_return(msg);
        if (reply)
        {
            dbus_connection_send(conn, reply, NULL);
            dbus_message_unref(reply);
        }
        return DBUS_HANDLER_RESULT_HANDLED;
    }

    if (dbus_message_get_type(msg) == DBUS_MESSAGE_TYPE_METHOD_CALL)
    {
        DBusMessage *reply = dbus_message_new_method_return(msg);
        if (reply)
        {
            dbus_connection_send(conn, reply, NULL);
            dbus_message_unref(reply);
        }
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}

static void *mpris_thread_func(void *arg)
{
    (void)arg;
    
    // Do not register on DBus until the user plays a song
    while (mpris_running && atomic_load(&current_track_id) == 0) {
        usleep(100000);
    }
    
    if (!mpris_running) return NULL;

    DBusError err;
    dbus_error_init(&err);

    dbus_conn = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err))
    {
        fprintf(stderr, "MPRIS DBus Connection Error: %s\n", err.message);
        dbus_error_free(&err);
        return NULL;
    }
    if (dbus_conn == NULL)
        return NULL;

    char bus_name[256];
    snprintf(bus_name, sizeof(bus_name), "org.mpris.MediaPlayer2.koni.instance%d", getpid());
    int ret = dbus_bus_request_name(dbus_conn, bus_name, DBUS_NAME_FLAG_REPLACE_EXISTING, &err);
    if (dbus_error_is_set(&err))
    {
        fprintf(stderr, "MPRIS DBus Name Error: %s\n", err.message);
        dbus_error_free(&err);
        return NULL;
    }

    if (ret != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER)
        return NULL;

    dbus_connection_add_filter(dbus_conn, mpris_handle_methods, NULL, NULL);

    PlayState last_state = STATE_STOPPED;
    int last_track = -1;
    int last_repeat = -1;
    int last_shuffle = -1;

    while (mpris_running)
    {
        // Dispatch commands, timeout serves as the rate limiter
        dbus_connection_read_write_dispatch(dbus_conn, 100);

        PlayState current_state = atomic_load(&play_state_atomic);
        int current_track = atomic_load(&current_track_id);
        int current_repeat = atomic_load(&play_mode_repeat);
        int current_shuffle = atomic_load(&play_mode_shuffle);

        // Check for sudden jumps to emit Seeked signal
        uint32_t srate = atomic_load(&vis_srate);
        if (srate == 0) srate = 44100;
        int64_t current_pos_us = ((int64_t)atomic_load(&p_frames_consumed) * 1000000LL) / srate;

        static int64_t last_pos_us = -1;
        if (last_pos_us != -1 && current_state == STATE_PLAYING && llabs(current_pos_us - last_pos_us) > 2000000LL) {
            DBusMessage *seek_sig = dbus_message_new_signal("/org/mpris/MediaPlayer2", "org.mpris.MediaPlayer2.Player", "Seeked");
            if (seek_sig) {
                dbus_message_append_args(seek_sig, DBUS_TYPE_INT64, &current_pos_us, DBUS_TYPE_INVALID);
                dbus_connection_send(dbus_conn, seek_sig, NULL);
                dbus_message_unref(seek_sig);
            }
        }
        last_pos_us = current_pos_us;

        // Detects changes internally to broadcast properties
        if (current_state != last_state || current_track != last_track ||
            current_repeat != last_repeat || current_shuffle != last_shuffle)
        {

            bool state_changed = (current_state != last_state || current_track != last_track);
            bool repeat_changed = (current_repeat != last_repeat);
            bool shuffle_changed = (current_shuffle != last_shuffle);

            last_state = current_state;
            last_track = current_track;
            last_repeat = current_repeat;
            last_shuffle = current_shuffle;

            DBusMessage *sig = dbus_message_new_signal(
                "/org/mpris/MediaPlayer2",
                "org.freedesktop.DBus.Properties",
                "PropertiesChanged");
            if (sig)
            {
                DBusMessageIter iter, array, dict_entry;
                dbus_message_iter_init_append(sig, &iter);
                const char *iface = "org.mpris.MediaPlayer2.Player";
                dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &iface);

                dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "{sv}", &array);

                if (state_changed)
                {
                    dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
                    const char *ps_key = "PlaybackStatus";
                    dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &ps_key);
                    const char *status = (current_state == STATE_PLAYING) ? "Playing" : (current_state == STATE_PAUSED ? "Paused" : "Stopped");
                    append_variant_string(&dict_entry, status);
                    dbus_message_iter_close_container(&array, &dict_entry);

                    dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
                    const char *meta_key = "Metadata";
                    dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &meta_key);
                    append_metadata_variant(&dict_entry);
                    dbus_message_iter_close_container(&array, &dict_entry);
                }

                if (repeat_changed)
                {
                    dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
                    const char *ls_key = "LoopStatus";
                    dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &ls_key);
                    const char *ls = (current_repeat == 2) ? "Track" : ((current_repeat == 1) ? "Playlist" : "None");
                    append_variant_string(&dict_entry, ls);
                    dbus_message_iter_close_container(&array, &dict_entry);
                }

                if (shuffle_changed)
                {
                    dbus_message_iter_open_container(&array, DBUS_TYPE_DICT_ENTRY, NULL, &dict_entry);
                    const char *sh_key = "Shuffle";
                    dbus_message_iter_append_basic(&dict_entry, DBUS_TYPE_STRING, &sh_key);
                    DBusMessageIter variant;
                    dbus_message_iter_open_container(&dict_entry, DBUS_TYPE_VARIANT, "b", &variant);
                    dbus_bool_t shuf_val = current_shuffle ? TRUE : FALSE;
                    dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &shuf_val);
                    dbus_message_iter_close_container(&dict_entry, &variant);
                    dbus_message_iter_close_container(&array, &dict_entry);
                }

                dbus_message_iter_close_container(&iter, &array);

                DBusMessageIter inv_array;
                dbus_message_iter_open_container(&iter, DBUS_TYPE_ARRAY, "s", &inv_array);
                dbus_message_iter_close_container(&iter, &inv_array);

                dbus_connection_send(dbus_conn, sig, NULL);
                dbus_message_unref(sig);
            }
        }
    }

    dbus_connection_remove_filter(dbus_conn, mpris_handle_methods, NULL);
    dbus_connection_unref(dbus_conn);
    return NULL;
}

void mpris_init(void)
{
    mpris_running = true;
    pthread_create(&mpris_thread, NULL, mpris_thread_func, NULL);
}

void mpris_shutdown(void)
{
    mpris_running = false;
    pthread_join(mpris_thread, NULL);
}