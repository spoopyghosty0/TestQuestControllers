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
MODELS = ROOT / "app/src/main/assets/models"


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


def node_names(path):
    data = path.read_bytes()
    jlen = struct.unpack_from("<I", data, 12)[0]
    return {n["name"] for n in json.loads(data[20:20 + jlen])["nodes"]}


def main():
    text = INC.read_text()
    tables = dict(re.findall(r"static const Component (kComponents\d+)\[\] = \{(.*?)\n\};", text, re.S))
    profiles = re.findall(r'\{"(/interaction_profiles/[^"]+)", "[^"]*", "[^"]*", [^,]+, (\d), "(\w+)", '
                          r"(kComponents\d+), (\d+)\}", text)
    errors = []
    if not profiles:
        errors.append("no profiles parsed")
    for path, _minor, family, table, count in profiles:
        rows = re.findall(r'\{(kLeft|kRight|kBoth), "([^"]+)", (\w+), (true|false), [^}]+\}', tables[table])
        if len(rows) != int(count):
            errors.append(f"{path}: {len(rows)} rows, count says {count}")
        for hand_name, hand in (("left", ("kLeft", "kBoth")), ("right", ("kRight", "kBoth"))):
            model = MODELS / f"{family}_{hand_name}.glb"
            if not model.exists():
                errors.append(f"{path}: model {model.name} missing")
                continue
            nodes = node_names(model)
            for h, sub, _type, _system in rows:
                part = part_for(sub)
                if h in hand and part and part not in nodes:
                    errors.append(f"{path}: {hand_name} {sub} -> part '{part}' not in {model.name}")
        for _h, sub, _t, _s in rows:
            if not re.fullmatch(r"/(input|output)/[a-z0-9_/]+", sub):
                errors.append(f"{path}: odd path {sub}")
    for e in errors:
        print("FAIL", e)
    print(f"{len(profiles)} profiles checked, {len(errors)} problem(s)")
    sys.exit(1 if errors else 0)


if __name__ == "__main__":
    main()
