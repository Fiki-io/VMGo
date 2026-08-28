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
    core/guest_bootstrapper.cpp
    core/elf_loader.cpp
    core/virtual_binder.cpp
    core/user_kernel.cpp
    core/init_sequencer.cpp
    core/crash_handler.cpp
    hal/qemu_pipe_server.cpp
    graphics/egl_renderer.cpp
    input/input_dispatcher.cpp
    storage/sparse_parser.cpp
    storage/ext4_extractor.cpp
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

# Standalone Executable Loader (placed in nativeLibraryDir with valid exec permissions)
add_executable(
    vmgo_loader
    core/loader_main.cpp
    core/seccomp_trap.cpp
    core/vfs_router.cpp
    core/virtual_binder.cpp
    core/user_kernel.cpp
    core/crash_handler.cpp
    core/elf_loader.cpp
    core/jump_to_entry.S
)
set_target_properties(vmgo_loader PROPERTIES OUTPUT_NAME "libvmgo_loader.so")
target_link_libraries(
    vmgo_loader
    ${log-lib}
)

