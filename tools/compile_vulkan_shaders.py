#!/usr/bin/env python3
"""Compile the Vulkan bring-up shaders with the pinned DXC."""

import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

from dependencies import DXC_TAG


SHADERS = (
    ("bruteforce_vk_base_vs", "base_vs", "vs_6_0"),
    ("bruteforce_vk_base_ps", "base_ps", "ps_6_0"),
    ("bruteforce_vk_clear_vs", "clear_vs", "vs_6_0"),
    ("bruteforce_vk_clear_ps", "clear_ps", "ps_6_0"),
    ("bruteforce_vk_extended_vs", "extended_vs", "vs_6_0"),
    ("bruteforce_vk_extended_ps", "extended_ps", "ps_6_0"),
    ("bruteforce_vk_vsh_vs", "vsh_vs", "vs_6_0"),
    ("bruteforce_vk_vsh_gs", "vsh_gs", "gs_6_0"),
    ("bruteforce_vk_vsh_verify_cs", "vsh_verify_cs", "cs_6_0"),
)


def compile_shader(dxc, source, entry, profile, output):
    command = [
        str(dxc),
        "-spirv",
        "-fspv-target-env=vulkan1.3",
        "-fvk-use-dx-layout",
        "-Ges",
        "-O3",
        "-Qstrip_debug",
        "-E", entry,
        "-T", profile,
        "-Fo", str(output),
        str(source),
    ]
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    if result.returncode:
        raise RuntimeError(
            f"DXC failed for {entry}/{profile}:\n{result.stdout}{result.stderr}"
        )


def words(path):
    data = Path(path).read_bytes()
    if not data or len(data) % 4:
        raise RuntimeError(f"Invalid SPIR-V output: {path}")
    return struct.unpack(f"<{len(data) // 4}I", data)


def write_header(path, compiled):
    lines = [
        "#ifndef BRUTEFORCE_VULKAN_SHADERS_H",
        "#define BRUTEFORCE_VULKAN_SHADERS_H",
        "",
        "#include <stdint.h>",
        "",
        f'#define BRUTEFORCE_VULKAN_DXC_ID "{DXC_TAG}"',
        "",
    ]
    for name, binary in compiled:
        shader_words = words(binary)
        lines.append(f"static const uint32_t {name}[] = {{")
        for start in range(0, len(shader_words), 8):
            group = shader_words[start:start + 8]
            lines.append("    " + ", ".join(f"0x{word:08x}u" for word in group) + ",")
        lines += [
            "};",
            f"#define {name.upper()}_SIZE (sizeof({name}))",
            "",
        ]
    lines += ["#endif", ""]
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    temporary.write_text("\n".join(lines), encoding="ascii")
    temporary.replace(output)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dxc", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    if not args.dxc.is_file():
        parser.error(f"DXC executable not found: {args.dxc}")
    if not args.source.is_file():
        parser.error(f"shader source not found: {args.source}")
    with tempfile.TemporaryDirectory(prefix="bruteforce-vulkan-shaders-") as temp:
        compiled = []
        for name, entry, profile in SHADERS:
            binary = Path(temp) / f"{entry}.spv"
            compile_shader(args.dxc, args.source, entry, profile, binary)
            compiled.append((name, binary))
        write_header(args.output, compiled)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
