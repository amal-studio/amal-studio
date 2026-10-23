"""Génère les icônes d'Amal Studio (émeraude, blanc et or).

Usage : python3 build-aux/amal/make_icons.py
Remplace l'icône de l'application (.ico Windows et .png Qt) par le logo Amal Studio :
un arc de mihrab doré sur fond émeraude, avec un triangle « lecture » blanc.
"""

import math
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
EMERALD = (11, 107, 79, 255)
EMERALD_DARK = (6, 72, 53, 255)
GOLD = (212, 175, 55, 255)
WHITE = (255, 255, 255, 255)


def draw_logo(size: int) -> Image.Image:
    s = 4 * size  # supersampling pour des bords lisses
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    r = int(0.22 * s)
    d.rounded_rectangle([0, 0, s - 1, s - 1], radius=r, fill=EMERALD_DARK)
    m = int(0.035 * s)
    d.rounded_rectangle([m, m, s - 1 - m, s - 1 - m], radius=r - m, fill=EMERALD)

    # Arc de mihrab (porte en ogive) en or
    w = int(0.05 * s)
    left, right = int(0.29 * s), int(0.71 * s)
    base = int(0.84 * s)
    spring = int(0.60 * s)
    cx = (left + right) // 2
    # Ogive équilatérale : chaque côté est un arc centré sur le pied opposé,
    # tracé d'un seul trait avec les montants verticaux.
    span = right - left
    apex_angle = math.acos(0.5)  # 60 degrés : les deux arcs se rejoignent au centre
    steps = 48
    left_side = [(left, base), (left, spring)]
    for i in range(1, steps + 1):
        a = math.pi - apex_angle * i / steps
        left_side.append((right + span * math.cos(a), spring - span * math.sin(a)))
    right_side = [(2 * cx - x, y) for (x, y) in reversed(left_side)]
    path = left_side + right_side[1:]
    d.line(path, fill=GOLD, width=w, joint="curve")
    apex_y = spring - span * math.sin(apex_angle)
    d.ellipse([cx - 0.8 * w, apex_y - 2.6 * w, cx + 0.8 * w, apex_y - 1.0 * w], fill=GOLD)
    # base
    d.line([(left - w, base), (right + w, base)], fill=GOLD, width=w)

    # Triangle « lecture » blanc
    tx, ty, th = int(0.45 * s), int(0.63 * s), int(0.19 * s)
    d.polygon([(tx, ty - th // 2), (tx, ty + th // 2), (tx + int(0.17 * s), ty)], fill=WHITE)

    return img.resize((size, size), Image.LANCZOS)


def main() -> None:
    big = draw_logo(256)
    big.save(ROOT / "frontend/forms/images/obs.png")
    sizes = [16, 20, 24, 32, 40, 48, 64, 256]
    draw_logo(256).save(
        ROOT / "frontend/cmake/windows/obs-studio.ico",
        sizes=[(n, n) for n in sizes],
    )
    draw_logo(512).save(ROOT / "build-aux/amal/amal-studio-logo-512.png")
    print("Icônes Amal Studio générées.")


if __name__ == "__main__":
    main()
