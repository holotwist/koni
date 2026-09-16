#include "sparkles_widgets.h"
#include "state.h"
#include "codec.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#include "third_party/stb_image.h"

static Texture2D s_cover_texture = {0};
static char s_cached_filepath[1024] = {0};
static int s_cached_track_id = -1;

// Texture loader (reads a photo data, gets RAW and loads as texture)
static Texture2D load_texture_robust(const char *filepath) {
    Texture2D tex = {0};
    if (!filepath || !FileExists(filepath)) return tex;

    int w = 0, h = 0, channels = 0;
    // Force 4 channels (RGBA)
    unsigned char *pixels = stbi_load(filepath, &w, &h, &channels, 4);
    if (!pixels) return tex;

    Image img = {
        .data = pixels,
        .width = w,
        .height = h,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
    };

    tex = LoadTextureFromImage(img);
    stbi_image_free(pixels);
    return tex;
}

static bool try_load_folder_cover(const char *audio_path, Texture2D *out_tex) {
    if (!audio_path || !audio_path[0]) return false;
    char dir[1024];
    strncpy(dir, audio_path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';
    char *last_slash = strrchr(dir, '/');
    if (!last_slash) return false;
    *last_slash = '\0';

    static const char *names[] = {
        "cover.jpg", "cover.png", "cover.jpeg",
        "folder.jpg", "folder.png", "folder.jpeg",
        "front.jpg", "front.png", "front.jpeg",
        "album.jpg", "album.png", NULL
    };

    for (int i = 0; names[i] != NULL; i++) {
        char test_path[1200];
        snprintf(test_path, sizeof(test_path), "%s/%s", dir, names[i]);
        if (FileExists(test_path)) {
            *out_tex = load_texture_robust(test_path);
            if (out_tex->id != 0) return true;
        }
    }
    return false;
}

static bool try_extract_embedded_cover(const char *audio_path, Texture2D *out_tex) {
    if (!audio_path || !audio_path[0]) return false;
    const KoniCodecImpl *codec = koni_find_codec_by_ext(audio_path);
    if (codec && codec->read_metadata) {
        KoniMetadata temp_meta = {0};
        uint32_t dur = 0;
        if (codec->read_metadata(audio_path, &temp_meta, &dur)) {
            if (temp_meta.art_url && strncmp(temp_meta.art_url, "file://", 7) == 0) {
                const char *path = temp_meta.art_url + 7;
                *out_tex = load_texture_robust(path);
            }
            koni_metadata_free(&temp_meta);
            if (out_tex->id != 0) return true;
        }
    }
    return false;
}

void tile_album_art_render(SparklesTile *tile, Rectangle b) {
    (void)tile;
    Rectangle art_box = { b.x + 8, b.y + 8, b.width - 16, b.height - 16 };
    DrawRectangleRec(art_box, (Color){ 10, 10, 14, 255 });
    DrawNothingCornerBrackets(art_box, 8.0f, (Color){ 50, 54, 66, 180 });

    pthread_mutex_lock(&state_mutex);
    char current_path[1024] = {0};
    strncpy(current_path, playing_filepath, sizeof(current_path) - 1);
    const char *art_url = p_metadata.art_url;
    const char *song_title = (p_metadata.title && p_metadata.title[0]) ? p_metadata.title : (playing_filename[0] ? playing_filename : "No Track");
    const char *artist = (p_metadata.artist && p_metadata.artist[0]) ? p_metadata.artist : "Unknown Artist";
    pthread_mutex_unlock(&state_mutex);

    int cur_track_id = atomic_load(&current_track_id);
    bool path_changed = (current_path[0] != '\0' && strcmp(s_cached_filepath, current_path) != 0);
    bool track_id_changed = (cur_track_id > 0 && cur_track_id != s_cached_track_id);

    // Reload when path changes OR when the audio thread finishes setting the new metadata
    if (path_changed || track_id_changed) {
        strncpy(s_cached_filepath, current_path, sizeof(s_cached_filepath) - 1);
        s_cached_track_id = cur_track_id;

        if (s_cover_texture.id != 0) {
            UnloadTexture(s_cover_texture);
            s_cover_texture.id = 0;
        }

        // Check local directory art first
        try_load_folder_cover(current_path, &s_cover_texture);

        // Check extracted temp file only if track_id has synchronized
        if (s_cover_texture.id == 0 && art_url && strncmp(art_url, "file://", 7) == 0 && FileExists(art_url + 7)) {
            s_cover_texture = load_texture_robust(art_url + 7);
        }

        // Direct extraction from file if metadata hasn't caught up yet
        if (s_cover_texture.id == 0) {
            try_extract_embedded_cover(current_path, &s_cover_texture);
        }
    } else if (current_path[0] == '\0' && s_cover_texture.id != 0) {
        UnloadTexture(s_cover_texture);
        s_cover_texture.id = 0;
        s_cached_filepath[0] = '\0';
        s_cached_track_id = -1;
    }

    // Draw artwork texture if found
    if (s_cover_texture.id != 0) {
        Rectangle src = { 0, 0, (float)s_cover_texture.width, (float)s_cover_texture.height };
        DrawTexturePro(s_cover_texture, src, art_box, (Vector2){0, 0}, 0.0f, WHITE);

        DrawRectangleGradientV((int)art_box.x, (int)(art_box.y + art_box.height * 0.40f),
                               (int)art_box.width, (int)(art_box.height * 0.60f),
                               ColorAlpha(BLACK, 0.0f), ColorAlpha(BLACK, 0.85f));
    } else {
        Vector2 c = { art_box.x + art_box.width / 2.0f, art_box.y + art_box.height / 2.0f };
        DrawCircleLines((int)c.x, (int)c.y, art_box.height * 0.25f, ColorAlpha(COLOR_ACCENT, 0.3f));
        DrawCircleLines((int)c.x, (int)c.y, art_box.height * 0.35f, ColorAlpha(COLOR_ACCENT, 0.15f));
    }

    DrawText("SONG", (int)(art_box.x + 14), (int)(art_box.y + 14), FONT_SIZE_SM, COLOR_TEXT_MUTED);
    DrawText("<3", (int)(art_box.x + art_box.width - 28), (int)(art_box.y + 14), FONT_SIZE_MD, COLOR_ACCENT);

    float artist_h = (float)FONT_SIZE_SM;
    float title_h  = (float)FONT_SIZE_MD;
    float artist_y = art_box.y + art_box.height - artist_h - 12.0f;
    float title_y  = artist_y - title_h - 4.0f;

    Rectangle title_rect  = { art_box.x + 14, title_y,  art_box.width - 28, title_h };
    Rectangle artist_rect = { art_box.x + 14, artist_y, art_box.width - 28, artist_h };

    DrawTextMarquee(song_title, title_rect, b, (int)title_h, COLOR_TEXT_PRIMARY, 25.0f);
    DrawTextMarquee(artist, artist_rect, b, (int)artist_h, COLOR_TEXT_MUTED, 20.0f);
}

void tile_album_art_input(SparklesTile *tile, Rectangle b) {
    (void)tile; (void)b;
}