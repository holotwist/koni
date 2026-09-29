cmake_minimum_required(VERSION 3.22.1)
project(koni_android LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_compile_options(-O3 -Wall -Wextra -fPIC)
add_compile_definitions(ANDROID PLATFORM_ANDROID NDEBUG)

set(KONI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

# Resolve Raylib source directory
if(NOT RAYLIB_SOURCE_DIR AND DEFINED ENV{RAYLIB_DIR})
    set(RAYLIB_SOURCE_DIR "$ENV{RAYLIB_DIR}")
endif()

if(NOT RAYLIB_SOURCE_DIR OR NOT EXISTS "${RAYLIB_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "RAYLIB_SOURCE_DIR not found. Set RAYLIB_DIR environment variable.")
endif()

# Build Raylib targeting Android GLES
set(PLATFORM "Android" CACHE STRING "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
set(SUPPORT_MODULE_RAUDIO OFF CACHE BOOL "" FORCE)
set(USE_AUDIO OFF CACHE BOOL "" FORCE)
add_subdirectory("${RAYLIB_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/raylib_build" EXCLUDE_FROM_ALL)

# Resolve SQLite amalg directory
if(NOT SQLITE_AMALGAMATION_DIR AND DEFINED ENV{SQLITE_AMALG})
    set(SQLITE_AMALGAMATION_DIR "$ENV{SQLITE_AMALG}")
endif()

if(NOT SQLITE_AMALGAMATION_DIR OR NOT EXISTS "${SQLITE_AMALGAMATION_DIR}/sqlite3.c")
    message(FATAL_ERROR "SQLITE_AMALGAMATION_DIR not found. Set SQLITE_AMALG environment variable.")
endif()

# Resolve Opus, Ogg, and Opusfile source directories
if(NOT OGG_SOURCE_DIR AND DEFINED ENV{OGG_DIR})
    set(OGG_SOURCE_DIR "$ENV{OGG_DIR}")
endif()
if(NOT OPUS_SOURCE_DIR AND DEFINED ENV{OPUS_DIR})
    set(OPUS_SOURCE_DIR "$ENV{OPUS_DIR}")
endif()
if(NOT OPUSFILE_SOURCE_DIR AND DEFINED ENV{OPUSFILE_DIR})
    set(OPUSFILE_SOURCE_DIR "$ENV{OPUSFILE_DIR}")
endif()

# Fallback, check sibling folders if OPUS_SOURCE_DIR is set
if(OPUS_SOURCE_DIR)
    if(NOT OGG_SOURCE_DIR AND EXISTS "${OPUS_SOURCE_DIR}/../ogg/CMakeLists.txt")
        set(OGG_SOURCE_DIR "${OPUS_SOURCE_DIR}/../ogg")
    endif()
    if(NOT OPUSFILE_SOURCE_DIR AND EXISTS "${OPUS_SOURCE_DIR}/../opusfile/include/opusfile.h")
        set(OPUSFILE_SOURCE_DIR "${OPUS_SOURCE_DIR}/../opusfile")
    endif()
endif()

set(OPUS_SOURCES "")
set(OPUS_LIBS "")
if(OGG_SOURCE_DIR AND OPUS_SOURCE_DIR AND OPUSFILE_SOURCE_DIR)
    if(EXISTS "${OGG_SOURCE_DIR}/CMakeLists.txt" AND EXISTS "${OPUS_SOURCE_DIR}/CMakeLists.txt" AND EXISTS "${OPUSFILE_SOURCE_DIR}/include/opusfile.h")
        message(STATUS "==> Opus Codec support enabled")

        set(CMAKE_POSITION_INDEPENDENT_CODE ON CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
        set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)

        add_subdirectory("${OGG_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/ogg_build" EXCLUDE_FROM_ALL)
        add_subdirectory("${OPUS_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/opus_build" EXCLUDE_FROM_ALL)

        if(TARGET opus)
            set_target_properties(opus PROPERTIES
                POSITION_INDEPENDENT_CODE ON
                C_VISIBILITY_PRESET hidden
            )
            target_compile_options(opus PRIVATE -fPIC -fvisibility=hidden)
        endif()

        if(TARGET ogg)
            set_target_properties(ogg PROPERTIES
                POSITION_INDEPENDENT_CODE ON
            )
            target_compile_options(ogg PRIVATE -fPIC)
        endif()

        add_library(opusfile_static STATIC
            ${OPUSFILE_SOURCE_DIR}/src/info.c
            ${OPUSFILE_SOURCE_DIR}/src/internal.c
            ${OPUSFILE_SOURCE_DIR}/src/opusfile.c
            ${OPUSFILE_SOURCE_DIR}/src/stream.c
        )
        target_include_directories(opusfile_static PUBLIC
            ${OPUSFILE_SOURCE_DIR}/include
            ${OPUSFILE_SOURCE_DIR}/src
            ${OGG_SOURCE_DIR}/include
            ${CMAKE_CURRENT_BINARY_DIR}/ogg_build/include
            ${OPUS_SOURCE_DIR}/include
        )
        target_link_libraries(opusfile_static PRIVATE opus ogg)

        set(OPUS_SOURCES
            ${KONI_ROOT}/src/codecs/opus_codec/opus_codec.c
            ${KONI_ROOT}/src/codecs/opus_codec/metadata.c
        )
        set(OPUS_LIBS opusfile_static opus ogg)
        set(HAVE_OPUS TRUE)
    endif()
endif()

# Compile SQLite amalg
add_library(sqlite3_amalg STATIC "${SQLITE_AMALGAMATION_DIR}/sqlite3.c")
target_include_directories(sqlite3_amalg PUBLIC "${SQLITE_AMALGAMATION_DIR}")
target_compile_definitions(sqlite3_amalg PRIVATE
    SQLITE_THREADSAFE=1
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_RTREE
    SQLITE_DEFAULT_MEMSTATUS=0
)

# Koni core sources
set(KONI_CORE_SOURCES
    ${KONI_ROOT}/src/utils/koni_paths.c
    ${KONI_ROOT}/src/utils/state.c
    ${KONI_ROOT}/src/utils/file_list.c
    ${KONI_ROOT}/src/utils/vis_math.c
    ${KONI_ROOT}/src/utils/config_manager/config.c
    ${KONI_ROOT}/src/audio/audio_engine.c
    ${KONI_ROOT}/src/audio/output_device.c
    ${KONI_ROOT}/src/audio/stream_reader.c
    ${KONI_ROOT}/src/audio/dsp_rack.c
    ${KONI_ROOT}/src/audio/dc_blocker.c
    ${KONI_ROOT}/src/audio/limiter.c
    ${KONI_ROOT}/src/audio/replaygain/replaygain.c
    ${KONI_ROOT}/src/codecs/codec_registry.c
    ${KONI_ROOT}/src/codecs/pxtone_codec/pxtn_tables.c
    ${KONI_ROOT}/src/codecs/pxtone_codec/pxtn_noise.c
    ${KONI_ROOT}/src/codecs/pxtone_codec/pxtn_loader.c
    ${KONI_ROOT}/src/codecs/pxtone_codec/pxtn_synth.c
    ${KONI_ROOT}/src/codecs/pxtone_codec/pxtone_codec.c
    ${KONI_ROOT}/src/codecs/ogg_codec/ogg_codec.c
    ${KONI_ROOT}/src/codecs/ogg_codec/metadata.c
    ${KONI_ROOT}/src/codecs/ma_codec/ma_codec.c
    ${KONI_ROOT}/src/codecs/ma_codec/metadata.c
    ${OPUS_SOURCES}
    ${KONI_ROOT}/src/database/db.c
    ${KONI_ROOT}/src/database/library_scanner.c
    ${KONI_ROOT}/src/playlist/playlist_manager.c
    ${KONI_ROOT}/src/profile/listening_profile.c
    ${KONI_ROOT}/src/equ/peq.c
    ${KONI_ROOT}/src/equ/equalizer.c
    ${KONI_ROOT}/src/audio/krystal/krystal_profiles.c
    ${KONI_ROOT}/src/audio/krystal/krystal_engine.c
    ${KONI_ROOT}/src/audio/krystal/krystal_preset_manager.c
    ${KONI_ROOT}/src/audio/krystal/krystal_oversample.c
    ${KONI_ROOT}/src/audio/krystal/krystal_saturator.c
    ${KONI_ROOT}/src/audio/krystal/krystal_spectral.c
    ${KONI_ROOT}/src/audio/krystal/krystal_transient.c
    ${KONI_ROOT}/src/audio/krystal/krystal_meter.c
    ${KONI_ROOT}/src/audio/krystal/krystal_loudness.c
    ${KONI_ROOT}/src/audio/krystal/krystal_bass.c
    ${KONI_ROOT}/src/audio/krystal/krystal_spatial.c
    ${KONI_ROOT}/src/audio/krystal/krystal_exciter.c
    ${KONI_ROOT}/src/extensions/ext_registry.c
    ${KONI_ROOT}/src/ui/ui_common.c
    ${KONI_ROOT}/src/lyrics/lyrics_engine.c
    ${KONI_ROOT}/src/lyrics/lyrics_registry.c
    ${KONI_ROOT}/src/lyrics/lrc_plugin/lrc_parser.c
    ${KONI_ROOT}/src/lyrics/lrc_plugin/lrc_net.c
    ${KONI_ROOT}/src/lyrics/ttml_plugin/ttml_plugin.c
)

# Sparkles sources
file(GLOB_RECURSE SPARKLES_SOURCES "${KONI_ROOT}/src/sparkles/*.c")

add_library(koni-sparkles SHARED
    ${KONI_CORE_SOURCES}
    ${SPARKLES_SOURCES}
)

target_include_directories(koni-sparkles PRIVATE
    ${ANDROID_NDK}/sources/android/native_app_glue
    ${KONI_ROOT}/src/include
    ${KONI_ROOT}/src/include/codec_api
    ${KONI_ROOT}/src/include/lyrics_api
    ${KONI_ROOT}/src/include/extensions_api
    ${KONI_ROOT}/src/include/third_party
    ${KONI_ROOT}/src/audio
    ${KONI_ROOT}/src/playlist
    ${KONI_ROOT}/src/equ
    ${KONI_ROOT}/src/audio/krystal
    ${KONI_ROOT}/src/ui/widgets
    ${KONI_ROOT}/src/codecs/pxtone_codec
    ${KONI_ROOT}/src/codecs/opus_codec
    ${KONI_ROOT}/src/sparkles
)

if(HAVE_OPUS)
    target_include_directories(koni-sparkles PRIVATE
        ${OPUSFILE_SOURCE_DIR}/include
        ${OPUSFILE_SOURCE_DIR}/include/opus
        ${OPUS_SOURCE_DIR}/include
        ${OGG_SOURCE_DIR}/include
    )
endif()

target_link_options(koni-sparkles PRIVATE "-uANativeActivity_onCreate" "-Wl,-Bsymbolic")

target_link_libraries(koni-sparkles PRIVATE
    -Wl,--whole-archive raylib -Wl,--no-whole-archive
    sqlite3_amalg
    ${OPUS_LIBS}
    GLESv2
    EGL
    android
    log
    aaudio
    OpenSLES
    m
)