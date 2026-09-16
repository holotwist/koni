#ifndef SPARKLES_VIS_H
#define SPARKLES_VIS_H

#include "raylib.h"

typedef enum {
    VIS_DANCER = 0,        // Koni dancing
    VIS_ORB_FLUID,         // Clean fluid orb
    VIS_WAVES_FLOW,        // Flowing chromatic wave layers
    VIS_RADIAL_SPECTRUM,   // Circular starburst visualizer
    VIS_VECTOR_SCOPE,      // Stereo Lissajous oscilloscope
    VIS_PARTICLE_VORTEX,   // Audio-reactive vortex
    VIS_CYMATICS,          // Chladni plate resonance mandala
    VIS_TERRAIN,           // Wireframe horizon
    VIS_VECTOR_POLY,       // 3D Vectrex audio-reactive polyhedron
    VIS_TAPE_REELS,        // Tape spools
    VIS_WARP_TUNNEL,       // Warp tunnel
    VIS_SPARKLES,          // Dynamic transient starbursts & flares
    VIS_VOYAGER,           // 3D forward network voyage with trailing camera
    VIS_GALVANOMETER,      // Dual mechanical VU meters
    VIS_SEISMOGRAPH,       // Continuous strip-chart info
    VIS_GIMBAL,            // 3-axis gyroscope
    VIS_MODE_COUNT
} SparklesVisMode;

void sparkles_vis_init(void);
void sparkles_vis_cycle(void);
void sparkles_vis_set_mode(SparklesVisMode mode);
SparklesVisMode sparkles_vis_get_mode(void);
const char* sparkles_vis_get_name(SparklesVisMode mode);
float sparkles_vis_get_badge_alpha(void);

// Render the active visualizer within bounds
void sparkles_vis_render(Rectangle bounds, float dt);

// Audio analysis helpers for visualizers
void sparkles_vis_get_spectrum(float *out_bins, int count);
void sparkles_vis_get_bands(float *out_bass, float *out_mid, float *out_high);
void sparkles_vis_get_raw_bands(float *out_bass, float *out_mid, float *out_high);

#endif // SPARKLES_VIS_H