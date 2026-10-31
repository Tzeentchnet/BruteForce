# xemu MCPX DSP notice

This directory contains the MCPX DSP56300 interpreter from xemu v0.8.139:

https://github.com/xemu-project/xemu/tree/v0.8.139/hw/xbox/mcpx/apu/dsp

The standalone build layout, wrapper, and compatibility shims were adapted
from bloodwake-recomp commit
`e631edddfe56d22344e9f696923aeb6d78a9e7ba`.

The xemu interpreter files are GPL-2.0-or-later, except these files, whose
headers specify LGPL-2.0-or-later:

- `dsp_dma.c`
- `dsp_dma.h`
- `dsp_dma_regs.h`

The `bw_dsp.*` wrapper and files under `shim/` support the standalone build.
The build uses the C interpreter and does not vendor xemu's JIT.

Copyright and license headers in individual files are authoritative. The
corresponding license texts are in the repository's `LICENSES/` directory:

- `GPL-2.0.txt`
- `LGPL-2.0.txt`
