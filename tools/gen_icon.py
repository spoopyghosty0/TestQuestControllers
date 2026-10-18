#!/usr/bin/env python3
"""Makes the app icon from the host renderer's close-up of the Touch model: docs/icon.png (512 px) and the
launcher mipmaps. Build tools/render_host first (see README).

    python3 tools/gen_icon.py
"""
import subprocess
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
MIPMAPS = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}


def main():
    shot = ROOT / "out/icon.ppm"
    subprocess.run([str(ROOT / "out/render_host/render_host"), str(ROOT / "app/src/main/assets"), str(shot), "0",
                    "3.4", "--icon"], check=True, capture_output=True)
    arr = np.array(Image.open(shot).convert("RGBA"))
    arr[(arr[:, :, :3] < 6).all(axis=2), 3] = 0  # the cleared background
    model = Image.fromarray(arr)
    model = model.crop(model.getbbox())  # the controller alone, so it can fill the icon
    fit = 450 / max(model.size)
    model = model.resize((round(model.width * fit), round(model.height * fit)), Image.LANCZOS)

    # a light diagonal blue -> orange gradient: the white controller stays readable at 48-64 px
    size = 512
    t = np.clip((np.arange(size)[None, :] + np.arange(size)[:, None]) / (2 * size - 2), 0, 1)[..., None]
    blue, orange = np.array([38, 96, 214], float), np.array([236, 118, 34], float)
    grad = (blue * (1 - t) + orange * t).astype(np.uint8)
    bg = Image.fromarray(np.dstack([grad, np.full((size, size, 1), 255, np.uint8)]), "RGBA")
    at = ((size - model.width) // 2, (size - model.height) // 2)
    shadow = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    shadow.paste((10, 14, 30, 150), (at[0], at[1] + 14), model.getchannel("A"))
    bg = Image.alpha_composite(bg, shadow.filter(ImageFilter.GaussianBlur(14)))
    bg.alpha_composite(model, at)
    mask = Image.new("L", (size, size), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, size - 1, size - 1), radius=110, fill=255)
    icon = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    icon.paste(bg, (0, 0), mask)

    icon.save(ROOT / "docs/icon.png")
    for name, px in MIPMAPS.items():
        folder = ROOT / f"app/src/main/res/mipmap-{name}"
        folder.mkdir(parents=True, exist_ok=True)
        icon.resize((px, px), Image.LANCZOS).save(folder / "ic_launcher.png")
    print(f"wrote docs/icon.png and {len(MIPMAPS)} mipmaps")


if __name__ == "__main__":
    main()
