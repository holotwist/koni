#define _DEFAULT_SOURCE
#include "pxtn_loader.h"
#include "pxtn_tables.h"
#include "pxtn_noise.h"
#include "miniaudio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct {
    const uint8_t *data;
    size_t size;
    size_t cur;
} MemReader;

static bool mr_read(MemReader *mr, void *dst, size_t sz) {
    if (mr->cur + sz > mr->size) return false;
    memcpy(dst, mr->data + mr->cur, sz);
    mr->cur += sz;
    return true;
}

static bool mr_seek(MemReader *mr, long offset, int whence) {
    size_t ncur = mr->cur;
    if (whence == SEEK_SET) ncur = (size_t)offset;
    else if (whence == SEEK_CUR) ncur += offset;
    else if (whence == SEEK_END) ncur = mr->size + offset;
    if (ncur > mr->size) return false;
    mr->cur = ncur;
    return true;
}

static bool mr_read_var(MemReader *mr, int32_t *out) {
    uint8_t a[5];
    int count = 0;
    for (count = 0; count < 5; count++) {
        if (!mr_read(mr, &a[count], 1)) return false;
        if (!(a[count] & 0x80)) break;
    }
    switch (count) {
        case 0: *out = a[0]; break;
        case 1: *out = (a[0] & 0x7F) | (a[1] << 7); break;
        case 2: *out = (a[0] & 0x7F) | ((a[1] & 0x7F) << 7) | (a[2] << 14); break;
        case 3: *out = (a[0] & 0x7F) | ((a[1] & 0x7F) << 7) | ((a[2] & 0x7F) << 14) | (a[3] << 21); break;
        case 4: *out = (a[0] & 0x7F) | ((a[1] & 0x7F) << 7) | ((a[2] & 0x7F) << 14) | ((a[3] & 0x7F) << 21) | (a[4] << 28); break;
        default: return false;
    }
    return true;
}

static int compare_events(const void *a, const void *b) {
    const PxEvent *ea = (const PxEvent*)a;
    const PxEvent *eb = (const PxEvent*)b;
    if (ea->clock != eb->clock) return ea->clock < eb->clock ? -1 : 1;
    static const int prio[16] = { 0, 50, 40, 60, 70, 80, 30, 0, 0, 0, 0, 255, 10, 20, 90, 100 };
    int pa = ea->kind < 16 ? prio[ea->kind] : 50;
    int pb = eb->kind < 16 ? prio[eb->kind] : 50;
    return pa - pb;
}

static void convert_pcm(PxVoiceUnit *vu, const uint8_t *src_pcm, uint32_t ch, uint32_t bps, uint32_t sps, uint32_t data_size, uint32_t target_sps) {
    if (!src_pcm || ch == 0 || bps == 0 || sps == 0 || data_size == 0) return;
    uint32_t bytes_per_frame = (bps / 8) * ch;
    uint32_t src_frames = data_size / bytes_per_frame;
    if (src_frames == 0) return;

    uint32_t dst_frames = (uint32_t)((uint64_t)src_frames * target_sps / sps);
    if (dst_frames == 0) dst_frames = 1;

    vu->p_smp_w = malloc(dst_frames * 4);
    if (!vu->p_smp_w) return;
    vu->smp_body_w = dst_frames;

    for (uint32_t d = 0; d < dst_frames; d++) {
        uint32_t s_idx = (uint32_t)((uint64_t)d * sps / target_sps);
        if (s_idx >= src_frames) s_idx = src_frames - 1;

        int16_t l = 0, r = 0;
        if (bps == 8) {
            const uint8_t *sp = src_pcm + (s_idx * ch);
            l = (int16_t)(((int32_t)sp[0] - 128) << 8);
            r = (ch > 1) ? (int16_t)(((int32_t)sp[1] - 128) << 8) : l;
        } else {
            const int16_t *sp = (const int16_t*)(src_pcm + (s_idx * bytes_per_frame));
            l = sp[0];
            r = (ch > 1) ? sp[1] : l;
        }
        vu->p_smp_w[d * 2]     = l;
        vu->p_smp_w[d * 2 + 1] = r;
    }
}

static void decode_ogg(const uint8_t *ogg_data, size_t size, PxVoiceUnit *vu, uint32_t target_sps) {
    ma_decoder_config config = ma_decoder_config_init(ma_format_s16, 2, target_sps);
    ma_decoder dec;
    if (ma_decoder_init_memory(ogg_data, size, &config, &dec) != MA_SUCCESS) return;

    ma_uint64 total_frames = 0;
    ma_decoder_get_length_in_pcm_frames(&dec, &total_frames);
    if (total_frames > 0) {
        vu->p_smp_w = malloc((size_t)total_frames * 4);
        if (vu->p_smp_w) {
            ma_uint64 read_frames = 0;
            ma_decoder_read_pcm_frames(&dec, vu->p_smp_w, total_frames, &read_frames);
            vu->smp_body_w = (int32_t)read_frames;
        }
    }
    ma_decoder_uninit(&dec);
}

static void synth_ptv_wave(PxVoiceUnit *vu, int wave_type, PxPoint *pts, int pt_num, int reso, int volume, int pan) {
    vu->smp_body_w = 400;
    vu->p_smp_w = malloc(vu->smp_body_w * 4);
    if (!vu->p_smp_w) return;

    int pan_l = 64, pan_r = 64;
    if (pan > 64) pan_l = 128 - pan;
    else if (pan < 64) pan_r = pan;

    for (int s = 0; s < vu->smp_body_w; s++) {
        double osc = 0.0;
        if (wave_type == 1) { // Overtone
            for (int p = 0; p < pt_num; p++) {
                if (pts[p].x == 0) continue;
                double sss = 2.0 * M_PI * (double)pts[p].x * (double)s / (double)vu->smp_body_w;
                osc += (sin(sss) * (double)pts[p].y / ((double)pts[p].x * 128.0));
            }
            osc = osc * (double)volume / 128.0;
        } else { // Coordinate
            int i = (reso > 0) ? (reso * s / vu->smp_body_w) : 0;
            int c = 0;
            while (c < pt_num && pts[c].x <= i) c++;
            int x1, y1, x2, y2;
            if (c == pt_num) {
                x1 = pt_num > 0 ? pts[pt_num - 1].x : 0;
                y1 = pt_num > 0 ? pts[pt_num - 1].y : 0;
                x2 = reso;
                y2 = pt_num > 0 ? pts[0].y : 0;
            } else if (c > 0) {
                x1 = pts[c - 1].x; y1 = pts[c - 1].y;
                x2 = pts[c].x;     y2 = pts[c].y;
            } else {
                x1 = pts[0].x; y1 = pts[0].y;
                x2 = pts[0].x; y2 = pts[0].y;
            }
            int w = x2 - x1;
            int h = y2 - y1;
            if (w != 0) {
                osc = (double)y1 + (double)h * (double)(i - x1) / (double)w;
            } else {
                osc = (double)y1;
            }
            osc = osc * (double)volume / (128.0 * 128.0);
        }

        if (osc > 1.0) osc = 1.0;
        if (osc < -1.0) osc = -1.0;

        vu->p_smp_w[s * 2]     = (int16_t)(osc * (double)pan_l / 64.0 * 32767.0);
        vu->p_smp_w[s * 2 + 1] = (int16_t)(osc * (double)pan_r / 64.0 * 32767.0);
    }
}

static void build_envelope(PxVoiceUnit *vu, PxPoint *pts, int head_num, int tail_num, int fps, uint32_t sps) {
    if (head_num <= 0 || fps <= 0) return;
    int size = 0;
    for (int i = 0; i < head_num; i++) size += pts[i].x;
    vu->env_size = (int32_t)((double)size * sps / fps);
    if (vu->env_size <= 0) vu->env_size = 1;
    vu->p_env = calloc(vu->env_size, 1);
    if (!vu->p_env) return;

    PxPoint *conv = malloc(sizeof(PxPoint) * head_num);
    if (!conv) { free(vu->p_env); vu->p_env = NULL; return; }

    int offset = 0;
    for (int i = 0; i < head_num; i++) {
        offset += (int32_t)((double)pts[i].x * sps / fps);
        conv[i].x = offset;
        conv[i].y = pts[i].y;
    }

    int e = 0;
    int start_x = 0, start_y = 0;
    for (int s = 0; s < vu->env_size; s++) {
        while (e < head_num && s >= conv[e].x) {
            start_x = conv[e].x;
            start_y = conv[e].y;
            e++;
        }
        if (e < head_num && conv[e].x > start_x) {
            vu->p_env[s] = (uint8_t)(start_y + (conv[e].y - start_y) * (s - start_x) / (conv[e].x - start_x));
        } else {
            vu->p_env[s] = (uint8_t)start_y;
        }
    }
    free(conv);

    if (tail_num > 0) {
        vu->env_release = (int32_t)((double)pts[head_num].x * sps / fps);
    }
    vu->has_env = true;
}

PxtnTiny* pxtn_load_file(const char *filepath, uint32_t target_sps) {
    pxtn_tables_init();

    FILE *f = fopen(filepath, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long fsz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (fsz < 16) { fclose(f); return NULL; }

    uint8_t *file_data = malloc(fsz);
    if (!file_data) { fclose(f); return NULL; }
    if (fread(file_data, 1, fsz, f) != (size_t)fsz) {
        free(file_data);
        fclose(f);
        return NULL;
    }
    fclose(f);

    MemReader mr = { .data = file_data, .size = (size_t)fsz, .cur = 0 };
    char header[16];
    if (!mr_read(&mr, header, 16)) { free(file_data); return NULL; }

    if (memcmp(header, "PTCOLLAGE", 9) != 0 && memcmp(header, "PTTUNE", 6) != 0) {
        free(file_data);
        return NULL;
    }

    uint16_t exe_ver = 0, dummy = 0;
    mr_read(&mr, &exe_ver, 2);
    mr_read(&mr, &dummy, 2);

    PxtnTiny *p = calloc(1, sizeof(PxtnTiny));
    if (!p) { free(file_data); return NULL; }

    p->dst_sps = target_sps;
    p->info.sample_rate = target_sps;
    p->info.channels = 2;
    p->info.beat_tempo = 120.0f;
    p->info.beat_num = 4;
    int beat_clock = 480;
    int meas_num = 1;

    int events_cap = 16384;
    p->events = malloc(sizeof(PxEvent) * events_cap);
    if (!p->events) { free(file_data); free(p); return NULL; }

    char tag[8];
    while (mr_read(&mr, tag, 8)) {
        if (memcmp(tag, "MasterV5", 8) == 0) {
            uint32_t sz = 0;
            int16_t bclock = 0;
            int8_t bnum = 0;
            float btempo = 0;
            int32_t clock_rep = 0, clock_last = 0;
            mr_read(&mr, &sz, 4);
            mr_read(&mr, &bclock, 2);
            mr_read(&mr, &bnum, 1);
            mr_read(&mr, &btempo, 4);
            mr_read(&mr, &clock_rep, 4);
            mr_read(&mr, &clock_last, 4);
            p->info.beat_tempo = btempo > 0 ? btempo : 120.0f;
            p->info.beat_num = bnum > 0 ? bnum : 4;
            beat_clock = bclock > 0 ? bclock : 480;
            if (clock_last > 0 && bnum > 0 && beat_clock > 0) {
                meas_num = clock_last / (bnum * beat_clock);
            }
        } else if (memcmp(tag, "evenMAST", 8) == 0) {
            int32_t sz = 0;
            uint16_t data_num = 0, rrr = 0;
            uint32_t num = 0;
            mr_read(&mr, &sz, 4);
            mr_read(&mr, &data_num, 2);
            mr_read(&mr, &rrr, 2);
            mr_read(&mr, &num, 4);
            for (uint32_t e = 0; e < num; e++) {
                int32_t st = 0, clk = 0, vol = 0;
                mr_read_var(&mr, &st);
                mr_read_var(&mr, &clk);
                mr_read_var(&mr, &vol);
                if (st == PX_EVENT_BEATCLOCK && vol > 0) beat_clock = vol;
                else if (st == PX_EVENT_BEATNUM && vol > 0) p->info.beat_num = vol;
                else if (st == PX_EVENT_BEATTEMPO) memcpy(&p->info.beat_tempo, &vol, 4);
                else if (st == PX_EVENT_LAST && p->info.beat_num > 0 && beat_clock > 0) {
                    meas_num = clk / (p->info.beat_num * beat_clock);
                }
            }
        } else if (memcmp(tag, "Event V5", 8) == 0) {
            int32_t sz = 0, num = 0;
            mr_read(&mr, &sz, 4);
            mr_read(&mr, &num, 4);
            if (num > 0 && num < 500000) {
                if (p->event_count + num > events_cap) {
                    events_cap = p->event_count + num + 2048;
                    PxEvent *new_events = realloc(p->events, sizeof(PxEvent) * events_cap);
                    if (new_events) p->events = new_events;
                }
                int32_t abs_clock = 0;
                for (int e = 0; e < num; e++) {
                    int32_t clk = 0, val = 0;
                    uint8_t uno = 0, k = 0;
                    mr_read_var(&mr, &clk);
                    mr_read(&mr, &uno, 1);
                    mr_read(&mr, &k, 1);
                    mr_read_var(&mr, &val);
                    abs_clock += clk;
                    if (p->event_count < events_cap) {
                        p->events[p->event_count].clock = abs_clock;
                        p->events[p->event_count].unit_no = uno;
                        p->events[p->event_count].kind = k;
                        p->events[p->event_count].value = val;
                        p->event_count++;
                    }
                }
            }
        } else if (memcmp(tag, "evenUNIT", 8) == 0) {
            int32_t sz = 0;
            uint16_t uno = 0, ekind = 0, dnum = 0, rrr = 0;
            uint32_t num = 0;
            mr_read(&mr, &sz, 4);
            mr_read(&mr, &uno, 2);
            mr_read(&mr, &ekind, 2);
            mr_read(&mr, &dnum, 2);
            mr_read(&mr, &rrr, 2);
            mr_read(&mr, &num, 4);
            if (p->event_count + (int)num > events_cap) {
                events_cap = p->event_count + (int)num + 2048;
                PxEvent *new_events = realloc(p->events, sizeof(PxEvent) * events_cap);
                if (new_events) p->events = new_events;
            }
            int32_t abs_clock = 0;
            for (uint32_t e = 0; e < num; e++) {
                int32_t clk = 0, val = 0;
                mr_read_var(&mr, &clk);
                mr_read_var(&mr, &val);
                abs_clock += clk;
                if (p->event_count < events_cap) {
                    p->events[p->event_count].clock = abs_clock;
                    p->events[p->event_count].unit_no = (uint8_t)uno;
                    p->events[p->event_count].kind = (uint8_t)ekind;
                    p->events[p->event_count].value = val;
                    p->event_count++;
                }
            }
        } else if (memcmp(tag, "num UNIT", 8) == 0) {
            int32_t sz = 0;
            int16_t num = 0, rrr = 0;
            mr_read(&mr, &sz, 4);
            mr_read(&mr, &num, 2);
            mr_read(&mr, &rrr, 2);
            p->unit_count = (num <= MAX_UNITS) ? num : MAX_UNITS;
            for (int u = 0; u < p->unit_count; u++) {
                p->units[u].velocity = 104;
                p->units[u].volume = 104;
                p->units[u].tuning = 1.0f;
                p->units[u].key_now = 0x6000;
                p->units[u].key_start = 0x6000;
                p->units[u].pan_vols[0] = 64;
                p->units[u].pan_vols[1] = 64;
            }
        } else if (memcmp(tag, "matePCM ", 8) == 0) {
            uint32_t sz = 0;
            mr_read(&mr, &sz, 4);
            size_t start = mr.cur;
            uint16_t x3x_uno = 0, bkey = 0;
            uint32_t vflags = 0;
            uint16_t ch = 0, bps = 0;
            uint32_t sps = 0;
            float tuning = 1.0f;
            uint32_t data_sz = 0;

            mr_read(&mr, &x3x_uno, 2);
            mr_read(&mr, &bkey, 2);
            mr_read(&mr, &vflags, 4);
            mr_read(&mr, &ch, 2);
            mr_read(&mr, &bps, 2);
            mr_read(&mr, &sps, 4);
            mr_read(&mr, &tuning, 4);
            mr_read(&mr, &data_sz, 4);

            if (p->voice_count < MAX_VOICES && data_sz > 0 && mr.cur + data_sz <= mr.size) {
                PxVoice *v = &p->voices[p->voice_count++];
                v->num_units = 1;
                v->units[0].basic_key = bkey;
                v->units[0].tuning = tuning;
                v->units[0].loop = (vflags & 0x01) != 0;
                v->units[0].smooth = (vflags & 0x02) != 0;
                convert_pcm(&v->units[0], mr.data + mr.cur, ch, bps, sps, data_sz, target_sps);
            }
            mr_seek(&mr, start + sz, SEEK_SET);
        } else if (memcmp(tag, "matePTN ", 8) == 0) {
            int32_t sz = 0;
            mr_read(&mr, &sz, 4);
            size_t start = mr.cur;

            uint16_t x3x_uno = 0, bkey = 0;
            uint32_t vflags = 0;
            float tuning = 1.0f;
            int32_t rrr = 0;
            mr_read(&mr, &x3x_uno, 2);
            mr_read(&mr, &bkey, 2);
            mr_read(&mr, &vflags, 4);
            mr_read(&mr, &tuning, 4);
            mr_read(&mr, &rrr, 4);

            char ptn_code[8];
            mr_read(&mr, ptn_code, 8);
            if (memcmp(ptn_code, "PTNOISE-", 8) == 0 && p->voice_count < MAX_VOICES) {
                uint32_t ver = 0;
                int32_t smp_num_44k = 0;
                char unit_num = 0;
                mr_read(&mr, &ver, 4);
                mr_read_var(&mr, &smp_num_44k);
                mr_read(&mr, &unit_num, 1);

                PxNoiseUnit nu_list[16] = {0};
                int valid_nu = (unit_num <= 16) ? unit_num : 16;

                for (int u = 0; u < valid_nu; u++) {
                    PxNoiseUnit *nu = &nu_list[u];
                    nu->enable = true;
                    int32_t nflags = 0;
                    mr_read_var(&mr, &nflags);

                    if (nflags & 0x04) {
                        mr_read_var(&mr, &nu->env_num);
                        int read_cnt = nu->env_num < 16 ? nu->env_num : 16;
                        for (int e = 0; e < read_cnt; e++) {
                            mr_read_var(&mr, &nu->enves[e].x);
                            mr_read_var(&mr, &nu->enves[e].y);
                        }
                        for (int e = read_cnt; e < nu->env_num; e++) {
                            int32_t dx = 0, dy = 0;
                            mr_read_var(&mr, &dx);
                            mr_read_var(&mr, &dy);
                        }
                    }
                    if (nflags & 0x08) {
                        int8_t pan_b = 0;
                        mr_read(&mr, &pan_b, 1);
                        nu->pan = pan_b;
                    }
                    if (nflags & 0x10) {
                        int32_t t = 0, rev = 0, fq = 0, vo = 0, of = 0;
                        mr_read_var(&mr, &t); mr_read_var(&mr, &rev);
                        mr_read_var(&mr, &fq); mr_read_var(&mr, &vo); mr_read_var(&mr, &of);
                        nu->main_osc.type = t; nu->main_osc.b_rev = rev ? true : false;
                        nu->main_osc.freq = fq * 0.1f; nu->main_osc.vol = vo * 0.1f; nu->main_osc.offset = of * 0.1f;
                    }
                    if (nflags & 0x20) {
                        int32_t t = 0, rev = 0, fq = 0, vo = 0, of = 0;
                        mr_read_var(&mr, &t); mr_read_var(&mr, &rev);
                        mr_read_var(&mr, &fq); mr_read_var(&mr, &vo); mr_read_var(&mr, &of);
                        nu->freq_osc.type = t; nu->freq_osc.b_rev = rev ? true : false;
                        nu->freq_osc.freq = fq * 0.1f; nu->freq_osc.vol = vo * 0.1f; nu->freq_osc.offset = of * 0.1f;
                    }
                    if (nflags & 0x40) {
                        int32_t t = 0, rev = 0, fq = 0, vo = 0, of = 0;
                        mr_read_var(&mr, &t); mr_read_var(&mr, &rev);
                        mr_read_var(&mr, &fq); mr_read_var(&mr, &vo); mr_read_var(&mr, &of);
                        nu->vol_osc.type = t; nu->vol_osc.b_rev = rev ? true : false;
                        nu->vol_osc.freq = fq * 0.1f; nu->vol_osc.vol = vo * 0.1f; nu->vol_osc.offset = of * 0.1f;
                    }
                }

                PxVoice *v = &p->voices[p->voice_count++];
                v->num_units = 1;
                v->units[0].basic_key = bkey;
                v->units[0].tuning = tuning;
                v->units[0].loop = (vflags & 0x01) != 0;
                v->units[0].smooth = (vflags & 0x02) != 0;
                pxtn_synth_noise(&v->units[0], nu_list, valid_nu, smp_num_44k, target_sps);
            }
            mr_seek(&mr, start + sz, SEEK_SET);
        } else if (memcmp(tag, "mateOGGV", 8) == 0) {
            uint32_t sz = 0;
            mr_read(&mr, &sz, 4);
            size_t start = mr.cur;
            uint16_t xxx = 0, bkey = 0;
            uint32_t vflags = 0;
            float tuning = 1.0f;
            mr_read(&mr, &xxx, 2);
            mr_read(&mr, &bkey, 2);
            mr_read(&mr, &vflags, 4);
            mr_read(&mr, &tuning, 4);
            int32_t ogg_sz = sz - 16;
            if (p->voice_count < MAX_VOICES && ogg_sz > 0) {
                PxVoice *v = &p->voices[p->voice_count++];
                v->num_units = 1;
                v->units[0].basic_key = bkey;
                v->units[0].tuning = tuning;
                v->units[0].loop = (vflags & 0x01) != 0;
                v->units[0].smooth = (vflags & 0x02) != 0;
                decode_ogg(mr.data + mr.cur, ogg_sz, &v->units[0], target_sps);
            }
            mr_seek(&mr, start + sz, SEEK_SET);
        } else if (memcmp(tag, "matePTV ", 8) == 0) {
            uint32_t sz = 0;
            mr_read(&mr, &sz, 4);
            size_t start = mr.cur;
            uint16_t uno = 0, rrr = 0;
            float x3x_t = 0;
            int32_t ptv_sz = 0;
            mr_read(&mr, &uno, 2);
            mr_read(&mr, &rrr, 2);
            mr_read(&mr, &x3x_t, 4);
            mr_read(&mr, &ptv_sz, 4);

            char ptv_code[8];
            mr_read(&mr, ptv_code, 8);
            if (memcmp(ptv_code, "PTVOICE-", 8) == 0 && p->voice_count < MAX_VOICES) {
                uint32_t ptv_ver = 0;
                int32_t tot = 0, dummy_v = 0, sub_units = 0;
                mr_read(&mr, &ptv_ver, 4);
                mr_read(&mr, &tot, 4);
                mr_read_var(&mr, &dummy_v);
                mr_read_var(&mr, &dummy_v);
                mr_read_var(&mr, &dummy_v);
                mr_read_var(&mr, &sub_units);

                PxVoice *v = &p->voices[p->voice_count++];
                v->num_units = (sub_units <= 2) ? sub_units : 2;
                for (int u = 0; u < v->num_units; u++) {
                    int32_t bkey = 0, vol = 128, pan = 64, tun_bits = 0, vflags = 0, dflags = 0;
                    mr_read_var(&mr, &bkey);
                    mr_read_var(&mr, &vol);
                    mr_read_var(&mr, &pan);
                    mr_read_var(&mr, &tun_bits);
                    mr_read_var(&mr, &vflags);
                    mr_read_var(&mr, &dflags);

                    v->units[u].basic_key = bkey;
                    memcpy(&v->units[u].tuning, &tun_bits, 4);
                    // PTV coordinate/overtone synth waves are pitched waveforms that loop
                    v->units[u].loop = ((vflags & 0x01) != 0) || (dflags & 0x01);
                    v->units[u].smooth = (vflags & 0x02) != 0;

                    if (dflags & 0x01) { // Wave
                        int32_t wtype = 0, num_pts = 0, reso = 0;
                        mr_read_var(&mr, &wtype);
                        mr_read_var(&mr, &num_pts);
                        mr_read_var(&mr, &reso);
                        PxPoint pts[128];
                        int read_p = num_pts < 128 ? num_pts : 128;
                        for (int i = 0; i < read_p; i++) {
                            if (wtype == 0) {
                                uint8_t px = 0; int8_t py = 0;
                                mr_read(&mr, &px, 1);
                                mr_read(&mr, &py, 1);
                                pts[i].x = px; pts[i].y = py;
                            } else {
                                int32_t px = 0, py = 0;
                                mr_read_var(&mr, &px);
                                mr_read_var(&mr, &py);
                                pts[i].x = px; pts[i].y = py;
                            }
                        }
                        for (int i = read_p; i < num_pts; i++) {
                            if (wtype == 0) { uint8_t px; int8_t py; mr_read(&mr, &px, 1); mr_read(&mr, &py, 1); }
                            else { int32_t px, py; mr_read_var(&mr, &px); mr_read_var(&mr, &py); }
                        }
                        synth_ptv_wave(&v->units[u], wtype, pts, read_p, reso, vol, pan);
                    }

                    if (dflags & 0x02) { // Envelope
                        int32_t fps = 0, head = 0, body = 0, tail = 0;
                        mr_read_var(&mr, &fps);
                        mr_read_var(&mr, &head);
                        mr_read_var(&mr, &body);
                        mr_read_var(&mr, &tail);
                        int tot_pts = head + body + tail;
                        PxPoint epts[64];
                        int read_e = tot_pts < 64 ? tot_pts : 64;
                        for (int i = 0; i < read_e; i++) {
                            int32_t ex = 0, ey = 0;
                            mr_read_var(&mr, &ex);
                            mr_read_var(&mr, &ey);
                            epts[i].x = ex; epts[i].y = ey;
                        }
                        for (int i = read_e; i < tot_pts; i++) {
                            int32_t ex, ey; mr_read_var(&mr, &ex); mr_read_var(&mr, &ey);
                        }
                        build_envelope(&v->units[u], epts, head, tail, fps, target_sps);
                    }
                }
            }
            mr_seek(&mr, start + sz, SEEK_SET);
        } else if (memcmp(tag, "effeDELA", 8) == 0) {
            uint32_t sz = 0;
            mr_read(&mr, &sz, 4);
            if (p->delay_count < MAX_DELAYS) {
                uint16_t unit = 0, group = 0;
                float rate = 0, freq = 0;
                mr_read(&mr, &unit, 2);
                mr_read(&mr, &group, 2);
                mr_read(&mr, &rate, 4);
                mr_read(&mr, &freq, 4);

                PxDelay *d = &p->delays[p->delay_count++];
                d->group = group < MAX_GROUPS ? group : 0;
                d->rate_pct = (int)rate;
                if (freq > 0.0f) {
                    if (unit == 0) d->smp_num = (int)(target_sps * 60.0f / p->info.beat_tempo / freq);
                    else if (unit == 1) d->smp_num = (int)(target_sps * 60.0f * p->info.beat_num / p->info.beat_tempo / freq);
                    else d->smp_num = (int)(target_sps / freq);
                }
                if (d->smp_num > 0) {
                    d->bufs[0] = calloc(d->smp_num, sizeof(int32_t));
                    d->bufs[1] = calloc(d->smp_num, sizeof(int32_t));
                }
            }
        } else if (memcmp(tag, "effeOVER", 8) == 0) {
            uint32_t sz = 0;
            mr_read(&mr, &sz, 4);
            if (p->ovdrv_count < MAX_OVERDRIVES) {
                uint16_t xxx = 0, grp = 0;
                float cut = 0, amp = 0, yyy = 0;
                mr_read(&mr, &xxx, 2);
                mr_read(&mr, &grp, 2);
                mr_read(&mr, &cut, 4);
                mr_read(&mr, &amp, 4);
                mr_read(&mr, &yyy, 4);

                PxOverDrive *od = &p->ovdrvs[p->ovdrv_count++];
                od->group = grp < MAX_GROUPS ? grp : 0;
                od->cut_f = cut;
                od->amp_f = amp;
                od->cut_top = (int32_t)(32767.0f * (100.0f - cut) / 100.0f);
            }
        } else if (memcmp(tag, "textNAME", 8) == 0) {
            int32_t sz = 0;
            mr_read(&mr, &sz, 4);
            if (sz > 0 && sz < (int)sizeof(p->info.name)) {
                mr_read(&mr, p->info.name, sz);
                p->info.name[sz] = '\0';
            } else if (sz > 0) mr_seek(&mr, sz, SEEK_CUR);
        } else if (memcmp(tag, "textCOMM", 8) == 0) {
            int32_t sz = 0;
            mr_read(&mr, &sz, 4);
            if (sz > 0 && sz < (int)sizeof(p->info.comment)) {
                mr_read(&mr, p->info.comment, sz);
                p->info.comment[sz] = '\0';
            } else if (sz > 0) mr_seek(&mr, sz, SEEK_CUR);
        } else if (memcmp(tag, "pxtoneND", 8) == 0) {
            break;
        } else {
            uint32_t sz = 0;
            if (mr_read(&mr, &sz, 4)) mr_seek(&mr, sz, SEEK_CUR);
            else break;
        }
    }

    free(file_data);

    if (p->event_count > 1) {
        qsort(p->events, p->event_count, sizeof(PxEvent), compare_events);
    }

    // Auto-detect max unit ID if not given
    int max_u = 0;
    for (int i = 0; i < p->event_count; i++) {
        if (p->events[i].unit_no > max_u) max_u = p->events[i].unit_no;
    }
    if (p->unit_count < max_u + 1) {
        p->unit_count = (max_u + 1 <= MAX_UNITS) ? (max_u + 1) : MAX_UNITS;
    }
    if (p->unit_count == 0) p->unit_count = 1;

    for (int u = 0; u < p->unit_count; u++) {
        p->units[u].velocity = 104;
        p->units[u].volume = 104;
        p->units[u].tuning = 1.0f;
        p->units[u].key_now = 0x6000;
        p->units[u].key_start = 0x6000;
        p->units[u].pan_vols[0] = 64;
        p->units[u].pan_vols[1] = 64;
        int v_idx = (u < p->voice_count) ? u : 0;
        p->units[u].voice_idx = v_idx;
    }

    p->clock_rate = (float)(60.0 * (double)target_sps / ((double)p->info.beat_tempo * (double)beat_clock));
    p->smp_stride = 44100.0f / (float)target_sps;
    p->smp_smooth = target_sps / 250;

    int max_clock = 0;
    for (int i = 0; i < p->event_count; i++) {
        int end_c = p->events[i].clock + ((p->events[i].kind == PX_EVENT_ON) ? p->events[i].value : 0);
        if (end_c > max_clock) max_clock = end_c;
    }
    int m_clock = meas_num * p->info.beat_num * beat_clock;
    if (m_clock > max_clock) max_clock = m_clock;

    p->total_samples = (uint64_t)((double)max_clock * p->clock_rate);
    p->info.total_samples = (uint32_t)p->total_samples;
    p->info.duration_sec = p->total_samples / target_sps;

    return p;
}