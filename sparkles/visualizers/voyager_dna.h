#ifndef VOYAGER_DNA_H
#define VOYAGER_DNA_H

#include <stdbool.h>
#include <stdint.h>

#define VOYAGER_SECTOR_COUNT 16

typedef struct {
    float energy;   // 0.0 to 1.0 (loudness / power)
    float chaos;    // 0.0 to 1.0 (frequency flux, arpeggios, rapid note changes)
    float bass;     // 0.0 to 1.0 (low-end weight)
    float treble;   // 0.0 to 1.0 (high-end sheen / violin / air)
    bool ready;
} VoyagerSectorDNA;

void voyager_dna_init(void);
void voyager_dna_check_update(const char *filepath, uint32_t total_sec);
VoyagerSectorDNA voyager_dna_get_sector(int idx);
int  voyager_dna_get_ready_count(void);
bool voyager_dna_is_ready(void);

#endif // VOYAGER_DNA_H