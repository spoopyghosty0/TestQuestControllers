#!/usr/bin/env python3
"""Checks the generated profile table against the models: every bindable input of every controller type lights up
a part that exists in that type's model (both hands), and every profile has a model.

    python3 tools/test_tables.py
"""
import json
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
INC = ROOT / "app/src/main/cpp/profiles_gen.inc"
MODELS = ROOT / "app/src/main/assets/webxr"
# inputs some WebXR models can't show (no component, no geometry, or nodes missing in the asset): panel only
NO_COMPONENT = {"system", "back", "stylus", "menu", "thumbrest"}


def part_for(subpath):
    """Same rule as part_for() in app/src/main/cpp/profiles.h."""
    if not subpath.startswith("/input/"):
        return ""
    ident = subpath[7:].split("/")[0]
    if ident in ("grip", "aim"):
        return ""
    for prefix, part in (("trigger", "trigger"), ("thumbstick", "thumbstick"), ("thumb", "thumbrest"),
                         ("stylus", "stylus")):
        if ident.startswith(prefix):
            return part
    return ident


def component_part(cid):
    """Same rule as component_part() in app/src/main/cpp/models.cpp."""
    named = {"xr-standard-trigger": "trigger", "xr-standard-squeeze": "squeeze",
             "xr-standard-thumbstick": "thumbstick", "xr-standard-touchpad": "trackpad"}
    return named.get(cid, cid[:-len("-button")] if cid.endswith("-button") else cid)


def model_parts(folder, hand):
    """Parts a model can show: components with a response whose value node has geometry below it (as the app
    assigns them, see models.cpp). Also returns the response nodes missing in the glb (gaps in the asset)."""
    lines = (folder / f"{hand}.responses").read_text().splitlines()
    asset = next(line.split()[1] for line in lines if line.startswith("asset "))
    data = (folder / asset).read_bytes()
    jlen = struct.unpack_from("<I", data, 12)[0]
    nodes = json.loads(data[20:20 + jlen])["nodes"]
    index = {n.get("name"): i for i, n in enumerate(nodes)}

    def has_mesh(i):
        return "mesh" in nodes[i] or any(has_mesh(c) for c in nodes[i].get("children", []))

    parts, missing = set(), []
    for line in lines:
        f = line.split()
        if f[0] != "response":
            continue
        names = [n for n in f[4:7] if n != "-"]
        gone = [n for n in names if n not in index]
        missing += gone
        if not gone and f[3] == "transform" and has_mesh(index[f[4]]):
            parts.add(component_part(f[1]))
    return parts, missing


def main():
    text = INC.read_text()
    tables = dict(re.findall(r"static const Component (kComponents\d+)\[\] = \{(.*?)\n\};", text, re.S))
    profiles = re.findall(r'\{"(/interaction_profiles/[^"]+)", "[^"]*", "[^"]*", [^,]+, (\d), "([\w-]+)", '
                          r"(kComponents\d+), (\d+)\}", text)
    errors, notes = [], []
    if not profiles:
        errors.append("no profiles parsed")
    for path, _minor, family, table, count in profiles:
        rows = re.findall(r'\{(kLeft|kRight|kBoth), "([^"]+)", (\w+), (true|false), [^}]+\}', tables[table])
        if len(rows) != int(count):
            errors.append(f"{path}: {len(rows)} rows, count says {count}")
        for hand_name, hand in (("left", ("kLeft", "kBoth")), ("right", ("kRight", "kBoth"))):
            folder = MODELS / family
            if not (folder / f"{hand_name}.responses").exists():
                errors.append(f"{path}: model {family}/{hand_name} missing")
                continue
            parts, missing = model_parts(folder, hand_name)
            for n in missing:
                note = f"{family}/{hand_name}: node {n} missing in the asset"
                if note not in notes:
                    notes.append(note)
            for h, sub, _type, _system in rows:
                part = part_for(sub)
                if h in hand and part and part not in parts and part not in NO_COMPONENT:
                    errors.append(f"{path}: {hand_name} {sub} -> part '{part}' has no component in {family}")
        for _h, sub, _t, _s in rows:
            if not re.fullmatch(r"/(input|output)/[a-z0-9_/]+", sub):
                errors.append(f"{path}: odd path {sub}")
    for n in notes:
        print("note", n)
    for e in errors:
        print("FAIL", e)
    print(f"{len(profiles)} profiles checked, {len(errors)} problem(s)")
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
