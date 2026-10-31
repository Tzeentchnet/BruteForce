cmake_minimum_required(VERSION 3.20)
project(BruteForceRecomp LANGUAGES C)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Brute Force bring-up currently requires 64-bit Windows.")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(XBOXRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.deps/xboxrecomp" CACHE PATH
    "Path to the pinned, patched xboxrecomp checkout")
set(BRUTEFORCE_GEN_DIR "" CACHE PATH "Directory containing this build's generated C")
set(BRUTEFORCE_COMPILE_JOBS 4 CACHE STRING "Parallel MSVC compilation jobs")
option(BRUTEFORCE_LINK_SYMBOLS "Generate local symbols for crash diagnostics" OFF)

if(NOT EXISTS "${XBOXRECOMP_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "xboxrecomp is missing. Run: python tools/toolkit.py")
endif()
foreach(required recomp_types.h recomp_funcs.h recomp_dispatch.c)
    if(NOT EXISTS "${BRUTEFORCE_GEN_DIR}/${required}")
        message(FATAL_ERROR "Missing ${required}. Run: python tools/build.py generate")
    endif()
endforeach()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/fingerprints/retail.json" BRUTEFORCE_FINGERPRINT)
string(JSON BRUTEFORCE_XBE_SHA256 GET "${BRUTEFORCE_FINGERPRINT}" xbe_sha256)
string(JSON BRUTEFORCE_XBE_SIZE GET "${BRUTEFORCE_FINGERPRINT}" xbe_size)
string(JSON BRUTEFORCE_XBE_ENTRY_POINT GET "${BRUTEFORCE_FINGERPRINT}" entry_point)
configure_file(src/bruteforce_config.h.in bruteforce_config.h @ONLY)

add_subdirectory("${XBOXRECOMP_DIR}" "${CMAKE_BINARY_DIR}/xboxrecomp")
add_subdirectory(src/xemu_dsp)
target_sources(xbox_apu PRIVATE src/audio/recomp_apu_xdsp.c)
target_compile_definitions(xbox_apu PRIVATE RECOMP_EXTERNAL_APU_DSP=1)
target_link_libraries(xbox_apu PUBLIC recomp_xemu_dsp)

file(GLOB RECOMP_SOURCES CONFIGURE_DEPENDS "${BRUTEFORCE_GEN_DIR}/*.c")
set(BRUTEFORCE_GPU_SOURCES src/gpu/d3d11_backend.c src/gpu/render_scale.c src/gpu/rc_hlsl.c
    src/gpu/shader_cache.c src/gpu/vsh_hlsl.c src/gpu/texture_decode.c)
add_executable(bruteforce src/main.c src/recomp_manual.c src/crash_diagnostics.c
    src/profile.c src/gpu/null_backend.c ${BRUTEFORCE_GPU_SOURCES} ${RECOMP_SOURCES})
target_compile_definitions(bruteforce PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_include_directories(bruteforce PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${BRUTEFORCE_GEN_DIR}"
    "${XBOXRECOMP_DIR}/include"
    "${XBOXRECOMP_DIR}/src"
    "${XBOXRECOMP_DIR}/src/kernel"
)
target_link_libraries(bruteforce PRIVATE xboxrecomp bcrypt dbghelp winmm d3d11 d3dcompiler)

add_executable(bruteforce_fence_test EXCLUDE_FROM_ALL tests/fence_test.c src/recomp_manual.c)
target_compile_definitions(bruteforce_fence_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_include_directories(bruteforce_fence_test PRIVATE
    "${XBOXRECOMP_DIR}/src"
    "${XBOXRECOMP_DIR}/src/kernel"
)
target_link_libraries(bruteforce_fence_test PRIVATE host_time)

add_executable(bruteforce_read_trace_test EXCLUDE_FROM_ALL tests/read_trace_test.c)
target_compile_definitions(bruteforce_read_trace_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_include_directories(bruteforce_read_trace_test PRIVATE
    "${XBOXRECOMP_DIR}/src"
    "${XBOXRECOMP_DIR}/src/kernel"
)
target_link_libraries(bruteforce_read_trace_test PRIVATE xboxrecomp)

add_executable(bruteforce_present_test EXCLUDE_FROM_ALL tests/present_test.c)
target_compile_definitions(bruteforce_present_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_include_directories(bruteforce_present_test PRIVATE "${XBOXRECOMP_DIR}/src")
target_link_libraries(bruteforce_present_test PRIVATE xboxrecomp)

add_executable(bruteforce_gpu_shared_present_test EXCLUDE_FROM_ALL tests/gpu_shared_present_test.c)
target_compile_definitions(bruteforce_gpu_shared_present_test PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_include_directories(bruteforce_gpu_shared_present_test PRIVATE "${XBOXRECOMP_DIR}/src")
target_link_libraries(bruteforce_gpu_shared_present_test PRIVATE d3d11 dxgi dxguid d3dcompiler)

add_executable(bruteforce_usb_test EXCLUDE_FROM_ALL tests/usb_test.c)
target_compile_definitions(bruteforce_usb_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_include_directories(bruteforce_usb_test PRIVATE "${XBOXRECOMP_DIR}/src")
target_link_libraries(bruteforce_usb_test PRIVATE xboxrecomp)

add_executable(bruteforce_interrupt_test EXCLUDE_FROM_ALL tests/interrupt_test.c src/recomp_manual.c)
target_compile_definitions(bruteforce_interrupt_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_include_directories(bruteforce_interrupt_test PRIVATE "${XBOXRECOMP_DIR}/templates/runtime")
target_link_libraries(bruteforce_interrupt_test PRIVATE xboxrecomp)

add_executable(bruteforce_crash_test EXCLUDE_FROM_ALL tests/crash_test.c src/crash_diagnostics.c)
target_compile_definitions(bruteforce_crash_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_include_directories(bruteforce_crash_test PRIVATE
    "${XBOXRECOMP_DIR}/src"
    "${XBOXRECOMP_DIR}/src/kernel"
    "${XBOXRECOMP_DIR}/src/platform"
)
target_link_libraries(bruteforce_crash_test PRIVATE dbghelp)

add_executable(bruteforce_memory_stats_test EXCLUDE_FROM_ALL tests/memory_stats_test.c)
target_compile_definitions(bruteforce_memory_stats_test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_include_directories(bruteforce_memory_stats_test PRIVATE
    "${XBOXRECOMP_DIR}/src"
    "${XBOXRECOMP_DIR}/src/kernel"
)
target_link_libraries(bruteforce_memory_stats_test PRIVATE xboxrecomp)

add_executable(bruteforce_pushbuffer_test EXCLUDE_FROM_ALL
    tests/pushbuffer_test.c "${XBOXRECOMP_DIR}/src/kernel/nv2a_pb_scan.c")
target_compile_definitions(bruteforce_pushbuffer_test PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)

add_executable(bruteforce_pgraph_test EXCLUDE_FROM_ALL tests/pgraph_test.c)
target_compile_definitions(bruteforce_pgraph_test PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_include_directories(bruteforce_pgraph_test PRIVATE
    "${XBOXRECOMP_DIR}/src"
    "${XBOXRECOMP_DIR}/src/kernel"
)
target_link_libraries(bruteforce_pgraph_test PRIVATE xboxrecomp)

add_executable(bruteforce_window_clip_test EXCLUDE_FROM_ALL tests/window_clip_test.c)
target_compile_definitions(bruteforce_window_clip_test PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_include_directories(bruteforce_window_clip_test PRIVATE
    "${XBOXRECOMP_DIR}/src"
    "${XBOXRECOMP_DIR}/src/kernel"
)
target_link_libraries(bruteforce_window_clip_test PRIVATE xboxrecomp)

add_executable(bruteforce_frame_stats_test EXCLUDE_FROM_ALL tests/frame_stats_test.c)
target_compile_definitions(bruteforce_frame_stats_test PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_include_directories(bruteforce_frame_stats_test PRIVATE
    "${XBOXRECOMP_DIR}/src"
    "${XBOXRECOMP_DIR}/src/kernel"
)
target_link_libraries(bruteforce_frame_stats_test PRIVATE xboxrecomp)

add_executable(bruteforce_exit_hook_test EXCLUDE_FROM_ALL tests/exit_hook_test.c)
target_include_directories(bruteforce_exit_hook_test PRIVATE
    "${XBOXRECOMP_DIR}/src/platform"
)
target_link_libraries(bruteforce_exit_hook_test PRIVATE recomp_exit_hook)

add_executable(bruteforce_apu_dsp_lifecycle_test EXCLUDE_FROM_ALL
    tests/apu_dsp_lifecycle_test.c)
target_compile_definitions(bruteforce_apu_dsp_lifecycle_test PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_include_directories(bruteforce_apu_dsp_lifecycle_test PRIVATE
    "${XBOXRECOMP_DIR}/src"
    "${XBOXRECOMP_DIR}/src/apu"
)
target_link_libraries(bruteforce_apu_dsp_lifecycle_test PRIVATE xboxrecomp)

add_executable(bruteforce_gpu_audit_test EXCLUDE_FROM_ALL tests/gpu_audit_test.c)
target_compile_definitions(bruteforce_gpu_audit_test PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_include_directories(bruteforce_gpu_audit_test PRIVATE
    "${XBOXRECOMP_DIR}/src"
    "${XBOXRECOMP_DIR}/src/kernel"
)
target_link_libraries(bruteforce_gpu_audit_test PRIVATE xboxrecomp)

add_executable(bruteforce_d3d11_test EXCLUDE_FROM_ALL tests/d3d11_test.c
    tests/d3d11_texture_test.c tests/shader_cache_test.c src/gpu/null_backend.c ${BRUTEFORCE_GPU_SOURCES})
target_compile_definitions(bruteforce_d3d11_test PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_include_directories(bruteforce_d3d11_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${XBOXRECOMP_DIR}/src"
    "${XBOXRECOMP_DIR}/src/kernel"
)
target_link_libraries(bruteforce_d3d11_test PRIVATE xboxrecomp d3d11 d3dcompiler dbghelp bcrypt)

add_executable(bruteforce_color_unpack_test EXCLUDE_FROM_ALL
    tests/color_unpack_test.c "${XBOXRECOMP_DIR}/src/kernel/nv2a_combiner.c")
target_include_directories(bruteforce_color_unpack_test PRIVATE
    "${XBOXRECOMP_DIR}/src/kernel")

add_executable(bruteforce_texture_math_test EXCLUDE_FROM_ALL tests/texture_math_test.c)
target_include_directories(bruteforce_texture_math_test PRIVATE
    "${XBOXRECOMP_DIR}/src/kernel")

if(MSVC)
    if(MSVC_VERSION LESS 1935)
        message(FATAL_ERROR "The generated memory fence requires MSVC 19.35 (Visual Studio 2022 17.5) or newer.")
    endif()
    target_compile_options(bruteforce PRIVATE
        /bigobj /experimental:c11atomics "/MP${BRUTEFORCE_COMPILE_JOBS}")
    target_compile_options(bruteforce_interrupt_test PRIVATE /experimental:c11atomics)
    target_link_options(bruteforce PRIVATE /LARGEADDRESSAWARE)
    if(BRUTEFORCE_LINK_SYMBOLS)
        foreach(symbol_target IN ITEMS bruteforce bruteforce_crash_test
                xbox_kernel xbox_d3d8 xbox_dsound xbox_input xbox_input_map
                xbox_usb xbox_apu xbox_nv2a xbox_video host_time recomp_exit_hook)
            target_compile_options(${symbol_target} PRIVATE /Z7)
        endforeach()
        target_link_options(bruteforce PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF)
        target_link_options(bruteforce_crash_test PRIVATE /DEBUG:FULL /OPT:REF /OPT:ICF)
    endif()
endif()
