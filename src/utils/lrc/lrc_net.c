#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600

#include "lrc_net.h"
#include "ui_common.h"
#include "config.h"
#include <pthread.h>
#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <strings.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <ctype.h>

typedef struct {
    char title[256];
    char artist[256];
    char album[256];
    char filepath[1024];
    uint32_t duration;
} LrcFetchArgs;

struct MemoryStruct {
    char *memory;
    size_t size;
};

typedef enum {
    BACKEND_LRCLIB,
    BACKEND_NETEASE
} LrcBackendType;

typedef struct {
    long long id;
    char name[256];
    char artists[512];
    uint32_t duration_ms;
} NetEaseCandidate;

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
    if (!mem) return 0;
    
    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) return 0;
    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;
    return realsize;
}

static char* unescape_json_string(const char* json_str) {
    if (!json_str) return NULL;
    size_t in_len = strlen(json_str);
    char* out = malloc(in_len * 2 + 8);
    if (!out) return NULL;
    char* p = out;
    const char* s = json_str;
    
    while (*s && *s != '"') {
        if (*s == '\\') {
            s++;
            if (!*s) break;
            if (*s == 'n') {
                *p++ = '\n';
                s++;
            } else if (*s == 'r') {
                *p++ = '\r';
                s++;
            } else if (*s == 't') {
                *p++ = '\t';
                s++;
            } else if (*s == '"') {
                *p++ = '"';
                s++;
            } else if (*s == '\\') {
                *p++ = '\\';
                s++;
            } else if (*s == '/') {
                *p++ = '/';
                s++;
            } else if (*s == 'u') {
                s++;
                unsigned int cp = 0;
                int hex_count = 0;
                while (hex_count < 4 && *s) {
                    char c = *s;
                    int h = -1;
                    if (c >= '0' && c <= '9') h = c - '0';
                    else if (c >= 'a' && c <= 'f') h = c - 'a' + 10;
                    else if (c >= 'A' && c <= 'F') h = c - 'A' + 10;
                    if (h < 0) break;
                    cp = (cp << 4) | (unsigned int)h;
                    s++;
                    hex_count++;
                }
                if (hex_count == 4) {
                    // Handle UTF-16 surrogate pairs safely with bounds checking
                    if (cp >= 0xD800 && cp <= 0xDBFF && s[0] == '\\' && s[1] == 'u') {
                        const char* s2 = s + 2;
                        unsigned int cp2 = 0;
                        int hex_count2 = 0;
                        while (hex_count2 < 4 && *s2) {
                            char c = *s2;
                            int h = -1;
                            if (c >= '0' && c <= '9') h = c - '0';
                            else if (c >= 'a' && c <= 'f') h = c - 'a' + 10;
                            else if (c >= 'A' && c <= 'F') h = c - 'A' + 10;
                            if (h < 0) break;
                            cp2 = (cp2 << 4) | (unsigned int)h;
                            s2++;
                            hex_count2++;
                        }
                        if (hex_count2 == 4 && cp2 >= 0xDC00 && cp2 <= 0xDFFF) {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (cp2 - 0xDC00);
                            s = s2;
                        }
                    }
                    
                    // Encode codepoint to UTF-8
                    if (cp <= 0x7F) {
                        *p++ = (char)cp;
                    } else if (cp <= 0x7FF) {
                        *p++ = (char)(0xC0 | (cp >> 6));
                        *p++ = (char)(0x80 | (cp & 0x3F));
                    } else if (cp <= 0xFFFF) {
                        *p++ = (char)(0xE0 | (cp >> 12));
                        *p++ = (char)(0x80 | ((cp >> 6) & 0x3F));
                        *p++ = (char)(0x80 | (cp & 0x3F));
                    } else if (cp <= 0x10FFFF) {
                        *p++ = (char)(0xF0 | (cp >> 18));
                        *p++ = (char)(0x80 | ((cp >> 12) & 0x3F));
                        *p++ = (char)(0x80 | ((cp >> 6) & 0x3F));
                        *p++ = (char)(0x80 | (cp & 0x3F));
                    }
                }
            } else {
                *p++ = *s++;
            }
        } else {
            *p++ = *s++;
        }
    }
    *p = '\0';
    return out;
}

static bool contains_ci(const char *haystack, const char *needle) {
    if (!haystack || !needle) return false;
    size_t h_len = strlen(haystack);
    size_t n_len = strlen(needle);
    if (n_len == 0) return true;
    if (h_len < n_len) return false;
    
    for (size_t i = 0; i <= h_len - n_len; i++) {
        if (strncasecmp(haystack + i, needle, n_len) == 0) return true;
    }
    return false;
}

static void clean_title_for_search(const char *in, char *out, size_t out_size) {
    if (!in || !out || out_size == 0) return;
    strncpy(out, in, out_size - 1);
    out[out_size - 1] = '\0';
    
    // Strip audio extension if present
    char *dot = strrchr(out, '.');
    if (dot && (strcasecmp(dot, ".mp3") == 0 || strcasecmp(dot, ".flac") == 0 || 
                strcasecmp(dot, ".wav") == 0 || strcasecmp(dot, ".dana") == 0 || 
                strcasecmp(dot, ".dahl") == 0 || strcasecmp(dot, ".m4a") == 0 || 
                strcasecmp(dot, ".ogg") == 0)) {
        *dot = '\0';
    }
    
    // Strip parenthesized descriptions like (feat. ...), [Official Audio], etc.
    char *p = out;
    while (*p) {
        if (*p == '(' || *p == '[') {
            char *closing = strchr(p + 1, *p == '(' ? ')' : ']');
            if (closing) {
                char sub[256] = {0};
                size_t sub_len = (size_t)(closing - p - 1);
                if (sub_len < sizeof(sub)) {
                    strncpy(sub, p + 1, sub_len);
                    if (strcasestr(sub, "feat") || strcasestr(sub, "ft.") || 
                        strcasestr(sub, "official") || strcasestr(sub, "remaster") || 
                        strcasestr(sub, "audio") || strcasestr(sub, "video") ||
                        strcasestr(sub, "bonus") || strcasestr(sub, "prod") ||
                        strcasestr(sub, "version") || strcasestr(sub, "live")) {
                        memmove(p, closing + 1, strlen(closing + 1) + 1);
                        continue;
                    }
                }
            }
        }
        p++;
    }
    
    int len = (int)strlen(out);
    while (len > 0 && (out[len - 1] == ' ' || out[len - 1] == '\t' || out[len - 1] == '-' || out[len - 1] == '_')) {
        out[--len] = '\0';
    }
}

static int parse_netease_candidates(const char *json, NetEaseCandidate *candidates, int max_candidates) {
    if (!json) return 0;
    const char *p = strstr(json, "\"songs\":");
    if (!p) p = strstr(json, "\"songs\" :");
    if (!p) return 0;
    
    p = strchr(p, '[');
    if (!p) return 0;
    p++;
    
    int count = 0;
    while (*p && count < max_candidates) {
        while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',')) p++;
        if (*p == ']' || *p == '\0') break;
        if (*p != '{') { p++; continue; }
        
        const char *obj_start = p;
        int depth = 0;
        bool in_str = false;
        const char *obj_end = NULL;
        
        for (const char *c = obj_start; *c; c++) {
            if (*c == '"' && (c == obj_start || *(c - 1) != '\\')) {
                in_str = !in_str;
            } else if (!in_str) {
                if (*c == '{') depth++;
                else if (*c == '}') {
                    depth--;
                    if (depth == 0) {
                        obj_end = c;
                        break;
                    }
                }
            }
        }
        
        if (!obj_end) break;
        
        size_t obj_len = (size_t)(obj_end - obj_start + 1);
        char *song_obj = malloc(obj_len + 1);
        if (!song_obj) break;
        memcpy(song_obj, obj_start, obj_len);
        song_obj[obj_len] = '\0';
        
        candidates[count].id = -1;
        candidates[count].name[0] = '\0';
        candidates[count].artists[0] = '\0';
        candidates[count].duration_ms = 0;
        
        // Extract Song ID
        char *id_ptr = strstr(song_obj, "\"id\":");
        if (!id_ptr) id_ptr = strstr(song_obj, "\"id\" :");
        if (id_ptr) {
            id_ptr = strchr(id_ptr, ':');
            if (id_ptr) {
                id_ptr++;
                while (*id_ptr == ' ' || *id_ptr == '\t') id_ptr++;
                candidates[count].id = atoll(id_ptr);
            }
        }
        
        // Extract Song Name
        char *name_ptr = strstr(song_obj, "\"name\":");
        if (!name_ptr) name_ptr = strstr(song_obj, "\"name\" :");
        if (name_ptr) {
            name_ptr = strchr(name_ptr, ':');
            if (name_ptr) {
                name_ptr = strchr(name_ptr, '"');
                if (name_ptr) {
                    name_ptr++;
                    char *unesc = unescape_json_string(name_ptr);
                    if (unesc) {
                        strncpy(candidates[count].name, unesc, sizeof(candidates[count].name) - 1);
                        free(unesc);
                    }
                }
            }
        }
        
        // Extract Artists
        char *artists_arr = strstr(song_obj, "\"artists\":");
        if (!artists_arr) artists_arr = strstr(song_obj, "\"artists\" :");
        if (artists_arr) {
            artists_arr = strchr(artists_arr, '[');
            char *artists_end = artists_arr ? strchr(artists_arr, ']') : NULL;
            if (artists_arr && artists_end) {
                *artists_end = '\0';
                char *art_p = artists_arr;
                while (art_p) {
                    char *found1 = strstr(art_p, "\"name\":");
                    char *found2 = strstr(art_p, "\"name\" :");
                    if (!found1 && !found2) break;
                    if (found1 && found2) art_p = (found1 < found2) ? found1 : found2;
                    else art_p = found1 ? found1 : found2;

                    art_p = strchr(art_p, ':');
                    if (!art_p) break;
                    art_p = strchr(art_p, '"');
                    if (!art_p) break;
                    art_p++;
                    char *unesc = unescape_json_string(art_p);
                    if (unesc) {
                        if (candidates[count].artists[0] != '\0') {
                            strncat(candidates[count].artists, ", ", sizeof(candidates[count].artists) - strlen(candidates[count].artists) - 1);
                        }
                        strncat(candidates[count].artists, unesc, sizeof(candidates[count].artists) - strlen(candidates[count].artists) - 1);
                        free(unesc);
                    }
                    art_p = strchr(art_p, '"');
                    if (!art_p) break;
                    art_p++;
                }
            }
        }
        
        free(song_obj);
        
        if (candidates[count].id > 0) {
            count++;
        }
        
        p = obj_end + 1;
    }
    
    return count;
}

static long long select_best_netease_song(const NetEaseCandidate *candidates, int count, const char *target_artist) {
    if (count <= 0) return -1;
    
    if (!target_artist || target_artist[0] == '\0') {
        return candidates[0].id;
    }
    
    for (int i = 0; i < count; i++) {
        if (contains_ci(candidates[i].artists, target_artist) || contains_ci(target_artist, candidates[i].artists)) {
            return candidates[i].id;
        }
    }
    
    // Fallback to top-ranked candidate from NetEase
    return candidates[0].id;
}

static char* extract_netease_lyrics(const char *json) {
    if (!json) return NULL;
    const char *lrc = strstr(json, "\"lrc\":");
    if (!lrc) lrc = strstr(json, "\"lrc\" :");
    if (!lrc) return NULL;
    
    const char *lyric = strstr(lrc, "\"lyric\":");
    if (!lyric) lyric = strstr(lrc, "\"lyric\" :");
    if (!lyric) return NULL;
    
    const char *quote = strchr(lyric, ':');
    if (!quote) return NULL;
    quote = strchr(quote, '"');
    if (!quote) return NULL;
    quote++;
    
    char *text = unescape_json_string(quote);
    if (text && strlen(text) > 5) {
        return text;
    }
    if (text) free(text);
    return NULL;
}

static char* extract_lrclib_synced_lyrics(const char *json) {
    if (!json) return NULL;
    const char *p = json;
    
    while (p) {
        const char *p1 = strstr(p, "\"syncedLyrics\":");
        const char *p2 = strstr(p, "\"syncedLyrics\" :");
        const char *match = NULL;
        if (p1 && p2) match = (p1 < p2) ? p1 : p2;
        else match = p1 ? p1 : p2;
        
        if (!match) break;
        
        const char *colon = strchr(match, ':');
        if (!colon) break;
        colon++;
        while (*colon == ' ' || *colon == '\t') colon++;
        
        if (*colon == '"') {
            colon++;
            char *text = unescape_json_string(colon);
            if (text && strlen(text) > 10) {
                return text;
            }
            if (text) free(text);
        }
        p = colon;
    }
    return NULL;
}

static void apply_lyrics_doc(char* lyrics_text, const char* backend_name) {
    if (!lyrics_text) return;
    
    LrcDocument *doc = lrc_parse(lyrics_text);
    pthread_mutex_lock(&state_mutex);
    if (ui_cache.lrc_doc) lrc_free(ui_cache.lrc_doc);
    ui_cache.lrc_doc = doc;
    strncpy(current_lyrics_backend, backend_name, sizeof(current_lyrics_backend) - 1);
    force_redraw = true;
    pthread_mutex_unlock(&state_mutex);
    
    free(lyrics_text);
}

static char* try_read_local_file(const char* path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return NULL; }
    
    char *buf = malloc(size + 1);
    if (fread(buf, 1, size, f) != (size_t)size) { free(buf); fclose(f); return NULL; }
    buf[size] = '\0';
    fclose(f);
    return buf;
}

static void ensure_dir_exists(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    char *p = NULL;
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static void save_lyrics_file(const char* filepath, const char* artist, const char* title, const char* text) {
    (void)filepath;
    if (!app_config.download_online_lyrics) return;

    char safe_name[512];
    if (strlen(artist) > 0) snprintf(safe_name, sizeof(safe_name), "%s - %s", artist, title);
    else snprintf(safe_name, sizeof(safe_name), "%s", title);
    
    for (int i = 0; safe_name[i] != '\0'; i++) {
        if (safe_name[i] == '/' || safe_name[i] == '\\' || safe_name[i] == ':') safe_name[i] = '_';
    }

    char local_path[1024];

    ensure_dir_exists(app_config.lyrics_custom_path);
    snprintf(local_path, sizeof(local_path), "%s/%s.lrc", app_config.lyrics_custom_path, safe_name);
    FILE *f = fopen(local_path, "w");
    if (f) {
        fputs(text, f);
        fclose(f);
        return;
    }

    const char *home = getenv("HOME");
    if (home) {
        char fallback_dir[1024];
        snprintf(fallback_dir, sizeof(fallback_dir), "%s/.config/koni/lyrics", home);
        ensure_dir_exists(fallback_dir);

        snprintf(local_path, sizeof(local_path), "%s/%s.lrc", fallback_dir, safe_name);
        f = fopen(local_path, "w");
        if (f) {
            fputs(text, f);
            fclose(f);
        }
    }
}

static bool http_get(CURL *curl, const char *url, LrcBackendType backend, struct MemoryStruct *chunk) {
    if (!curl || !url || !chunk) return false;
    
    chunk->memory = malloc(1);
    if (!chunk->memory) return false;
    chunk->memory[0] = '\0';
    chunk->size = 0;

    struct curl_slist *headers = NULL;
    
    if (backend == BACKEND_NETEASE) {
        headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:120.0) Gecko/20100101 Firefox/120.0");
        headers = curl_slist_append(headers, "Referer: https://music.163.com/");
        headers = curl_slist_append(headers, "Accept: application/json");
        curl_easy_setopt(curl, CURLOPT_COOKIELIST, "ALL");
    } else if (backend == BACKEND_LRCLIB) {
        headers = curl_slist_append(headers, "User-Agent: koni-player (https://github.com/holotwist/koni)");
        headers = curl_slist_append(headers, "Accept: application/json");
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)chunk);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 6L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 4L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, ""); 
    
    CURLcode res = curl_easy_perform(curl);
    if (headers) curl_slist_free_all(headers);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (res != CURLE_OK || http_code < 200 || http_code >= 300 || !chunk->memory || chunk->size == 0) {
        if (chunk->memory) {
            free(chunk->memory);
            chunk->memory = NULL;
        }
        chunk->size = 0;
        return false;
    }
    return true;
}

static void* lrc_fetch_thread(void* arg) {
    LrcFetchArgs *args = (LrcFetchArgs*)arg;
    char local_path[1024];
    char *lyrics_text = NULL;

    // Try local files first
    snprintf(local_path, sizeof(local_path), "%s", args->filepath);
    char *ext = strrchr(local_path, '.');
    if (ext) strcpy(ext, ".lrc");
    else strcat(local_path, ".lrc");
    
    lyrics_text = try_read_local_file(local_path);
    
    char safe_name[512];
    if (strlen(args->artist) > 0) snprintf(safe_name, sizeof(safe_name), "%s - %s", args->artist, args->title);
    else snprintf(safe_name, sizeof(safe_name), "%s", args->title);

    for (int i = 0; safe_name[i] != '\0'; i++) {
        if (safe_name[i] == '/' || safe_name[i] == '\\' || safe_name[i] == ':') safe_name[i] = '_';
    }

    if (!lyrics_text) {
        snprintf(local_path, sizeof(local_path), "%s/%s.lrc", app_config.lyrics_custom_path, safe_name);
        lyrics_text = try_read_local_file(local_path);
    }
    
    if (!lyrics_text) {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(local_path, sizeof(local_path), "%s/.config/koni/lyrics/%s.lrc", home, safe_name);
            lyrics_text = try_read_local_file(local_path);
        }
    }

    if (lyrics_text) {
        apply_lyrics_doc(lyrics_text, "Local");
        free(args);
        return NULL;
    }

    if (!app_config.online_lyrics) {
        pthread_mutex_lock(&state_mutex);
        strncpy(current_lyrics_backend, "Not Found", sizeof(current_lyrics_backend) - 1);
        force_redraw = true;
        pthread_mutex_unlock(&state_mutex);
        free(args);
        return NULL;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        free(args);
        return NULL;
    }

    bool found = false;
    char url[4096];
    struct MemoryStruct chunk;
    char *esc_title = args->title[0] ? curl_easy_escape(curl, args->title, 0) : NULL;
    char *esc_artist = args->artist[0] ? curl_easy_escape(curl, args->artist, 0) : NULL;
    char *esc_album = args->album[0] ? curl_easy_escape(curl, args->album, 0) : NULL;

    // LRCLIB Search
    if (esc_title && esc_artist && esc_title[0] && esc_artist[0]) {
        char query_params[4096] = "";
        snprintf(query_params, sizeof(query_params), "track_name=%s&artist_name=%s", esc_title, esc_artist);
        if (esc_album && esc_album[0]) {
            snprintf(query_params + strlen(query_params), sizeof(query_params) - strlen(query_params), "&album_name=%s", esc_album);
        }
        if (args->duration > 0) {
            snprintf(query_params + strlen(query_params), sizeof(query_params) - strlen(query_params), "&duration=%u", args->duration);
        }

        // Strict /api/get
        snprintf(url, sizeof(url), "https://lrclib.net/api/get?%s", query_params);
        if (http_get(curl, url, BACKEND_LRCLIB, &chunk)) {
            lyrics_text = extract_lrclib_synced_lyrics(chunk.memory);
            if (lyrics_text) {
                save_lyrics_file(args->filepath, args->artist, args->title, lyrics_text);
                apply_lyrics_doc(lyrics_text, "LRCLIB");
                found = true;
            }
            free(chunk.memory);
        }

        // /api/search by track & artist fields
        if (!found) {
            snprintf(url, sizeof(url), "https://lrclib.net/api/search?track_name=%s&artist_name=%s", esc_title, esc_artist);
            if (http_get(curl, url, BACKEND_LRCLIB, &chunk)) {
                lyrics_text = extract_lrclib_synced_lyrics(chunk.memory);
                if (lyrics_text) {
                    save_lyrics_file(args->filepath, args->artist, args->title, lyrics_text);
                    apply_lyrics_doc(lyrics_text, "LRCLIB");
                    found = true;
                }
                free(chunk.memory);
            }
        }
    }

    // LRCLIB Fuzzy query search
    if (!found && esc_title && esc_title[0]) {
        char fuzzy_query[1024];
        if (args->artist[0]) snprintf(fuzzy_query, sizeof(fuzzy_query), "%s %s", args->title, args->artist);
        else snprintf(fuzzy_query, sizeof(fuzzy_query), "%s", args->title);
        
        char *esc_q = curl_easy_escape(curl, fuzzy_query, 0);
        if (esc_q) {
            snprintf(url, sizeof(url), "https://lrclib.net/api/search?q=%s", esc_q);
            curl_free(esc_q);
            if (http_get(curl, url, BACKEND_LRCLIB, &chunk)) {
                lyrics_text = extract_lrclib_synced_lyrics(chunk.memory);
                if (lyrics_text) {
                    save_lyrics_file(args->filepath, args->artist, args->title, lyrics_text);
                    apply_lyrics_doc(lyrics_text, "LRCLIB");
                    found = true;
                }
                free(chunk.memory);
            }
        }
    }

    // NetEase Search Fallback
    if (!found) {
        char query[1024];
        if (args->artist[0]) snprintf(query, sizeof(query), "%s %s", args->title, args->artist);
        else snprintf(query, sizeof(query), "%s", args->title);

        char *esc_query = curl_easy_escape(curl, query, 0);
        snprintf(url, sizeof(url), "https://music.163.com/api/search/get?s=%s&type=1&limit=10", esc_query);
        curl_free(esc_query);

        if (http_get(curl, url, BACKEND_NETEASE, &chunk)) {
            NetEaseCandidate candidates[10];
            int count = parse_netease_candidates(chunk.memory, candidates, 10);
            free(chunk.memory);

            long long song_id = select_best_netease_song(candidates, count, args->artist);
            if (song_id > 0) {
                snprintf(url, sizeof(url), "https://music.163.com/api/song/lyric?id=%lld&lv=1&kv=1&tv=-1", song_id);
                if (http_get(curl, url, BACKEND_NETEASE, &chunk)) {
                    lyrics_text = extract_netease_lyrics(chunk.memory);
                    if (lyrics_text) {
                        save_lyrics_file(args->filepath, args->artist, args->title, lyrics_text);
                        apply_lyrics_doc(lyrics_text, "NetEase");
                        found = true;
                    }
                    free(chunk.memory);
                }
            }
        }
    }

    // NetEase Cleaned Title Query Fallback
    if (!found) {
        char clean_title[256];
        clean_title_for_search(args->title, clean_title, sizeof(clean_title));
        
        if (strcmp(clean_title, args->title) != 0 && clean_title[0] != '\0') {
            char query[1024];
            if (args->artist[0]) snprintf(query, sizeof(query), "%s %s", clean_title, args->artist);
            else snprintf(query, sizeof(query), "%s", clean_title);

            char *esc_query = curl_easy_escape(curl, query, 0);
            snprintf(url, sizeof(url), "https://music.163.com/api/search/get?s=%s&type=1&limit=10", esc_query);
            curl_free(esc_query);

            if (http_get(curl, url, BACKEND_NETEASE, &chunk)) {
                NetEaseCandidate candidates[10];
                int count = parse_netease_candidates(chunk.memory, candidates, 10);
                free(chunk.memory);

                long long song_id = select_best_netease_song(candidates, count, args->artist);
                if (song_id > 0) {
                    snprintf(url, sizeof(url), "https://music.163.com/api/song/lyric?id=%lld&lv=1&kv=1&tv=-1", song_id);
                    if (http_get(curl, url, BACKEND_NETEASE, &chunk)) {
                        lyrics_text = extract_netease_lyrics(chunk.memory);
                        if (lyrics_text) {
                            save_lyrics_file(args->filepath, args->artist, args->title, lyrics_text);
                            apply_lyrics_doc(lyrics_text, "NetEase");
                            found = true;
                        }
                        free(chunk.memory);
                    }
                }
            }
        }
    }

    if (!found) {
        pthread_mutex_lock(&state_mutex);
        strncpy(current_lyrics_backend, "Not Found", sizeof(current_lyrics_backend) - 1);
        force_redraw = true;
        pthread_mutex_unlock(&state_mutex);
    }

    if (esc_title) curl_free(esc_title);
    if (esc_artist) curl_free(esc_artist);
    if (esc_album) curl_free(esc_album);
    curl_easy_cleanup(curl);
    
    free(args);
    return NULL;
}

void lrc_fetch_async(const char* title, const char* artist, const char* album, uint32_t duration, const char* filepath) {
    if (!filepath) return;
    
    LrcFetchArgs *args = malloc(sizeof(LrcFetchArgs));
    memset(args, 0, sizeof(LrcFetchArgs));
    strncpy(args->filepath, filepath, sizeof(args->filepath) - 1);
    
    if (title) strncpy(args->title, title, sizeof(args->title) - 1);
    if (artist) strncpy(args->artist, artist, sizeof(args->artist) - 1);
    if (album) strncpy(args->album, album, sizeof(args->album) - 1);
    args->duration = duration;
    
    if (strlen(args->title) == 0 && strlen(args->artist) == 0) {
        const char *slash = strrchr(filepath, '/');
        const char *fname = slash ? slash + 1 : filepath;
        strncpy(args->title, fname, sizeof(args->title) - 1);
        char *dot = strrchr(args->title, '.');
        if (dot) *dot = '\0';
    }
    
    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&t, &attr, lrc_fetch_thread, args);
    pthread_attr_destroy(&attr);
}