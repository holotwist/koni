cmake_minimum_required(VERSION 3.16)
project(Koni VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)

include(CheckCCompilerFlag)

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

add_executable(koni
    src/main.c
    src/utils/state.c
    src/utils/file_list.c
    src/utils/vis_math.c
    src/audio/audio.c
    src/codecs/codec_registry.c
    src/codecs/dana_codec.c
    src/codecs/ma_codec.c
    src/ui/ui_common.c
    src/ui/ui_animations.c
    src/ui/ui_files.c
    src/ui/ui_playlist.c
    src/ui/ui_info.c
    src/ui/ui_vis.c
    src/ui/ui_player.c
    src/ui/ui_input.c
    src/ui/ui.c
    ${DANA_SOURCES}
)

target_include_directories(koni PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/include/codec_api>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/include/third_party>
)

target_link_libraries(koni ncursesw pthread m ${CMAKE_DL_LIBS})