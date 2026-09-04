#define _DEFAULT_SOURCE
#include "pxtn_tables.h"
#include "pxtn_types.h"
#include <math.h>
#include <stdlib.h>

static float g_freq_table[16 * 12 * 16];
static int16_t g_noise_tables[18][NOISE_TABLE_SIZE];
static int16_t g_noise_rand[44100];
static bool g_tables_inited = false;

void pxtn_tables_init(void) {
    if (g_tables_inited) return;

    double oct_x24 = pow(2.0, 1.0 / (12.0 * 16.0));
    double oct_val = 0.00390625; // 2^-8
    int idx = 0;
    for (int o = 0; o < 16; o++) {
        double w = oct_val;
        for (int k = 0; k < 12 * 16; k++) {
            g_freq_table[idx++] = (float)w;
            w *= oct_x24;
        }
        oct_val *= 2.0;
    }

    // Generate PxTone noise tables
    uint32_t rand_seed = 0x44448888;
    for (int s = 0; s < 44100; s++) {
        rand_seed = rand_seed * 1103515245 + 12345;
        g_noise_rand[s] = (int16_t)(rand_seed >> 16);
    }

    for (int s = 0; s < NOISE_TABLE_SIZE; s++) {
        double ph = (double)s / (double)NOISE_TABLE_SIZE;
        // Sine
        g_noise_tables[1][s] = (int16_t)(sin(ph * 2.0 * M_PI) * 32767.0);
        // Saw
        g_noise_tables[2][s] = (int16_t)((1.0 - 2.0 * ph) * 32767.0);
        // Rect
        g_noise_tables[3][s] = (s < NOISE_TABLE_SIZE / 2) ? 32767 : -32767;
        // Tri
        g_noise_tables[7][s] = (int16_t)((4.0 * fabs(ph - 0.5) - 1.0) * -32767.0);
        // Rect duty cycles
        g_noise_tables[9][s]  = (s < NOISE_TABLE_SIZE / 3) ? 32767 : -32767;
        g_noise_tables[10][s] = (s < NOISE_TABLE_SIZE / 4) ? 32767 : -32767;
        g_noise_tables[11][s] = (s < NOISE_TABLE_SIZE / 8) ? 32767 : -32767;
        g_noise_tables[12][s] = (s < NOISE_TABLE_SIZE / 16) ? 32767 : -32767;
    }

    g_tables_inited = true;
}

float pxtn_get_freq(int32_t key) {
    int32_t i = (key + 0x6000) * 16 / 0x100;
    if (i < 0) i = 0;
    else if (i >= 16 * 12 * 16) i = 16 * 12 * 16 - 1;
    return g_freq_table[i];
}

float pxtn_get_freq2(int32_t key) {
    int32_t i = key >> 4;
    if (i < 0) i = 0;
    else if (i >= 16 * 12 * 16) i = 16 * 12 * 16 - 1;
    return g_freq_table[i];
}

const int16_t* pxtn_noise_get_table(int type) {
    if (type < 0 || type >= 18) return g_noise_tables[1];
    return g_noise_tables[type];
}

const int16_t* pxtn_noise_get_rand_table(void) {
    return g_noise_rand;
}