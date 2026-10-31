# xemu's MCPX DSP interpreter (v0.8.139, hw/xbox/mcpx/apu/dsp,
# GPL-2.0-or-later), adapted from bloodwake-recomp's standalone build.
add_library(recomp_xemu_dsp STATIC
    bw_dsp.c
    dsp.c
    dsp_c.c
    dsp_dma.c
    interp/dsp_cpu.c
    interp/debug.c
)
target_include_directories(recomp_xemu_dsp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/shim
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/interp)
target_include_directories(recomp_xemu_dsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_options(recomp_xemu_dsp PRIVATE /W2 /wd4244 /wd4267 /wd4146 /wd4018)
    target_compile_options(recomp_xemu_dsp PRIVATE $<$<CONFIG:Release>:/GS- /Ob3 /Oi /Ot>)
endif()
