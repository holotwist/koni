#include "state.h"
#include "file_list.h"
#include "audio.h"
#include "ui.h"
#include "codec.h"
#include "protocols/mpris.h"
#include "vis_math.h"

#include <locale.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

int main(int argc, char **argv) {
    bool force_colors = false;
    int dir_idx = -1;
    
    // Parse args
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--force-colors") == 0 || strcmp(argv[i], "-f") == 0) {
            force_colors = true;
        } else {
            dir_idx = i;
        }
    }

    setlocale(LC_ALL, ""); 
    if (dir_idx != -1) { 
        if (chdir(argv[dir_idx]) != 0) perror("chdir failed"); 
    }
    load_directory(".");
    vis_math_init();
    
    // Expose DBus methods and properties
    mpris_init();
    
    pthread_t audio_thread;
    pthread_create(&audio_thread, NULL, audio_thread_func, NULL);
    
    ui_run(force_colors);
    
    atomic_store(&current_cmd_atomic, CMD_QUIT);
    pthread_join(audio_thread, NULL);
    
    mpris_shutdown();
    koni_metadata_free(&p_metadata);
    return 0;
}