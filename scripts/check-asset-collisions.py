#!/usr/bin/env python3
"""Gate on custom-asset path collisions between soh and mm (issue #97).

Both games pack <game>/assets/custom into their own archive under ONE shared
resource-path namespace; a path present in both with different bytes silently
resolves to the wrong game's asset if a cross-game draw loses its @oot:/@mm:
route marker. This script diffs the two tracked trees against the checked-in
baseline (asset-collisions.json) and fails on NEW differing-content collisions
or STALE baseline entries. Regenerate the baseline with --update.
Content is compared by git blob OID (staged/committed bytes), so line-ending
smudge can't skew results across platforms; stage new assets before running.
See docs/UPSTREAM_MERGES.md "Standing policy: custom asset path collisions".
"""

import argparse
import json
import os
import subprocess
import sys

TREES = {"soh": "soh/assets/custom", "mm": "mm/assets/custom"}
# soh/assets/custom/shaders/ is untracked, wiped + re-copied from here on every GenerateSohOtr;
# overlay this source so shader drift against mm's tracked snapshot is gated too.
SOH_SHADER_SRC = "libultraship/src/fast/shaders"
BASELINE = "asset-collisions.json"
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def tracked_blobs(tree):
    # git ls-files, not a filesystem walk: soh/assets/custom/shaders/ is
    # untracked build-generated content that must not count.
    out = subprocess.run(
        ["git", "ls-files", "-s", "-z", tree],
        cwd=REPO_ROOT, capture_output=True, check=True,
    ).stdout.decode("utf-8")
    prefix = tree + "/"
    blobs = {}
    for entry in out.split("\0"):
        if not entry:
            continue
        meta, path = entry.split("\t", 1)
        blobs[path[len(prefix):]] = meta.split()[1]
    return blobs


def error(msg):
    if os.environ.get("GITHUB_ACTIONS"):
        print(f"::error::{msg}")
    print(f"ERROR: {msg}", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--update", action="store_true",
                        help=f"rewrite the collision list in {BASELINE}")
    args = parser.parse_args()

    try:
        soh = tracked_blobs(TREES["soh"])
        for p, oid in tracked_blobs(SOH_SHADER_SRC).items():
            soh["shaders/" + p] = oid
        mm = tracked_blobs(TREES["mm"])
    except (subprocess.CalledProcessError, FileNotFoundError) as e:
        if os.environ.get("GITHUB_ACTIONS"):
            error(f"git enumeration failed in CI: {e}")
            return 1
        print(f"WARNING: git enumeration failed ({e}); skipping asset-collision check.")
        return 0

    shared = soh.keys() & mm.keys()
    differing = sorted(p for p in shared if soh[p] != mm[p])

    baseline_path = os.path.join(REPO_ROOT, BASELINE)
    try:
        with open(baseline_path, encoding="utf-8") as f:
            baseline = json.load(f)
    except FileNotFoundError:
        if not args.update:
            error(f"{BASELINE} is missing; regenerate with --update.")
            return 1
        baseline = {"_comment": "", "collisions": []}

    if args.update:
        baseline["collisions"] = differing
        with open(baseline_path, "w", encoding="utf-8", newline="\n") as f:
            json.dump(baseline, f, indent=2)
            f.write("\n")
        print(f"Updated {BASELINE}: {len(differing)} differing-content collisions.")
        return 0

    known = set(baseline["collisions"])
    new = [p for p in differing if p not in known]
    stale = sorted(known - set(differing))

    for p in new:
        error(f"NEW custom-asset collision: '{p}' lands in both soh.o2r and 2ship.o2r "
              f"with different content. Rename the asset per-game, or add it to "
              f"{BASELINE} deliberately (it must never be drawn cross-game without "
              f"the @oot:/@mm: route marker).")
    for p in stale:
        error(f"STALE baseline entry: '{p}' is no longer a differing-content collision. "
              f"Remove it from {BASELINE} (rerun scripts/check-asset-collisions.py --update).")

    if new or stale:
        return 1
    print(f"Asset-collision check passed: {len(shared)} shared paths, "
          f"{len(differing)} known differing-content collisions, no new ones.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
