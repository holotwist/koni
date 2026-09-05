cmake_minimum_required(VERSION 3.16)
project(Koni VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)

include(CheckCCompilerFlag)

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(DBUS QUIET dbus-1)
endif()

find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED)

add_compile_options(-O3 -flto -ffast-math -funroll-loops -Wall -Wextra)
add_compile_definitions(NDEBUG)

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
else()
    set(DANA_SOURCES "")
endif()

set(MPRIS_SOURCES "")
if(DBUS_FOUND)
    add_compile_definitions(ENABLE_MPRIS)
    set(MPRIS_SOURCES src/protocols/mpris.c)
endif()

add_executable(koni
    src/main.c
    src/utils/state.c
    src/utils/file_list.c
    src/utils/vis_math.c
    src/lyrics/lyrics_engine.c
    src/lyrics/lyrics_registry.c
    src/lyrics/lrc_plugin/lrc_parser.c
    src/lyrics/lrc_plugin/lrc_net.c
    src/lyrics/ttml_plugin/ttml_plugin.c
    src/audio/audio.c
    src/audio/replaygain/replaygain.c
    src/utils/config_manager/config.c
    src/codecs/codec_registry.c
    src/codecs/dana_codec/dana_codec.c
    src/codecs/dana_codec/metadata.c
    src/codecs/pxtone_codec/pxtn_tables.c
    src/codecs/pxtone_codec/pxtn_noise.c
    src/codecs/pxtone_codec/pxtn_loader.c
    src/codecs/pxtone_codec/pxtn_synth.c
    src/codecs/pxtone_codec/pxtone_codec.c
    src/codecs/ma_codec/ma_codec.c
    src/codecs/ma_codec/metadata.c
    src/ui/ui_common.c
    src/ui/ui_animations.c
    src/ui/widgets/ui_search.c
    src/database/db.c
    src/database/library_scanner.c
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
    src/extensions/ext_registry.c
    ${MPRIS_SOURCES}
    ${DANA_SOURCES}
)

target_include_directories(koni PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/include/codec_api>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/include/lyrics_api>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/include/extensions_api>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/extensions>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/include/third_party>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/codecs/pxtone_codec>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/protocols>
)

if(DBUS_FOUND)
    target_include_directories(koni PUBLIC ${DBUS_INCLUDE_DIRS} ${CURL_INCLUDE_DIRS} ${SQLite3_INCLUDE_DIRS})
    target_link_libraries(koni ncursesw pthread m ${CMAKE_DL_LIBS} ${DBUS_LIBRARIES} ${CURL_LIBRARIES} SQLite::SQLite3)
else()
    target_include_directories(koni PUBLIC ${CURL_INCLUDE_DIRS} ${SQLite3_INCLUDE_DIRS})
    target_link_libraries(koni ncursesw pthread m ${CMAKE_DL_LIBS} ${CURL_LIBRARIES} SQLite::SQLite3)
endif()