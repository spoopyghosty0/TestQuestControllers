#!/usr/bin/env python3
"""Renders every model in app/src/main/assets/models to one PNG (default out/previews/models.png), coloured by part,
to check the models without a headset.

    python3 tools/preview_models.py [--out FILE] [--highlight PART]
"""
import argparse
import json
import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
MODELS = ROOT / "app/src/main/assets/models"
COLORS = {
    "body": (70, 74, 82), "ring": (95, 100, 110), "camera": (40, 40, 44),
    "x": (80, 160, 255), "a": (80, 160, 255), "y": (255, 150, 60), "b": (255, 150, 60),
    "menu": (200, 200, 200), "system": (200, 200, 200), "back": (200, 200, 200),
    "thumbstick": (120, 220, 120), "thumbrest": (220, 120, 220), "trigger": (240, 220, 80),
    "squeeze": (240, 90, 90), "trackpad": (120, 220, 120), "stylus": (90, 230, 230),
}


def quat_matrix(q):
    x, y, z, w = q
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def load_glb(path):
    data = path.read_bytes()
    jlen = struct.unpack_from("<I", data, 12)[0]
    gltf = json.loads(data[20:20 + jlen])
    binary = data[20 + jlen + 8:]

    def accessor(i):
        a = gltf["accessors"][i]
        v = gltf["bufferViews"][a["bufferView"]]
        dtype = {5126: np.float32, 5123: np.uint16, 5125: np.uint32}[a["componentType"]]
        width = {"VEC3": 3, "SCALAR": 1}[a["type"]]
        arr = np.frombuffer(binary, dtype, a["count"] * width, v["byteOffset"])
        return arr.reshape(-1, width) if width > 1 else arr

    parts = []
    for node in gltf["nodes"]:
        prim = gltf["meshes"][node["mesh"]]["primitives"][0]
        pos = accessor(prim["attributes"]["POSITION"]).astype(np.float64)
        idx = accessor(prim["indices"]).astype(np.int64).reshape(-1, 3)
        m = quat_matrix(node.get("rotation", [0, 0, 0, 1]))
        world = pos @ m.T + np.array(node.get("translation", [0, 0, 0]))
        parts.append((node["name"], world, idx))
    return parts


def render(parts, view, size=360, highlight=None):
    """view: rotation matrix grip -> camera (camera looks along -Z)."""
    img = np.zeros((size, size, 3), np.float64) + 18
    depth = np.full((size, size), np.inf)
    allpts = np.vstack([p[1] for p in parts]) @ view.T
    centre = (allpts.max(0) + allpts.min(0)) / 2
    scale = size * 0.8 / max(np.ptp(allpts[:, 0]), np.ptp(allpts[:, 1]))
    light = np.array([0.3, 0.5, 0.8])
    light /= np.linalg.norm(light)
    for name, pts, tri in parts:
        p = (pts @ view.T - centre) * scale
        sx, sy, sz = p[:, 0] + size / 2, size / 2 - p[:, 1], -p[:, 2]
        base = np.array(COLORS.get(name, (150, 150, 150)), float)
        if highlight and name == highlight:
            base = np.array((255, 120, 0), float)
        for a, b, c in tri:
            n = np.cross(p[b] - p[a], p[c] - p[a])
            ln = np.linalg.norm(n)
            if ln == 0:
                continue
            shade = 0.35 + 0.65 * abs(n @ light) / ln
            xs = np.array([sx[a], sx[b], sx[c]])
            ys = np.array([sy[a], sy[b], sy[c]])
            x0, x1 = int(max(xs.min(), 0)), int(min(xs.max() + 1, size))
            y0, y1 = int(max(ys.min(), 0)), int(min(ys.max() + 1, size))
            if x0 >= x1 or y0 >= y1:
                continue
            gx, gy = np.meshgrid(np.arange(x0, x1) + 0.5, np.arange(y0, y1) + 0.5)
            d = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2])
            if abs(d) < 1e-12:
                continue
            w0 = ((ys[1] - ys[2]) * (gx - xs[2]) + (xs[2] - xs[1]) * (gy - ys[2])) / d
            w1 = ((ys[2] - ys[0]) * (gx - xs[2]) + (xs[0] - xs[2]) * (gy - ys[2])) / d
            w2 = 1 - w0 - w1
            inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
            if not inside.any():
                continue
            z = w0 * sz[a] + w1 * sz[b] + w2 * sz[c]
            region = depth[y0:y1, x0:x1]
            closer = inside & (z < region)
            region[closer] = z[closer]
            img[y0:y1, x0:x1][closer] = base * shade
    return Image.fromarray(img.clip(0, 255).astype(np.uint8))


def rot(axis, deg):
    a = np.radians(deg)
    c, s = np.cos(a), np.sin(a)
    if axis == "x":
        return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])
    if axis == "y":
        return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "out/previews/models.png"))
    ap.add_argument("--highlight")
    args = ap.parse_args()
    files = sorted(MODELS.glob("*.glb"))
    # grip frame: -Z along the handle towards the thumb, -Y forward. "Top" looks down the handle at the face,
    # "side" looks from the right with the handle vertical.
    grip_from_auth = np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]], float)  # as in gen_models.R_GRIP

    def camera(z_axis, x_axis):  # camera frame given in the authoring frame -> grip-to-camera matrix
        z = np.asarray(z_axis, float) / np.linalg.norm(z_axis)
        x = np.asarray(x_axis, float)
        y = np.cross(z, x)
        return np.array([x, y, z]) @ grip_from_auth.T

    tilt = np.radians(28)
    views = {"face": camera((0, np.cos(tilt), np.sin(tilt)), (1, 0, 0)),  # looking down at the buttons
             "side": camera((-1, 0, 0), (0, 0, 1))}  # from the left, forward to the left
    tile = 300
    sheet = Image.new("RGB", (tile * len(views) * 2, (tile + 24) * ((len(files) + 1) // 2)), (10, 10, 12))
    draw = ImageDraw.Draw(sheet)
    for i, path in enumerate(files):
        parts = load_glb(path)
        col, row = i % 2, i // 2
        for j, (vname, view) in enumerate(views.items()):
            im = render(parts, view, tile, args.highlight)
            x, y = (col * len(views) + j) * tile, row * (tile + 24)
            sheet.paste(im, (x, y + 24))
            draw.text((x + 6, y + 6), f"{path.stem} ({vname})", fill=(230, 230, 230))
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    sheet.save(args.out)
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
