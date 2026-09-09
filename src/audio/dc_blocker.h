#ifndef DC_BLOCKER_H
#define DC_BLOCKER_H

#include <stdint.h>
#include <stdbool.h>

#define DC_BLOCKER_MAX_CHANNELS 8

typedef struct {
    float x1[DC_BLOCKER_MAX_CHANNELS];
    float y1[DC_BLOCKER_MAX_CHANNELS];
    float R;
    uint32_t sample_rate;
} DCBlocker;

void dc_blocker_init(DCBlocker *dc, uint32_t sample_rate);
void dc_blocker_reset(DCBlocker *dc);

// Removes DC bias and infrasonic rumble (< 5 Hz) in-place on float buffers 
void dc_blocker_process(DCBlocker *dc, float *samples_interleaved, uint32_t num_frames, uint16_t num_channels, uint32_t sample_rate);

#endif // DC_BLOCKER_H