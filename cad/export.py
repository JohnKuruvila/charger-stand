"""Export W6: print-oriented 3MF + STL per part, fit-test coupons, and assembled-pose STLs for rendering.
Output: out/print/*.3mf|stl, out/coupons/*.3mf|stl, out/assembled/*.stl, out/w6_assembly.step"""
import math
import os
import trimesh
from build123d import (Compound, Location, Pos, Rot, export_step, export_stl)
import bought
import w6
from w6 import BR_LOC, FACE, MIN, bx, cyl

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")


def on_bed(shape):
    """Centre on x/y and drop to z = 0."""
    bb = shape.bounding_box()
    return Pos(-bb.center().X, -bb.center().Y, -bb.min.Z) * shape


def save(shape, folder, name):
    """STL (and 3MF for printable files) from a cleaned, watertight mesh."""
    d = os.path.join(OUT, folder)
    os.makedirs(d, exist_ok=True)
    stl = os.path.join(d, name + ".stl")
    export_stl(shape, stl, tolerance=0.01, angular_tolerance=0.1)
    m = trimesh.load(stl)
    m.merge_vertices(digits_vertex=3)
    m.update_faces(m.nondegenerate_faces())
    m.remove_unreferenced_vertices()
    m.fill_holes()
    m.fix_normals()
    if folder == "assembled":                      # render meshes: cleaned, but not required to be watertight
        if not m.is_watertight:
            print("note: assembled/%s is not watertight" % name)
        m.export(stl)
        return
    assert m.is_watertight, name
    m.export(stl)
    m.export(os.path.join(d, name + ".3mf"))


cover_loc = BR_LOC * Location((0, w6.BR_T - w6.COVER_T, w6.COIL_Z)) * Location((0, 0, 0), (-90, 0, 0))
PRINT_POSE = {
    # upside down, split plane on the plate, then turned so the overhanging front face points to the printer's left
    # (-X): the A1 cools from the left of the head, and a front corner facing +X comes out rough
    "base": lambda p: Rot(0, 0, 90) * Rot(180, 0, 0) * p,
    "cap": lambda p: p,                                            # right side up, underside on the plate
    # phone face on the plate, long side along X: the rounded long edges overhang near the bed, so they face front and
    # back instead of the side away from the fan (same lesson as the base)
    "backrest": lambda p: Rot(0, 0, -90) * Rot(90, 0, 0) * (BR_LOC.inverse() * p),
    "lip": lambda p: p,                                            # flat bottom down
    "plate": lambda p: p,                                          # bosses/trays up
    "retainer": lambda p: FACE.location.inverse() * p,             # bar down, pads up
    # outer face down, turned so the edge that faces down on the backrest is at the back of the plate: the "back" seam
    # then lands there, towards the base, instead of on the top edge
    "coil_cover": lambda p: Rot(0, 0, 180) * Rot(180, 0, 0) * (cover_loc.inverse() * p),
    "feet": lambda p: p,                                           # chamfered side up as modelled; flat top on the bed
}


def coupons(parts):
    base, br = w6._base(), parts["backrest"]        # base + cap in one piece for the coupons
    lo = -(w6.L_SLOPE / 2 + w6.DISP_SHIFT) + 2.5
    hi = w6.L_SLOPE / 2 - w6.DISP_SHIFT - 2.5
    face_box = FACE * bx(-38, 38, lo + 1.5, hi + 1.2, -11, 2)
    face = Rot(180, 0, 0) * (FACE.location.inverse() * (base & face_box))
    socket = Rot(180, 0, 0) * (base & bx(-47.5, 47.5, -8, 24, 21.5, w6.ZT))
    tenon = PRINT_POSE["backrest"](br & bx(-45, 45, -20, 40, w6.SOCKET_Z, 42))   # printed face down, like the backrest
    # M2 pilot holes 1.6 / 1.7 / 1.8 mm (1, 2, 3 notches on the side), and a loose 2 mm tab with a countersunk hole:
    # screw the tab onto each hole with an M2 x 5 and keep the hole that grips firmly without splitting
    scr = bx(-22, 22, -7, 7, 0, 8)
    for i, d in enumerate((1.6, 1.7, 1.8)):
        scr -= cyl(d / 2, 8 - w6.PILOT_DEPTH, 9, -13 + 13 * i, 0)
        for k in range(i + 1):
            scr -= bx(-13 + 13 * i - 3 + 3 * k - 0.6, -13 + 13 * i - 3 + 3 * k + 0.6, -8, -6.2, 5, 9)
    tab = Pos(0, 16, 0) * (bx(-6, 6, -6, 6, 0, 2) - Pos(0, 0, 2) * Rot(180, 0, 0) * w6.csk_hole(3))
    scr = scr + tab
    # Qi coil pocket: 1.2 mm skin, same pocket diameter as the backrest
    coil = cyl(w6.COIL_D / 2 + 3.5, 0, 1.2 + 4) - cyl(w6.COIL_D / 2 + 0.5, 1.2, 10)
    # glass pocket only: the sloped wall around the display (no bosses), a few grams, to check the glass drops in
    glass = Rot(180, 0, 0) * (FACE.location.inverse() * (base & (FACE * bx(-21, 28, -21, 19.5, -3.0, 2))))
    return {"coupon_glass": glass, "coupon_display_face": face, "coupon_socket": socket, "coupon_tenon": tenon,
            "coupon_screws": scr, "coupon_coil_pocket": coil}


def write_consts():
    """Numbers the Blender renderer needs (Blender's Python has no build123d)."""
    tilt = math.degrees(math.asin(-w6.N_OUT[0]))
    vals = dict(BR_POS=(0.0, 2.0, w6.ZT - 1), FACE_Y=w6.C[0], FACE_Z=w6.C[1], FACE_TILT=tilt,
                N_IN=(-w6.N_OUT[0], -w6.N_OUT[1]), LCD_FRONT=w6.LCD_FRONT, ACTIVE_D=w6.ACTIVE_D, ACTIVE_DX=w6.ACTIVE_DX,
                COIL_D=w6.COIL_D, COIL_T=w6.COIL_T, COIL_Z=w6.COIL_Z, PLATE_T=w6.PLATE_T,
                USB_Z=w6.qi_z() + 3.2)
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "w6_consts.py"), "w") as f:
        f.write('"""Generated by export.py from w6.py; do not edit."""\n')
        for n, v in vals.items():
            f.write(f"{n} = {v!r}\n")


def main():
    write_consts()
    parts = {n: f() for n, f in w6.PARTS.items()}
    for n, p in parts.items():
        save(p, "assembled", n)
        save(on_bed(PRINT_POSE[n](p)), "print", n)
    for n, p in coupons(parts).items():
        save(on_bed(p), "coupons", n)
    extra = bought.all_parts()
    extra.update(bought.fasteners())
    for n, p in extra.items():
        save(p, "assembled", n)
    # colour zones of the base for the renderer: body and foot
    save(parts["base"] & bx(-80, 80, -90, 80, w6.FOOT_H, 60), "assembled", "base_body")
    save(parts["base"] & bx(-80, 80, -90, 80, -1, w6.FOOT_H), "assembled", "base_foot")
    export_step(Compound(list(parts.values())), os.path.join(OUT, "w6_assembly.step"))
    for n, p in parts.items():
        bb = on_bed(PRINT_POSE[n](p)).bounding_box()
        print(f"{n:11s} print size {bb.size.X:6.1f} x {bb.size.Y:6.1f} x {bb.size.Z:6.1f} mm")


if __name__ == "__main__":
    main()
