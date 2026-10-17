#!/usr/bin/env python3
"""Generates the controller models (app/src/main/assets/models/<family>_<hand>.glb).

    python3 tools/gen_models.py

The models are stylised look-alikes built from simple shapes, not copies of Meta's models (CC0, see README). Every
part the app highlights is its own node, named like the input it shows: x, y, a, b, menu, system, back, trigger,
squeeze, thumbstick, thumbrest, trackpad, stylus. Decoration nodes are body, ring and camera.

Frames: parts are authored with +Y = up along the handle (towards the buttons), -Z = forward (where the controller
points, trigger side), +X = right, then rotated into OpenXR's grip pose frame (-Z = along the handle towards the
thumb, +X = right, +Y = towards the user's wrist side) when written. Each node's own frame is what the app animates:
local +Y is the part's "out" direction (a button presses in along -Y, the stick stands along +Y and tilts about its
local X and Z axes), and the trigger hinges about its local X axis at the node's origin.
"""
import json
import math
import struct
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "app/src/main/assets/models"

# authoring frame -> grip frame: rotation of -90 degrees about X
R_GRIP = np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]], dtype=np.float64)


# ---------------------------------------------------------------- math helpers
def rot_x(deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def rot_y(deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])


def rot_z(deg):
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def quat(m):
    """Rotation matrix -> glTF quaternion (x, y, z, w)."""
    t = np.trace(m)
    if t > 0:
        s = math.sqrt(t + 1.0) * 2
        w, x, y, z = 0.25 * s, (m[2, 1] - m[1, 2]) / s, (m[0, 2] - m[2, 0]) / s, (m[1, 0] - m[0, 1]) / s
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = math.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2]) * 2
        w, x, y, z = (m[2, 1] - m[1, 2]) / s, 0.25 * s, (m[0, 1] + m[1, 0]) / s, (m[0, 2] + m[2, 0]) / s
    elif m[1, 1] > m[2, 2]:
        s = math.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2]) * 2
        w, x, y, z = (m[0, 2] - m[2, 0]) / s, (m[0, 1] + m[1, 0]) / s, 0.25 * s, (m[1, 2] + m[2, 1]) / s
    else:
        s = math.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1]) * 2
        w, x, y, z = (m[1, 0] - m[0, 1]) / s, (m[0, 2] + m[2, 0]) / s, (m[1, 2] + m[2, 1]) / s, 0.25 * s
    q = np.array([x, y, z, w])
    return q / np.linalg.norm(q)


# ---------------------------------------------------------------- shapes (local frame, +Y up)
class Mesh:
    def __init__(self, pos, idx):
        self.pos = np.asarray(pos, dtype=np.float64)
        self.idx = np.asarray(idx, dtype=np.uint32)

    def transformed(self, m=np.eye(3), t=(0, 0, 0)):
        return Mesh(self.pos @ np.asarray(m).T + np.asarray(t), self.idx)

    def __add__(self, other):
        return Mesh(np.vstack([self.pos, other.pos]), np.concatenate([self.idx, other.idx + len(self.pos)]))

    def normals(self):
        n = np.zeros_like(self.pos)
        tri = self.idx.reshape(-1, 3)
        fn = np.cross(self.pos[tri[:, 1]] - self.pos[tri[:, 0]], self.pos[tri[:, 2]] - self.pos[tri[:, 0]])
        for k in range(3):
            np.add.at(n, tri[:, k], fn)
        length = np.linalg.norm(n, axis=1, keepdims=True)
        return n / np.where(length == 0, 1, length)


def _grid(fn, nu, nv, wrap_u=True):
    """Surface from fn(u, v) with u in [0, 1) around and v in [0, 1] from bottom to top."""
    pos = []
    for j in range(nv + 1):
        for i in range(nu + (0 if wrap_u else 1)):
            pos.append(fn(i / nu, j / nv))
    width = nu + (0 if wrap_u else 1)
    idx = []
    for j in range(nv):
        for i in range(nu if wrap_u else nu):
            a = j * width + i
            b = j * width + (i + 1) % width if wrap_u else a + 1
            c, d = a + width, b + width
            idx += [a, c, b, b, c, d]
    return Mesh(pos, idx)


def _spow(x, e):
    return math.copysign(abs(x) ** e, x)


def superellipsoid(rx, ry, rz, e1=1.0, e2=1.0, nu=32, nv=18):
    """e = 1 sphere-like, smaller e = boxier (rounded box)."""
    def f(u, v):
        th = 2 * math.pi * u
        ph = math.pi * (v - 0.5)
        cp, sp = math.cos(ph), math.sin(ph)
        return (rx * _spow(cp, e1) * _spow(math.cos(th), e2), ry * _spow(sp, e1),
                -rz * _spow(cp, e1) * _spow(math.sin(th), e2))
    return _grid(f, nu, nv)


def cylinder(r, h, nu=24, rounded=0.0):
    """Capped cylinder from y=0 to y=h (rounded > 0 rounds the top edge)."""
    def f(u, v):
        th = 2 * math.pi * u
        # v: 0 bottom centre, .1 bottom rim, .9 top rim, 1 top centre
        if v <= 0.1:
            rr, y = r * v / 0.1, 0.0
        elif v >= 0.9:
            k = (1 - v) / 0.1
            rr, y = r * k, h - rounded * (1 - k)
            if rounded:
                rr = (r - rounded) * k + rounded * math.sin(math.acos(1 - k)) * k
        else:
            rr, y = r, h * (v - 0.1) / 0.8
        return (rr * math.cos(th), y, -rr * math.sin(th))
    return _grid(f, nu, 20)


def torus(big, small, nu=40, nv=12, arc=1.0):
    def f(u, v):
        th = 2 * math.pi * u * arc
        ph = 2 * math.pi * v
        rr = big + small * math.cos(ph)
        return (rr * math.cos(th), small * math.sin(ph), -rr * math.sin(th))
    if arc >= 1.0:
        return _grid(f, nu, nv)
    # open arc: no wrap in u, capped ends are left open (thin ring, not visible)
    pos, idx = [], []
    for i in range(nu + 1):
        for j in range(nv):
            pos.append(f(i / nu, j / nv))
    for i in range(nu):
        for j in range(nv):
            a, b = i * nv + j, i * nv + (j + 1) % nv
            c, d = a + nv, b + nv
            idx += [a, b, c, b, d, c]
    return Mesh(pos, idx)


# ---------------------------------------------------------------- parts
class Part:
    def __init__(self, name, mesh, t=(0, 0, 0), m=np.eye(3)):
        self.name, self.mesh, self.t, self.m = name, mesh, np.asarray(t, float), np.asarray(m, float)


def button(name, at, face_m, face_t, r=0.0062):
    """A round face button on the face plane (face_m/face_t = face frame), at = (x, z) on the face."""
    t = face_t + face_m @ np.array([at[0], 0.0, at[1]])
    return Part(name, cylinder(r, 0.0035, rounded=0.0015), t, face_m)


def touch_controller(hand, family):
    """Touch-style controller (touch_q1, touch_q2, touch_cv1, plus, pro). hand: +1 left, -1 right."""
    s = hand  # mirror x for the right hand
    parts = []
    # handle along +Y, centred at the grip origin
    handle = superellipsoid(0.0175, 0.056, 0.020, e1=0.9, e2=0.85).transformed(rot_x(-8), (0, -0.004, 0.002))
    # head on top, slightly forward
    head_rx = 0.034 if family in ("plus", "pro") else 0.031
    head = superellipsoid(head_rx, 0.016, 0.034, e1=0.55, e2=0.75).transformed(
        rot_x(28), (-s * 0.006, 0.058, -0.008))
    body = handle + head
    if family == "pro":  # tracking cameras on the front of the head
        for dx, dy, dz in ((0.0, 0.066, -0.038), (-s * 0.024, 0.058, -0.030), (s * 0.020, 0.052, -0.034)):
            body = body + superellipsoid(0.0055, 0.0055, 0.004).transformed(rot_x(60), (dx, dy, dz))
        parts.append(Part("stylus", superellipsoid(0.007, 0.006, 0.007), (0, -0.062, 0.004)))
    parts.append(Part("body", body))

    # face frame: centre of the top surface, normal tilted towards the user
    face_m = rot_x(28)
    face_t = np.array([-s * 0.006, 0.058, -0.008]) + face_m @ np.array([0, 0.0155, 0])
    # stick on the outer side, buttons inside, small button below
    if s > 0:
        b_low, b_high, small = "x", "y", "menu"
    else:
        b_low, b_high, small = "a", "b", "system"
    parts.append(button(b_low, (s * 0.009, 0.007), face_m, face_t))
    parts.append(button(b_high, (s * 0.017, -0.006), face_m, face_t))
    parts.append(button(small, (-s * 0.002, 0.020), face_m, face_t, r=0.0042))
    stick = cylinder(0.0028, 0.010) + cylinder(0.0085, 0.0045, rounded=0.003).transformed(t=(0, 0.010, 0))
    parts.append(Part("thumbstick", stick, face_t + face_m @ np.array([-s * 0.011, 0, -0.004]), face_m))
    rest = superellipsoid(0.0085, 0.0012, 0.0065, e1=0.4, e2=0.6)
    parts.append(Part("thumbrest", rest, face_t + face_m @ np.array([s * 0.024, 0.0005, 0.010]), face_m))

    # trigger: hinge at its top, hangs down in front of the handle
    trig = superellipsoid(0.0085, 0.017, 0.0055, e1=0.6, e2=0.7).transformed(rot_x(-15), (0, -0.015, -0.002))
    parts.append(Part("trigger", trig, (-s * 0.002, 0.052, -0.034), rot_x(10)))
    # squeeze (grip button) on the inner side of the handle, local +Y = outwards from the handle
    sq = superellipsoid(0.0075, 0.0035, 0.020, e1=0.6, e2=0.6)
    parts.append(Part("squeeze", sq, (s * 0.017, 0.008, -0.010), rot_z(-s * 90) @ rot_x(-8)))

    # tracking ring
    if family == "touch_q2":
        ring = torus(0.040, 0.0055).transformed(rot_z(s * 20) @ rot_x(40), (-s * 0.024, 0.080, 0.006))
        parts.append(Part("ring", ring))
    elif family == "touch_q1":
        ring = torus(0.046, 0.0055).transformed(rot_x(12), (-s * 0.010, 0.088, 0.002))
        parts.append(Part("ring", ring))
    elif family == "touch_cv1":
        ring = torus(0.044, 0.006).transformed(rot_z(s * 70) @ rot_x(10), (-s * 0.040, 0.030, -0.004))
        parts.append(Part("ring", ring))
    return parts


def go_controller(hand):
    s = hand
    parts = []
    body = superellipsoid(0.019, 0.013, 0.056, e1=0.6, e2=0.6).transformed(t=(0, 0, -0.024))
    parts.append(Part("body", body))
    top = np.array([0, 0.0125, 0.0])
    pad = cylinder(0.0145, 0.002, nu=32, rounded=0.001)
    parts.append(Part("trackpad", pad, top + (0, 0, -0.052)))
    parts.append(Part("back", cylinder(0.0045, 0.003, rounded=0.0012), top + (-s * 0.006, 0, -0.026)))
    parts.append(Part("system", cylinder(0.0045, 0.003, rounded=0.0012), top + (s * 0.006, 0, -0.026)))
    trig = superellipsoid(0.010, 0.0045, 0.012, e1=0.6, e2=0.6)
    parts.append(Part("trigger", trig, (0, -0.013, -0.058), rot_x(180)))
    return parts


# ---------------------------------------------------------------- glb writer
def write_glb(path, parts):
    bin_data = bytearray()
    accessors, buffer_views, meshes, nodes = [], [], [], []

    def add_view(data, target):
        while len(bin_data) % 4:
            bin_data.append(0)
        buffer_views.append({"buffer": 0, "byteOffset": len(bin_data), "byteLength": len(data), "target": target})
        bin_data.extend(data)
        return len(buffer_views) - 1

    for p in parts:
        pos = p.mesh.pos.astype(np.float32)
        nrm = p.mesh.normals().astype(np.float32)
        idx = p.mesh.idx.astype(np.uint16 if len(pos) < 65536 else np.uint32)
        pv = add_view(pos.tobytes(), 34962)
        accessors.append({"bufferView": pv, "componentType": 5126, "count": len(pos), "type": "VEC3",
                          "min": pos.min(0).tolist(), "max": pos.max(0).tolist()})
        nv = add_view(nrm.tobytes(), 34962)
        accessors.append({"bufferView": nv, "componentType": 5126, "count": len(nrm), "type": "VEC3"})
        iv = add_view(idx.tobytes(), 34963)
        accessors.append({"bufferView": iv, "componentType": 5123 if idx.dtype == np.uint16 else 5125,
                          "count": len(idx), "type": "SCALAR"})
        n = len(accessors)
        meshes.append({"name": p.name, "primitives": [{"attributes": {"POSITION": n - 3, "NORMAL": n - 2},
                                                       "indices": n - 1}]})
        t = R_GRIP @ p.t
        m = R_GRIP @ p.m
        node = {"name": p.name, "mesh": len(meshes) - 1}
        if np.any(np.abs(t) > 1e-9):
            node["translation"] = [round(float(v), 6) for v in t]
        q = quat(m)
        if abs(q[3]) < 1 - 1e-9:
            node["rotation"] = [round(float(v), 6) for v in q]
        nodes.append(node)

    gltf = {
        "asset": {"version": "2.0", "generator": "TestQuestControllers tools/gen_models.py",
                  "copyright": "CC0-1.0"},
        "scene": 0,
        "scenes": [{"nodes": list(range(len(nodes)))}],
        "nodes": nodes, "meshes": meshes, "accessors": accessors, "bufferViews": buffer_views,
        "buffers": [{"byteLength": len(bin_data)}],
    }
    js = json.dumps(gltf, separators=(",", ":")).encode()
    js += b" " * (-len(js) % 4)
    bin_data += b"\0" * (-len(bin_data) % 4)
    total = 12 + 8 + len(js) + 8 + len(bin_data)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(bin_data), 0x004E4942) + bytes(bin_data))


FAMILIES = ("touch_q2", "touch_q1", "touch_cv1", "plus", "pro", "go")


def build(family, hand):
    return go_controller(hand) if family == "go" else touch_controller(hand, family)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for family in FAMILIES:
        for hand, name in ((1, "left"), (-1, "right")):
            path = OUT / f"{family}_{name}.glb"
            write_glb(path, build(family, hand))
            print(f"wrote {path.relative_to(ROOT)} ({path.stat().st_size // 1024} KiB)")


if __name__ == "__main__":
    main()
