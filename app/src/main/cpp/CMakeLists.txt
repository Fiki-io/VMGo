cmake_minimum_required(VERSION 3.22.1)

project("vmgo_engine")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(
    include
    core
    hal
    graphics
    input
    storage
    jni
)

add_library(
    vmgo_engine
    SHARED
    core/vfs_router.cpp
    core/seccomp_trap.cpp
    hal/qemu_pipe_server.cpp
    graphics/egl_renderer.cpp
    input/input_dispatcher.cpp
    storage/sparse_parser.cpp
    jni/native_bridge.cpp
)

find_library(log-lib log)
find_library(android-lib android)
find_library(egl-lib EGL)
find_library(gles2-lib GLESv2)

target_link_libraries(
    vmgo_engine
    ${log-lib}
    ${android-lib}
    ${egl-lib}
    ${gles2-lib}
)
