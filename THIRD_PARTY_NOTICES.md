# Third-party notices

This project downloads [xboxrecomp](https://github.com/sp00nznet/xboxrecomp)
at a pinned revision and applies the public pull requests listed in the README.
It also applies the project-local kernel-thunk fix in `patches/xboxrecomp`.
The framework and its contributions are under the MIT license below. Its
checkout also includes xemu-derived components under LGPL-2.1-or-later; see the
framework's `NOTICE`, individual source headers, and `LICENSES` directory for
the full notices and license terms. Distributing a linked executable can impose
additional obligations, including those concerning modified LGPL components
and relinking.

The build workflow was informed by
[bloodwake-recomp](https://github.com/minimalice92/bloodwake-recomp), whose
project-owned files are MIT-licensed. Its game-specific code, assets, and
additional GPL components are not included here.

Generic code adapted from bloodwake-recomp at commit
`e631edddfe56d22344e9f696923aeb6d78a9e7ba`, under its MIT license below:

- `src/profile.c`, from `src/bw_profile.c` (sampling profiler).
- `src/gpu/d3d11_backend.c`, from the generic parts of `src/gpu/bw_d3d11.c`
  (surfaces, depth buffers, texture cache, state objects, vertex ring,
  vertex-program shaders, their verification readback, and stale texture-pitch
  normalization; later physical-resolution surface sizing, scaled scissor and
  readback integration also follow that backend). The GPU texture decoder in
  `src/gpu/texture_decode.c`
  follows the framework's swizzle and colour-conversion semantics; Blood
  Wake's `fast_texture` is prior art for bypassing per-texel CPU decoding.
- `src/gpu/render_scale.c` and `render_scale.h`, from the generic render-scale,
  surface-size and full-screen resolve portions of `src/gpu/bw_vfx.c` and
  `src/gpu/bw_vfx_post.c` (bilinear seed, box downsample and Catmull-Rom
  upscale). Blood Wake-specific visual effects, hooks and assets are not used.
- `src/gpu/rc_hlsl.c` and `rc_hlsl.h`, from `src/gpu/bw_rc_hlsl.c` and `.h`
  (pixel shaders from register-combiner state).
- `src/gpu/vsh_hlsl.c` and `vsh_hlsl.h`, from `src/gpu/bw_vsh_hlsl.c` and `.h`
  (vertex shaders from NV2A vertex programs).
- `src/gpu/shader_cache.c`, whose persistent combiner pixel-shader caching
  design follows `src/gpu/bw_pscache.c`.
- In the local framework patches `0016-extended-render-backend.patch` and
  `0017-gpu-vertex-programs.patch`: the extended back-end interface (from
  `src/toolkit_patches/kernel/bw_backend_ext.h`) and the executor's GPU
  vertex-program hand-off and verification (from the carried
  `nv2a_pb_exec.c`).
- In `patches/xboxrecomp/0025-gpu-audit.patch`: generic bounded method and
  request accounting adapted from `src/gpu/bw_gpu_audit.c` and `.h`. The
  framework observer contains no Blood Wake-specific hooks or assets.

## bloodwake-recomp MIT license

MIT License

Copyright (c) 2026 the bloodwake-recomp contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## xboxrecomp MIT license

Copyright (c) 2026 sp00nz

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
