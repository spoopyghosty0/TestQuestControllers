#!/usr/bin/env python3
"""Renders the app icon from the Touch model: docs/icon.png (512 px) and the launcher mipmaps.

    python3 tools/gen_icon.py
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, str(Path(__file__).resolve().parent))
import preview_models as pm  # noqa: E402

ROOT = pm.ROOT
MIPMAPS = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}


def main():
    size = 512
    parts = pm.load_glb(pm.MODELS / "touch_q2_left.glb")
    grip_from_auth = np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]], float)
    tilt = np.radians(40)
    z = np.array([0.35, np.cos(tilt), np.sin(tilt)])
    z /= np.linalg.norm(z)
    x = np.cross([0, 0, -1.0], z)
    x /= np.linalg.norm(x)
    view = np.array([x, np.cross(z, x), z]) @ grip_from_auth.T
    pm.COLORS.update({"x": (255, 110, 20), "y": (255, 110, 20), "thumbstick": (60, 140, 255),
                      "thumbrest": (95, 100, 110), "squeeze": (95, 100, 110), "trigger": (60, 140, 255)})
    model = pm.render(parts, view, 420).convert("RGBA")
    # transparent background where the renderer left its backdrop colour
    arr = np.array(model)
    arr[(arr[:, :, 0] == 18) & (arr[:, :, 1] == 18) & (arr[:, :, 2] == 18), 3] = 0
    model = Image.fromarray(arr)

    icon = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    bg = Image.new("RGBA", (size, size), (14, 16, 24, 255))
    glow = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(glow)
    d.ellipse((60, 70, 300, 310), fill=(40, 110, 255, 110))
    d.ellipse((220, 200, 460, 440), fill=(255, 110, 20, 110))
    bg = Image.alpha_composite(bg, glow.filter(ImageFilter.GaussianBlur(70)))
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, size - 1, size - 1), radius=110, fill=255)
    icon.paste(bg, (0, 0), mask)
    icon.alpha_composite(model, ((size - 420) // 2, (size - 420) // 2 + 6))

    out = ROOT / "docs/icon.png"
    out.parent.mkdir(exist_ok=True)
    icon.save(out)
    for name, px in MIPMAPS.items():
        folder = ROOT / f"app/src/main/res/mipmap-{name}"
        folder.mkdir(parents=True, exist_ok=True)
        icon.resize((px, px), Image.LANCZOS).save(folder / "ic_launcher.png")
    print(f"wrote {out.relative_to(ROOT)} and {len(MIPMAPS)} mipmaps")


if __name__ == "__main__":
    main()
