#include "codec.h"
#include "state.h"
#include "pxtn_tiny.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static const char* pxtn_exts[] = { ".ptcop", ".pttune", NULL };

typedef struct {
    char note[4];
    uint8_t inst;
    uint8_t vol;
} TrackerCell;

struct KoniDecoder {
    PxtnTiny *pxtn;
    KoniAudioFormat fmt;
    TrackerCell *grid;
    uint32_t grid_rows;
    uint32_t grid_channels;
    uint32_t step_clocks;
};

static uint32_t pxtn_get_row_step_clocks(const PxtnTiny *p) {
    uint32_t beat_clock = 480;
    if (p && p->info.beat_tempo > 0.0f && p->clock_rate > 0.0f) {
        beat_clock = (uint32_t)((60.0 * (double)p->dst_sps) / ((double)p->info.beat_tempo * (double)p->clock_rate) + 0.5);
        if (beat_clock == 0) beat_clock = 480;
    }
    return (uint32_t)(beat_clock / 4); // 16th-note row resolution
}

static void pxtn_prebake_tracker_grid(KoniDecoder *dec) {
    if (!dec || !dec->pxtn) return;
    PxtnTiny *p = dec->pxtn;
    uint32_t step = pxtn_get_row_step_clocks(p);
    dec->step_clocks = step;

    int32_t max_clk = 0;
    for (int i = 0; i < p->event_count; i++) {
        if (p->events[i].clock > max_clk) max_clk = p->events[i].clock;
    }
    uint32_t total_clock = (p->clock_rate > 0.0f) ? (uint32_t)((double)p->total_samples / (double)p->clock_rate) : 0;
    if ((uint32_t)max_clk > total_clock) total_clock = (uint32_t)max_clk;

    uint32_t total_rows = step > 0 ? (total_clock / step + 64) : 64;
    if (total_rows > 65536) total_rows = 65536; // Guard against corrupt duration values

    uint32_t num_channels = (uint32_t)p->unit_count;
    if (num_channels == 0) num_channels = 1;

    dec->grid_rows = total_rows;
    dec->grid_channels = num_channels;
    dec->grid = calloc((size_t)total_rows * (size_t)num_channels, sizeof(TrackerCell));
    if (!dec->grid) return;

    // Chronological state tracking for units
    int unit_key[MAX_UNITS];
    int unit_voice[MAX_UNITS];
    int unit_vol[MAX_UNITS];
    int unit_vel[MAX_UNITS];
    for (int u = 0; u < MAX_UNITS; u++) {
        unit_key[u] = 0x6000;
        unit_voice[u] = u;
        unit_vol[u] = 104;
        unit_vel[u] = 104;
    }

    static const char *note_names[12] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };

    // Linear O(N) pre-pass over all song events
    for (int i = 0; i < p->event_count; i++) {
        const PxEvent *e = &p->events[i];
        if (e->unit_no >= MAX_UNITS) continue;
        uint8_t u = e->unit_no;

        if (e->kind == PX_EVENT_KEY) {
            unit_key[u] = e->value;
        } else if (e->kind == PX_EVENT_VOICENO) {
            unit_voice[u] = e->value >= 0 ? e->value : (-e->value - 1);
        } else if (e->kind == PX_EVENT_VOLUME) {
            unit_vol[u] = e->value;
        } else if (e->kind == PX_EVENT_VELOCITY) {
            unit_vel[u] = e->value;
        } else if (e->kind == PX_EVENT_ON && step > 0) {
            uint32_t row = (uint32_t)(e->clock / step);
            if (row < total_rows && u < num_channels) {
                TrackerCell *cell = &dec->grid[row * num_channels + u];
                int semi = unit_key[u] / 0x100;
                int note_idx = semi % 12;
                if (note_idx < 0) note_idx += 12;
                int octave = (semi / 12) - 4;
                if (octave < 0) octave = 0;
                if (octave > 9) octave = 9;

                snprintf(cell->note, sizeof(cell->note), "%s%d", note_names[note_idx], octave);
                cell->inst = (uint8_t)(unit_voice[u] + 1);
                cell->vol = (uint8_t)((unit_vol[u] * unit_vel[u]) / 128);
            }
        }
    }
}

static KoniDecoder* pxtn_codec_open(const char* filepath) {
    uint32_t sample_rate = 44100;
    PxtnTiny *pt = pxtn_tiny_open(filepath, sample_rate);
    if (!pt) return NULL;

    PxtnSongInfo info;
    pxtn_tiny_get_info(pt, &info);

    KoniDecoder *dec = calloc(1, sizeof(KoniDecoder));
    dec->pxtn = pt;
    dec->fmt.sample_rate = sample_rate;
    dec->fmt.num_channels = 2;
    dec->fmt.bits_per_sample = 32;
    dec->fmt.bitrate = sample_rate * 2 * 32;
    dec->fmt.total_samples = info.total_samples;

    pxtn_prebake_tracker_grid(dec);
    return dec;
}

static void pxtn_codec_close(KoniDecoder* dec) {
    if (!dec) return;
    if (dec->grid) free(dec->grid);
    if (dec->pxtn) pxtn_tiny_close(dec->pxtn);
    free(dec);
}

static bool pxtn_codec_get_format(KoniDecoder* dec, KoniAudioFormat* fmt) {
    if (!dec || !fmt) return false;
    *fmt = dec->fmt;
    return true;
}

static bool pxtn_codec_read_metadata(const char* filepath, KoniMetadata* meta, uint32_t* duration_sec) {
    memset(meta, 0, sizeof(KoniMetadata));
    if (duration_sec) *duration_sec = 0;

    PxtnTiny *pt = pxtn_tiny_open(filepath, 44100);
    if (!pt) return false;

    PxtnSongInfo info;
    pxtn_tiny_get_info(pt, &info);

    if (info.name[0] != '\0') {
        meta->title = strdup(info.name);
    }
    if (info.comment[0] != '\0') {
        meta->lyrics = strdup(info.comment);
    }

    meta->has_track_gain = false;
    meta->track_gain = 0.0f;

    if (duration_sec) *duration_sec = info.duration_sec;

    pxtn_tiny_close(pt);
    return true;
}

static uint32_t pxtn_codec_decode(KoniDecoder* dec, int32_t* pcm_out_interleaved, uint32_t max_samples) {
    if (!dec || !dec->pxtn) return 0;
    return pxtn_tiny_render(dec->pxtn, pcm_out_interleaved, max_samples);
}

static bool pxtn_codec_seek(KoniDecoder* dec, uint64_t target_sample) {
    if (!dec || !dec->pxtn) return false;
    return pxtn_tiny_seek(dec->pxtn, target_sample);
}

/* Tracker Interface Implementation */

static uint32_t pxtn_iface_get_num_channels(KoniDecoder *dec) {
    return (dec && dec->pxtn) ? (uint32_t)dec->pxtn->unit_count : 0;
}

static const char* pxtn_iface_get_channel_name(KoniDecoder *dec, uint32_t channel_idx) {
    static char buf[32];
    if (!dec || !dec->pxtn || channel_idx >= (uint32_t)dec->pxtn->unit_count) return "";
    snprintf(buf, sizeof(buf), "Unit %02u", channel_idx);
    return buf;
}

static uint32_t pxtn_iface_get_current_row(KoniDecoder *dec) {
    if (!dec || !dec->pxtn || dec->pxtn->clock_rate <= 0.0f) return 0;
    // Track audio playhead from speakers rather than decoding buffer
    uint32_t played_frames = atomic_load(&p_frames_consumed);
    uint32_t cur_clock = (uint32_t)((double)played_frames / (double)dec->pxtn->clock_rate);
    uint32_t step = dec->step_clocks > 0 ? dec->step_clocks : pxtn_get_row_step_clocks(dec->pxtn);
    return step > 0 ? (cur_clock / step) : 0;
}

static uint32_t pxtn_iface_get_total_rows(KoniDecoder *dec) {
    if (!dec) return 0;
    return dec->grid_rows;
}

static uint32_t pxtn_iface_get_bpm(KoniDecoder *dec) {
    return (dec && dec->pxtn) ? (uint32_t)dec->pxtn->info.beat_tempo : 120;
}

static bool pxtn_iface_get_cell_data(KoniDecoder *dec, uint32_t row, uint32_t channel_idx,
                                     char out_note[4], uint8_t *out_instrument, uint8_t *out_volume) {
    if (!dec || !dec->grid || row >= dec->grid_rows || channel_idx >= dec->grid_channels) {
        strncpy(out_note, "...", 4);
        if (out_instrument) *out_instrument = 0;
        if (out_volume) *out_volume = 0;
        return false;
    }

    // Direct O(1) memory lookup
    const TrackerCell *cell = &dec->grid[row * dec->grid_channels + channel_idx];
    if (cell->note[0] != '\0') {
        memcpy(out_note, cell->note, 4);
    } else {
        strncpy(out_note, "...", 4);
    }
    if (out_instrument) *out_instrument = cell->inst;
    if (out_volume) *out_volume = cell->vol;

    return true;
}

static const KoniTrackerInterface pxtn_tracker_iface = {
    .get_num_channels = pxtn_iface_get_num_channels,
    .get_channel_name = pxtn_iface_get_channel_name,
    .get_current_row = pxtn_iface_get_current_row,
    .get_total_rows = pxtn_iface_get_total_rows,
    .get_bpm = pxtn_iface_get_bpm,
    .get_cell_data = pxtn_iface_get_cell_data
};

static void* pxtn_codec_get_interface(KoniDecoder *dec, KoniInterfaceID iface_id) {
    (void)dec;
    if (iface_id == KONI_IFACE_TRACKER) {
        return (void*)&pxtn_tracker_iface;
    }
    return NULL;
}

const KoniCodecImpl pxtone_codec_impl = {
    .name = "PxTone Audio (.ptcop, .pttune)",
    .supported_extensions = pxtn_exts,
    .capabilities = KONI_CODEC_CAP_TRACKER | KONI_CODEC_CAP_SYNTH,
    .open = pxtn_codec_open,
    .close = pxtn_codec_close,
    .get_format = pxtn_codec_get_format,
    .read_metadata = pxtn_codec_read_metadata,
    .decode = pxtn_codec_decode,
    .seek = pxtn_codec_seek,
    .get_interface = pxtn_codec_get_interface,
    .control = NULL
};