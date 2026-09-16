#define _DEFAULT_SOURCE
#include "voyager_dna.h"
#include "codec.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Utility functions to detect music features for some visualizers

static VoyagerSectorDNA s_sectors[VOYAGER_SECTOR_COUNT];
static int s_ready_count = 0;
static char s_cached_path[1024] = {0};
static pthread_t s_worker_thread;
static bool s_worker_running = false;
static pthread_mutex_t s_dna_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    char filepath[1024];
    uint32_t total_sec;
} ProbeJob;

static void* dna_probe_worker(void *arg) {
    ProbeJob *job = (ProbeJob*)arg;

    const KoniCodecImpl *codec = koni_find_codec_by_ext(job->filepath);
    KoniDecoder *dec = codec ? codec->open(job->filepath) : NULL;

    if (!codec || !dec) {
        unsigned int h = 5381;
        for (const char *p = job->filepath; *p; p++) h = ((h << 5) + h) + (unsigned char)*p;

        pthread_mutex_lock(&s_dna_mutex);
        for (int i = 0; i < VOYAGER_SECTOR_COUNT; i++) {
            float phase = (float)i * 0.6f + (float)(h % 100) * 0.1f;
            s_sectors[i] = (VoyagerSectorDNA){
                .energy = 0.35f + sinf(phase) * 0.30f,
                .chaos  = 0.30f + cosf(phase * 1.5f) * 0.35f,
                .bass   = 0.40f + sinf(phase * 0.8f) * 0.25f,
                .treble = 0.30f + cosf(phase * 2.0f) * 0.30f,
                .ready  = true
            };
        }
        s_ready_count = VOYAGER_SECTOR_COUNT;
        pthread_mutex_unlock(&s_dna_mutex);

        free(job);
        s_worker_running = false;
        return NULL;
    }

    KoniAudioFormat fmt = {0};
    codec->get_format(dec, &fmt);

    uint64_t total_frames = fmt.total_samples;
    if (total_frames == 0 && job->total_sec > 0 && fmt.sample_rate > 0) {
        total_frames = (uint64_t)job->total_sec * fmt.sample_rate;
    }
    if (total_frames == 0) total_frames = 44100 * 180;

    uint32_t channels = fmt.num_channels > 0 ? fmt.num_channels : 2;
    const uint32_t window_size = 8192;
    int32_t *pcm_buf = malloc(sizeof(int32_t) * window_size * channels);

    VoyagerSectorDNA raw_dna[VOYAGER_SECTOR_COUNT];
    float max_energy = 0.001f, max_chaos = 0.001f, max_bass = 0.001f;

    for (int s = 0; s < VOYAGER_SECTOR_COUNT; s++) {
        uint64_t target_frame = (total_frames * (uint64_t)s) / VOYAGER_SECTOR_COUNT;
        codec->seek(dec, target_frame);

        uint32_t read_frames = codec->decode(dec, pcm_buf, window_size);

        float sum_sq = 0.0f;
        float zcr = 0.0f;
        float flux_sum = 0.0f;
        float prev_val = 0.0f;

        if (read_frames > 0) {
            for (uint32_t f = 0; f < read_frames; f++) {
                float val = 0.0f;
                for (uint32_t c = 0; c < channels; c++) {
                    val += (float)pcm_buf[f * channels + c] / 2147483648.0f;
                }
                val /= (float)channels;
                sum_sq += val * val;

                if ((val > 0.0f && prev_val <= 0.0f) || (val < 0.0f && prev_val >= 0.0f)) {
                    zcr += 1.0f;
                }
                flux_sum += fabsf(val - prev_val);
                prev_val = val;
            }

            float rms = sqrtf(sum_sq / (float)read_frames);
            float norm_zcr = zcr / (float)read_frames;
            float norm_flux = flux_sum / (float)read_frames;

            raw_dna[s].energy = rms;
            raw_dna[s].chaos = norm_zcr * 2.0f + norm_flux;
            raw_dna[s].bass = (rms > 0.01f) ? (rms / (norm_flux + 0.05f)) : 0.0f;

            if (raw_dna[s].energy > max_energy) max_energy = raw_dna[s].energy;
            if (raw_dna[s].chaos > max_chaos) max_chaos = raw_dna[s].chaos;
            if (raw_dna[s].bass > max_bass) max_bass = raw_dna[s].bass;
        } else {
            raw_dna[s] = (VoyagerSectorDNA){0};
        }
    }

    free(pcm_buf);
    codec->close(dec);

    pthread_mutex_lock(&s_dna_mutex);
    for (int s = 0; s < VOYAGER_SECTOR_COUNT; s++) {
        s_sectors[s].energy = fminf(1.0f, raw_dna[s].energy / max_energy);
        s_sectors[s].chaos  = fminf(1.0f, raw_dna[s].chaos / max_chaos);
        s_sectors[s].bass   = fminf(1.0f, raw_dna[s].bass / max_bass);
        s_sectors[s].treble = raw_dna[s].chaos;
        s_sectors[s].ready  = true;
    }
    s_ready_count = VOYAGER_SECTOR_COUNT;
    pthread_mutex_unlock(&s_dna_mutex);

    free(job);
    s_worker_running = false;
    return NULL;
}

void voyager_dna_init(void) {
    pthread_mutex_lock(&s_dna_mutex);
    memset(s_sectors, 0, sizeof(s_sectors));
    s_ready_count = 0;
    s_cached_path[0] = '\0';
    pthread_mutex_unlock(&s_dna_mutex);
}

void voyager_dna_check_update(const char *filepath, uint32_t total_sec) {
    if (!filepath || !filepath[0]) return;

    pthread_mutex_lock(&s_dna_mutex);
    if (strcmp(s_cached_path, filepath) == 0 && s_ready_count > 0) {
        pthread_mutex_unlock(&s_dna_mutex);
        return;
    }

    strncpy(s_cached_path, filepath, sizeof(s_cached_path) - 1);
    memset(s_sectors, 0, sizeof(s_sectors));
    s_sectors[0] = (VoyagerSectorDNA){ .energy = 0.35f, .chaos = 0.20f, .bass = 0.4f, .treble = 0.3f, .ready = true };
    s_ready_count = 1;
    pthread_mutex_unlock(&s_dna_mutex);

    if (!s_worker_running) {
        ProbeJob *job = malloc(sizeof(ProbeJob));
        strncpy(job->filepath, filepath, sizeof(job->filepath) - 1);
        job->total_sec = total_sec;
        s_worker_running = true;
        pthread_create(&s_worker_thread, NULL, dna_probe_worker, job);
        pthread_detach(s_worker_thread);
    }
}

VoyagerSectorDNA voyager_dna_get_sector(int idx) {
    pthread_mutex_lock(&s_dna_mutex);
    if (idx < 0) idx = 0;
    if (idx >= VOYAGER_SECTOR_COUNT) idx = VOYAGER_SECTOR_COUNT - 1;
    VoyagerSectorDNA d = s_sectors[idx];
    pthread_mutex_unlock(&s_dna_mutex);
    return d;
}

int voyager_dna_get_ready_count(void) {
    pthread_mutex_lock(&s_dna_mutex);
    int c = s_ready_count;
    pthread_mutex_unlock(&s_dna_mutex);
    return c;
}

bool voyager_dna_is_ready(void) {
    return voyager_dna_get_ready_count() >= VOYAGER_SECTOR_COUNT;
}