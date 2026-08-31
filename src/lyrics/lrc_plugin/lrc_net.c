#define _DEFAULT_SOURCE
#define _XOPEN_SOURCE 600

#include "lyrics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

struct MemoryStruct {
    char *memory;
    size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;
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
            if (*s == 'n') { *p++ = '\n'; s++; }
            else if (*s == 'r') { *p++ = '\r'; s++; }
            else if (*s == 't') { *p++ = '\t'; s++; }
            else if (*s == '"') { *p++ = '"'; s++; }
            else if (*s == '\\') { *p++ = '\\'; s++; }
            else if (*s == '/') { *p++ = '/'; s++; }
            else if (*s == 'u') {
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

static bool http_get(CURL *curl, const char *url, bool is_netease, struct MemoryStruct *chunk) {
    if (!curl || !url || !chunk) return false;
    chunk->memory = malloc(1);
    if (!chunk->memory) return false;
    chunk->memory[0] = '\0';
    chunk->size = 0;

    struct curl_slist *headers = NULL;
    if (is_netease) {
        headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:120.0) Gecko/20100101 Firefox/120.0");
        headers = curl_slist_append(headers, "Referer: https://music.163.com/");
        headers = curl_slist_append(headers, "Accept: application/json");
        curl_easy_setopt(curl, CURLOPT_COOKIELIST, "ALL");
    } else {
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
        if (chunk->memory) { free(chunk->memory); chunk->memory = NULL; }
        chunk->size = 0;
        return false;
    }
    return true;
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
            if (text && strlen(text) > 10) return text;
            if (text) free(text);
        }
        p = colon;
    }
    return NULL;
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
    if (text && strlen(text) > 5) return text;
    if (text) free(text);
    return NULL;
}

bool lrc_fetch_remote(const LyricFetchQuery *query, char **out_raw_data, char out_method_label[32]) {
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    char url[4096];
    struct MemoryStruct chunk;
    char *esc_title = query->title[0] ? curl_easy_escape(curl, query->title, 0) : NULL;
    char *esc_artist = query->artist[0] ? curl_easy_escape(curl, query->artist, 0) : NULL;
    char *esc_album = query->album[0] ? curl_easy_escape(curl, query->album, 0) : NULL;
    char *found_text = NULL;

    // LRCLIB Get
    if (esc_title && esc_artist && esc_title[0] && esc_artist[0]) {
        snprintf(url, sizeof(url), "https://lrclib.net/api/get?track_name=%s&artist_name=%s%s%s%s%u",
                 esc_title, esc_artist,
                 esc_album ? "&album_name=" : "", esc_album ? esc_album : "",
                 query->duration_sec > 0 ? "&duration=" : "", query->duration_sec);

        if (http_get(curl, url, false, &chunk)) {
            found_text = extract_lrclib_synced_lyrics(chunk.memory);
            free(chunk.memory);
            if (found_text) strncpy(out_method_label, "LRCLIB", 31);
        }

        if (!found_text) {
            snprintf(url, sizeof(url), "https://lrclib.net/api/search?track_name=%s&artist_name=%s", esc_title, esc_artist);
            if (http_get(curl, url, false, &chunk)) {
                found_text = extract_lrclib_synced_lyrics(chunk.memory);
                free(chunk.memory);
                if (found_text) strncpy(out_method_label, "LRCLIB", 31);
            }
        }
    }

    // LRCLIB Fuzzy
    if (!found_text && esc_title && esc_title[0]) {
        char fuzzy[1024];
        if (query->artist[0]) snprintf(fuzzy, sizeof(fuzzy), "%s %s", query->title, query->artist);
        else snprintf(fuzzy, sizeof(fuzzy), "%s", query->title);

        char *esc_q = curl_easy_escape(curl, fuzzy, 0);
        if (esc_q) {
            snprintf(url, sizeof(url), "https://lrclib.net/api/search?q=%s", esc_q);
            curl_free(esc_q);
            if (http_get(curl, url, false, &chunk)) {
                found_text = extract_lrclib_synced_lyrics(chunk.memory);
                free(chunk.memory);
                if (found_text) strncpy(out_method_label, "LRCLIB", 31);
            }
        }
    }

    // NetEase Fallback
    if (!found_text) {
        char q[1024];
        if (query->artist[0]) snprintf(q, sizeof(q), "%s %s", query->title, query->artist);
        else snprintf(q, sizeof(q), "%s", query->title);

        char *esc_q = curl_easy_escape(curl, q, 0);
        if (esc_q) {
            snprintf(url, sizeof(url), "https://music.163.com/api/search/get?s=%s&type=1&limit=5", esc_q);
            curl_free(esc_q);
            if (http_get(curl, url, true, &chunk)) {
                const char *id_p = strstr(chunk.memory, "\"id\":");
                if (id_p) {
                    long long song_id = atoll(id_p + 5);
                    if (song_id > 0) {
                        snprintf(url, sizeof(url), "https://music.163.com/api/song/lyric?id=%lld&lv=1&kv=1&tv=-1", song_id);
                        struct MemoryStruct lrc_chunk;
                        if (http_get(curl, url, true, &lrc_chunk)) {
                            found_text = extract_netease_lyrics(lrc_chunk.memory);
                            free(lrc_chunk.memory);
                            if (found_text) strncpy(out_method_label, "NetEase", 31);
                        }
                    }
                }
                free(chunk.memory);
            }
        }
    }

    if (esc_title) curl_free(esc_title);
    if (esc_artist) curl_free(esc_artist);
    if (esc_album) curl_free(esc_album);
    curl_easy_cleanup(curl);

    if (found_text) {
        *out_raw_data = found_text;
        return true;
    }
    return false;
}