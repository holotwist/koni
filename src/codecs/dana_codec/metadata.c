#include "codec.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#ifdef ENABLE_DANA
#include "DANADecoder.h"

static char* save_temp_cover(const uint8_t* data, size_t size) {
    char tmpl[] = "/tmp/koni_cover_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) return NULL;
    
    size_t written = 0;
    while (written < size) {
        ssize_t res = write(fd, data + written, size - written);
        if (res < 0) break;
        written += res;
    }
    close(fd);
    
    const char* ext = ".jpg";
    if (size >= 4 && data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G') {
        ext = ".png";
    }
    
    char* url = malloc(256);
    char new_path[256];
    snprintf(new_path, sizeof(new_path), "%s%s", tmpl, ext);
    rename(tmpl, new_path); 
    
    snprintf(url, 256, "file://%s", new_path);
    return url;
}

bool dana_read_metadata(const char* filepath, KoniMetadata* meta, uint32_t* duration_sec) {
    memset(meta, 0, sizeof(KoniMetadata));
    if (duration_sec) *duration_sec = 0;
    
    FILE* fp = fopen(filepath, "rb");
    if (!fp) return false;
    
    uint8_t header_buf[43];
    if (fread(header_buf, 1, 43, fp) < 43) { fclose(fp); return false; }
    
    uint32_t offset = (((uint32_t)header_buf[4] << 24) | ((uint32_t)header_buf[5] << 16) | ((uint32_t)header_buf[6] << 8) | header_buf[7]);
    uint32_t full_header_size = offset + 8;
    
    uint8_t* full_header_buf = malloc(full_header_size);
    if (!full_header_buf) { fclose(fp); return false; }
    
    fseek(fp, 0, SEEK_SET);
    if (fread(full_header_buf, 1, full_header_size, fp) < full_header_size) {
        free(full_header_buf); fclose(fp); return false;
    }
    fclose(fp);
    
    struct DANAHeaderInfo header;
    if (DANADecoder_DecodeHeader(full_header_buf, full_header_size, &header, NULL) == DANA_APIRESULT_OK) {
        if (header.metadata.title) meta->title = strdup((char*)header.metadata.title);
        if (header.metadata.artist) meta->artist = strdup((char*)header.metadata.artist);
        if (header.metadata.album) meta->album = strdup((char*)header.metadata.album);
        if (header.metadata.lyrics) meta->lyrics = strdup((char*)header.metadata.lyrics);
        
        if (header.metadata.cover_data && header.metadata.cover_size > 0) {
            meta->art_url = save_temp_cover(header.metadata.cover_data, header.metadata.cover_size);
        }
        
        // for now Dana does not support ReplayGain metadata
        meta->has_track_gain = false;
        meta->track_gain = 0.0f;
        
        if (header.wave_format.sampling_rate > 0) {
            if (duration_sec) *duration_sec = header.num_samples / header.wave_format.sampling_rate;
        }
        
        DANAMetadata_Release(&header.metadata);
        free(full_header_buf);
        return true;
    }
    free(full_header_buf);
    return false;
}

#else

bool dana_read_metadata(const char* filepath, KoniMetadata* meta, uint32_t* duration_sec) {
    (void)filepath;
    memset(meta, 0, sizeof(KoniMetadata));
    if (duration_sec) *duration_sec = 0;
    return false;
}

#endif