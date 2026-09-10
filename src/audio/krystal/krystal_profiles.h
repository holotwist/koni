#ifndef KRYSTAL_PROFILES_H
#define KRYSTAL_PROFILES_H

#include <stdbool.h>
#include <stdint.h>

#define KRYSTAL_MAX_CHANNELS 8

typedef struct {
    float b0, b1, b2, a1, a2;
    double s1[KRYSTAL_MAX_CHANNELS];
    double s2[KRYSTAL_MAX_CHANNELS];
} KrystalBiquad;

typedef enum {
    KRYSTAL_PROFILE_BYPASS = 0,
    KRYSTAL_PROFILE_HEADPHONES_REF,
    KRYSTAL_PROFILE_HEADPHONES_WIDE,
    KRYSTAL_PROFILE_DESKTOP_MONITORS,
    KRYSTAL_PROFILE_COMPACT_SPEAKERS,
    KRYSTAL_PROFILE_LATE_NIGHT,
    KRYSTAL_NUM_PROFILES
} KrystalProfileID;

typedef enum {
    LOUDNESS_MODE_OFF = 0,
    LOUDNESS_MODE_DYNAMIC,
    LOUDNESS_MODE_FIXED
} KrystalLoudnessMode;

typedef struct {
    bool enabled;
    KrystalLoudnessMode mode;
    float intensity;     // 0.0 to 1.0
    float ref_vol;       // 0.5 to 1.0
    float bass_boost_db;
    float treb_boost_db;
} KrystalLoudnessConfig;

typedef struct {
    bool enabled;
    float cutoff_hz;     // 40 to 140 Hz (crossover) 
    float intensity;     // 0.0 to 1.0 (harmonic drive) 
    float mix;           // 0.0 to 1.0 (harmonic wet level) 
    float sub_weight;    // 0.0 to 1.0 (direct fundamental weight) 
    float harmonic_tone; // 0.0 (2nd-order tube) to 1.0 (3rd-order punch) 
    float rumble_hz;     // 10 to 35 Hz (subsonic high-pass) 
    float sub_octave;    // 0.0 to 1.0 (synthesized f/2 sub level) 
    float sub_phase_deg; // 0.0 to 180.0 degrees (crossover phase alignment) 
} KrystalBassConfig;

typedef struct {
    bool enabled;
    float azimuth_deg;
    float elevation_deg;
    float distance_m;
    float stage_angle_deg;
    float room_refl;
    float center_gain_db;
    float mono_cut_hz;
    float safety_limit;
} KrystalSpatialConfig;

typedef struct {
    bool enabled;
    float cutoff_hz;     // 2500 to 10000 Hz (Air crossover)
    float drive;         // 0.0 to 1.0 (Harmonic excitation drive)
    float mix;           // 0.0 to 1.0 (Exciter wet level)
    float shimmer;       // 0.0 to 1.0 (Ultra-high 10.5kHz+ air sheen)
    float attack_ms;     // 0.5 to 5.0 ms (Transient detector speed)
} KrystalExciterConfig;

typedef enum {
    SAT_MODE_OFF = 0,
    SAT_MODE_TRIODE,
    SAT_MODE_PENTODE,
    SAT_MODE_TAPE,
    SAT_MODE_TRANSFORMER,
    SAT_MODE_COUNT
} KrystalSatMode;

typedef struct {
    bool enabled;
    KrystalSatMode mode;
    float drive;         // 0.0 to 1.0
    float bias;          // -0.5 to +0.5 (triode asymmetry)
    float tone;          // 0.0 to 1.0 (warm to bright tilt)
    float mix;           // 0.0 to 1.0
    bool oversample;     // 2x linear-phase anti-aliasing
} KrystalSatConfig;

typedef struct {
    bool enabled;
    float tilt_db;       // -6.0 to +6.0 dB pivot at 1 kHz 
    float de_harsh;      // 0.0 to 1.0 (dynamic high-mid cut sensitivity) 
    float de_boom;       // 0.0 to 1.0 (dynamic low-mid cut sensitivity) 
    float harsh_freq;    // 2000 to 5000 Hz 
    float boom_freq;     // 80 to 300 Hz 
} KrystalSpectralConfig;

typedef struct {
    bool enabled;
    float attack;        // -1.0 to +1.0 (punch cut/boost) 
    float sustain;       // -1.0 to +1.0 (body/decay cut/boost) 
    float mix;           // 0.0 to 1.0 
    bool declip_enable;  // Flat-top peak reconstruction 
    float declip_thresh; // 0.80 to 0.999 
} KrystalTransientConfig;

typedef enum {
    CHAN_MODE_STEREO = 0,
    CHAN_MODE_SWAP,
    CHAN_MODE_MONO_SUM,
    CHAN_MODE_SIDE_SOLO,
    CHAN_MODE_LEFT_ONLY,
    CHAN_MODE_RIGHT_ONLY,
    CHAN_MODE_COUNT
} KrystalChannelMode;

typedef enum {
    POLARITY_NORMAL = 0,
    POLARITY_INVERT_L,
    POLARITY_INVERT_R,
    POLARITY_INVERT_BOTH,
    POLARITY_COUNT
} KrystalPolarity;

typedef struct {
    float pre_gain_db;     // -12.0 to +12.0 dB
    float balance;         // -1.0 (Left) to +1.0 (Right)
    KrystalChannelMode mode;
    KrystalPolarity polarity;
    float master_mix;      // 0.0 to 1.0 (Parallel Dry/Wet)
    bool auto_gain;        // RMS-matched negative trim
    float headroom_db;     // 0.0 to -3.0 dB (Inter-sample peak guard)
} KrystalGeneralConfig;

typedef struct {
    bool master_enabled;
    int active_profile; // KrystalProfileID or -1 for Custom
    KrystalGeneralConfig general;
    KrystalLoudnessConfig loudness;
    KrystalSpatialConfig spatial;
    KrystalBassConfig bass;
    KrystalExciterConfig exciter;
    KrystalSatConfig saturator;
    KrystalSpectralConfig spectral;
    KrystalTransientConfig transient;
} KrystalConfig;

typedef struct {
    float phase_correlation;
    float mid_energy;
    float side_energy;
    float bass_injected_energy;
    float exciter_injected_energy;
    float sat_energy;
    float sub_energy;
    float de_harsh_cut_db;
    float de_boom_cut_db;
    float transient_energy;
    float sub_octave_energy;
    float loudness_bass_db;
    float loudness_treb_db;
    float auto_trim_db;
    float crest_factor_db; // Real-time dynamic range / crest factor
    float spectral_tilt;   // Low vs High energy ratio (<1.0 bright, >1.0 dark)
    float lufs_momentary;
    float peak_dbfs;
    bool safety_clamp_active;
} KrystalTelemetry;

typedef struct {
    const char *name;
    const char *tagline;
    KrystalConfig config;
} KrystalPresetDef;

int krystal_get_profile_count(void);
const char* krystal_get_profile_name(int profile_idx);
const char* krystal_get_profile_tagline(int profile_idx);
const KrystalConfig* krystal_get_preset_config(int profile_idx);

#endif // KRYSTAL_PROFILES_H