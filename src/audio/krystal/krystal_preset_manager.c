#define _DEFAULT_SOURCE
#include "krystal_preset_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <sys/stat.h>

typedef struct {
    char name[KRYSTAL_PRESET_NAME_MAX];
    KrystalConfig config;
} CustomPresetEntry;

static CustomPresetEntry s_custom_presets[KRYSTAL_MAX_CUSTOM_PRESETS];
static int s_custom_count = 0;
static pthread_mutex_t s_preset_mutex = PTHREAD_MUTEX_INITIALIZER;

static void get_ini_path(char *dst, size_t sz) {
    const char *home = getenv("HOME");
    if (home) {
        snprintf(dst, sz, "%s/.config/koni/krystal_customs.ini", home);
    } else {
        dst[0] = '\0';
    }
}

static void trim_inplace(char *str) {
    if (!str || !str[0]) return;
    char *p = str;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
    if (p != str) memmove(str, p, strlen(p) + 1);
    size_t len = strlen(str);
    while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\t' ||
                       str[len - 1] == '\r' || str[len - 1] == '\n')) {
        str[--len] = '\0';
    }
}

static void parse_key_val(KrystalConfig *cfg, const char *key, const char *val) {
    float f = (float)atof(val);
    int i = atoi(val);

    // General
    if (strcmp(key, "gen_pre") == 0) cfg->general.pre_gain_db = f;
    else if (strcmp(key, "gen_bal") == 0) cfg->general.balance = f;
    else if (strcmp(key, "gen_mode") == 0) cfg->general.mode = (KrystalChannelMode)i;
    else if (strcmp(key, "gen_pol") == 0) cfg->general.polarity = (KrystalPolarity)i;
    else if (strcmp(key, "master_mix") == 0) cfg->general.master_mix = f;
    else if (strcmp(key, "autogain") == 0) cfg->general.auto_gain = (i != 0);
    else if (strcmp(key, "gen_head") == 0) cfg->general.headroom_db = f;

    // Loudness
    else if (strcmp(key, "loud_en") == 0) cfg->loudness.enabled = (i != 0);
    else if (strcmp(key, "loud_mode") == 0) cfg->loudness.mode = (KrystalLoudnessMode)i;
    else if (strcmp(key, "loud_int") == 0) cfg->loudness.intensity = f;
    else if (strcmp(key, "loud_ref") == 0) cfg->loudness.ref_vol = f;

    // Spatial
    else if (strcmp(key, "spat_en") == 0) cfg->spatial.enabled = (i != 0);
    else if (strcmp(key, "spat_az") == 0) cfg->spatial.azimuth_deg = f;
    else if (strcmp(key, "spat_el") == 0) cfg->spatial.elevation_deg = f;
    else if (strcmp(key, "spat_dist") == 0) cfg->spatial.distance_m = f;
    else if (strcmp(key, "spat_angle") == 0) cfg->spatial.stage_angle_deg = f;
    else if (strcmp(key, "spat_refl") == 0) cfg->spatial.room_refl = f;
    else if (strcmp(key, "spat_cntr") == 0) cfg->spatial.center_gain_db = f;
    else if (strcmp(key, "spat_cut") == 0) cfg->spatial.mono_cut_hz = f;
    else if (strcmp(key, "spat_safe") == 0) cfg->spatial.safety_limit = f;

    // Bass
    else if (strcmp(key, "bass_en") == 0) cfg->bass.enabled = (i != 0);
    else if (strcmp(key, "bass_cut") == 0) cfg->bass.cutoff_hz = f;
    else if (strcmp(key, "bass_int") == 0) cfg->bass.intensity = f;
    else if (strcmp(key, "bass_mix") == 0) cfg->bass.mix = f;
    else if (strcmp(key, "bass_sub") == 0) cfg->bass.sub_weight = f;
    else if (strcmp(key, "bass_tone") == 0) cfg->bass.harmonic_tone = f;
    else if (strcmp(key, "bass_rumble") == 0) cfg->bass.rumble_hz = f;
    else if (strcmp(key, "bass_oct") == 0) cfg->bass.sub_octave = f;
    else if (strcmp(key, "bass_phase") == 0) cfg->bass.sub_phase_deg = f;

    // Exciter
    else if (strcmp(key, "exct_en") == 0) cfg->exciter.enabled = (i != 0);
    else if (strcmp(key, "exct_cut") == 0) cfg->exciter.cutoff_hz = f;
    else if (strcmp(key, "exct_drv") == 0) cfg->exciter.drive = f;
    else if (strcmp(key, "exct_mix") == 0) cfg->exciter.mix = f;
    else if (strcmp(key, "exct_shim") == 0) cfg->exciter.shimmer = f;
    else if (strcmp(key, "exct_att") == 0) cfg->exciter.attack_ms = f;

    // Saturator
    else if (strcmp(key, "sat_en") == 0) cfg->saturator.enabled = (i != 0);
    else if (strcmp(key, "sat_mode") == 0) cfg->saturator.mode = (KrystalSatMode)i;
    else if (strcmp(key, "sat_drv") == 0) cfg->saturator.drive = f;
    else if (strcmp(key, "sat_bias") == 0) cfg->saturator.bias = f;
    else if (strcmp(key, "sat_tone") == 0) cfg->saturator.tone = f;
    else if (strcmp(key, "sat_mix") == 0) cfg->saturator.mix = f;
    else if (strcmp(key, "sat_os") == 0) cfg->saturator.oversample = (i != 0);

    // Spectral
    else if (strcmp(key, "spec_en") == 0) cfg->spectral.enabled = (i != 0);
    else if (strcmp(key, "spec_tilt") == 0) cfg->spectral.tilt_db = f;
    else if (strcmp(key, "spec_harsh") == 0) cfg->spectral.de_harsh = f;
    else if (strcmp(key, "spec_boom") == 0) cfg->spectral.de_boom = f;
    else if (strcmp(key, "spec_hfreq") == 0) cfg->spectral.harsh_freq = f;
    else if (strcmp(key, "spec_bfreq") == 0) cfg->spectral.boom_freq = f;

    // Transient
    else if (strcmp(key, "tran_en") == 0) cfg->transient.enabled = (i != 0);
    else if (strcmp(key, "tran_att") == 0) cfg->transient.attack = f;
    else if (strcmp(key, "tran_sus") == 0) cfg->transient.sustain = f;
    else if (strcmp(key, "tran_mix") == 0) cfg->transient.mix = f;
    else if (strcmp(key, "tran_decl") == 0) cfg->transient.declip_enable = (i != 0);
    else if (strcmp(key, "tran_dthr") == 0) cfg->transient.declip_thresh = f;
}

static void write_preset_section(FILE *fp, const CustomPresetEntry *entry) {
    const KrystalConfig *c = &entry->config;
    fprintf(fp, "[%s]\n", entry->name);
    fprintf(fp, "gen_pre=%.1f\n", c->general.pre_gain_db);
    fprintf(fp, "gen_bal=%.2f\n", c->general.balance);
    fprintf(fp, "gen_mode=%d\n", (int)c->general.mode);
    fprintf(fp, "gen_pol=%d\n", (int)c->general.polarity);
    fprintf(fp, "master_mix=%.2f\n", c->general.master_mix);
    fprintf(fp, "autogain=%d\n", c->general.auto_gain ? 1 : 0);
    fprintf(fp, "gen_head=%.1f\n", c->general.headroom_db);

    fprintf(fp, "loud_en=%d\n", c->loudness.enabled ? 1 : 0);
    fprintf(fp, "loud_mode=%d\n", (int)c->loudness.mode);
    fprintf(fp, "loud_int=%.2f\n", c->loudness.intensity);
    fprintf(fp, "loud_ref=%.2f\n", c->loudness.ref_vol);

    fprintf(fp, "spat_en=%d\n", c->spatial.enabled ? 1 : 0);
    fprintf(fp, "spat_az=%.1f\n", c->spatial.azimuth_deg);
    fprintf(fp, "spat_el=%.1f\n", c->spatial.elevation_deg);
    fprintf(fp, "spat_dist=%.2f\n", c->spatial.distance_m);
    fprintf(fp, "spat_angle=%.1f\n", c->spatial.stage_angle_deg);
    fprintf(fp, "spat_refl=%.2f\n", c->spatial.room_refl);
    fprintf(fp, "spat_cntr=%.2f\n", c->spatial.center_gain_db);
    fprintf(fp, "spat_cut=%.1f\n", c->spatial.mono_cut_hz);
    fprintf(fp, "spat_safe=%.2f\n", c->spatial.safety_limit);

    fprintf(fp, "bass_en=%d\n", c->bass.enabled ? 1 : 0);
    fprintf(fp, "bass_cut=%.1f\n", c->bass.cutoff_hz);
    fprintf(fp, "bass_int=%.2f\n", c->bass.intensity);
    fprintf(fp, "bass_mix=%.2f\n", c->bass.mix);
    fprintf(fp, "bass_sub=%.2f\n", c->bass.sub_weight);
    fprintf(fp, "bass_tone=%.2f\n", c->bass.harmonic_tone);
    fprintf(fp, "bass_rumble=%.1f\n", c->bass.rumble_hz);
    fprintf(fp, "bass_oct=%.2f\n", c->bass.sub_octave);
    fprintf(fp, "bass_phase=%.1f\n", c->bass.sub_phase_deg);

    fprintf(fp, "exct_en=%d\n", c->exciter.enabled ? 1 : 0);
    fprintf(fp, "exct_cut=%.1f\n", c->exciter.cutoff_hz);
    fprintf(fp, "exct_drv=%.2f\n", c->exciter.drive);
    fprintf(fp, "exct_mix=%.2f\n", c->exciter.mix);
    fprintf(fp, "exct_shim=%.2f\n", c->exciter.shimmer);
    fprintf(fp, "exct_att=%.2f\n", c->exciter.attack_ms);

    fprintf(fp, "sat_en=%d\n", c->saturator.enabled ? 1 : 0);
    fprintf(fp, "sat_mode=%d\n", (int)c->saturator.mode);
    fprintf(fp, "sat_drv=%.2f\n", c->saturator.drive);
    fprintf(fp, "sat_bias=%.2f\n", c->saturator.bias);
    fprintf(fp, "sat_tone=%.2f\n", c->saturator.tone);
    fprintf(fp, "sat_mix=%.2f\n", c->saturator.mix);
    fprintf(fp, "sat_os=%d\n", c->saturator.oversample ? 1 : 0);

    fprintf(fp, "spec_en=%d\n", c->spectral.enabled ? 1 : 0);
    fprintf(fp, "spec_tilt=%.2f\n", c->spectral.tilt_db);
    fprintf(fp, "spec_harsh=%.2f\n", c->spectral.de_harsh);
    fprintf(fp, "spec_boom=%.2f\n", c->spectral.de_boom);
    fprintf(fp, "spec_hfreq=%.1f\n", c->spectral.harsh_freq);
    fprintf(fp, "spec_bfreq=%.1f\n", c->spectral.boom_freq);

    fprintf(fp, "tran_en=%d\n", c->transient.enabled ? 1 : 0);
    fprintf(fp, "tran_att=%.2f\n", c->transient.attack);
    fprintf(fp, "tran_sus=%.2f\n", c->transient.sustain);
    fprintf(fp, "tran_mix=%.2f\n", c->transient.mix);
    fprintf(fp, "tran_decl=%d\n", c->transient.declip_enable ? 1 : 0);
    fprintf(fp, "tran_dthr=%.3f\n\n", c->transient.declip_thresh);
}

static bool save_all_to_disk_locked(void) {
    char path[1024];
    get_ini_path(path, sizeof(path));
    if (!path[0]) return false;

    FILE *f = fopen(path, "w");
    if (!f) return false;

    for (int idx = 0; idx < s_custom_count; idx++) {
        write_preset_section(f, &s_custom_presets[idx]);
    }
    fclose(f);
    return true;
}

void krystal_presets_init(void) {
    pthread_mutex_lock(&s_preset_mutex);
    s_custom_count = 0;

    char path[1024];
    get_ini_path(path, sizeof(path));
    if (!path[0]) {
        pthread_mutex_unlock(&s_preset_mutex);
        return;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        pthread_mutex_unlock(&s_preset_mutex);
        return;
    }

    char line[512];
    CustomPresetEntry *curr = NULL;

    while (fgets(line, sizeof(line), f)) {
        trim_inplace(line);
        if (!line[0] || line[0] == '#' || line[0] == ';') continue;

        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (end && s_custom_count < KRYSTAL_MAX_CUSTOM_PRESETS) {
                *end = '\0';
                char *sec_name = line + 1;
                trim_inplace(sec_name);
                if (sec_name[0]) {
                    curr = &s_custom_presets[s_custom_count++];
                    strncpy(curr->name, sec_name, KRYSTAL_PRESET_NAME_MAX - 1);
                    curr->name[KRYSTAL_PRESET_NAME_MAX - 1] = '\0';
                    curr->config = *krystal_get_preset_config(KRYSTAL_PROFILE_HEADPHONES_REF);
                    curr->config.active_profile = -1;
                }
            }
        } else if (curr) {
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = '\0';
                char *k = line;
                char *v = eq + 1;
                trim_inplace(k);
                trim_inplace(v);
                parse_key_val(&curr->config, k, v);
            }
        }
    }
    fclose(f);
    pthread_mutex_unlock(&s_preset_mutex);
}

void krystal_presets_shutdown(void) {
    pthread_mutex_lock(&s_preset_mutex);
    s_custom_count = 0;
    pthread_mutex_unlock(&s_preset_mutex);
}

int krystal_presets_get_count(void) {
    pthread_mutex_lock(&s_preset_mutex);
    int c = s_custom_count;
    pthread_mutex_unlock(&s_preset_mutex);
    return c;
}

const char* krystal_presets_get_name(int idx) {
    pthread_mutex_lock(&s_preset_mutex);
    if (idx < 0 || idx >= s_custom_count) {
        pthread_mutex_unlock(&s_preset_mutex);
        return "";
    }
    const char *name = s_custom_presets[idx].name;
    pthread_mutex_unlock(&s_preset_mutex);
    return name;
}

bool krystal_presets_get_config(int idx, KrystalConfig *out_cfg) {
    if (!out_cfg) return false;
    pthread_mutex_lock(&s_preset_mutex);
    if (idx < 0 || idx >= s_custom_count) {
        pthread_mutex_unlock(&s_preset_mutex);
        return false;
    }
    *out_cfg = s_custom_presets[idx].config;
    pthread_mutex_unlock(&s_preset_mutex);
    return true;
}

int krystal_presets_find(const char *name) {
    if (!name || !name[0]) return -1;
    pthread_mutex_lock(&s_preset_mutex);
    for (int idx = 0; idx < s_custom_count; idx++) {
        if (strcasecmp(s_custom_presets[idx].name, name) == 0) {
            pthread_mutex_unlock(&s_preset_mutex);
            return idx;
        }
    }
    pthread_mutex_unlock(&s_preset_mutex);
    return -1;
}

bool krystal_presets_save(const char *name, const KrystalConfig *cfg) {
    if (!name || !name[0] || !cfg) return false;

    pthread_mutex_lock(&s_preset_mutex);
    int target = -1;
    for (int idx = 0; idx < s_custom_count; idx++) {
        if (strcasecmp(s_custom_presets[idx].name, name) == 0) {
            target = idx;
            break;
        }
    }

    if (target == -1) {
        if (s_custom_count >= KRYSTAL_MAX_CUSTOM_PRESETS) {
            pthread_mutex_unlock(&s_preset_mutex);
            return false;
        }
        target = s_custom_count++;
        strncpy(s_custom_presets[target].name, name, KRYSTAL_PRESET_NAME_MAX - 1);
        s_custom_presets[target].name[KRYSTAL_PRESET_NAME_MAX - 1] = '\0';
    }

    s_custom_presets[target].config = *cfg;
    s_custom_presets[target].config.active_profile = -1;

    bool ok = save_all_to_disk_locked();
    pthread_mutex_unlock(&s_preset_mutex);
    return ok;
}

bool krystal_presets_delete(const char *name) {
    if (!name || !name[0]) return false;

    pthread_mutex_lock(&s_preset_mutex);
    int target = -1;
    for (int idx = 0; idx < s_custom_count; idx++) {
        if (strcasecmp(s_custom_presets[idx].name, name) == 0) {
            target = idx;
            break;
        }
    }
    if (target == -1) {
        pthread_mutex_unlock(&s_preset_mutex);
        return false;
    }

    for (int idx = target; idx < s_custom_count - 1; idx++) {
        s_custom_presets[idx] = s_custom_presets[idx + 1];
    }
    s_custom_count--;

    bool ok = save_all_to_disk_locked();
    pthread_mutex_unlock(&s_preset_mutex);
    return ok;
}

bool krystal_presets_rename(const char *old_name, const char *new_name) {
    if (!old_name || !new_name || !old_name[0] || !new_name[0]) return false;

    pthread_mutex_lock(&s_preset_mutex);
    int target = -1;
    for (int idx = 0; idx < s_custom_count; idx++) {
        if (strcasecmp(s_custom_presets[idx].name, old_name) == 0) {
            target = idx;
            break;
        }
    }
    if (target == -1) {
        pthread_mutex_unlock(&s_preset_mutex);
        return false;
    }

    strncpy(s_custom_presets[target].name, new_name, KRYSTAL_PRESET_NAME_MAX - 1);
    s_custom_presets[target].name[KRYSTAL_PRESET_NAME_MAX - 1] = '\0';

    bool ok = save_all_to_disk_locked();
    pthread_mutex_unlock(&s_preset_mutex);
    return ok;
}