#!/usr/bin/env python3
"""Aerial maps of every level for planning trailer shots (Trailer.md).

1. Survey: run each level with 8 AI players for 60 s in --preview mode with
   MANIAC_CINE_SURVEY=1, logging item spawn points and every player's path.
2. Render straight-down views tiled over the surveyed extent (camera 18 000
   units above the highest floor, 4:3 frames, the game's FOV), so every tile
   has the same scale; stitch them into one mosaic per level.
3. Overlay a 5 000-unit world grid with labels, the AI paths and numbered
   item spawn points.

Outputs build/trailer/aerial/L<n>_<name>.png (+ the raw survey logs).
In a top-down view +x is right and +z is up; at height h above a floor
point, one world unit is 720/h pixels in a 1440x1080 frame.

Usage (repository root, cinematic build, DISPLAY=:0):
  tools/trailer/aerial.py [--levels 0,1,2,3,4] [--skip-survey]
"""

import argparse
import collections
import os
import subprocess
import sys

from PIL import Image, ImageDraw

NAMES = ["ica", "woodshop", "orient", "aqua", "future"]
OUT = "build/trailer/aerial"
EXE = "./build-cine/maniac_rebuild"
TW, TH = 1440, 1080
HEIGHT = 18000.0                     # camera height above the highest floor
PX = (TW / 2) / HEIGHT               # pixels per world unit at the reference floor
TILE_X = TW / PX * 0.95              # world span per tile (5% overlap)
TILE_Z = TH / PX * 0.95
MARGIN = 3000


def run(shot_text, shot_path, out, env=None, preview=False):
    with open(shot_path, "w") as f:
        f.write(shot_text)
    cmd = [EXE, "--cinematic", shot_path, "--out", out] + (["--preview"] if preview else [])
    e = dict(os.environ, DISPLAY=os.environ.get("DISPLAY", ":0"), **(env or {}))
    return subprocess.run(cmd, env=e, capture_output=True, text=True, encoding="latin1").stderr


def survey(level):
    log = run(f"shot survey{level}\nlevel {level}\nplayers 8\nstart 6.0\nframes 1500\n",
              f"{OUT}/survey{level}.txt", f"{OUT}/survey{level}.mkv",
              env={"MANIAC_CINE_SURVEY": "1"}, preview=True)
    with open(f"{OUT}/survey{level}.log", "w", encoding="latin1") as f:
        f.write(log)
    os.remove(f"{OUT}/survey{level}.mkv")


def parse(level):
    items, paths = [], collections.defaultdict(list)
    for line in open(f"{OUT}/survey{level}.log", encoding="latin1"):
        p = line.split()
        if line.startswith("[survey] item"):
            items.append((int(p[2]), int(p[3]), int(p[4]), int(p[5]), " ".join(p[6:])))
        elif line.startswith("[survey] player"):
            paths[int(p[3])].append((int(p[4]), int(p[5]), int(p[6])))
    return items, paths


def render_tiles(level, x0, x1, z0, z1, cam_y):
    nx = max(1, int((x1 - x0) // TILE_X + 1))
    nz = max(1, int((z1 - z0) // TILE_Z + 1))
    tiles = []
    for iz in range(nz):
        for ix in range(nx):
            cx = x0 + (ix + 0.5) * (x1 - x0) / nx
            cz = z0 + (iz + 0.5) * (z1 - z0) / nz
            name = f"{OUT}/tile{level}_{ix}_{iz}"
            run(f"shot tile\nlevel {level}\nplayers 1\nstart 1.0\nframes 1\nsize {TW} {TH}\nzones all\n"
                "actor 0 hide\ncart 0 hide\n"
                f"key 0  {cx:.0f} {cam_y:.0f} {cz:.0f}   {cx:.0f} {cam_y + 10000:.0f} {cz + 1:.0f}   73.74\n",
                name + ".txt", name + ".mkv")
            subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", name + ".mkv", "-frames:v", "1",
                            name + ".png"], check=True)
            tiles.append((cx, cz, name + ".png"))
            for ext in (".txt", ".mkv"):
                os.remove(name + ext)
    return tiles


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--levels", default="0,1,2,3,4")
    ap.add_argument("--skip-survey", action="store_true")
    args = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)

    for level in (int(v) for v in args.levels.split(",")):
        if not args.skip_survey:
            survey(level)
        items, paths = parse(level)
        pts = [(x, y, z) for v in paths.values() for x, y, z in v] + [(x, y, z) for _, x, y, z, _ in items]
        x0 = min(p[0] for p in pts) - MARGIN
        x1 = max(p[0] for p in pts) + MARGIN
        z0 = min(p[2] for p in pts) - MARGIN
        z1 = max(p[2] for p in pts) + MARGIN
        top = min(p[1] for p in pts)              # highest floor (up is -y)
        cam_y = top - HEIGHT
        tiles = render_tiles(level, x0, x1, z0, z1, cam_y)

        # Mosaic at the reference scale; +x right, +z up.
        W, H = int((x1 - x0) * PX) + TW, int((z1 - z0) * PX) + TH
        mosaic = Image.new("RGB", (W, H), (0, 0, 0))
        ox, oz = x0 - TW / 2 / PX, z1 + TH / 2 / PX   # world point at mosaic pixel (0, 0)
        for cx, cz, path in tiles:
            im = Image.open(path)
            mosaic.paste(im, (int((cx - ox) * PX - TW / 2), int((oz - cz) * PX - TH / 2)))
            os.remove(path)

        def P(x, y, z):
            # Perspective: points on lower floors (larger y) appear closer to the tile centre;
            # the overlay uses the reference scale, which is exact on the highest floor.
            return ((x - ox) * PX, (oz - z) * PX)

        dr = ImageDraw.Draw(mosaic)
        for gx in range(int(x0 // 5000) * 5000, int(x1) + 1, 5000):
            dr.line([P(gx, 0, z0), P(gx, 0, z1)], fill=(90, 90, 90))
            dr.text(P(gx + 150, 0, z1 - 200), f"x {gx}", fill=(200, 200, 200))
        for gz in range(int(z0 // 5000) * 5000, int(z1) + 1, 5000):
            dr.line([P(x0, 0, gz), P(x1, 0, gz)], fill=(90, 90, 90))
            dr.text(P(x0 + 150, 0, gz + 400), f"z {gz}", fill=(200, 200, 200))
        cols = [(255, 80, 80), (80, 255, 80), (80, 160, 255), (255, 220, 60),
                (255, 80, 255), (80, 255, 255), (255, 160, 60), (220, 220, 220)]
        for k, v in paths.items():
            dr.line([P(x, y, z) for x, y, z in v[::4]], fill=cols[k % 8], width=2)
        for i, x, y, z, name in items:
            cx, cy = P(x, y, z)
            dr.ellipse([cx - 9, cy - 9, cx + 9, cy + 9], outline=(255, 255, 255), width=3)
            dr.text((cx + 11, cy - 7), f"{i} (y {y})", fill=(255, 255, 255))
        dr.text((20, 20), f"L{level} {NAMES[level]}: x {x0}..{x1}, z {z0}..{z1}, floors y {top}..",
                fill=(255, 255, 255))
        out = f"{OUT}/L{level}_{NAMES[level]}.png"
        mosaic.save(out)
        print(out, mosaic.size, f"{len(tiles)} tiles")
    return 0


if __name__ == "__main__":
    sys.exit(main())
