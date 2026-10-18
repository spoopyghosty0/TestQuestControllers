#!/usr/bin/env python3
"""Writes the release's install manifest (frameport-install.json, schema framedrop.install/v1) for an APK.

    python3 tools/make_install_manifest.py --apk FILE --tag vX.Y.Z [--repo owner/name] [--out FILE]

The release workflow attaches it to the release and commits it to main, where the README's "Install with FramePort"
button reads it (raw.githubusercontent.com: the install page can read it there, CORS). It names the release's APK by
its versioned address with its SHA-256.
"""
import argparse
import hashlib
import json
from pathlib import Path


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--apk", required=True)
    ap.add_argument("--tag", required=True)
    ap.add_argument("--repo", default="spoopyghosty0/TestQuestControllers")
    ap.add_argument("--out", default="frameport-install.json")
    args = ap.parse_args()
    apk = Path(args.apk)
    base = f"https://github.com/{args.repo}/releases/download/{args.tag}"
    manifest = {
        "schema": "framedrop.install/v1",
        "name": "Test Quest Controllers",
        # FramePort offers an update when a newer manifest (same address) has a higher version
        "version": args.tag.lstrip("v"),
        "files": [{"url": f"{base}/{apk.name}", "sha256": hashlib.sha256(apk.read_bytes()).hexdigest()}],
        "frameport": {
            "description": "Pick a Quest controller type and see every input the runtime delivers for it on a 3D "
                           "model.",
            "icon": f"https://raw.githubusercontent.com/{args.repo}/{args.tag}/docs/icon.png",
        },
    }
    Path(args.out).write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"wrote {args.out}")


if __name__ == "__main__":
    main()
