#define _DEFAULT_SOURCE
#define _GNU_SOURCE
#include "mp4_demux.h"
#include <stdlib.h>
#include <string.h>

#define FOURCC(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

static inline uint32_t read_u32(FILE *f) {
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return 0;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

static inline uint16_t read_u16(FILE *f) {
    uint8_t b[2];
    if (fread(b, 1, 2, f) != 2) return 0;
    return ((uint16_t)b[0] << 8) | (uint16_t)b[1];
}

static inline uint64_t read_u64(FILE *f) {
    uint8_t b[8];
    if (fread(b, 1, 8, f) != 8) return 0;
    return ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) | ((uint64_t)b[2] << 40) | ((uint64_t)b[3] << 32) |
           ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) | ((uint64_t)b[6] << 8)  | (uint64_t)b[7];
}

static bool read_atom_header(FILE *f, uint32_t *out_type, uint64_t *out_size, uint32_t *out_hdr_len) {
    uint8_t b[8];
    if (fread(b, 1, 8, f) != 8) return false;

    uint32_t s32 = ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
    *out_type = ((uint32_t)b[4] << 24) | ((uint32_t)b[5] << 16) | ((uint32_t)b[6] << 8) | (uint32_t)b[7];

    if (s32 == 1) {
        *out_size = read_u64(f);
        *out_hdr_len = 16;
    } else {
        *out_size = s32;
        *out_hdr_len = 8;
    }
    return true;
}

static void parse_esds(Mp4Demuxer *d, FILE *f, uint64_t atom_end) {
    fseeko(f, 4, SEEK_CUR); // skip version & flags

    while ((uint64_t)ftello(f) + 2 < atom_end) {
        int tag = fgetc(f);
        if (tag == EOF) break;

        uint32_t len = 0;
        for (int i = 0; i < 4; i++) {
            int b = fgetc(f);
            if (b == EOF) return;
            len = (len << 7) | (b & 0x7F);
            if (!(b & 0x80)) break;
        }

        uint64_t desc_end = (uint64_t)ftello(f) + len;
        if (desc_end > atom_end) break;

        if (tag == 0x03) {
            fseeko(f, 3, SEEK_CUR); // ES_ID + flags
        } else if (tag == 0x04) {
            fseeko(f, 13, SEEK_CUR); // config descriptor
        } else if (tag == 0x05) { // AudioSpecificConfig
            if (len > 0 && len < 256) {
                if (d->dsi) free(d->dsi);
                d->dsi = malloc(len);
                if (d->dsi && fread(d->dsi, 1, len, f) == len) {
                    d->dsi_len = len;
                }
            }
            return;
        } else {
            fseeko(f, (off_t)desc_end, SEEK_SET);
        }
    }
}

static void parse_stsd(Mp4Demuxer *d, FILE *f, uint64_t atom_end) {
    fseeko(f, 4, SEEK_CUR);
    uint32_t entry_count = read_u32(f);
    if (entry_count == 0) return;

    uint32_t sub_type = 0, hdr_len = 0;
    uint64_t sub_size = 0;

    while ((uint64_t)ftello(f) + 8 <= atom_end) {
        uint64_t entry_start = (uint64_t)ftello(f);
        if (!read_atom_header(f, &sub_type, &sub_size, &hdr_len) || sub_size < hdr_len) break;
        uint64_t entry_end = entry_start + sub_size;

        if (sub_type == FOURCC('m', 'p', '4', 'a')) {
            fseeko(f, 16, SEEK_CUR);
            d->num_channels = read_u16(f);
            d->bits_per_sample = read_u16(f);
            fseeko(f, 4, SEEK_CUR);
            d->sample_rate = read_u32(f) >> 16;

            while ((uint64_t)ftello(f) + 8 <= entry_end) {
                uint64_t child_start = (uint64_t)ftello(f);
                uint32_t child_type = 0, child_hdr = 0;
                uint64_t child_size = 0;
                if (!read_atom_header(f, &child_type, &child_size, &child_hdr) || child_size < child_hdr) break;

                if (child_type == FOURCC('e', 's', 'd', 's')) {
                    parse_esds(d, f, child_start + child_size);
                }
                fseeko(f, (off_t)(child_start + child_size), SEEK_SET);
            }
        }
        fseeko(f, (off_t)entry_end, SEEK_SET);
    }
}

static void parse_stbl(Mp4Demuxer *d, FILE *f, uint64_t atom_end) {
    uint32_t type = 0, hdr = 0;
    uint64_t size = 0;

    while ((uint64_t)ftello(f) + 8 <= atom_end) {
        uint64_t start = (uint64_t)ftello(f);
        if (!read_atom_header(f, &type, &size, &hdr) || size < hdr) break;
        uint64_t end = start + size;

        if (type == FOURCC('s', 't', 's', 'd')) {
            parse_stsd(d, f, end);
        } else if (type == FOURCC('s', 't', 't', 's')) {
            fseeko(f, 4, SEEK_CUR);
            d->stts_count = read_u32(f);
            if (d->stts_count > 0 && d->stts_count < 1000000) {
                d->stts_table = malloc(sizeof(Mp4SttsEntry) * d->stts_count);
                if (d->stts_table) {
                    for (uint32_t i = 0; i < d->stts_count; i++) {
                        d->stts_table[i].sample_count = read_u32(f);
                        d->stts_table[i].sample_delta = read_u32(f);
                        d->total_pcm_frames += (uint64_t)d->stts_table[i].sample_count * d->stts_table[i].sample_delta;
                    }
                }
            }
        } else if (type == FOURCC('s', 't', 's', 'c')) {
            fseeko(f, 4, SEEK_CUR);
            d->stsc_count = read_u32(f);
            if (d->stsc_count > 0 && d->stsc_count < 1000000) {
                d->stsc_table = malloc(sizeof(Mp4StscEntry) * d->stsc_count);
                if (d->stsc_table) {
                    for (uint32_t i = 0; i < d->stsc_count; i++) {
                        d->stsc_table[i].first_chunk = read_u32(f);
                        d->stsc_table[i].samples_per_chunk = read_u32(f);
                        d->stsc_table[i].sample_desc_idx = read_u32(f);
                    }
                }
            }
        } else if (type == FOURCC('s', 't', 's', 'z')) {
            fseeko(f, 4, SEEK_CUR);
            d->const_sample_size = read_u32(f);
            d->sample_count = read_u32(f);
            if (d->const_sample_size == 0 && d->sample_count > 0 && d->sample_count < 2000000) {
                d->sample_sizes = malloc(sizeof(uint32_t) * d->sample_count);
                if (d->sample_sizes) {
                    for (uint32_t i = 0; i < d->sample_count; i++) {
                        d->sample_sizes[i] = read_u32(f);
                    }
                }
            }
        } else if (type == FOURCC('s', 't', 'c', 'o')) {
            fseeko(f, 4, SEEK_CUR);
            d->chunk_count = read_u32(f);
            if (d->chunk_count > 0 && d->chunk_count < 1000000) {
                d->chunk_offsets = malloc(sizeof(uint64_t) * d->chunk_count);
                if (d->chunk_offsets) {
                    for (uint32_t i = 0; i < d->chunk_count; i++) {
                        d->chunk_offsets[i] = (uint64_t)read_u32(f);
                    }
                }
            }
        } else if (type == FOURCC('c', 'o', '6', '4')) {
            fseeko(f, 4, SEEK_CUR);
            d->chunk_count = read_u32(f);
            if (d->chunk_count > 0 && d->chunk_count < 1000000) {
                d->chunk_offsets = malloc(sizeof(uint64_t) * d->chunk_count);
                if (d->chunk_offsets) {
                    for (uint32_t i = 0; i < d->chunk_count; i++) {
                        d->chunk_offsets[i] = read_u64(f);
                    }
                }
            }
        }
        fseeko(f, (off_t)end, SEEK_SET);
    }
}

static bool parse_trak(Mp4Demuxer *d, FILE *f, uint64_t atom_end) {
    uint32_t type = 0, hdr = 0;
    uint64_t size = 0;
    bool is_audio = false;

    while ((uint64_t)ftello(f) + 8 <= atom_end) {
        uint64_t start = (uint64_t)ftello(f);
        if (!read_atom_header(f, &type, &size, &hdr) || size < hdr) break;
        uint64_t end = start + size;

        if (type == FOURCC('t', 'k', 'h', 'd')) {
            uint8_t ver = (uint8_t)fgetc(f);
            fseeko(f, 3, SEEK_CUR);
            if (ver == 1) {
                fseeko(f, 16, SEEK_CUR);
                d->audio_track_id = read_u32(f);
            } else {
                fseeko(f, 8, SEEK_CUR);
                d->audio_track_id = read_u32(f);
            }
        } else if (type == FOURCC('m', 'd', 'i', 'a')) {
            while ((uint64_t)ftello(f) + 8 <= end) {
                uint64_t m_start = (uint64_t)ftello(f);
                uint32_t m_type = 0, m_hdr = 0;
                uint64_t m_size = 0;
                if (!read_atom_header(f, &m_type, &m_size, &m_hdr) || m_size < m_hdr) break;
                uint64_t m_end = m_start + m_size;

                if (m_type == FOURCC('m', 'd', 'h', 'd')) {
                    uint8_t ver = (uint8_t)fgetc(f);
                    fseeko(f, 3, SEEK_CUR);
                    if (ver == 1) {
                        fseeko(f, 16, SEEK_CUR);
                        d->timescale = read_u32(f);
                        d->track_duration = read_u64(f);
                    } else {
                        fseeko(f, 8, SEEK_CUR);
                        d->timescale = read_u32(f);
                        d->track_duration = read_u32(f);
                    }
                } else if (m_type == FOURCC('h', 'd', 'l', 'r')) {
                    fseeko(f, 8, SEEK_CUR);
                    uint32_t sub_handler = read_u32(f);
                    if (sub_handler == FOURCC('s', 'o', 'u', 'n')) {
                        is_audio = true;
                    }
                } else if (m_type == FOURCC('m', 'i', 'n', 'f') && is_audio) {
                    while ((uint64_t)ftello(f) + 8 <= m_end) {
                        uint64_t mi_start = (uint64_t)ftello(f);
                        uint32_t mi_type = 0, mi_hdr = 0;
                        uint64_t mi_size = 0;
                        if (!read_atom_header(f, &mi_type, &mi_size, &mi_hdr) || mi_size < mi_hdr) break;
                        if (mi_type == FOURCC('s', 't', 'b', 'l')) {
                            parse_stbl(d, f, mi_start + mi_size);
                        }
                        fseeko(f, (off_t)(mi_start + mi_size), SEEK_SET);
                    }
                }
                fseeko(f, (off_t)m_end, SEEK_SET);
            }
        }
        fseeko(f, (off_t)end, SEEK_SET);
    }
    return is_audio;
}

static void build_sample_lookup(Mp4Demuxer *d) {
    if (d->sample_count == 0 || d->chunk_count == 0 || d->stsc_count == 0) return;

    d->sample_byte_offsets = malloc(sizeof(uint64_t) * d->sample_count);
    if (!d->sample_byte_offsets) return;

    uint32_t sample_idx = 0;
    uint32_t stsc_idx = 0;

    for (uint32_t chunk = 1; chunk <= d->chunk_count && sample_idx < d->sample_count; chunk++) {
        if (stsc_idx + 1 < d->stsc_count && chunk >= d->stsc_table[stsc_idx + 1].first_chunk) {
            stsc_idx++;
        }
        uint32_t spc = d->stsc_table[stsc_idx].samples_per_chunk;
        uint64_t offset = d->chunk_offsets[chunk - 1];

        for (uint32_t s = 0; s < spc && sample_idx < d->sample_count; s++) {
            d->sample_byte_offsets[sample_idx] = offset;
            uint32_t sz = d->const_sample_size ? d->const_sample_size : (d->sample_sizes ? d->sample_sizes[sample_idx] : 0);
            offset += sz;
            sample_idx++;
        }
    }
}

// Scans Fragmented MP4 (DASH / fMP4) moof boxes
static void parse_moof_fragment(Mp4Demuxer *d, FILE *f, uint64_t moof_start, uint64_t moof_end, uint32_t *io_cap) {
    while ((uint64_t)ftello(f) + 8 <= moof_end) {
        uint64_t sub_start = (uint64_t)ftello(f);
        uint32_t sub_type = 0, sub_hdr = 0;
        uint64_t sub_size = 0;
        if (!read_atom_header(f, &sub_type, &sub_size, &sub_hdr) || sub_size < sub_hdr) break;
        uint64_t sub_end = sub_start + sub_size;

        if (sub_type == FOURCC('t', 'r', 'a', 'f')) {
            uint64_t base_data_offset = moof_start;
            bool base_data_offset_present = false;
            bool default_base_is_moof = false;
            uint32_t def_duration = 1024;
            uint32_t def_size = 0;

            while ((uint64_t)ftello(f) + 8 <= sub_end) {
                uint64_t c_start = (uint64_t)ftello(f);
                uint32_t c_type = 0, c_hdr = 0;
                uint64_t c_size = 0;
                if (!read_atom_header(f, &c_type, &c_size, &c_hdr) || c_size < c_hdr) break;
                uint64_t c_end = c_start + c_size;

                if (c_type == FOURCC('t', 'f', 'h', 'd')) {
                    fgetc(f); // version
                    uint32_t flags = ((uint32_t)fgetc(f) << 16) | ((uint32_t)fgetc(f) << 8) | (uint32_t)fgetc(f);
                    uint32_t track_id = read_u32(f);
                    if (d->audio_track_id != 0 && track_id != d->audio_track_id) {
                        fseeko(f, (off_t)sub_end, SEEK_SET);
                        break;
                    }

                    if (flags & 0x000001) { base_data_offset = read_u64(f); base_data_offset_present = true; }
                    if (flags & 0x000002) read_u32(f); // sample_description_index
                    if (flags & 0x000008) def_duration = read_u32(f);
                    if (flags & 0x000010) def_size = read_u32(f);
                    if (flags & 0x000020) read_u32(f); // default_sample_flags
                    if (flags & 0x020000) default_base_is_moof = true;
                } else if (c_type == FOURCC('t', 'r', 'u', 'n')) {
                    fgetc(f); // version
                    uint32_t tr_flags = ((uint32_t)fgetc(f) << 16) | ((uint32_t)fgetc(f) << 8) | (uint32_t)fgetc(f);
                    uint32_t sample_count = read_u32(f);
                    int32_t data_offset = 0;

                    if (tr_flags & 0x000001) data_offset = (int32_t)read_u32(f);
                    if (tr_flags & 0x000004) read_u32(f); // first_sample_flags

                    uint64_t cur_offset = 0;
                    if (default_base_is_moof || !base_data_offset_present) {
                        cur_offset = (uint64_t)((int64_t)moof_start + (int64_t)data_offset);
                    } else {
                        cur_offset = (uint64_t)((int64_t)base_data_offset + (int64_t)data_offset);
                    }

                    for (uint32_t s = 0; s < sample_count; s++) {
                        uint32_t dur = def_duration;
                        uint32_t sz = def_size;
                        if (tr_flags & 0x000100) dur = read_u32(f);
                        if (tr_flags & 0x000200) sz = read_u32(f);
                        if (tr_flags & 0x000400) read_u32(f); // sample_flags
                        if (tr_flags & 0x000800) read_u32(f); // composition_time_offset

                        if (d->sample_count >= *io_cap) {
                            *io_cap = (*io_cap == 0) ? 4096 : (*io_cap * 2);
                            d->sample_byte_offsets = realloc(d->sample_byte_offsets, sizeof(uint64_t) * (*io_cap));
                            d->sample_sizes = realloc(d->sample_sizes, sizeof(uint32_t) * (*io_cap));
                            d->sample_durations = realloc(d->sample_durations, sizeof(uint32_t) * (*io_cap));
                        }

                        d->sample_byte_offsets[d->sample_count] = cur_offset;
                        d->sample_sizes[d->sample_count] = sz;
                        d->sample_durations[d->sample_count] = dur;
                        d->total_pcm_frames += dur;
                        d->sample_count++;
                        cur_offset += sz;
                    }
                }
                fseeko(f, (off_t)c_end, SEEK_SET);
            }
        }
        fseeko(f, (off_t)sub_end, SEEK_SET);
    }
}

static void parse_fragmented_mp4(Mp4Demuxer *d, FILE *f) {
    fseeko(f, 0, SEEK_SET);
    uint32_t cap = 4096;
    d->sample_byte_offsets = malloc(sizeof(uint64_t) * cap);
    d->sample_sizes = malloc(sizeof(uint32_t) * cap);
    d->sample_durations = malloc(sizeof(uint32_t) * cap);
    d->sample_count = 0;

    uint32_t type = 0, hdr = 0;
    uint64_t size = 0;

    while (read_atom_header(f, &type, &size, &hdr)) {
        uint64_t start = (uint64_t)ftello(f) - hdr;
        uint64_t end = start + size;
        if (size < hdr) break;

        if (type == FOURCC('m', 'o', 'o', 'f')) {
            parse_moof_fragment(d, f, start, end, &cap);
        }
        fseeko(f, (off_t)end, SEEK_SET);
    }
}

Mp4Demuxer* mp4_demux_open(FILE *fp) {
    if (!fp) return NULL;
    fseeko(fp, 0, SEEK_SET);

    Mp4Demuxer *d = calloc(1, sizeof(Mp4Demuxer));
    if (!d) return NULL;
    d->fp = fp;

    uint32_t type = 0, hdr = 0;
    uint64_t size = 0;
    bool found_audio = false;

    while (read_atom_header(fp, &type, &size, &hdr)) {
        uint64_t start = (uint64_t)ftello(fp) - hdr;
        uint64_t end = start + size;
        if (size < hdr) break;

        if (type == FOURCC('m', 'o', 'o', 'v')) {
            while ((uint64_t)ftello(fp) + 8 <= end) {
                uint64_t sub_start = (uint64_t)ftello(fp);
                uint32_t sub_type = 0, sub_hdr = 0;
                uint64_t sub_size = 0;
                if (!read_atom_header(fp, &sub_type, &sub_size, &sub_hdr) || sub_size < sub_hdr) break;

                if (sub_type == FOURCC('t', 'r', 'a', 'k') && !found_audio) {
                    if (parse_trak(d, fp, sub_start + sub_size)) {
                        found_audio = true;
                    }
                }
                fseeko(fp, (off_t)(sub_start + sub_size), SEEK_SET);
            }
        }
        fseeko(fp, (off_t)end, SEEK_SET);
    }

    if (!found_audio || !d->dsi) {
        mp4_demux_close(d);
        return NULL;
    }

    // Standard unfragmented MP4 path
    if (d->sample_count > 0) {
        build_sample_lookup(d);
    } else {
        // Fragmented MP4 (DASH / fMP4) path
        parse_fragmented_mp4(d, fp);
    }

    if (d->sample_count == 0 || !d->sample_byte_offsets) {
        mp4_demux_close(d);
        return NULL;
    }

    return d;
}

void mp4_demux_close(Mp4Demuxer *d) {
    if (!d) return;
    if (d->dsi) free(d->dsi);
    if (d->sample_sizes) free(d->sample_sizes);
    if (d->chunk_offsets) free(d->chunk_offsets);
    if (d->stsc_table) free(d->stsc_table);
    if (d->stts_table) free(d->stts_table);
    if (d->sample_byte_offsets) free(d->sample_byte_offsets);
    if (d->sample_durations) free(d->sample_durations);
    free(d);
}

bool mp4_demux_get_sample_pos(const Mp4Demuxer *d, uint32_t sample_idx, uint64_t *out_offset, uint32_t *out_size) {
    if (!d || !d->sample_byte_offsets || sample_idx >= d->sample_count) return false;
    if (out_offset) *out_offset = d->sample_byte_offsets[sample_idx];
    if (out_size) {
        *out_size = d->const_sample_size ? d->const_sample_size : (d->sample_sizes ? d->sample_sizes[sample_idx] : 0);
    }
    return true;
}

uint32_t mp4_demux_frame_to_sample(const Mp4Demuxer *d, uint64_t target_frame) {
    if (!d || d->sample_count == 0) return 0;

    if (d->sample_durations) {
        uint64_t accum = 0;
        for (uint32_t i = 0; i < d->sample_count; i++) {
            accum += d->sample_durations[i];
            if (accum > target_frame) return i;
        }
        return d->sample_count - 1;
    }

    if (d->stts_table && d->stts_count > 0) {
        uint64_t accum_frames = 0;
        uint32_t accum_samples = 0;
        for (uint32_t i = 0; i < d->stts_count; i++) {
            uint64_t span = (uint64_t)d->stts_table[i].sample_count * d->stts_table[i].sample_delta;
            if (accum_frames + span > target_frame) {
                uint64_t rem = target_frame - accum_frames;
                uint32_t delta = d->stts_table[i].sample_delta ? d->stts_table[i].sample_delta : 1024;
                return accum_samples + (uint32_t)(rem / delta);
            }
            accum_frames += span;
            accum_samples += d->stts_table[i].sample_count;
        }
    }

    uint32_t est = (uint32_t)(target_frame / 1024);
    return (est < d->sample_count) ? est : (d->sample_count - 1);
}