#!/usr/bin/env python3
"""Title cards and logo for the Mall Maniacs HD trailer (Trailer.md, phase T7).

Everything is built from the game's own art, decoded straight from the
shipped .tpg textures (256x256 8-bit indices + 256 RGBA palette, see
docs/10-fileformats.md and tests/render_font.c):
  - text is set in the menu font MFONT00.TPG with the metrics of
    menu/menyfont.txt (40x28 cells, 6 per row, per-glyph widths);
  - the logo is the menu's MALL MANIACS sign (SIGN100 + SIGN200) with an
    "HD" badge set in the same menu font.

Outputs (default build/trailer/cards/), all 1920x1080 RGBA PNG overlays
placed inside the 2.39:1 picture band (y 138..942):
  caption_1999.png      "1999"
  caption_rebuilt.png   "REBUILT. NOW IN HD."
  logo.png              sign + HD badge
  logo_glow.png         blurred bright copy of logo.png for the edit's bloom
  preview_*.png         the overlays composited over black and a footage frame

Usage:
  tools/trailer/cards.py [--data DIR] [--out DIR] [--footage FRAME.png]
"""

import argparse
import os
import sys

import numpy as np
from PIL import Image, ImageFilter

W, H = 1920, 1080
BAND_TOP, BAND_H = 138, 804            # 2.39:1 picture area inside 1080

CAPTIONS = {     # name: (text, scale (glyphs are 28 px tall), vertical centre as a fraction of the band)
    "caption_1999": ("1999", 3.0, 0.80),
    "caption_rebuilt": ("REBUILT. NOW IN HD.", 2.0, 0.80),
    "card_malls": ("5 MALLS", 4.0, 0.50),
    "card_maniacs": ("10 MANIACS", 4.0, 0.50),
    "card_list": ("1 SHOPPING LIST", 3.5, 0.50),
}
LOGO_BADGE = "HD"

CAPTION_TINT = (255, 244, 225)         # warm white for captions
BADGE_SCALE = 4.5
LOGO_SCALE = 2.0                       # 512x256 sign -> 1024x512


# --- .tpg decoding --------------------------------------------------------------

def load_tpg(path):
    data = open(path, "rb").read()
    idx = np.frombuffer(data[:65536], np.uint8).reshape(256, 256)
    pal = np.frombuffer(data[65536:65536 + 1024], np.uint8).reshape(256, 4)
    return pal[idx][:, :, :3].copy()


def key_black(rgb, lo=8, hi=40):
    """Alpha from brightness: the game colour-keys near-black texels, and
    anti-aliased glyph edges fade towards black, so ramp alpha over lo..hi."""
    lum = rgb.max(axis=2).astype(np.float32)
    alpha = np.clip((lum - lo) / (hi - lo), 0.0, 1.0)
    return alpha


# --- menu font ------------------------------------------------------------------

class MenuFont:
    """menyfont.txt: 40x28 cells, A-Z at 0, ÅÄÖ at 26..28, digits at 30
    (see below), punctuation per the positions table; lowercase maps to
    uppercase."""

    def __init__(self, data_dir):
        self.rgb = load_tpg(os.path.join(data_dir, "menu", "MFONT00.TPG"))
        text = open(os.path.join(data_dir, "menu", "menyfont.txt"), "rb").read().decode("latin1")
        self.cell_w, self.cell_h = 40, 28
        self.space, self.gap = 12, 2
        # Card-only kerning: the italic T's cell leaves a hole before the next
        # letter ("GIT HUB"); pull the following glyph in.
        self.kern_after = {"T": -5}
        self.index, self.width = {}, {}
        for i in range(26):
            self.index[chr(ord("A") + i)] = i
        # menyfont.txt says pos0 = 29, but in MFONT00.TPG cell 29 is the
        # empty slot after Ö and the digits occupy cells 30..39 (',' at 40
        # and '.' at 41 confirm the grid). The game would draw '1' as '0';
        # the cards use the atlas's real digit cells.
        for i in range(10):
            self.index[chr(ord("0") + i)] = 30 + i
        section = None
        for line in text.splitlines():
            line = line.strip()
            if line.startswith("positions"):
                section = "pos"
            elif line.startswith("widths"):
                section = "w"
            elif line.startswith("}"):
                section = None
            elif section and len(line) >= 3 and line[1] in " \t":
                ch, val = line[0], int(line[1:].strip().rstrip(";"))
                (self.index if section == "pos" else self.width)[ch] = val

    def glyph(self, ch):
        g = self.index[ch]
        x, y = (g % 6) * self.cell_w, (g // 6) * self.cell_h
        w = self.width.get(ch, self.cell_w)
        return self.rgb[y:y + self.cell_h, x:x + w]

    def render(self, text):
        """Return an RGBA image of the text at native size (28 px tall)."""
        text = text.upper()
        pieces, total = [], 0
        for i, ch in enumerate(text):
            if ch == " " or ch not in self.index:
                pieces.append((None, 0))
                total += self.space + self.gap
            else:
                g = self.glyph(ch)
                nxt = text[i + 1] if i + 1 < len(text) else " "
                kern = self.kern_after.get(ch, 0) if nxt != " " else 0
                pieces.append((g, kern))
                total += g.shape[1] + self.gap + kern
        rgb = np.zeros((self.cell_h, total + 8, 3), np.uint8)
        alpha = np.zeros((self.cell_h, total + 8), np.float32)
        x = 4
        for g, kern in pieces:
            if g is None:
                x += self.space + self.gap
                continue
            a = key_black(g)
            w = g.shape[1]
            sel = a > alpha[:, x:x + w]
            rgb[:, x:x + w][sel] = g[sel]
            alpha[:, x:x + w] = np.maximum(alpha[:, x:x + w], a)
            x += w + self.gap + kern
        return to_rgba(rgb, alpha)


def to_rgba(rgb, alpha):
    a = (np.clip(alpha, 0, 1) * 255).astype(np.uint8)
    return Image.fromarray(np.dstack([rgb, a]), "RGBA")


def scaled(im, factor):
    w, h = im.size
    return im.resize((round(w * factor), round(h * factor)), Image.LANCZOS)


def tinted(im, rgb):
    """Recolour keeping the glyph's own shading as brightness."""
    arr = np.asarray(im).astype(np.float32)
    lum = arr[:, :, :3].max(axis=2, keepdims=True) / 255.0
    out = np.concatenate([np.clip(lum * np.array(rgb, np.float32), 0, 255), arr[:, :, 3:]], axis=2)
    return Image.fromarray(out.astype(np.uint8), "RGBA")


def outlined(im, radius, colour=(0, 0, 0), strength=0.85):
    """Dark outline / drop shadow under im for legibility over footage."""
    a = im.split()[3]
    grown = a.filter(ImageFilter.MaxFilter(radius * 2 + 1)).filter(ImageFilter.GaussianBlur(radius * 0.6))
    shadow = Image.new("RGBA", im.size, colour + (0,))
    shadow.putalpha(grown.point(lambda v: int(v * strength)))
    return Image.alpha_composite(shadow, im)


def padded(im, pad):
    out = Image.new("RGBA", (im.width + 2 * pad, im.height + 2 * pad), (0, 0, 0, 0))
    out.paste(im, (pad, pad))
    return out


# --- logo -----------------------------------------------------------------------

def sign_image(data_dir):
    """MALL MANIACS sign from SIGN100|SIGN200 with a solid alpha mask (the
    sign's dark rim must not turn transparent) and the pole faded out."""
    left = load_tpg(os.path.join(data_dir, "menu", "SIGN100.TPG"))
    right = load_tpg(os.path.join(data_dir, "menu", "SIGN200.TPG"))
    rgb = np.concatenate([left, right], axis=1)
    mask = Image.fromarray(((rgb.max(axis=2) > 14) * 255).astype(np.uint8))
    mask = mask.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.MinFilter(5))   # close gaps
    mask = mask.filter(ImageFilter.MinFilter(3)).filter(ImageFilter.GaussianBlur(1.0))  # soft edge
    alpha = np.asarray(mask).astype(np.float32) / 255.0
    rows = np.arange(rgb.shape[0], dtype=np.float32)[:, None]
    alpha *= np.clip((240.0 - rows) / 50.0, 0.0, 1.0)                 # pole fades into the dark
    # The texture crops the oval's left tip and top rim; feather those edges
    # so the sign fades out instead of ending in a straight cut.
    cols = np.arange(rgb.shape[1], dtype=np.float32)[None, :]
    alpha *= np.clip(cols / 70.0, 0.0, 1.0) ** 1.5
    alpha *= np.clip(rows / 14.0, 0.0, 1.0)
    return to_rgba(rgb, alpha)


def logo_layer(data_dir, font):
    canvas = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    sign = scaled(sign_image(data_dir), LOGO_SCALE)
    sx = (W - sign.width) // 2
    sy = BAND_TOP + int(BAND_H * 0.08)
    canvas.alpha_composite(sign, (sx, sy))

    badge = outlined(padded(scaled(font.render(LOGO_BADGE), BADGE_SCALE), 24), 8)
    bx = sx + int(sign.width * 0.80) - badge.width // 2
    by = sy + int(sign.height * 0.58)
    canvas.alpha_composite(badge, (bx, by))
    return canvas


def glow_layer(logo):
    """Bright, wide blur of the logo; the edit screens it under the sharp
    logo and animates its strength for the bloom-in."""
    arr = np.asarray(logo).astype(np.float32)
    rgb = arr[:, :, :3] * (arr[:, :, 3:] / 255.0)
    glow = Image.fromarray(np.clip(rgb * 1.4, 0, 255).astype(np.uint8), "RGB").filter(ImageFilter.GaussianBlur(28))
    return glow


def caption_layer(font, text, scale, ypos):
    canvas = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    cap = outlined(padded(tinted(scaled(font.render(text), scale), CAPTION_TINT), 16), 5 + int(scale))
    canvas.alpha_composite(cap, ((W - cap.width) // 2, BAND_TOP + int(BAND_H * ypos) - cap.height // 2))
    return canvas


# --- previews -------------------------------------------------------------------

def preview(layer, background, glow=None):
    bg = background.convert("RGB").copy()
    if glow is not None:
        bg = Image.fromarray(np.clip(np.asarray(bg, np.int32) + np.asarray(glow, np.int32) * 0.6, 0, 255).astype(np.uint8))
    out = bg.convert("RGBA")
    out.alpha_composite(layer)
    return out.convert("RGB")


def letterboxed(frame_path):
    frame = Image.open(frame_path).convert("RGB").resize((W, BAND_H), Image.LANCZOS)
    bg = Image.new("RGB", (W, H), (0, 0, 0))
    bg.paste(frame, (0, BAND_TOP))
    return bg


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--data", default=os.environ.get("MM_DATA_DIR", "/home/wasd/MallManiacsUnmodified"))
    ap.add_argument("--out", default="build/trailer/cards")
    ap.add_argument("--footage", help="a 1920x804 frame for the preview composites")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    font = MenuFont(args.data)
    layers = {name: caption_layer(font, text, scale, ypos) for name, (text, scale, ypos) in CAPTIONS.items()}
    layers["logo"] = logo_layer(args.data, font)
    for name, im in layers.items():
        im.save(os.path.join(args.out, name + ".png"))
    glow = glow_layer(layers["logo"])
    glow.save(os.path.join(args.out, "logo_glow.png"))

    black = Image.new("RGB", (W, H), (0, 0, 0))
    preview(layers["logo"], black, glow).save(os.path.join(args.out, "preview_logo_black.png"))
    if args.footage:
        bg = letterboxed(args.footage)
        preview(layers["logo"], bg, glow).save(os.path.join(args.out, "preview_logo_footage.png"))
        for name in CAPTIONS:
            preview(layers[name], bg).save(os.path.join(args.out, f"preview_{name}.png"))
    print("wrote", ", ".join(sorted(os.listdir(args.out))))
    return 0


if __name__ == "__main__":
    sys.exit(main())
