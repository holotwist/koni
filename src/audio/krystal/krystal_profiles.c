#include "krystal_profiles.h"

static const KrystalGeneralConfig s_default_general = {
    .pre_gain_db = 0.0f,
    .balance = 0.0f,
    .mode = CHAN_MODE_STEREO,
    .polarity = POLARITY_NORMAL,
    .master_mix = 1.0f,
    .auto_gain = true,
    .headroom_db = -0.5f
};

static const KrystalPresetDef s_presets[KRYSTAL_NUM_PROFILES] = {
    [KRYSTAL_PROFILE_BYPASS] = {
        .name = "Bypass",
        .tagline = "Bit-perfect pass-through",
        .config = {
            .master_enabled = false,
            .active_profile = KRYSTAL_PROFILE_BYPASS,
            .general = {
                .pre_gain_db = 0.0f,
                .balance = 0.0f,
                .mode = CHAN_MODE_STEREO,
                .polarity = POLARITY_NORMAL,
                .master_mix = 1.0f,
                .auto_gain = false,
                .headroom_db = 0.0f
            },
            .loudness = {
                .enabled = false,
                .mode = LOUDNESS_MODE_OFF,
                .intensity = 0.0f,
                .ref_vol = 0.80f
            },
            .spatial = {
                .enabled = false,
                .azimuth_deg = 0.0f,
                .elevation_deg = 0.0f,
                .distance_m = 1.4f,
                .stage_angle_deg = 60.0f,
                .room_refl = 0.15f,
                .center_gain_db = 0.0f,
                .mono_cut_hz = 120.0f,
                .safety_limit = 0.20f
            },
            .bass = {
                .enabled = false,
                .cutoff_hz = 75.0f,
                .intensity = 0.0f,
                .mix = 0.0f,
                .sub_weight = 0.0f,
                .harmonic_tone = 0.4f,
                .rumble_hz = 20.0f,
                .sub_octave = 0.0f,
                .sub_phase_deg = 0.0f
            },
            .exciter = {
                .enabled = false,
                .cutoff_hz = 6000.0f,
                .drive = 0.0f,
                .mix = 0.0f,
                .shimmer = 0.0f,
                .attack_ms = 1.5f
            },
            .saturator = {
                .enabled = false,
                .mode = SAT_MODE_OFF,
                .drive = 0.0f,
                .bias = 0.0f,
                .tone = 0.50f,
                .mix = 0.0f,
                .oversample = true
            },
            .spectral = {
                .enabled = false,
                .tilt_db = 0.0f,
                .de_harsh = 0.0f,
                .de_boom = 0.0f,
                .harsh_freq = 3200.0f,
                .boom_freq = 160.0f
            },
            .transient = {
                .enabled = false,
                .attack = 0.0f,
                .sustain = 0.0f,
                .mix = 0.0f,
                .declip_enable = false,
                .declip_thresh = 0.99f
            }
        }
    },

    [KRYSTAL_PROFILE_HEADPHONES_REF] = {
        .name = "Headphones (Reference)",
        .tagline = "Bauer crossfeed with clean in-phase depth",
        .config = {
            .master_enabled = true,
            .active_profile = KRYSTAL_PROFILE_HEADPHONES_REF,
            .general = s_default_general,
            .loudness = {
                .enabled = true,
                .mode = LOUDNESS_MODE_DYNAMIC,
                .intensity = 0.35f,
                .ref_vol = 0.80f
            },
            .spatial = {
                .enabled = true,
                .azimuth_deg = 0.0f,
                .elevation_deg = 5.0f,
                .distance_m = 1.5f,
                .stage_angle_deg = 65.0f,
                .room_refl = 0.18f,
                .center_gain_db = 0.0f,
                .mono_cut_hz = 120.0f,
                .safety_limit = 0.25f
            },
            .bass = {
                .enabled = true,
                .cutoff_hz = 75.0f,
                .intensity = 0.15f,
                .mix = 0.15f,
                .sub_weight = 0.20f,
                .harmonic_tone = 0.40f,
                .rumble_hz = 20.0f,
                .sub_octave = 0.0f,
                .sub_phase_deg = 0.0f
            },
            .exciter = {
                .enabled = false,
                .cutoff_hz = 6000.0f,
                .drive = 0.0f,
                .mix = 0.0f,
                .shimmer = 0.0f,
                .attack_ms = 1.5f
            },
            .saturator = {
                .enabled = false,
                .mode = SAT_MODE_OFF,
                .drive = 0.0f,
                .bias = 0.0f,
                .tone = 0.50f,
                .mix = 0.0f,
                .oversample = true
            },
            .spectral = {
                .enabled = true,
                .tilt_db = 0.0f,
                .de_harsh = 0.10f,
                .de_boom = 0.0f,
                .harsh_freq = 3400.0f,
                .boom_freq = 160.0f
            },
            .transient = {
                .enabled = false,
                .attack = 0.0f,
                .sustain = 0.0f,
                .mix = 0.0f,
                .declip_enable = false,
                .declip_thresh = 0.99f
            }
        }
    },

    [KRYSTAL_PROFILE_HEADPHONES_WIDE] = {
        .name = "Headphones (Wide)",
        .tagline = "Expanded soundstage with subtle warmth",
        .config = {
            .master_enabled = true,
            .active_profile = KRYSTAL_PROFILE_HEADPHONES_WIDE,
            .general = s_default_general,
            .loudness = {
                .enabled = true,
                .mode = LOUDNESS_MODE_DYNAMIC,
                .intensity = 0.40f,
                .ref_vol = 0.80f
            },
            .spatial = {
                .enabled = true,
                .azimuth_deg = 0.0f,
                .elevation_deg = 12.0f,
                .distance_m = 2.0f,
                .stage_angle_deg = 85.0f,
                .room_refl = 0.28f,
                .center_gain_db = +0.2f,
                .mono_cut_hz = 130.0f,
                .safety_limit = 0.20f
            },
            .bass = {
                .enabled = true,
                .cutoff_hz = 75.0f,
                .intensity = 0.20f,
                .mix = 0.20f,
                .sub_weight = 0.35f,
                .harmonic_tone = 0.40f,
                .rumble_hz = 20.0f,
                .sub_octave = 0.0f,
                .sub_phase_deg = 0.0f
            },
            .exciter = {
                .enabled = true,
                .cutoff_hz = 6000.0f,
                .drive = 0.18f,
                .mix = 0.18f,
                .shimmer = 0.20f,
                .attack_ms = 1.5f
            },
            .saturator = {
                .enabled = true,
                .mode = SAT_MODE_TAPE,
                .drive = 0.10f,
                .bias = 0.0f,
                .tone = 0.50f,
                .mix = 0.20f,
                .oversample = true
            },
            .spectral = {
                .enabled = true,
                .tilt_db = +0.2f,
                .de_harsh = 0.15f,
                .de_boom = 0.0f,
                .harsh_freq = 3200.0f,
                .boom_freq = 180.0f
            },
            .transient = {
                .enabled = true,
                .attack = +0.08f,
                .sustain = 0.0f,
                .mix = 0.20f,
                .declip_enable = false,
                .declip_thresh = 0.99f
            }
        }
    },

    [KRYSTAL_PROFILE_DESKTOP_MONITORS] = {
        .name = "Desktop Monitors",
        .tagline = "Tight punch with phase-locked mono sub",
        .config = {
            .master_enabled = true,
            .active_profile = KRYSTAL_PROFILE_DESKTOP_MONITORS,
            .general = s_default_general,
            .loudness = {
                .enabled = true,
                .mode = LOUDNESS_MODE_DYNAMIC,
                .intensity = 0.30f,
                .ref_vol = 0.85f
            },
            .spatial = {
                .enabled = true,
                .azimuth_deg = 0.0f,
                .elevation_deg = 0.0f,
                .distance_m = 1.0f,
                .stage_angle_deg = 60.0f,
                .room_refl = 0.10f,
                .center_gain_db = 0.0f,
                .mono_cut_hz = 140.0f,
                .safety_limit = 0.30f
            },
            .bass = {
                .enabled = true,
                .cutoff_hz = 75.0f,
                .intensity = 0.18f,
                .mix = 0.18f,
                .sub_weight = 0.30f,
                .harmonic_tone = 0.50f,
                .rumble_hz = 22.0f,
                .sub_octave = 0.0f,
                .sub_phase_deg = 0.0f
            },
            .exciter = {
                .enabled = false,
                .cutoff_hz = 6000.0f,
                .drive = 0.0f,
                .mix = 0.0f,
                .shimmer = 0.0f,
                .attack_ms = 2.0f
            },
            .saturator = {
                .enabled = false,
                .mode = SAT_MODE_OFF,
                .drive = 0.0f,
                .bias = 0.0f,
                .tone = 0.50f,
                .mix = 0.0f,
                .oversample = true
            },
            .spectral = {
                .enabled = true,
                .tilt_db = 0.0f,
                .de_harsh = 0.10f,
                .de_boom = 0.0f,
                .harsh_freq = 3200.0f,
                .boom_freq = 140.0f
            },
            .transient = {
                .enabled = true,
                .attack = +0.10f,
                .sustain = 0.0f,
                .mix = 0.25f,
                .declip_enable = false,
                .declip_thresh = 0.99f
            }
        }
    },

    [KRYSTAL_PROFILE_COMPACT_SPEAKERS] = {
        .name = "Compact Speakers",
        .tagline = "Missing-fundamental bass for small drivers",
        .config = {
            .master_enabled = true,
            .active_profile = KRYSTAL_PROFILE_COMPACT_SPEAKERS,
            .general = {
                .pre_gain_db = 0.0f,
                .balance = 0.0f,
                .mode = CHAN_MODE_STEREO,
                .polarity = POLARITY_NORMAL,
                .master_mix = 0.95f,
                .auto_gain = true,
                .headroom_db = -0.5f
            },
            .loudness = {
                .enabled = true,
                .mode = LOUDNESS_MODE_FIXED,
                .intensity = 0.45f,
                .ref_vol = 0.80f
            },
            .spatial = {
                .enabled = true,
                .azimuth_deg = 0.0f,
                .elevation_deg = 15.0f,
                .distance_m = 1.8f,
                .stage_angle_deg = 70.0f,
                .room_refl = 0.22f,
                .center_gain_db = +0.5f,
                .mono_cut_hz = 150.0f,
                .safety_limit = 0.25f
            },
            .bass = {
                .enabled = true,
                .cutoff_hz = 85.0f,
                .intensity = 0.35f,
                .mix = 0.30f,
                .sub_weight = 0.15f,
                .harmonic_tone = 0.60f,
                .rumble_hz = 32.0f,
                .sub_octave = 0.0f,
                .sub_phase_deg = 0.0f
            },
            .exciter = {
                .enabled = true,
                .cutoff_hz = 5500.0f,
                .drive = 0.20f,
                .mix = 0.20f,
                .shimmer = 0.15f,
                .attack_ms = 1.2f
            },
            .saturator = {
                .enabled = true,
                .mode = SAT_MODE_PENTODE,
                .drive = 0.12f,
                .bias = 0.0f,
                .tone = 0.48f,
                .mix = 0.20f,
                .oversample = true
            },
            .spectral = {
                .enabled = true,
                .tilt_db = +0.5f,
                .de_harsh = 0.20f,
                .de_boom = 0.0f,
                .harsh_freq = 3000.0f,
                .boom_freq = 200.0f
            },
            .transient = {
                .enabled = true,
                .attack = +0.15f,
                .sustain = 0.0f,
                .mix = 0.30f,
                .declip_enable = false,
                .declip_thresh = 0.99f
            }
        }
    },

    [KRYSTAL_PROFILE_LATE_NIGHT] = {
        .name = "Late Night",
        .tagline = "Equal-loudness compensation for quiet levels",
        .config = {
            .master_enabled = true,
            .active_profile = KRYSTAL_PROFILE_LATE_NIGHT,
            .general = s_default_general,
            .loudness = {
                .enabled = true,
                .mode = LOUDNESS_MODE_DYNAMIC,
                .intensity = 0.65f,
                .ref_vol = 0.75f
            },
            .spatial = {
                .enabled = true,
                .azimuth_deg = 0.0f,
                .elevation_deg = 8.0f,
                .distance_m = 1.6f,
                .stage_angle_deg = 60.0f,
                .room_refl = 0.16f,
                .center_gain_db = 0.0f,
                .mono_cut_hz = 120.0f,
                .safety_limit = 0.25f
            },
            .bass = {
                .enabled = true,
                .cutoff_hz = 75.0f,
                .intensity = 0.20f,
                .mix = 0.20f,
                .sub_weight = 0.25f,
                .harmonic_tone = 0.35f,
                .rumble_hz = 20.0f,
                .sub_octave = 0.0f,
                .sub_phase_deg = 0.0f
            },
            .exciter = {
                .enabled = true,
                .cutoff_hz = 6000.0f,
                .drive = 0.15f,
                .mix = 0.15f,
                .shimmer = 0.20f,
                .attack_ms = 1.5f
            },
            .saturator = {
                .enabled = true,
                .mode = SAT_MODE_TAPE,
                .drive = 0.08f,
                .bias = 0.0f,
                .tone = 0.50f,
                .mix = 0.15f,
                .oversample = true
            },
            .spectral = {
                .enabled = true,
                .tilt_db = -0.2f,
                .de_harsh = 0.15f,
                .de_boom = 0.0f,
                .harsh_freq = 3200.0f,
                .boom_freq = 160.0f
            },
            .transient = {
                .enabled = true,
                .attack = 0.0f,
                .sustain = +0.10f,
                .mix = 0.20f,
                .declip_enable = false,
                .declip_thresh = 0.99f
            }
        }
    }
};

int krystal_get_profile_count(void) {
    return KRYSTAL_NUM_PROFILES;
}

const char* krystal_get_profile_name(int profile_idx) {
    if (profile_idx < 0 || profile_idx >= KRYSTAL_NUM_PROFILES) return "Custom";
    return s_presets[profile_idx].name;
}

const char* krystal_get_profile_tagline(int profile_idx) {
    if (profile_idx < 0 || profile_idx >= KRYSTAL_NUM_PROFILES) return "Custom tailored acoustic profile";
    return s_presets[profile_idx].tagline;
}

const KrystalConfig* krystal_get_preset_config(int profile_idx) {
    if (profile_idx < 0 || profile_idx >= KRYSTAL_NUM_PROFILES) return &s_presets[0].config;
    return &s_presets[profile_idx].config;
}