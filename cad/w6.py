"""W6 'Walnut' charger stand: printable parts, true to size (mm).

World frame: x to the right as seen from the front, y away from the viewer, z up, desk at z = 0.
The base: 18 mm plan corners, sides leaning in 4 degrees, a 6 mm rounded top edge, a separate cap and a stepped-in foot.
  base      white PLA + brown foot (one filament change), 100 wide, 107 deep, front slope with a near-flush GC9A01
  cap       brown PLA  the top 2.0 mm of the base, printed separately right side up
  backrest  brown PLA  82 x 150 x 16 rounded slab, leaning back 16 degrees, Qi coil behind its face
  lip       brown PLA  82 x 12.5 x 7 phone ledge
plus hidden parts: bottom plate, display retainer, coil cover (brown PLA).

Sizes marked ASSUMED are not measured yet; change them here and re-run.
"""
import math
from build123d import (Align, Axis, Box, Cone, Cylinder, Location, Plane, Polygon, Pos, RectangleRounded, Rot,
                       Part, chamfer, extrude, fillet, offset)

MIN = (Align.CENTER, Align.CENTER, Align.MIN)

# ---------------------------------------------------------------- bought parts
LCD_D, LCD_T, ACTIVE_D = 36.2, 1.2, 32.4   # glass MEASURED 36.0-36.2; snug in a 36.6 pocket.
                                            # Flat on the tab side, 35 mm from the round edge to the flat;
                                            # ribbon strip 14.5 wide, soldered to the front of the tab up to ~3.5 mm past the flat
# glass front to PCB back MEASURED 3.0: glass + backlight 1.2 + bond 0.2 + PCB 1.6
GLASS_POCKET_D = 36.6                       # snug fit, checked with coupon_glass; 37.8 fits but leaves noticeable play
LCD_PCB_OFF = 0.3                           # MEASURED: the PCB sits 0.3 off the glass centre, towards the foot (-v), in both
                                            # the glass-up and the face-on photo; the recess behind the wall allows for it
PCB_CLEAR = 0.6                             # all-round clearance for the PCB behind the face wall
# MEASURED = from photos on a 1 mm grid, about +-0.5 mm (heights +-1 mm)
LCD_PCB_D = 37.7                            # MEASURED blue carrier PCB diameter
LCD_TAB_L, LCD_TAB_W = 45.1, 22.6           # MEASURED overall length incl. header tab, tab width
# 7-pin straight header soldered on the back of the tab, pins pointing back (MEASURED); wires are soldered to the pins
HDR_X0, HDR_X1, HDR_HW = 23.45, 25.95, 8.9  # plastic strip in face coordinates (x from the display centre), half length
HDR_PLASTIC, HDR_PIN, HDR_STUB = 2.6, 5.3, 0.6   # plastic, pin above it, solder stubs on the front of the tab
WIRE_ALLOW = 2.5                            # soldered wire end beyond the pin tips
QI_L, QI_W = 42.5, 17.0                     # MEASURED
QI_T = 1.6                                  # ASSUMED board thickness (not needed for the USB alignment below)
QI_USB_C, QI_USB_OVER = 1.0, 1.0            # MEASURED: USB-C sunk into a cut-out in the board, 3 mm tall, ~0.5 below the
                                            # board's underside, so its centre is ~1.0 above the underside; 1 mm past the board end
QI_CAP = (8.5, 4.5, 10.0)                   # MEASURED film cap 404J (w, t, h), bent upright on its legs at the board end
COIL_D = 50.0                               # MEASURED ferrite diameter
COIL_T = 2.0                                # MEASURED: copper winding + 1 mm ferrite
COIL_FOAM = 0.8                             # gap between coil and cover tube, for 1 mm foam (or felt/card) slightly squeezed
C3_L, C3_W = 22.6, 18.6                     # MEASURED ESP32-C3 SuperMini envelope incl. its soldered header strips
C3_UNDER, C3_PLASTIC, C3_PIN = 2.5, 2.4, 5.3   # MEASURED: board + solder stubs under the strips, strip, pin above it
BUCK_L, BUCK_W, BUCK_H = 21.8, 16.8, 4.2    # MEASURED MP1584-type mini buck (green)
PHONE = (77.0, 163.0, 9.0)                  # ASSUMED: "a new flagship OnePlus", exact model unknown
LIP_D = 12.5                                # lip depth: phones up to ~12 mm thick including a case

# Fasteners: M2 x 5 countersunk (~2 mm shank, ~5 long, head ~4), threaded straight
# into printed pilot holes, no inserts. Every pilot is deep enough for an M2 x 6 too. Tune PILOT_D with coupon_screws.
SCREW_L = 5.0
PILOT_D, PILOT_DEPTH = 1.7, 6.5             # 1.7 was the best fit in coupon_screws (black PLA)
M2_CLEAR, CSK_D = 2.4, 4.4                  # clearance hole; 90 degree countersink, a little over the ~4.0 head

# ---------------------------------------------------------------- base geometry
YF, YT, ZT, YB, W = -62.0, -30.0, 34.0, 45.0, 100.0
WALL, TOP = 2.4, 2.0                              # slope wall (normal) and cap thickness (2.0 leaves 1.25 mm of body above the display opening)
PLAN_R, TAPER, TOP_R = 18.0, 4.0, 6.0             # plan corner radius, side lean (deg), top/slope edge radius
FOOT_H, FOOT_IN, FOOT_FWD = 4.0, 1.6, 0.9         # black foot: height, step all round, then moved forward (front step 0.7, back 2.5)
FOOT_FRONT_WALL = 1.5                             # the foot's front face is vertical; keep this much plastic behind it
CAV = 4.4                                         # cavity wall offset from the plan outline (vertical inner walls)
ZC = ZT - TOP                                     # split plane between base and cap
D, CY = YB - YF, (YF + YB) / 2
PLATE_T = 3.0
L_SLOPE = math.hypot(YT - YF, ZT)
U = ((YT - YF) / L_SLOPE, ZT / L_SLOPE)          # up the slope, (y, z)
N_OUT = (-ZT / L_SLOPE, (YT - YF) / L_SLOPE)     # slope outward normal, (y, z)
DISP_SHIFT = 2.55                                 # display moved up the slope so the PCB clears the plate and the top
C = ((YF + YT) / 2 + U[0] * DISP_SHIFT, ZT / 2 + U[1] * DISP_SHIFT)
FACE = Plane(origin=(0, C[0], C[1]), x_dir=(1, 0, 0), z_dir=(0, N_OUT[0], N_OUT[1]))
APERTURE_D = 33.6
APERTURE_BEVEL = 0.4                              # 45 degree bevel on the window's front edge (front opening 34.4)
ACTIVE_DX = -0.4                                  # MEASURED: the lit area sits 0.4 mm towards -x (viewer's left, away from the
                                                  # header tab) of the glass centre
APERTURE_DX = ACTIVE_DX                           # window centred on the lit area, not on the glass
LCD_FRONT = 0.8                                   # face skin in front of the glass: the screen sits 0.8 below the face
PCB_BACK = LCD_FRONT + LCD_T + 0.2 + 1.6          # glass, tape, PCB
RET_FRONT = 9.5                                   # retainer plate front face depth (3 mm of plastic left in front of the pilot hole)
RET_BOSSES = [(-27.0, 0.0), (33.0, 0.0)]

# backrest pose
BR_LOC = Location((0, 2, ZT - 1), (-16, 0, 0))
BR_W, BR_H, BR_T, BR_R = 82.0, 150.0, 16.0, 16.0
SOCKET_Z = 26.0                                   # socket floor (world z)
SKIN = 0.2                                        # one-layer skin over the socket floor holes (opened after printing)
TENON_GAP = 0.4                                   # tenon stops short of the floor: the slab shoulders seat on the base top
COIL_Z = 85.0                                     # ASSUMED coil centre height on the backrest (phone centre)
COVER_D, COVER_T = 66.0, 2.0
COVER_SCREWS = [(-28.5, COIL_Z), (28.5, COIL_Z)]
TENON_SCREWS = [(-15.0, 10.0), (15.0, 10.0)]         # world (x, y) of the two screws up into the tenon (clear of the lip groove)
PLATE_BOSSES = [(-40.0, -48.0), (40.0, -48.0), (-38.0, 33.0), (38.0, 33.0)]
FEET = [(-33.0, -38.0), (33.0, -38.0), (-24.0, 34.0), (24.0, 34.0)]   # rear feet far back: tipping pivot
FOOT_PAD_D, FOOT_PAD_T = 10.0, 3.0                # feet: hot glue or printed TPU, 2 mm in the recess, 1 mm proud
BALLAST = (-38.0, 38.0, -28.5, -7.5, 10.0)        # x0, x1, y0, y1 inside, wall height: bin for 100-150 g of coins/nuts


def csk_hole(length, recess=0.0):
    """M2 countersunk clearance hole along +z. The head goes in at z = 0, sunk `recess` deeper (a counterbore), so
    the screw reaches `recess` further. 90 degree cone, then the clearance hole to z = length."""
    cone_h = (CSK_D - M2_CLEAR) / 2
    return (Pos(0, 0, -1) * Cylinder(CSK_D / 2, recess + 1, align=MIN)
            + Pos(0, 0, recess) * Cone(CSK_D / 2, M2_CLEAR / 2, cone_h, align=MIN)
            + Pos(0, 0, -1) * Cylinder(M2_CLEAR / 2, length + 1, align=MIN))


def bx(x0, x1, y0, y1, z0, z1):
    return Pos((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2) * Box(x1 - x0, y1 - y0, z1 - z0)


def cyl(r, z0, z1, x=0.0, y=0.0):
    return Pos(x, y, z0) * Cylinder(r, z1 - z0, align=MIN)


def side_prism(pts, x0, x1):
    """Extrude a side profile [(y, z), ...] along x from x0 to x1."""
    return extrude(Plane.YZ.offset(x0) * Polygon(*pts, align=None), x1 - x0)


def slope_y(z, inset):
    """y of the slope face offset `inset` mm inwards, at height z."""
    py, pz = YF - N_OUT[0] * inset, 0 - N_OUT[1] * inset
    return py + (z - pz) / U[1] * U[0]


def br_profile(grow, y0, y1, z_top=None):
    """Backrest slab outline (rounded 82 x 150, in the backrest's local xz plane) grown by `grow`,
    extruded along local y from y0 to y1, in local coordinates."""
    pl = Plane(origin=(0, y0, BR_H / 2), x_dir=(1, 0, 0), z_dir=(0, 1, 0))
    return extrude(pl * RectangleRounded(BR_W + 2 * grow, BR_H + 2 * grow, BR_R + grow), y1 - y0)


def tenon(grow):
    return bx(-25 - grow, 25 + grow, -grow, BR_T + grow, -18 - grow, 20)


# ---------------------------------------------------------------- base + cap
def slope_cut(shift=0.0, inset=0.0):
    """Everything in front of the slope plane (moved back by `shift` in y, or `inset` along its normal)."""
    return side_prism([(slope_y(-2, inset) + shift, -2), (slope_y(ZT + 20, inset) + shift, ZT + 20),
                       (YF - 60, ZT + 20), (YF - 60, -2)], -W, W)


def outer_shell():
    # cut at the foot height before rounding: lower down, the slope meets the rounded front corners at a
    # vanishing angle and the fillet there is degenerate
    s = extrude(Pos(0, CY) * RectangleRounded(W, D, PLAN_R), ZT, taper=TAPER) & bx(-80, 80, -90, 80, FOOT_H, ZT + 1)
    s -= slope_cut()
    bottom, top = s.faces().sort_by(Axis.Z)[0], s.faces().sort_by(Axis.Z)[-1]
    slope = [f for f in s.faces() if abs(f.normal_at().Y - N_OUT[0]) < 1e-3 and abs(f.normal_at().Z - N_OUT[1]) < 1e-3][0]
    upper = fillet((top.edges() + slope.edges()) - bottom.edges(), TOP_R)
    # foot: the body's own outline at z = FOOT_H, stepped in (so it can never stick out past the body)
    section = upper.faces().sort_by(Axis.Z)[0]
    foot = Pos(0, -FOOT_FWD, 0) * extrude(offset(section, -FOOT_IN), FOOT_H + 0.5, dir=(0, 0, -1))
    return upper + (Pos(0, 0, 0.5) * foot & bx(-80, 80, -90, 80, 0, FOOT_H + 0.5))


def cavity(side_chamfer=True):
    """Inside of the base: vertical walls, open through the split plane. The top edge has a 4 mm chamfer at the
    sides and back, which leaves a flange for gluing the cap and keeps the wall thick behind the rounded top edge."""
    c = Pos(0, 0, -1) * extrude(Pos(0, CY) * RectangleRounded(W - 2 * CAV, D - 2 * CAV, PLAN_R - CAV), ZC + 1)
    c -= slope_cut(inset=WALL)
    y_foot = slope_y(FOOT_H, 0) + FOOT_IN - FOOT_FWD          # front face of the foot
    c -= bx(-80, 80, -90, y_foot + FOOT_FRONT_WALL, -2, FOOT_H)
    top = c.faces().sort_by(Axis.Z)[-1]
    y_front = slope_y(ZC, WALL)
    c = chamfer([e for e in top.edges() if abs(e.center().Y - y_front) > 0.5 or abs(e.center().X) > 30], 4.0)
    if not side_chamfer:
        return c
    # same along the two side edges of the inner slope, behind the rounded edges of the outer slope
    slope = [f for f in c.faces() if abs(f.normal_at().Y - N_OUT[0]) < 1e-3 and abs(f.normal_at().Z - N_OUT[1]) < 1e-3][0]
    sides = [e for e in slope.edges() if abs(e.center().X) > 36 and FOOT_H + 2 < e.center().Z < ZC - 3]
    return chamfer(sides, 3.0)


def base_solid():
    outer = outer_shell()
    body = outer - cavity()

    # bosses for the bottom plate: full-height columns (print upside down without support)
    for x, y in PLATE_BOSSES:
        body += cyl(4.0, PLATE_T, ZT - 1, x, y) & outer
        sx = 1 if x > 0 else -1
        body += bx(*sorted((x, sx * (W / 2 - CAV + 0.5))), y - 1.5, y + 1.5, PLATE_T, ZT - 1) & outer   # rib to side wall
        if y > 0:
            body += bx(x - 1.5, x + 1.5, y, YB - CAV + 0.5, PLATE_T, ZT - 1) & outer                  # rib to back wall
    # display retainer bosses, perpendicular to the face
    for u, v in RET_BOSSES:
        body += (FACE * Pos(u, v, -RET_FRONT) * Cylinder(4.0, RET_FRONT, align=MIN)) & outer
    # backrest socket: a beam from side wall to side wall (it carries the socket now that the top is a separate cap)
    body += (BR_LOC * bx(-60, 60, -2.2, BR_T + 2.2, -30, 30)) & bx(-60, 60, -60, 60, SOCKET_Z - 2, ZT) & outer

    # cuts
    body -= FACE * cyl(APERTURE_D / 2, -3, 1, APERTURE_DX)
    body -= FACE * Pos(APERTURE_DX, 0, -APERTURE_BEVEL) * Cone(APERTURE_D / 2, APERTURE_D / 2 + APERTURE_BEVEL + 1, APERTURE_BEVEL + 1, align=MIN)
    body -= FACE * (Pos(0, 0, -LCD_FRONT) * Rot(180, 0, 0) * Cylinder(GLASS_POCKET_D / 2, 3, align=MIN)
                    + bx(0, 20.5, -8.0, 8.0, -LCD_FRONT - 3, -LCD_FRONT))      # + ribbon notch, as in the first coupon
    # the PCB (offset towards the foot) clears the ledge above the foot; starts 0.2 in front of where it should sit
    pcb_d0 = LCD_FRONT + LCD_T
    body -= FACE * Pos(0, -LCD_PCB_OFF, -pcb_d0 - 3) * Cylinder(LCD_PCB_D / 2 + PCB_CLEAR, 3, align=MIN)
    tab_end = LCD_TAB_L - LCD_PCB_D / 2                                   # and the header tab, which now sits inside the wall
    body -= FACE * bx(0, tab_end + PCB_CLEAR, -LCD_PCB_OFF - LCD_TAB_W / 2 - PCB_CLEAR, -LCD_PCB_OFF + LCD_TAB_W / 2 + PCB_CLEAR,
                      -pcb_d0 - 3, -pcb_d0)
    # the header's solder stubs stick 0.6 mm out of the front of the tab, which sits against the inner wall
    pcb_front = LCD_FRONT + LCD_T + 0.2
    body -= FACE * bx(HDR_X0 - 1, HDR_X1 + 1, -HDR_HW - 1, HDR_HW + 1, -pcb_front - 0.1, -pcb_front + HDR_STUB + 0.4)
    for u, v in RET_BOSSES:
        body -= FACE * Pos(u, v, -RET_FRONT) * Cylinder(PILOT_D / 2, PILOT_DEPTH, align=MIN)
    for x, y in PLATE_BOSSES:
        body -= cyl(PILOT_D / 2, PLATE_T - 1, PLATE_T + PILOT_DEPTH, x, y)
    body -= (BR_LOC * tenon(0.2)) & bx(-60, 60, -60, 60, SOCKET_Z, 60)     # tenon only: no sloped ceilings to print
    # Holes in the 2 mm socket floor stop SKIN short of the socket: the base prints upside down, so the floor is a
    # 16 mm bridge, and holes in a bridge leave "floating cantilevers" (Bambu Studio warns). The one-layer skin is
    # opened after printing: an awl or the screw itself for the screw holes, a knife for the wire hole.
    below_skin = bx(-80, 80, -90, 80, -1, SOCKET_Z - SKIN)
    for x, y in TENON_SCREWS:
        body -= (Pos(x, y, SOCKET_Z - 2) * csk_hole(3, recess=0.5)) & below_skin   # from below, through the floor
    body -= cyl(4.5, SOCKET_Z - 4, SOCKET_Z - SKIN, 0, 8.3)              # coil wires into the base
    body -= Pos(0, YB - 2, USB_Z) * Rot(90, 0, 0) * extrude(RectangleRounded(14.0, 8.0, 3.5), 6, both=True)   # plug overmould must pass
    return body


_BASE = None


def _base():
    global _BASE
    if _BASE is None:
        _BASE = base_solid()
    return _BASE


def base():
    """White body + brown foot (filament change at the foot), printed upside down on the split plane."""
    return _base() & bx(-80, 80, -90, 80, -1, ZC)


def cap():
    """Top of the base, printed right side up so its rounded edge is clean. The backrest
    tenon passes through it and the backrest screws clamp it; glue is optional."""
    return _base() & bx(-80, 80, -90, 80, ZC, ZT + 1)


USB_Z = PLATE_T + 6.2                             # centre of the rear USB-C opening in the base


def qi_z():
    return USB_Z - QI_USB_C         # Qi PCB underside: the board sits so its sunk USB-C port lines up with the opening


# ---------------------------------------------------------------- backrest
def backrest():
    slab = fillet(br_profile(0, 0, BR_T).edges(), 2.5)
    part = slab + tenon(0)
    # coil pocket from the back (1.2 mm skin under the phone), cover recess, cover screw pilots
    part -= Pos(0, 0, COIL_Z) * Rot(-90, 0, 0) * Cylinder(COIL_D / 2 + 0.5, BR_T, align=MIN).moved(Location((0, 0, 1.2)))
    part -= Pos(0, BR_T - COVER_T, COIL_Z) * Rot(-90, 0, 0) * Cylinder(COVER_D / 2 + 0.2, 5, align=MIN)
    for x, z in COVER_SCREWS:
        part -= Pos(x, BR_T - COVER_T - PILOT_DEPTH, z) * Rot(-90, 0, 0) * Cylinder(PILOT_D / 2, PILOT_DEPTH + 1, align=MIN)
    # wire tunnel from the bottom of the coil pocket down through the tenon
    part -= Pos(0, 8.0, -30) * Cylinder(3.0, COIL_Z - COIL_D / 2 + 31, align=MIN)
    # groove for the lip tongue
    part -= bx(-30.2, 30.2, -1, 3.2, 1.1, 3.3)
    part = BR_LOC * part
    # above the base top: the whole slab. Below it: only the 50 mm tenon, so the slab's rounded corners
    # sit on the base top as shoulders.
    part &= bx(-60, 60, -60, 80, ZT, 250) + bx(-25, 25, -60, 80, SOCKET_Z + TENON_GAP, ZT + 1)
    for x, y in TENON_SCREWS:
        part -= cyl(PILOT_D / 2, SOCKET_Z - 1, SOCKET_Z + TENON_GAP + PILOT_DEPTH, x, y)
    return part


def lip():
    # PLA tongue: the printed groove MEASURED 60.0 long, 2.2 high, 3.0-3.2 deep, so 0.3 at each
    # end, 0.25 above and below, and 2.7 deep so it can never bottom out. Held with a drop of CA glue.
    body = fillet(bx(-41, 41, -LIP_D, 0, -2, 5).edges(), 1.5) + bx(-29.7, 29.7, -1.0, 2.7, 1.35, 3.05)
    return (BR_LOC * body) & bx(-60, 60, -60, 60, ZT, 80)


def coil_cover():
    """Round lid on the back of the backrest; its tube holds the coil against the front skin through 1 mm foam tape."""
    disc = Cylinder(COVER_D / 2, COVER_T, align=MIN)
    gap = (BR_T - COVER_T) - (1.2 + COIL_T + COIL_FOAM)
    part = disc + Pos(0, 0, -gap) * (Cylinder(20, gap, align=MIN) - Cylinder(18, gap, align=MIN))
    part -= Pos(0, 0, -gap) * Cylinder(4, gap, align=MIN).moved(Location((0, 19, 0)))   # lead notch
    for x, _ in COVER_SCREWS:
        part -= Pos(x, 0, COVER_T) * Rot(180, 0, 0) * csk_hole(COVER_T + 1)       # head flush with the outer face
    # into backrest space: disc outer face flush with the back
    return BR_LOC * Pos(0, BR_T - COVER_T, COIL_Z) * Rot(-90, 0, 0) * part


# ---------------------------------------------------------------- bottom plate
def plate():
    c = WALL + 0.25
    k = CAV + 0.25
    p = extrude(Pos(0, CY) * RectangleRounded(W - 2 * k, D - 2 * k, PLAN_R - k), PLATE_T)
    p &= bx(-80, 80, slope_y(PLATE_T, c), 80, -1, PLATE_T + 1)
    # the base's inner chamfers reach down into the plate's front corners: cut them away with 0.3 mm clearance
    wedge = (cavity(False) - cavity()) & bx(-80, 80, -90, 80, -1, PLATE_T + 1)
    for dx, dy in ((0, 0), (0.3, 0), (-0.3, 0), (0, 0.3), (0.3, 0.3), (-0.3, 0.3)):
        p -= Pos(dx, dy, 0) * wedge
    p = Part(max(p.solids(), key=lambda q: q.volume).wrapped)                  # drop the slivers those cuts leave behind
    y0 = slope_y(PLATE_T, c)
    p -= bx(-22, 22, y0 - 1, y0 + 12, 0.8, PLATE_T + 1)           # relief for the display PCB edge
    p -= FACE * bx(HDR_X0 - 1, HDR_X1 + 1, -HDR_HW - 1, HDR_HW + 1,  # display header pins + soldered wire ends
                   -(PCB_BACK + HDR_PLASTIC + HDR_PIN + WIRE_ALLOW + 1), -PCB_BACK)
    for x, y in PLATE_BOSSES:
        p -= Pos(x, y, 0) * csk_hole(PLATE_T + 1, recess=1.5)     # head sunk 1.5 mm so an M2 x 5 bites 3.5 mm
    for x, y in FEET:
        p -= cyl(FOOT_PAD_D / 2 + 0.3, -1, 2.0, x, y)                     # recess for a foot, 1 mm proud
    # Qi board cradle: support ledges + side rails, USB end against the back wall
    y1 = YB - CAV - 0.3
    for s in (-1, 1):
        p += bx(*sorted((s * 5.5, s * 8.9)), y1 - QI_L + 2, y1, PLATE_T, qi_z())
        p += bx(*sorted((s * 8.9, s * 10.1)), y1 - QI_L + 2, y1 - 8, PLATE_T, qi_z() + 3.5)
    # ESP32-C3 and buck trays (2 mm walls, part held with foam tape). The C3 lies header pins up, USB-C end
    # towards the side wall, on its solder stubs; the tray fits the header strips' envelope.
    for cx, (l, w) in ((-29.0, (C3_L, C3_W)), (29.0, (BUCK_L, BUCK_W))):
        cy = 12.0
        tray = bx(cx - l / 2 - 1.4, cx + l / 2 + 1.4, cy - w / 2 - 1.4, cy + w / 2 + 1.4, PLATE_T, PLATE_T + 2)
        tray -= bx(cx - l / 2 - 0.2, cx + l / 2 + 0.2, cy - w / 2 - 0.2, cy + w / 2 + 0.2, PLATE_T - 1, PLATE_T + 3)
        tray -= bx(cx - 5, cx + 5, cy - w, cy + w, PLATE_T + 0.8, PLATE_T + 3)   # wire gaps
        tray -= bx(cx - l / 2 - 2, cx - l / 2 + 1, cy - 5.5, cy + 5.5, PLATE_T - 0.01, PLATE_T + 3)  # USB-C end, open to the floor
        p += tray
    # ballast bin (open top): without ~150 g here a tap on the top of the phone rocks the stand back
    x0, x1, b0, b1, h = BALLAST
    p += bx(x0 - 1.6, x1 + 1.6, b0 - 1.6, b1 + 1.6, PLATE_T, PLATE_T + h) - bx(x0, x1, b0, b1, PLATE_T, PLATE_T + h + 1)
    return p


# ---------------------------------------------------------------- display retainer
def retainer():
    """Bar screwed to the two face bosses; two pads press the display PCB (through foam tape) forward."""
    pad_h = RET_FRONT - PCB_BACK - 1.0                      # 1 mm foam tape between pad and PCB
    r = bx(-31, 37, -7, 14.5, 0, 2)
    r -= bx(17, 29, -7.1, HDR_HW + 1.7, -1, 3)             # window: header pins and soldered wires pass through
    for u, v in RET_BOSSES:
        r -= Pos(u, v, 0) * csk_hole(3)                           # head flush with the back of the bar
    for u in (-12.0, 8.0):
        r += bx(u - 4, u + 4, -5, 5, 2, 2 + pad_h)
    # face frame: plate from depth RET_FRONT + 2 to RET_FRONT, pads towards the display
    return FACE * Pos(0, 0, -RET_FRONT - 2) * r


# ---------------------------------------------------------------- feet
def feet():
    """Four optional TPU feet for the plate's recesses (CA glue). Hot glue set in the recesses works too."""
    out = None
    for x, y in FEET:
        f = cyl(FOOT_PAD_D / 2, 2.0 - FOOT_PAD_T, 2.0, x, y)
        f = chamfer(f.edges().sort_by(Axis.Z)[0], 0.6)               # softened bottom edge
        out = f if out is None else out + f
    return out


PARTS = {
    "base": base,
    "cap": cap,
    "backrest": backrest,
    "lip": lip,
    "plate": plate,
    "retainer": retainer,
    "coil_cover": coil_cover,
    "feet": feet,
}
