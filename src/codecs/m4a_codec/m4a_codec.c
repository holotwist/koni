#define _DEFAULT_SOURCE
#define _GNU_SOURCE

#include "m4a_codec.h"
#include "mp4_demux.h"
#include <stdlib.h>
#include <string.h>

#if defined(__has_include)
  #if __has_include(<neaacdec.h>)
    #include <neaacdec.h>
  #elif __has_include(<faad.h>)
    #include <faad.h>
  #else
    #include <neaacdec.h>
  #endif
#else
  #include <neaacdec.h>
#endif

static const char* m4a_exts[] = { ".m4a", ".aac", ".mp4", NULL };

#define PACKET_BUF_MAX 32768
#define DECODE_FIFO_MAX 32768

struct KoniDecoder {
    FILE *fp;
    Mp4Demuxer *demux;
    NeAACDecHandle faad;
    KoniAudioFormat fmt;

    uint32_t cur_sample_idx;
    uint8_t packet_buf[PACKET_BUF_MAX];

    // Interleaved 16-bit intermediate buffer from faad
    int16_t fifo[DECODE_FIFO_MAX];
    uint32_t fifo_head;
    uint32_t fifo_len;
};

static KoniDecoder* m4a_open(const char *filepath) {
    if (!filepath || !filepath[0]) return NULL;

    FILE *fp = fopen(filepath, "rb");
    if (!fp) return NULL;

    Mp4Demuxer *demux = mp4_demux_open(fp);
    if (!demux) {
        fclose(fp);
        return NULL;
    }

    NeAACDecHandle faad = NeAACDecOpen();
    if (!faad) {
        mp4_demux_close(demux);
        fclose(fp);
        return NULL;
    }

    NeAACDecConfigurationPtr conf = NeAACDecGetCurrentConfiguration(faad);
    conf->outputFormat = FAAD_FMT_16BIT;
    conf->dontUpSampleImplicitSBR = 0; // Enables automatic 2x SBR upsampling for HE-AAC
    NeAACDecSetConfiguration(faad, conf);

    unsigned long actual_srate = 0;
    unsigned char actual_channels = 0;
    char res = NeAACDecInit2(faad, demux->dsi, demux->dsi_len, &actual_srate, &actual_channels);
    if (res < 0) {
        NeAACDecClose(faad);
        mp4_demux_close(demux);
        fclose(fp);
        return NULL;
    }

    KoniDecoder *dec = calloc(1, sizeof(KoniDecoder));
    if (!dec) {
        NeAACDecClose(faad);
        mp4_demux_close(demux);
        fclose(fp);
        return NULL;
    }

    dec->fp = fp;
    dec->demux = demux;
    dec->faad = faad;
    dec->cur_sample_idx = 0;

    dec->fmt.sample_rate = (uint32_t)actual_srate;
    dec->fmt.num_channels = (uint16_t)actual_channels;
    dec->fmt.bits_per_sample = 32;
    dec->fmt.bitrate = (actual_srate * actual_channels * 16);

    // If SBR doubled the sample rate compared to demuxer timescale, adjust total PCM frames
    if (demux->timescale > 0 && actual_srate > demux->timescale) {
        dec->fmt.total_samples = (demux->total_pcm_frames * (uint64_t)actual_srate) / demux->timescale;
    } else {
        dec->fmt.total_samples = demux->total_pcm_frames;
    }

    return dec;
}

static void m4a_close(KoniDecoder *dec) {
    if (!dec) return;
    if (dec->faad) NeAACDecClose(dec->faad);
    if (dec->demux) mp4_demux_close(dec->demux);
    if (dec->fp) fclose(dec->fp);
    free(dec);
}

static bool m4a_get_format(KoniDecoder *dec, KoniAudioFormat *fmt) {
    if (!dec || !fmt) return false;
    *fmt = dec->fmt;
    return true;
}

static uint32_t m4a_decode(KoniDecoder *dec, int32_t *pcm_out_interleaved, uint32_t max_samples) {
    if (!dec || !pcm_out_interleaved || max_samples == 0) return 0;

    uint32_t channels = dec->fmt.num_channels > 0 ? dec->fmt.num_channels : 2;
    uint32_t frames_produced = 0;

    while (frames_produced < max_samples) {
        // Drain available FIFO frames
        uint32_t avail_frames = dec->fifo_len / channels;
        if (avail_frames > 0) {
            uint32_t to_copy = max_samples - frames_produced;
            if (to_copy > avail_frames) to_copy = avail_frames;

            for (uint32_t i = 0; i < to_copy * channels; i++) {
                pcm_out_interleaved[frames_produced * channels + i] = ((int32_t)dec->fifo[dec->fifo_head + i]) << 16;
            }

            dec->fifo_head += to_copy * channels;
            dec->fifo_len -= to_copy * channels;
            frames_produced += to_copy;

            if (dec->fifo_len == 0) dec->fifo_head = 0;
            continue;
        }

        // Decode next compressed AAC access unit from file
        if (dec->cur_sample_idx >= dec->demux->sample_count) break;

        uint64_t offset = 0;
        uint32_t size = 0;
        if (!mp4_demux_get_sample_pos(dec->demux, dec->cur_sample_idx, &offset, &size) || size == 0 || size > PACKET_BUF_MAX) {
            dec->cur_sample_idx++;
            continue;
        }

        fseeko(dec->fp, (off_t)offset, SEEK_SET);
        if (fread(dec->packet_buf, 1, size, dec->fp) != size) {
            break;
        }
        dec->cur_sample_idx++;

        NeAACDecFrameInfo fi;
        memset(&fi, 0, sizeof(fi));
        void *samples = NeAACDecDecode(dec->faad, &fi, dec->packet_buf, size);

        if (fi.error == 0 && samples && fi.samples > 0) {
            if (fi.samples <= DECODE_FIFO_MAX) {
                memcpy(dec->fifo, samples, fi.samples * sizeof(int16_t));
                dec->fifo_head = 0;
                dec->fifo_len = (uint32_t)fi.samples;
            }
        }
    }

    return frames_produced;
}

static bool m4a_seek(KoniDecoder *dec, uint64_t target_sample) {
    if (!dec || !dec->demux) return false;

    // Reset decoder core
    NeAACDecClose(dec->faad);
    dec->faad = NeAACDecOpen();
    if (!dec->faad) return false;

    NeAACDecConfigurationPtr conf = NeAACDecGetCurrentConfiguration(dec->faad);
    conf->outputFormat = FAAD_FMT_16BIT;
    conf->dontUpSampleImplicitSBR = 0;
    NeAACDecSetConfiguration(dec->faad, conf);

    unsigned long srate = 0;
    unsigned char ch = 0;
    if (NeAACDecInit2(dec->faad, dec->demux->dsi, dec->demux->dsi_len, &srate, &ch) < 0) {
        return false;
    }

    dec->fifo_head = 0;
    dec->fifo_len = 0;

    uint64_t target_demux_frame = target_sample;
    if (dec->demux->timescale > 0 && dec->fmt.sample_rate > dec->demux->timescale) {
        target_demux_frame = (target_sample * (uint64_t)dec->demux->timescale) / dec->fmt.sample_rate;
    }
    uint32_t target_sample_idx = mp4_demux_frame_to_sample(dec->demux, target_demux_frame);
    dec->cur_sample_idx = (target_sample_idx > 2) ? (target_sample_idx - 2) : 0;
    return true;
}

const KoniCodecImpl m4a_codec_impl = {
    .name = "AAC / HE-AAC (FAAD2)",
    .supported_extensions = m4a_exts,
    .capabilities = KONI_CODEC_CAP_STREAM,
    .open = m4a_open,
    .close = m4a_close,
    .get_format = m4a_get_format,
    .read_metadata = m4a_read_metadata,
    .decode = m4a_decode,
    .seek = m4a_seek,
    .get_interface = NULL,
    .control = NULL
};
REGISTER_KONI_CODEC(m4a_codec_impl);