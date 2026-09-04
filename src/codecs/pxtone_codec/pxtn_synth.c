#define _DEFAULT_SOURCE
#include "pxtn_synth.h"
#include "pxtn_tables.h"
#include <stdlib.h>
#include <string.h>

static void reset_unit_tones(PxUnit *u, const PxVoice *v, float clock_rate) {
    for (int i = 0; i < v->num_units; i++) {
        const PxVoiceUnit *vu = &v->units[i];
        float tun = (vu->tuning != 0.0f) ? vu->tuning : 1.0f;
        float ofs_freq = pxtn_get_freq(0x4500 - vu->basic_key) * tun;

        u->tones[i].smp_pos = 0.0;
        u->tones[i].offset_freq = ofs_freq;
        u->tones[i].env_volume = vu->has_env ? 0 : 128;
        u->tones[i].life_count = 0;
        u->tones[i].on_count = 0;
        u->tones[i].env_start = vu->has_env ? 0 : 128;
        u->tones[i].env_pos = 0;
        u->tones[i].env_release_clock = clock_rate > 0 ? (int32_t)(vu->env_release / clock_rate) : 0;
    }
}

static void process_event(PxtnTiny *p, const PxEvent *e) {
    if (e->unit_no >= MAX_UNITS) return;
    if (e->unit_no >= p->unit_count) {
        p->unit_count = e->unit_no + 1;
    }
    PxUnit *u = &p->units[e->unit_no];

    switch (e->kind) {
        case PX_EVENT_ON: {
            int32_t on_count = (int32_t)(e->value * p->clock_rate);
            if (on_count <= 0) {
                for (int vi = 0; vi < 2; vi++) u->tones[vi].life_count = 0;
                break;
            }

            u->key_now = u->key_start + u->key_margin;
            u->key_start = u->key_now;
            u->key_margin = 0;

            if (u->voice_idx < 0 || u->voice_idx >= p->voice_count) break;
            const PxVoice *v = &p->voices[u->voice_idx];
            for (int vi = 0; vi < v->num_units; vi++) {
                PxVoiceTone *vt = &u->tones[vi];
                const PxVoiceUnit *vu = &v->units[vi];
                vt->life_count = on_count + (vu->has_env ? vu->env_release : 0);
                vt->on_count = on_count;
                vt->smp_pos = 0.0;
                vt->env_pos = 0;
                if (vu->has_env) {
                    vt->env_volume = 0;
                    vt->env_start = 0;
                } else {
                    vt->env_volume = 128;
                    vt->env_start = 128;
                }
            }
            break;
        }
        case PX_EVENT_KEY:
            u->key_start = u->key_now;
            u->key_margin = e->value - u->key_start;
            u->portamento_pos = 0;
            break;
        case PX_EVENT_PAN_VOLUME:
            if (e->value >= 64) {
                u->pan_vols[0] = 128 - e->value;
                u->pan_vols[1] = 64;
            } else {
                u->pan_vols[0] = 64;
                u->pan_vols[1] = e->value;
            }
            break;
        case PX_EVENT_PAN_TIME:
            if (e->value >= 64) {
                u->pan_times[0] = ((e->value - 64) * 44100) / p->dst_sps;
                u->pan_times[1] = 0;
            } else {
                u->pan_times[0] = 0;
                u->pan_times[1] = ((64 - e->value) * 44100) / p->dst_sps;
            }
            break;
        case PX_EVENT_VELOCITY:
            u->velocity = e->value;
            break;
        case PX_EVENT_VOLUME:
            u->volume = e->value;
            break;
        case PX_EVENT_PORTAMENTO:
            u->portamento_num = (int32_t)(e->value * p->clock_rate);
            break;
        case PX_EVENT_VOICENO: {
            int v_idx = e->value >= 0 ? e->value : (-e->value - 1);
            if (v_idx >= 0 && v_idx < p->voice_count) {
                u->voice_idx = v_idx;
                reset_unit_tones(u, &p->voices[u->voice_idx], p->clock_rate);
            }
            break;
        }
        case PX_EVENT_GROUPNO:
            u->group_no = (e->value >= 0 && e->value < MAX_GROUPS) ? e->value : 0;
            break;
        case PX_EVENT_TUNING:
            memcpy(&u->tuning, &e->value, 4);
            break;
    }
}

uint32_t pxtn_synth_render(PxtnTiny *p, int32_t *out_pcm, uint32_t max_frames) {
    if (!p || !out_pcm || max_frames == 0) return 0;
    uint32_t frames_rendered = 0;

    for (uint32_t f = 0; f < max_frames; f++) {
        if (p->cur_sample >= p->total_samples && p->total_samples > 0) break;

        int32_t cur_clock = (int32_t)(p->cur_sample / p->clock_rate);

        while (p->cur_event_idx < p->event_count && p->events[p->cur_event_idx].clock <= cur_clock) {
            process_event(p, &p->events[p->cur_event_idx]);
            p->cur_event_idx++;
        }

        // Envelope progression
        for (int u = 0; u < p->unit_count; u++) {
            if (p->units[u].voice_idx < 0 || p->units[u].voice_idx >= p->voice_count) continue;
            const PxVoice *v = &p->voices[p->units[u].voice_idx];
            for (int vi = 0; vi < v->num_units; vi++) {
                const PxVoiceUnit *vu = &v->units[vi];
                PxVoiceTone *vt = &p->units[u].tones[vi];
                if (vt->life_count > 0) {
                    if (vu->has_env && vu->p_env) {
                        if (vt->on_count > 0) {
                            if (vt->env_pos < vu->env_size) {
                                vt->env_volume = vu->p_env[vt->env_pos++];
                            }
                        } else if (vu->env_release > 0) {
                            vt->env_volume = vt->env_start - (vt->env_start * vt->env_pos / vu->env_release);
                            vt->env_pos++;
                        } else {
                            vt->life_count = 0;
                        }
                    } else {
                        vt->env_volume = 128;
                    }
                }
            }
        }

        // Timepan buffer accumulation
        for (int u = 0; u < p->unit_count; u++) {
            PxUnit *un = &p->units[u];
            if (un->voice_idx < 0 || un->voice_idx >= p->voice_count) continue;
            const PxVoice *v = &p->voices[un->voice_idx];

            for (int ch = 0; ch < 2; ch++) {
                int32_t time_pan_accum = 0;
                for (int vi = 0; vi < v->num_units; vi++) {
                    const PxVoiceUnit *vu = &v->units[vi];
                    PxVoiceTone *vt = &un->tones[vi];
                    if (vt->life_count > 0 && vu->smp_body_w > 0 && vu->p_smp_w && vt->smp_pos >= 0.0) {
                        int s_idx = (int)vt->smp_pos;
                        if (s_idx < vu->smp_body_w) {
                            int pos = s_idx * 2 + ch;
                            int32_t work = vu->p_smp_w[pos];
                            work = (work * un->velocity) / 128;
                            work = (work * un->volume) / 128;
                            work = (work * un->pan_vols[ch]) / 64;
                            if (vu->has_env) {
                                work = (work * vt->env_volume) / 128;
                            }
                            if (vu->smooth && vt->life_count < p->smp_smooth && p->smp_smooth > 0) {
                                work = (work * vt->life_count) / p->smp_smooth;
                            }
                            time_pan_accum += work;
                        }
                    }
                }
                un->pan_time_bufs[p->time_pan_idx][ch] = time_pan_accum;
            }
        }

        // Groups, Overdrive & Delays
        int32_t master_l = 0, master_r = 0;

        for (int ch = 0; ch < 2; ch++) {
            memset(p->group_smps, 0, sizeof(p->group_smps));

            for (int u = 0; u < p->unit_count; u++) {
                PxUnit *un = &p->units[u];
                int idx = (p->time_pan_idx - un->pan_times[ch]) & (TIMEPAN_BUF_SIZE - 1);
                p->group_smps[un->group_no] += un->pan_time_bufs[idx][ch];
            }

            for (int od = 0; od < p->ovdrv_count; od++) {
                PxOverDrive *o = &p->ovdrvs[od];
                int32_t w = p->group_smps[o->group];
                if (w > o->cut_top) w = o->cut_top;
                else if (w < -o->cut_top) w = -o->cut_top;
                p->group_smps[o->group] = (int32_t)((float)w * o->amp_f);
            }

            for (int d = 0; d < p->delay_count; d++) {
                PxDelay *del = &p->delays[d];
                if (del->smp_num > 0 && del->bufs[ch]) {
                    int ofs = del->offset % del->smp_num;
                    int32_t a = del->bufs[ch][ofs] * del->rate_pct / 100;
                    p->group_smps[del->group] += a;
                    del->bufs[ch][ofs] = p->group_smps[del->group];
                }
            }

            int32_t mix = 0;
            for (int g = 0; g < MAX_GROUPS; g++) mix += p->group_smps[g];
            if (mix > 32767) mix = 32767;
            else if (mix < -32768) mix = -32768;

            if (ch == 0) master_l = mix;
            else master_r = mix;
        }

        p->cur_sample++;
        p->time_pan_idx = (p->time_pan_idx + 1) & (TIMEPAN_BUF_SIZE - 1);

        for (int u = 0; u < p->unit_count; u++) {
            PxUnit *un = &p->units[u];
            if (un->portamento_num > 0 && un->key_margin != 0) {
                if (un->portamento_pos < un->portamento_num - 1) {
                    un->portamento_pos++;
                    un->key_now = (int32_t)(un->key_start + (double)un->key_margin * un->portamento_pos / un->portamento_num);
                } else {
                    un->key_now = un->key_start + un->key_margin;
                    un->key_start = un->key_now;
                    un->key_margin = 0;
                }
            } else if (un->portamento_num == 0) {
                un->key_now = un->key_start + un->key_margin;
                un->key_start = un->key_now;
                un->key_margin = 0;
            }

            if (un->voice_idx < 0 || un->voice_idx >= p->voice_count) continue;
            const PxVoice *v = &p->voices[un->voice_idx];

            for (int vi = 0; vi < v->num_units; vi++) {
                const PxVoiceUnit *vu = &v->units[vi];
                PxVoiceTone *vt = &un->tones[vi];
                if (vt->life_count > 0) {
                    vt->life_count--;
                    if (vt->on_count > 0) {
                        vt->on_count--;
                        if (vt->on_count == 0 && vu->has_env) {
                            vt->env_start = vt->env_volume;
                            vt->env_pos = 0;
                        }
                    }
                    float freq = pxtn_get_freq2(un->key_now) * p->smp_stride;
                    float u_tun = (un->tuning != 0.0f) ? un->tuning : 1.0f;
                    vt->smp_pos += (double)vt->offset_freq * (double)u_tun * (double)freq;

                    if (vt->smp_pos >= (double)vu->smp_body_w) {
                        if (vu->loop && vu->smp_body_w > 0) {
                            while (vt->smp_pos >= (double)vu->smp_body_w) {
                                vt->smp_pos -= (double)vu->smp_body_w;
                            }
                        } else {
                            vt->life_count = 0;
                        }
                    }
                }
            }
        }

        for (int d = 0; d < p->delay_count; d++) {
            if (p->delays[d].smp_num > 0) {
                if (++p->delays[d].offset >= p->delays[d].smp_num) p->delays[d].offset = 0;
            }
        }

        out_pcm[f * 2]     = master_l << 16;
        out_pcm[f * 2 + 1] = master_r << 16;
        frames_rendered++;
    }

    return frames_rendered;
}

bool pxtn_synth_seek(PxtnTiny *p, uint64_t target_sample) {
    if (!p) return false;
    if (target_sample > p->total_samples) target_sample = p->total_samples;

    p->cur_sample = 0;
    p->cur_event_idx = 0;
    p->time_pan_idx = 0;

    for (int u = 0; u < p->unit_count; u++) {
        p->units[u].key_now = 0x6000;
        p->units[u].key_start = 0x6000;
        p->units[u].key_margin = 0;
        p->units[u].portamento_pos = 0;
        memset(p->units[u].pan_time_bufs, 0, sizeof(p->units[u].pan_time_bufs));
        for (int vi = 0; vi < 2; vi++) {
            p->units[u].tones[vi].life_count = 0;
            p->units[u].tones[vi].on_count = 0;
            p->units[u].tones[vi].smp_pos = 0.0;
            p->units[u].tones[vi].env_pos = 0;
        }
    }

    for (int d = 0; d < p->delay_count; d++) {
        p->delays[d].offset = 0;
        if (p->delays[d].smp_num > 0 && p->delays[d].bufs[0]) {
            memset(p->delays[d].bufs[0], 0, sizeof(int32_t) * p->delays[d].smp_num);
            memset(p->delays[d].bufs[1], 0, sizeof(int32_t) * p->delays[d].smp_num);
        }
    }

    // Process non-note events up to target clock so state is correctly established
    int32_t target_clock = (int32_t)(target_sample / p->clock_rate);
    while (p->cur_event_idx < p->event_count && p->events[p->cur_event_idx].clock <= target_clock) {
        if (p->events[p->cur_event_idx].kind != PX_EVENT_ON) {
            process_event(p, &p->events[p->cur_event_idx]);
        }
        p->cur_event_idx++;
    }

    p->cur_sample = target_sample;
    return true;
}

void pxtn_synth_free(PxtnTiny *p) {
    if (!p) return;
    if (p->events) free(p->events);
    for (int i = 0; i < p->voice_count; i++) {
        for (int u = 0; u < p->voices[i].num_units; u++) {
            if (p->voices[i].units[u].p_smp_w) free(p->voices[i].units[u].p_smp_w);
            if (p->voices[i].units[u].p_env) free(p->voices[i].units[u].p_env);
        }
    }
    for (int i = 0; i < p->delay_count; i++) {
        if (p->delays[i].bufs[0]) free(p->delays[i].bufs[0]);
        if (p->delays[i].bufs[1]) free(p->delays[i].bufs[1]);
    }
    free(p);
}