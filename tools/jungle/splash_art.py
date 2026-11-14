# SPDX-License-Identifier: GPL-3.0-only
"""AMENITY's splash art: an angular chrome logo drawn from polygons (no font), 240x240 -> assets/splash.png.
usage: python -I tools/jungle/splash_art.py assets/splash.png [PREVIEW_x3.png]"""
import sys
from PIL import Image, ImageDraw, ImageFilter

S = 4                      # supersampling
W = 240 * S
SHEAR = 0.22               # italic lean (x += SHEAR * (1 - y))

# letters on a unit cap height (y down, 0 top, 1 baseline); each: (width, [outer polygon], [holes])
L = {
 "A": (1.00, [(0.00, 1.10), (0.12, 0.84), (0.42, -0.16), (0.60, -0.02), (0.92, 1.00), (0.70, 1.00), (0.63, 0.76),
              (0.34, 0.76), (0.27, 1.00)],
      [[(0.39, 0.58), (0.58, 0.58), (0.49, 0.26)]]),
 "M": (0.92, [(0.00, 1.00), (0.00, 0.00), (0.24, 0.00), (0.46, 0.36), (0.68, 0.00), (0.92, 0.00), (0.92, 1.00),
              (0.70, 1.00), (0.70, 0.42), (0.46, 0.80), (0.22, 0.42), (0.22, 1.00)], []),
 "E": (0.74, [(0.00, 0.00), (0.82, -0.06), (0.66, 0.20), (0.22, 0.20), (0.22, 0.40), (0.58, 0.40), (0.50, 0.60),
              (0.22, 0.60), (0.22, 0.80), (0.74, 0.80), (0.62, 1.00), (0.00, 1.00)], []),
 "N": (0.86, [(0.00, 1.00), (0.00, 0.00), (0.24, 0.00), (0.64, 0.62), (0.64, 0.00), (0.86, 0.00), (0.86, 1.00),
              (0.62, 1.00), (0.22, 0.38), (0.22, 1.00)], []),
 "I": (0.24, [(0.00, 0.00), (0.24, 0.00), (0.24, 1.00), (0.00, 1.00)], []),
 "T": (0.84, [(0.00, 0.00), (0.90, -0.06), (0.76, 0.20), (0.53, 0.20), (0.53, 1.00), (0.31, 1.00), (0.31, 0.20),
              (0.08, 0.20)], []),
 "Y": (0.96, [(0.00, 0.00), (0.25, 0.00), (0.44, 0.38), (0.66, -0.04), (1.02, -0.16), (0.56, 0.58), (0.56, 1.00),
              (0.33, 1.00), (0.33, 0.58)], []),
}
GAP = 0.05
WORD = "AMENITY"

def layout(unit):
    total = sum(L[c][0] for c in WORD) + GAP * (len(WORD) - 1) + SHEAR
    x0 = (W - total * unit) / 2
    return x0

def shapes(unit, top):
    x = layout(unit)
    out = []
    for c in WORD:
        w, poly, holes = L[c]
        f = lambda p: (x + (p[0] + SHEAR * (1 - p[1])) * unit, top + p[1] * unit)
        out.append(([f(p) for p in poly], [[f(p) for p in h] for h in holes]))
        x += (w + GAP) * unit
    return out

def mask(shp, grow=0):
    m = Image.new("L", (W, W), 0)
    d = ImageDraw.Draw(m)
    for poly, holes in shp:
        d.polygon(poly, fill=255)
        for h in holes:
            d.polygon(h, fill=0)
    if grow:
        m = m.filter(ImageFilter.MaxFilter(grow * 2 + 1))
    return m

def chrome(top, h):
    """vertical chrome: sky, a hard horizon, dark ground rising to light"""
    g = Image.new("RGB", (W, W), 0)
    stops = [(0.00, (255, 255, 255)), (0.40, (170, 200, 220)), (0.50, (230, 245, 255)), (0.51, (30, 40, 60)),
             (0.70, (80, 110, 140)), (1.00, (210, 235, 250))]
    d = ImageDraw.Draw(g)
    for y in range(W):
        t = (y - top) / h
        t = min(max(t, 0.0), 1.0)
        for (a, ca), (b, cb) in zip(stops, stops[1:]):
            if a <= t <= b:
                k = (t - a) / (b - a) if b > a else 0
                col = tuple(int(ca[i] + (cb[i] - ca[i]) * k) for i in range(3))
                break
        d.line([(0, y), (W, y)], fill=col)
    return g

def main(out, preview=None):
    unit = 36 * S
    top = 84 * S
    shp = shapes(unit, top)
    img = Image.new("RGB", (W, W), (0, 0, 0))
    # cyan glow, then a dark rim, then the chrome
    glow = mask(shp, grow=5 * S).filter(ImageFilter.GaussianBlur(7 * S))
    img.paste((60, 200, 235), (0, 0), glow.point(lambda v: int(v * 0.85)))
    img.paste((8, 14, 28), (0, 0), mask(shp, grow=2 * S))
    img.paste((60, 200, 235), (0, 0), mask(shp, grow=1 * S))
    img.paste(chrome(top - 0.16 * unit, 1.26 * unit), (0, 0), mask(shp))
    # under it: a break's hits as a row of blades (32 steps, an Amen-like pattern), cyan and dim
    d = ImageDraw.Draw(img)
    pat = "K.K...S...SK..S.KK.....S..K.S..S"
    bw = 6 * S
    x0 = (W - 32 * bw) // 2
    y = 150 * S
    for i, ch in enumerate(pat):
        hgt = {"K": 14, "S": 10}.get(ch, 3) * S
        col = (60, 200, 235) if ch == "K" else (230, 90, 60) if ch == "S" else (40, 60, 80)
        x = x0 + i * bw
        d.polygon([(x + S, y), (x + bw - S, y), (x + bw // 2, y + hgt)], fill=col)
    small = img.resize((240, 240), Image.LANCZOS)
    small.save(out)
    if preview:
        small.resize((720, 720), Image.NEAREST).save(preview)

if __name__ == "__main__":
    main(*sys.argv[1:])
