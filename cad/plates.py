"""Combined build plates: parts that share a filament and settings, pre-arranged in one 3MF each.
Run after export.py. Output: out/plates/*.3mf (one object per part, named after it).

The base (filament change at 28 mm) stays on its own plate. In Bambu Studio, open the 3MF,
keep the positions, and set any per-object setting listed in NOTES (right-click the object > Add settings)."""
import os
import numpy as np
import trimesh

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
GAP = 10.0          # mm between parts
BED = 256.0

# columns left to right; parts in a column stacked front to back
PLATES = {
    "brown_parts": [["print/backrest", "print/cap"], ["print/coil_cover"]],   # backrest lies along X (see export.py)
    "bottom_parts": [["print/plate"], ["print/retainer", "print/lip"]],
    "pla_coupons": [["coupons/coupon_socket", "coupons/coupon_display_face"]],   # coupon_screws printed on its own first
    "joint_coupons": [["coupons/coupon_tenon", "coupons/coupon_coil_pocket"]],
}
NOTES = {
    "brown_parts": "brown PLA, 3 walls, 15% gyroid; cap: sparse infill 100%",
    "bottom_parts": "brown PLA, 4 walls (the lip is fine with 4 too)",
    "pla_coupons": "any PLA",
    "joint_coupons": "any PLA",
}


def load(rel):
    m = trimesh.load(os.path.join(OUT, rel + ".stl"))
    m.apply_translation(-m.bounds[0])           # min corner at the origin
    return os.path.basename(rel), m


def build(columns):
    placed, x = [], 0.0
    for col in columns:
        items = [load(r) for r in col]
        width = max(m.extents[0] for _, m in items)
        y = 0.0
        for name, m in items:
            m.apply_translation([x + (width - m.extents[0]) / 2, y, 0])
            placed.append((name, m))
            y += m.extents[1] + GAP
        x += width + GAP
    lo = np.min([m.bounds[0] for _, m in placed], axis=0)
    hi = np.max([m.bounds[1] for _, m in placed], axis=0)
    size = hi - lo
    assert size[0] <= BED - 10 and size[1] <= BED - 10, size
    shift = [BED / 2 - (lo[0] + hi[0]) / 2, BED / 2 - (lo[1] + hi[1]) / 2, 0]   # centred on the A1 bed
    for _, m in placed:
        m.apply_translation(shift)
    return placed, size


def main():
    d = os.path.join(OUT, "plates")
    os.makedirs(d, exist_ok=True)
    for plate, columns in PLATES.items():
        placed, size = build(columns)
        scene = trimesh.Scene()
        for name, m in placed:
            scene.add_geometry(m, node_name=name, geom_name=name)
        scene.export(os.path.join(d, plate + ".3mf"))
        names = ", ".join(n for n, _ in placed)
        print(f"{plate:13s} {size[0]:5.1f} x {size[1]:5.1f} mm  [{names}]  {NOTES[plate]}")


if __name__ == "__main__":
    main()
