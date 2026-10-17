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
    model = Image.fromarray(arr).resize((400, 400), Image.LANCZOS)

    size = 512
    bg = Image.new("RGBA", (size, size), (14, 16, 24, 255))
    glow = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(glow)
    d.ellipse((50, 60, 300, 310), fill=(40, 110, 255, 120))
    d.ellipse((210, 200, 470, 450), fill=(255, 110, 20, 120))
    bg = Image.alpha_composite(bg, glow.filter(ImageFilter.GaussianBlur(70)))
    bg.alpha_composite(model, ((size - 400) // 2, (size - 400) // 2))
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
