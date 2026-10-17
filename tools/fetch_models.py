#!/usr/bin/env python3
"""Copies the controller models from the WebXR Input Profiles assets (MIT, github.com/immersive-web/
webxr-input-profiles) into app/src/main/assets/webxr/<profile id>/.

    python3 tools/fetch_models.py

Downloads the pinned npm package (checked against its SHA-256) and copies, per profile: the .glb files and
profile.json unchanged, plus <hand>.responses, a plain-text form of the profile's visual responses for the app:

    asset <file>
    root <root node>
    component <component id> <component root node> [<touch point node>]
    response <component id> <button|xAxis|yAxis|state> <transform|visibility> <value node> <min node> <max node> <states>
"""
import hashlib
import io
import json
import shutil
import tarfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "app/src/main/assets/webxr"
PACKAGE = "https://registry.npmjs.org/@webxr-input-profiles/assets/-/assets-1.0.20.tgz"
SHA256 = "30df2a2268220fc0d0e034bed1550aabdd7a2500573c5216f64fc70d59c3d91e"
PROFILES = ["oculus-touch", "oculus-touch-v2", "oculus-touch-v3", "meta-quest-touch-plus-v2", "meta-quest-touch-pro",
            "oculus-go"]


def responses(layout):
    lines = [f"asset {layout['assetPath']}", f"root {layout['rootNodeName']}"]
    for cid, comp in layout["components"].items():
        touch = comp.get("touchPointNodeName", "")
        lines.append(f"component {cid} {comp['rootNodeName']} {touch}".rstrip())
        for r in comp.get("visualResponses", {}).values():
            if r is None:
                continue
            lines.append(" ".join(["response", cid, r["componentProperty"], r["valueNodeProperty"], r["valueNodeName"],
                                   r.get("minNodeName", "-"), r.get("maxNodeName", "-"), ",".join(r["states"])]))
    return "\n".join(lines) + "\n"


def main():
    with urllib.request.urlopen(PACKAGE, timeout=120) as resp:
        data = resp.read()
    digest = hashlib.sha256(data).hexdigest()
    if digest != SHA256:
        raise SystemExit(f"package checksum {digest} != {SHA256}")
    if OUT.exists():
        shutil.rmtree(OUT)
    with tarfile.open(fileobj=io.BytesIO(data)) as tar:
        def read(name):
            return tar.extractfile(f"package/{name}").read()
        OUT.mkdir(parents=True)
        (OUT / "LICENSE.md").write_bytes(read("LICENSE.md"))
        for pid in PROFILES:
            src = f"dist/profiles/{pid}"
            dest = OUT / pid
            dest.mkdir()
            profile = json.loads(read(f"{src}/profile.json"))
            (dest / "profile.json").write_bytes(read(f"{src}/profile.json"))
            for hand in ("left", "right"):
                layout = profile["layouts"].get(hand) or profile["layouts"]["none"]
                asset = layout["assetPath"]
                if not (dest / asset).exists():
                    (dest / asset).write_bytes(read(f"{src}/{asset}"))
                (dest / f"{hand}.responses").write_text(responses(layout))
            print(f"{pid}: {sorted(p.name for p in dest.iterdir())}")


if __name__ == "__main__":
    main()
