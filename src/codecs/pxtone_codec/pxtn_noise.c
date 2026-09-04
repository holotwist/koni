#define _DEFAULT_SOURCE
#include "pxtn_noise.h"
#include "pxtn_tables.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    double inc;
    double offset;
    double volume;
    const int16_t *p_smp;
    bool b_rev;
    bool is_rand;
    bool is_rect_rand;
    int rdm_start;
    int rdm_margin;
    int rdm_idx;
} PxOscState;

static void setup_osc(PxOscState *st, const PxOsc *osc, uint32_t target_sps) {
    st->is_rand = (osc->type == 4 || osc->type == 8);
    st->is_rect_rand = (osc->type == 8);
    st->inc = (44100.0 / (double)target_sps) * (osc->freq / 100.0);
    st->offset = st->is_rand ? 0.0 : (double)NOISE_TABLE_SIZE * (osc->offset * 0.01);
    st->volume = osc->vol * 0.01;
    st->b_rev = osc->b_rev;
    st->p_smp = pxtn_noise_get_table(osc->type);

    const int16_t *rtbl = pxtn_noise_get_rand_table();
    st->rdm_idx = (int)(44100.0 * (osc->offset * 0.01)) % 44100;
    st->rdm_start = 0;
    st->rdm_margin = rtbl[st->rdm_idx];
}

static void advance_osc(PxOscState *st, double inc) {
    st->offset += inc;
    if (st->offset >= NOISE_TABLE_SIZE) {
        st->offset -= NOISE_TABLE_SIZE;
        if (st->offset >= NOISE_TABLE_SIZE) st->offset = 0.0;
        if (st->is_rand) {
            const int16_t *rtbl = pxtn_noise_get_rand_table();
            st->rdm_start = rtbl[st->rdm_idx];
            st->rdm_idx = (st->rdm_idx + 1) % 44100;
            st->rdm_margin = rtbl[st->rdm_idx] - st->rdm_start;
        }
    }
}

void pxtn_synth_noise(PxVoiceUnit *vu, PxNoiseUnit *units, int unit_num, int smp_num_44k, uint32_t target_sps) {
    if (smp_num_44k <= 0) smp_num_44k = 44100 / 2;
    int dst_len = (int)((double)smp_num_44k * target_sps / 44100.0);
    if (dst_len <= 0) dst_len = 1;

    vu->smp_body_w = dst_len;
    vu->p_smp_w = calloc(dst_len * 2, sizeof(int16_t));
    if (!vu->p_smp_w) return;

    for (int u = 0; u < unit_num; u++) {
        PxNoiseUnit *nu = &units[u];
        if (!nu->enable) continue;

        double pan[2] = { 1.0, 1.0 };
        if (nu->pan < 0) pan[1] = (100.0 + nu->pan) / 100.0;
        else if (nu->pan > 0) pan[0] = (100.0 - nu->pan) / 100.0;

        PxOscState osc_main, osc_freq, osc_volu;
        setup_osc(&osc_main, &nu->main_osc, target_sps);
        setup_osc(&osc_freq, &nu->freq_osc, target_sps);
        setup_osc(&osc_volu, &nu->vol_osc, target_sps);

        int env_idx = 0;
        double env_start = 0.0, env_margin = 0.0;
        int env_count = 0;

        while (env_idx < nu->env_num) {
            env_margin = (nu->enves[env_idx].y * 0.01) - env_start;
            if (nu->enves[env_idx].x > 0) break;
            env_start = nu->enves[env_idx].y * 0.01;
            env_idx++;
        }

        for (int s = 0; s < dst_len; s++) {
            // Main wave
            double work = 0.0;
            if (osc_main.is_rand) {
                if (osc_main.is_rect_rand) work = osc_main.rdm_start;
                else work = osc_main.rdm_start + osc_main.rdm_margin * osc_main.offset / (double)NOISE_TABLE_SIZE;
            } else {
                int ofs = (int)osc_main.offset % NOISE_TABLE_SIZE;
                work = osc_main.p_smp[ofs];
            }
            if (osc_main.b_rev) work = -work;
            work *= osc_main.volume;

            // Volume modulation
            double vol = 0.0;
            if (osc_volu.is_rand) {
                if (osc_volu.is_rect_rand) vol = osc_volu.rdm_start;
                else vol = osc_volu.rdm_start + osc_volu.rdm_margin * (int)osc_volu.offset / NOISE_TABLE_SIZE;
            } else {
                int ofs = (int)osc_volu.offset % NOISE_TABLE_SIZE;
                vol = osc_volu.p_smp[ofs];
            }
            if (osc_volu.b_rev) vol = -vol;
            vol *= osc_volu.volume;
            work = work * (vol + 32767.0) / (32767.0 * 2.0);

            // Envelope
            if (nu->env_num > 0 && env_idx < nu->env_num) {
                int smp_target = (int)((double)target_sps * nu->enves[env_idx].x / 1000.0);
                if (smp_target > 0) {
                    work *= env_start + (env_margin * env_count / smp_target);
                } else {
                    work *= env_start;
                }
            } else {
                work *= env_start;
            }

            // Route L/R
            for (int c = 0; c < 2; c++) {
                int32_t val = vu->p_smp_w[s * 2 + c] + (int32_t)(work * pan[c]);
                if (val > 32767) val = 32767;
                else if (val < -32768) val = -32768;
                vu->p_smp_w[s * 2 + c] = (int16_t)val;
            }

            // Frequency modulation & step
            double fre = 0.0;
            if (osc_freq.is_rand) {
                if (osc_freq.is_rect_rand) fre = osc_freq.rdm_start;
                else fre = osc_freq.rdm_start + osc_freq.rdm_margin * (int)osc_freq.offset / NOISE_TABLE_SIZE;
            } else {
                int ofs = (int)osc_freq.offset % NOISE_TABLE_SIZE;
                fre = (double)0x3200 * osc_freq.p_smp[ofs] / 32767.0;
            }
            if (osc_freq.b_rev) fre = -fre;
            fre *= osc_freq.volume;

            double mod = pxtn_get_freq((int32_t)fre);
            advance_osc(&osc_main, osc_main.inc * mod);
            advance_osc(&osc_freq, osc_freq.inc);
            advance_osc(&osc_volu, osc_volu.inc);

            // Advance envelope counter
            if (nu->env_num > 0 && env_idx < nu->env_num) {
                int smp_target = (int)((double)target_sps * nu->enves[env_idx].x / 1000.0);
                env_count++;
                if (env_count >= smp_target) {
                    env_count = 0;
                    env_start = nu->enves[env_idx].y * 0.01;
                    env_idx++;
                    if (env_idx < nu->env_num) {
                        env_margin = (nu->enves[env_idx].y * 0.01) - env_start;
                    }
                }
            }
        }
    }
}