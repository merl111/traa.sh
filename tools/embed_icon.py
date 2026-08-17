#!/usr/bin/env python3
"""Regenerate src/assets/icon_embedded.{c,h} from assets/icons/traash.png."""
from __future__ import annotations

import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("Pillow required: pip install Pillow", file=sys.stderr)
    sys.exit(1)

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "assets" / "icons" / "traash.png"
OUT_H = ROOT / "src" / "assets" / "icon_embedded.h"
OUT_C = ROOT / "src" / "assets" / "icon_embedded.c"
SIZES = (16, 32, 48, 64, 128, 256)


def main() -> None:
    img0 = Image.open(SRC).convert("RGBA")
    OUT_H.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.write_text(
        "#ifndef TRAASH_ICON_EMBEDDED_H\n"
        "#define TRAASH_ICON_EMBEDDED_H\n\n"
        "struct GLFWwindow;\n\n"
        "/* Set window icon from pixels baked into the binary (X11). No-op on Wayland. */\n"
        "void traash_window_set_icon(struct GLFWwindow *window);\n\n"
        "#endif\n"
    )
    parts = [
        '#include "assets/icon_embedded.h"',
        "",
        "#include <GLFW/glfw3.h>",
        "",
        "#include <stddef.h>",
        "",
    ]
    decls = []
    for s in SIZES:
        im = img0.resize((s, s), Image.Resampling.LANCZOS)
        data = im.tobytes()
        name = f"traash_icon_{s}_rgba"
        decls.append((s, name))
        parts.append(f"static const unsigned char {name}[{len(data)}] = {{")
        line = "  "
        for i, b in enumerate(data):
            line += f"{b},"
            if (i + 1) % 16 == 0:
                parts.append(line)
                line = "  "
        if line.strip():
            parts.append(line)
        parts.append("};")
        parts.append("")

    parts.append("void traash_window_set_icon(GLFWwindow *window) {")
    parts.append("  if (!window) {")
    parts.append("    return;")
    parts.append("  }")
    parts.append(f"  GLFWimage images[{len(SIZES)}];")
    for i, (s, name) in enumerate(decls):
        parts.append(f"  images[{i}].width = {s};")
        parts.append(f"  images[{i}].height = {s};")
        parts.append(f"  images[{i}].pixels = (unsigned char *){name};")
    parts.append(f"  glfwSetWindowIcon(window, {len(SIZES)}, images);")
    parts.append("}")
    parts.append("")
    OUT_C.write_text("\n".join(parts))
    print(f"wrote {OUT_C.relative_to(ROOT)} ({OUT_C.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
