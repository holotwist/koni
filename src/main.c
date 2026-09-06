#include "state.h"
#include "file_list.h"
#include "db.h"
#include "audio.h"
#include "ui.h"
#include "codec.h"
#include "extension.h"
#include "protocols/mpris.h"
#include "vis_math.h"
#include "config.h"
#include "playlist_manager.h"
#include <curl/curl.h>

#include <locale.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdlib.h>

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
    config_init(); // Initialize configuration manager
    db_init(); // Initialize SQLite cache
    playlist_mgmt_init(); // Initialize playlists & favourites
    curl_global_init(CURL_GLOBAL_DEFAULT);
    load_state(); // Load all the previous state
    library_reload(); // Read database tracks into memory
    library_scanner_start(); // Trigger background sync scan

    if (dir_idx != -1) { 
        if (chdir(argv[dir_idx]) != 0) perror("chdir failed"); 
    } else if (current_dir[0] != '\0') {
        if (chdir(current_dir) != 0) perror("chdir to saved dir failed");
    }
    
    // Update current_dir with absolute path if successful
    if (getcwd(current_dir, sizeof(current_dir)) == NULL) {
        strncpy(current_dir, ".", sizeof(current_dir));
    }

    file_list_init();
    load_directory(".");
    vis_math_init();
    koni_extensions_init();
    
    // Expose DBus methods and properties
    mpris_init();
    
    pthread_t audio_thread;
    pthread_create(&audio_thread, NULL, audio_thread_func, NULL);
    
    ui_run(force_colors);
    
    atomic_store(&current_cmd_atomic, CMD_QUIT);
    pthread_join(audio_thread, NULL);
    
    koni_extensions_shutdown();
    mpris_shutdown();
    library_scanner_shutdown();
    save_state(); // Dump state before exiting
    config_save(); // Save configuration
    file_list_shutdown();
    playlist_mgmt_shutdown();
    db_shutdown();
    config_cleanup();
    
    koni_metadata_free(&p_metadata);
    
    // Free dynamically allocated arrays
    if (files) {
        for (int i = 0; i < num_files; i++) koni_metadata_free(&files[i].meta);
        free(files);
    }
    if (playlist) {
        for (int i = 0; i < num_playlist_files; i++) koni_metadata_free(&playlist[i].meta);
        free(playlist);
    }
    if (active_folder.file_names) {
        for (int i = 0; i < active_folder.count; i++) free(active_folder.file_names[i]);
        free(active_folder.file_names);
    }
    
    curl_global_cleanup();
    
    return 0;
}