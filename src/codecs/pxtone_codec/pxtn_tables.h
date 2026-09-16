#ifndef PXTONE_TABLES_H
#define PXTONE_TABLES_H

#include <stdint.h>
#include <stdbool.h>

void  pxtn_tables_init(void);
float pxtn_get_freq(int32_t key);
float pxtn_get_freq2(int32_t key);

const int16_t* pxtn_noise_get_table(int type);
const int16_t* pxtn_noise_get_rand_table(void);

#endif // PXTONE_TABLES_H