cmake_minimum_required(VERSION 3.16)
project(Koni VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)

include(CheckCCompilerFlag)

option(PORTABLE_BUILD "Compile for portable baseline architecture instead of -march=native / -mcpu=native" OFF)
option(SEMI_STATIC "Statically link third-party libraries (codecs, sqlite, ncurses, curl) where available" OFF)
option(MODERN_X86_64 "Compile for modern x86_64 level when PORTABLE_BUILD is enabled" OFF)
set(X86_64_LEVEL "x86-64-v3" CACHE STRING "Target x86-64 baseline level when MODERN_X86_64 is enabled (x86-64-v2, x86-64-v3, x86-64-v4)")
option(BUILD_SPARKLES "Build Koni Sparkles (Raylib GUI)" OFF)

# Default to Release with maximum optimization
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(DBUS QUIET dbus-1)
endif()

set(CURL_TARGET "")
set(SQLITE3_TARGET "")
set(NCURSES_TARGET "ncursesw")

if(SEMI_STATIC)
    message(STATUS "Semi-static build requested: searching for static archives (.a)...")

    # SQLite3 static (.a only)
    find_library(SQLITE3_STATIC_LIB NAMES libsqlite3.a)
    if(SQLITE3_STATIC_LIB AND SQLITE3_STATIC_LIB MATCHES "\\.a$")
        set(SQLITE3_TARGET "${SQLITE3_STATIC_LIB}")
        message(STATUS "  [static] SQLite3: ${SQLITE3_STATIC_LIB}")
    endif()

    # ncursesw static (.a only)
    find_library(NCURSESW_STATIC_LIB NAMES libncursesw.a)
    find_library(TINFO_STATIC_LIB NAMES libtinfo.a)
    if(NCURSESW_STATIC_LIB AND NCURSESW_STATIC_LIB MATCHES "\\.a$")
        set(NCURSES_TARGET "${NCURSESW_STATIC_LIB}")
        if(TINFO_STATIC_LIB AND TINFO_STATIC_LIB MATCHES "\\.a$" AND NOT TINFO_STATIC_LIB STREQUAL NCURSESW_STATIC_LIB)
            list(APPEND NCURSES_TARGET "${TINFO_STATIC_LIB}")
        endif()
        message(STATUS "  [static] ncursesw: ${NCURSESW_STATIC_LIB}")
    endif()

    # curl static
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(CURL_STATIC_PKG QUIET STATIC IMPORTED_TARGET libcurl)
        if(CURL_STATIC_PKG_FOUND)
            set(CURL_TARGET PkgConfig::CURL_STATIC_PKG)
            message(STATUS "  [static] cURL: found via pkg-config --static")
        endif()
    endif()
    if(NOT CURL_TARGET)
        find_library(CURL_STATIC_LIB NAMES libcurl.a)
        if(CURL_STATIC_LIB AND CURL_STATIC_LIB MATCHES "\\.a$")
            set(CURL_TARGET "${CURL_STATIC_LIB}")
            message(STATUS "  [static] cURL: ${CURL_STATIC_LIB}")
        endif()
    endif()
endif()

# Dynamic fallback if static library was not found
if(NOT CURL_TARGET)
    find_package(CURL REQUIRED)
    set(CURL_TARGET ${CURL_LIBRARIES})
endif()
if(NOT SQLITE3_TARGET)
    find_package(SQLite3 REQUIRED)
    set(SQLITE3_TARGET SQLite3::SQLite3)
endif()

set(ENABLED_CODECS "")
set(DISABLED_CODECS "")

# Check FAAD2 (AAC / HE-AAC)
if(SEMI_STATIC)
    find_path(FAAD_INCLUDE_DIRS NAMES neaacdec.h faad.h)
    find_library(FAAD_STATIC_LIB NAMES libfaad.a)
    if(FAAD_INCLUDE_DIRS AND FAAD_STATIC_LIB AND FAAD_STATIC_LIB MATCHES "\\.a$")
        set(FAAD_FOUND TRUE)
        set(FAAD_LIBRARIES "${FAAD_STATIC_LIB}")
        message(STATUS "  [static] FAAD2: ${FAAD_STATIC_LIB}")
    endif()
endif()
if(NOT FAAD_FOUND)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(FAAD QUIET faad2)
    endif()
    if(NOT FAAD_FOUND)
        find_path(FAAD_INCLUDE_DIRS NAMES neaacdec.h faad.h)
        find_library(FAAD_LIBRARIES NAMES faad)
        if(FAAD_INCLUDE_DIRS AND FAAD_LIBRARIES)
            set(FAAD_FOUND TRUE)
        endif()
    endif()
endif()

set(M4A_SOURCES "")
if(FAAD_FOUND)
    set(M4A_SOURCES
        src/codecs/m4a_codec/mp4_demux.c
        src/codecs/m4a_codec/metadata.c
        src/codecs/m4a_codec/m4a_codec.c
    )
    list(APPEND ENABLED_CODECS "AAC / M4A (FAAD2)")
else()
    list(APPEND DISABLED_CODECS "AAC / M4A (.m4a, .aac - libfaad2 missing)")
endif()

# Check libopusfile (Opus)
if(SEMI_STATIC)
    find_path(OPUSFILE_INCLUDE_DIRS NAMES opusfile.h PATH_SUFFIXES opus)
    find_library(OPUSFILE_STATIC_LIB NAMES libopusfile.a)
    find_library(OPUS_STATIC_LIB NAMES libopus.a)
    find_library(OGG_STATIC_LIB NAMES libogg.a)
    if(OPUSFILE_INCLUDE_DIRS AND OPUSFILE_STATIC_LIB AND OPUSFILE_STATIC_LIB MATCHES "\\.a$")
        set(OPUSFILE_FOUND TRUE)
        set(OPUSFILE_LIBRARIES "${OPUSFILE_STATIC_LIB}")
        if(OPUS_STATIC_LIB AND OPUS_STATIC_LIB MATCHES "\\.a$")
            list(APPEND OPUSFILE_LIBRARIES "${OPUS_STATIC_LIB}")
        endif()
        if(OGG_STATIC_LIB AND OGG_STATIC_LIB MATCHES "\\.a$")
            list(APPEND OPUSFILE_LIBRARIES "${OGG_STATIC_LIB}")
        endif()
        message(STATUS "  [static] Opusfile: ${OPUSFILE_STATIC_LIB}")
    endif()
endif()
if(NOT OPUSFILE_FOUND)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(OPUSFILE QUIET opusfile)
    endif()
    if(NOT OPUSFILE_FOUND)
        find_path(OPUSFILE_INCLUDE_DIRS NAMES opusfile.h PATH_SUFFIXES opus)
        find_library(OPUSFILE_LIBRARIES NAMES opusfile)
        if(OPUSFILE_INCLUDE_DIRS AND OPUSFILE_LIBRARIES)
            set(OPUSFILE_FOUND TRUE)
        endif()
    endif()
endif()

set(OPUS_SOURCES "")
if(OPUSFILE_FOUND)
    set(OPUS_SOURCES
        src/codecs/opus_codec/opus_codec.c
        src/codecs/opus_codec/metadata.c
    )
    list(APPEND ENABLED_CODECS "Opus (.opus - libopusfile)")
else()
    list(APPEND DISABLED_CODECS "Opus (.opus - libopusfile missing)")
endif()

# Check DANA
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/dana")
    add_compile_definitions(ENABLE_DANA)
    include_directories(
        src/dana/src/include/public
        src/dana/src/include/private
    )
    file(GLOB DANA_SOURCES "src/dana/src/*.c")
    foreach(src_file ${DANA_SOURCES})
        if(src_file MATCHES "main\\.c$" OR src_file MATCHES "danaplayd\\.c$")
            list(REMOVE_ITEM DANA_SOURCES ${src_file})
        endif()
    endforeach()
    list(APPEND DANA_SOURCES
        src/codecs/dana_codec/dana_codec.c
        src/codecs/dana_codec/metadata.c
    )
    list(APPEND ENABLED_CODECS "DANA (.dana, .dahl)")
else()
    set(DANA_SOURCES "")
    list(APPEND DISABLED_CODECS "DANA (.dana, .dahl - src/dana not found)")
endif()

# Built-in codecs
list(APPEND ENABLED_CODECS "MP3 / WAV / FLAC (Miniaudio)")
list(APPEND ENABLED_CODECS "Ogg Vorbis (stb_vorbis)")
list(APPEND ENABLED_CODECS "PxTone (.ptcop, .pttune)")

# Performance flags
add_compile_options(-O3 -flto -fno-math-errno -funroll-loops -Wall -Wextra)
add_compile_definitions(NDEBUG)

# Strip binary symbols in Release builds
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_link_options(-s)
endif()

# Architecture selector
if(PORTABLE_BUILD)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|amd64|AMD64)")
        if(MODERN_X86_64)
            check_c_compiler_flag("-march=${X86_64_LEVEL}" HAS_X86_LEVEL)
            if(HAS_X86_LEVEL)
                add_compile_options("-march=${X86_64_LEVEL}")
            else()
                add_compile_options("-march=x86-64-v2")
            endif()
        else()
            add_compile_options("-march=x86-64")
        endif()
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(arm|aarch64|ARM64)")
        check_c_compiler_flag("-march=armv8-a" HAS_ARMV8_A)
        if(HAS_ARMV8_A)
            add_compile_options("-march=armv8-a")
        endif()
    endif()
else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|amd64|AMD64|i686|i386|x86)")
        check_c_compiler_flag("-march=native" HAS_MARCH_NATIVE)
        if(HAS_MARCH_NATIVE)
            add_compile_options(-march=native)
        endif()
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(arm|aarch64|ARM64)")
        check_c_compiler_flag("-mcpu=native" HAS_MCPU_NATIVE)
        if(HAS_MCPU_NATIVE)
            add_compile_options(-mcpu=native)
        endif()
    endif()
endif()

set(MPRIS_SOURCES "")
if(DBUS_FOUND)
    add_compile_definitions(ENABLE_MPRIS)
    set(MPRIS_SOURCES src/protocols/mpris.c)
endif()

add_library(koni_core OBJECT
    src/utils/state.c
    src/utils/file_list.c
    src/utils/vis_math.c
    src/utils/config_manager/config.c
    src/lyrics/lyrics_engine.c
    src/lyrics/lyrics_registry.c
    src/lyrics/lrc_plugin/lrc_parser.c
    src/lyrics/lrc_plugin/lrc_net.c
    src/lyrics/ttml_plugin/ttml_plugin.c
    src/audio/audio_engine.c
    src/audio/output_device.c
    src/audio/stream_reader.c
    src/audio/dsp_rack.c
    src/audio/dc_blocker.c
    src/audio/limiter.c
    src/audio/replaygain/replaygain.c
    src/codecs/codec_registry.c
    src/codecs/pxtone_codec/pxtn_tables.c
    src/codecs/pxtone_codec/pxtn_noise.c
    src/codecs/pxtone_codec/pxtn_loader.c
    src/codecs/pxtone_codec/pxtn_synth.c
    src/codecs/pxtone_codec/pxtone_codec.c
    src/codecs/ogg_codec/ogg_codec.c
    src/codecs/ogg_codec/metadata.c
    src/codecs/ma_codec/ma_codec.c
    src/codecs/ma_codec/metadata.c
    src/database/db.c
    src/database/library_scanner.c
    src/playlist/playlist_manager.c
    src/profile/listening_profile.c
    src/equ/peq.c
    src/equ/equalizer.c
    src/audio/krystal/krystal_profiles.c
    src/audio/krystal/krystal_engine.c
    src/audio/krystal/krystal_preset_manager.c
    src/audio/krystal/krystal_oversample.c
    src/audio/krystal/krystal_saturator.c
    src/audio/krystal/krystal_spectral.c
    src/audio/krystal/krystal_transient.c
    src/audio/krystal/krystal_meter.c
    src/audio/krystal/krystal_loudness.c
    src/audio/krystal/krystal_bass.c
    src/audio/krystal/krystal_spatial.c
    src/audio/krystal/krystal_exciter.c
    src/extensions/ext_registry.c
    src/extensions/tracker/tracker_ext.c
    src/ui/ui_common.c
    src/ui/ui_animations.c
    src/ui/ui_keybinds.c
    src/ui/widgets/ui_status.c
    src/ui/ui_playlists.c
    ${M4A_SOURCES}
    ${OPUS_SOURCES}
    ${MPRIS_SOURCES}
    ${DANA_SOURCES}
)

target_include_directories(koni_core PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/audio>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/playlist>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/equ>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/audio/krystal>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/include/codec_api>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/include/lyrics_api>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/include/extensions_api>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/ui/widgets>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/extensions>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/include/third_party>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/codecs/pxtone_codec>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/codecs/m4a_codec>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/codecs/opus_codec>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/protocols>
    ${CURL_INCLUDE_DIRS}
    ${SQLite3_INCLUDE_DIRS}
    ${FAAD_INCLUDE_DIRS}
    ${OPUSFILE_INCLUDE_DIRS}
)

set(CORE_LIBS ${NCURSES_TARGET} pthread m ${CMAKE_DL_LIBS} ${CURL_TARGET} ${SQLITE3_TARGET})
if(DBUS_FOUND)
    target_include_directories(koni_core PUBLIC ${DBUS_INCLUDE_DIRS})
    list(APPEND CORE_LIBS ${DBUS_LIBRARIES})
endif()
if(FAAD_FOUND)
    list(APPEND CORE_LIBS ${FAAD_LIBRARIES})
endif()
if(OPUSFILE_FOUND)
    list(APPEND CORE_LIBS ${OPUSFILE_LIBRARIES})
endif()

target_link_libraries(koni_core PUBLIC ${CORE_LIBS})

add_executable(koni
    src/main.c
    src/ui/widgets/ui_search.c
    src/ui/widgets/ui_modal.c
    src/ui/ui_eq.c
    src/ui/ui_krystal.c
    src/ui/ui_files.c
    src/ui/ui_queue.c
    src/ui/ui_musiclist.c
    src/ui/ui_vis.c
    src/ui/ui_lrc_overlay.c
    src/ui/visualizers/vis_renderer.c
    src/ui/visualizers/vis_spectrum.c
    src/ui/visualizers/vis_oscilloscope.c
    src/ui/visualizers/vis_ellipse.c
    src/ui/visualizers/vis_lissajous.c
    src/ui/ui_player.c
    src/ui/ui_input.c
    src/ui/ui.c
)

target_link_libraries(koni PRIVATE koni_core)

# Koni Sparkles (optional)
if(BUILD_SPARKLES)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/sparkles")
        if(SEMI_STATIC)
            find_library(RAYLIB_STATIC_LIB NAMES libraylib.a)
            if(RAYLIB_STATIC_LIB AND RAYLIB_STATIC_LIB MATCHES "\\.a$")
                set(RAYLIB_FOUND TRUE)
                set(RAYLIB_LIBRARIES "${RAYLIB_STATIC_LIB}" GL X11 rt)
                message(STATUS "  [static] Raylib: ${RAYLIB_STATIC_LIB}")
            endif()
        endif()

        if(NOT RAYLIB_FOUND)
            find_package(raylib QUIET)
            if(NOT raylib_FOUND AND PKG_CONFIG_FOUND)
                pkg_check_modules(RAYLIB QUIET raylib)
            endif()
        endif()

        if(TARGET raylib OR RAYLIB_FOUND)
            file(GLOB_RECURSE SPARKLES_SOURCES "src/sparkles/*.c")
            add_executable(koni-sparkles ${SPARKLES_SOURCES})
            target_include_directories(koni-sparkles PRIVATE src/sparkles)

            if(TARGET raylib)
                target_link_libraries(koni-sparkles PRIVATE koni_core raylib)
            else()
                target_include_directories(koni-sparkles PRIVATE ${RAYLIB_INCLUDE_DIRS})
                target_link_libraries(koni-sparkles PRIVATE koni_core ${RAYLIB_LIBRARIES})
            endif()
            message(STATUS "Koni Sparkles enabled & Raylib found — building koni-sparkles")
        else()
            message(WARNING "Koni Sparkles was requested (BUILD_SPARKLES=ON), but Raylib was not found! Skipping koni-sparkles.")
        endif()
    endif()
else()
    message(STATUS "Koni Sparkles is disabled (use -DBUILD_SPARKLES=ON or ./build.sh -s to build GUI)")
endif()

message(STATUS "--------------------------------------------------------")
message(STATUS "Koni Configuration Summary")
message(STATUS "  Build Type:      ${CMAKE_BUILD_TYPE}")
message(STATUS "  Sparkles (GUI):  ${BUILD_SPARKLES}")
message(STATUS "  Enabled Codecs:")
foreach(_codec ${ENABLED_CODECS})
    message(STATUS "    + ${_codec}")
endforeach()
if(DISABLED_CODECS)
    message(STATUS "  Compilation will continue with the following codecs disabled:")
    foreach(_codec ${DISABLED_CODECS})
        message(STATUS "    - ${_codec}")
    endforeach()
endif()
message(STATUS "--------------------------------------------------------")