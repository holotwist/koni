#define _DEFAULT_SOURCE
#include "pxtn_tables.h"
#include "pxtn_types.h"
#include <math.h>
#include <stdlib.h>

static float g_freq_table[16 * 12 * 16];
static int16_t g_noise_tables[18][NOISE_TABLE_SIZE];
static int16_t g_noise_rand[44100];
static bool g_tables_inited = false;

static double get_divide_octave_rate(int32_t divi) {
    double parameter = 1.0;
    for (int32_t i = 0; i < 17; i++) {
        double add = 1.0;
        for (int32_t j = 0; j < i; j++) add *= 0.1;
        int32_t j = 0;
        for (; j < 10; j++) {
            double work = parameter + add * j;
            double result = 1.0;
            int32_t k = 0;
            for (; k < divi; k++) {
                result *= work;
                if (result >= 2.0) break;
            }
            if (k != divi) break;
        }
        parameter += add * (j - 1);
    }
    return parameter;
}

void pxtn_tables_init(void) {
    if (g_tables_inited) return;

    static const double oct_table[16] = {
        0.00390625, 0.0078125, 0.015625, 0.03125,
        0.0625,     0.125,     0.25,     0.5,
        1.0,        2.0,       4.0,      8.0,
        16.0,       32.0,      64.0,     128.0
    };

    double oct_x24 = get_divide_octave_rate(12 * 16);
    int total_keys = 16 * 12 * 16;

    for (int f = 0; f < total_keys; f++) {
        double work = oct_table[f / (12 * 16)];
        int key_steps = f % (12 * 16);
        for (int k = 0; k < key_steps; k++) {
            work *= oct_x24;
        }
        g_freq_table[f] = (float)work;
    }

    // Generate Pixel's Fibonacci PRNG for noise
    uint16_t rand_buf[2] = { 0x4444, 0x8888 };
    for (int s = 0; s < 44100; s++) {
        int32_t w1 = (int16_t)rand_buf[0] + (int16_t)rand_buf[1];
        int16_t w2 = (int16_t)(((w1 & 0xFF) << 8) | ((w1 >> 8) & 0xFF));
        rand_buf[1] = rand_buf[0];
        rand_buf[0] = (uint16_t)w2;
        g_noise_rand[s] = w2;
    }

    for (int s = 0; s < NOISE_TABLE_SIZE; s++) {
        double ph = (double)s / (double)NOISE_TABLE_SIZE;
        // Sine
        g_noise_tables[1][s] = (int16_t)(sin(ph * 2.0 * M_PI) * 32767.0);
        // Saw
        g_noise_tables[2][s] = (int16_t)((1.0 - 2.0 * ph) * 32767.0);
        // Rect (50%)
        g_noise_tables[3][s] = (s < NOISE_TABLE_SIZE / 2) ? 32767 : -32767;

        // Saw2 (16 band-limited harmonics)
        double saw2 = 0.0;
        for (int h = 1; h <= 16; h++) saw2 += sin(ph * 2.0 * M_PI * h) / (double)h;
        if (saw2 > 1.0) saw2 = 1.0; else if (saw2 < -1.0) saw2 = -1.0;
        g_noise_tables[5][s] = (int16_t)(saw2 * 32767.0);

        // Rect2 (8 odd band-limited harmonics)
        double rect2 = 0.0;
        for (int h = 1; h <= 15; h += 2) rect2 += sin(ph * 2.0 * M_PI * h) / (double)h;
        if (rect2 > 1.0) rect2 = 1.0; else if (rect2 < -1.0) rect2 = -1.0;
        g_noise_tables[6][s] = (int16_t)(rect2 * 32767.0);

        // Triangle
        if (s < NOISE_TABLE_SIZE / 4) {
            g_noise_tables[7][s] = (int16_t)((double)s / (NOISE_TABLE_SIZE / 4) * 32767.0);
        } else if (s < NOISE_TABLE_SIZE * 3 / 4) {
            g_noise_tables[7][s] = (int16_t)((1.0 - 2.0 * (double)(s - NOISE_TABLE_SIZE / 4) / (NOISE_TABLE_SIZE / 2)) * 32767.0);
        } else {
            g_noise_tables[7][s] = (int16_t)((-1.0 + (double)(s - NOISE_TABLE_SIZE * 3 / 4) / (NOISE_TABLE_SIZE / 4)) * 32767.0);
        }

        // Rect duty cycles
        g_noise_tables[9][s]  = (s < NOISE_TABLE_SIZE / 3) ? 32767 : -32767;
        g_noise_tables[10][s] = (s < NOISE_TABLE_SIZE / 4) ? 32767 : -32767;
        g_noise_tables[11][s] = (s < NOISE_TABLE_SIZE / 8) ? 32767 : -32767;
        g_noise_tables[12][s] = (s < NOISE_TABLE_SIZE / 16) ? 32767 : -32767;

        // Saw-3
        if (s < NOISE_TABLE_SIZE / 3) g_noise_tables[13][s] = 32767;
        else if (s < NOISE_TABLE_SIZE * 2 / 3) g_noise_tables[13][s] = 0;
        else g_noise_tables[13][s] = -32767;

        // Saw-4
        if (s < NOISE_TABLE_SIZE / 4) g_noise_tables[14][s] = 32767;
        else if (s < NOISE_TABLE_SIZE * 2 / 4) g_noise_tables[14][s] = 32767 / 3;
        else if (s < NOISE_TABLE_SIZE * 3 / 4) g_noise_tables[14][s] = -32767 / 3;
        else g_noise_tables[14][s] = -32767;

        // Saw-6
        int step6 = (s * 6) / NOISE_TABLE_SIZE;
        g_noise_tables[15][s] = (int16_t)(32767.0 - 32767.0 * 2.0 * step6 / 5.0);

        // Saw-8
        int step8 = (s * 8) / NOISE_TABLE_SIZE;
        g_noise_tables[16][s] = (int16_t)(32767.0 - 32767.0 * 2.0 * step8 / 7.0);
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