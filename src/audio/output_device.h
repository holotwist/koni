#ifndef OUTPUT_DEVICE_H
#define OUTPUT_DEVICE_H

#include <stdint.h>
#include <stdbool.h>

bool output_device_init(uint32_t sample_rate, uint16_t channels);
void output_device_uninit(void);
bool output_device_is_active(void);
void output_device_start(void);
void output_device_stop(void);
void output_device_reset_buffer(void);

uint32_t output_device_available_write(void);
uint32_t output_device_available_read(void);
uint32_t output_device_write(const float *pcm_interleaved_float, uint32_t num_frames, uint16_t channels);

// Gapless boundary synchronization 
void output_device_arm_gapless(uint32_t boundary_frames);
bool output_device_check_gapless_switched(void);
void output_device_clear_gapless(void);

#endif // OUTPUT_DEVICE_H