"""Bought parts as simple solids in their assembled positions (for fit checks and renders).
Sizes come from w6.py, where each is marked MEASURED or ASSUMED."""
from build123d import Cone, Cylinder, Pos, Rot
import w6
from w6 import FACE, BR_LOC, MIN, bx, cyl

QI_Y1 = w6.YB - w6.CAV - w6.QI_USB_OVER - 0.1   # board end; the USB-C shell sticks out 1 mm and almost touches the wall


def display():
    """GC9A01 module: glass + PCB with the header tab pointing to +x (right)."""
    d0 = w6.LCD_FRONT
    glass = FACE * Pos(0, 0, -d0 - w6.LCD_T) * Cylinder(w6.LCD_D / 2, w6.LCD_T, align=MIN)
    r = w6.LCD_PCB_D / 2
    tab_end = w6.LCD_TAB_L - r
    pcb = Cylinder(r, 1.6, align=MIN) + bx(0, tab_end, -w6.LCD_TAB_W / 2, w6.LCD_TAB_W / 2, 0, 1.6)
    pcb = FACE * Pos(0, -w6.LCD_PCB_OFF, -w6.PCB_BACK) * pcb          # PCB sits off the glass centre, towards the foot
    parts_back = FACE * Pos(0, 0, -w6.PCB_BACK - 0.6) * bx(-10, 10, 3, 9, 0, 0.6)   # small SMD parts
    hdr = FACE * bx(w6.HDR_X0, w6.HDR_X1, -w6.HDR_HW, w6.HDR_HW, -(w6.PCB_BACK + w6.HDR_PLASTIC), -w6.PCB_BACK)
    px = (w6.HDR_X0 + w6.HDR_X1) / 2
    pin_back = -(w6.PCB_BACK + w6.HDR_PLASTIC + w6.HDR_PIN)
    pin_front = -(w6.LCD_FRONT + w6.LCD_T + 0.2) + w6.HDR_STUB
    pins = None
    for i in range(7):
        p = FACE * bx(px - 0.32, px + 0.32, -7.62 + i * 2.54 - 0.32, -7.62 + i * 2.54 + 0.32, pin_back, pin_front)
        pins = p if pins is None else pins + p
    return {"lcd_glass": glass, "lcd_pcb": pcb + parts_back, "lcd_hdr": hdr, "lcd_pins": pins}


def qi():
    z = w6.qi_z()
    y1 = QI_Y1
    board = bx(-w6.QI_W / 2, w6.QI_W / 2, y1 - w6.QI_L, y1, z, z + w6.QI_T)
    usb = bx(-4.45, 4.45, y1 - 7.3 + w6.QI_USB_OVER, y1 + w6.QI_USB_OVER, z + w6.QI_USB_C - 1.5, z + w6.QI_USB_C + 1.5)   # sunk into the board
    ic = bx(-3, 5, y1 - 22, y1 - 12, z + 1.6, z + 3.2)
    cw, ct, ch = w6.QI_CAP
    cap = bx(-cw, 0, y1 - w6.QI_L + 0.3, y1 - w6.QI_L + 0.3 + ct, z + 1.6, z + 1.6 + ch)   # red film cap, bent upright
    coil = BR_LOC * Pos(0, 1.2, w6.COIL_Z) * Rot(-90, 0, 0) * Cylinder(w6.COIL_D / 2, w6.COIL_T, align=MIN)
    return {"qi_pcb": board, "qi_usb": usb, "qi_ic": ic, "qi_cap": cap, "qi_coil": coil}


def modules():
    """ESP32-C3 SuperMini lying header pins up on its solder stubs (USB-C to -x), and the buck."""
    z = w6.PLATE_T
    cx, cy, l, w = -29.0, 12.0, w6.C3_L, w6.C3_W
    zb = z + w6.C3_UNDER                                   # top of the C3 board
    c3 = bx(cx - l / 2 + 0.1, cx + l / 2, cy - 9, cy + 9, zb - 1.0, zb)
    c3_usb = bx(cx - l / 2 - 1.5, cx - l / 2 + 5.8, cy - 4.5, cy + 4.5, z + 0.4, z + 3.6)   # USB-C, ~1.5 mm proud
    rows = (cy - w / 2 + 1.27, cy + w / 2 - 1.27)
    hdr = None
    pins = None
    for ry in rows:
        h = bx(cx - 10.2, cx + 10.2, ry - 1.27, ry + 1.27, zb, zb + w6.C3_PLASTIC)
        hdr = h if hdr is None else hdr + h
        for i in range(8):
            px = cx - 8.89 + i * 2.54
            p = bx(px - 0.32, px + 0.32, ry - 0.32, ry + 0.32, z + 0.2, zb + w6.C3_PLASTIC + w6.C3_PIN)
            pins = p if pins is None else pins + p
    bx0, by0 = 29 - w6.BUCK_L / 2, 12 - w6.BUCK_W / 2
    buck = bx(bx0, bx0 + w6.BUCK_L, by0, by0 + w6.BUCK_W, z, z + 1.1)
    buck_l = bx(bx0 + 4.5, bx0 + 10.5, by0 + 5.5, by0 + 11.5, z + 1.1, z + w6.BUCK_H)        # inductor
    buck_ic = bx(bx0 + 12.5, bx0 + 17.5, by0 + 4.0, by0 + 8.0, z + 1.1, z + 2.6)
    return {"c3_pcb": c3, "c3_usb": c3_usb, "c3_hdr": hdr, "c3_pins": pins,
            "buck": buck, "buck_l": buck_l, "buck_ic": buck_ic}


def phone():
    w, h, t = w6.PHONE
    from build123d import RectangleRounded, Plane, extrude
    pl = Plane(origin=(0, -0.45, 5.05 + h / 2), x_dir=(1, 0, 0), z_dir=(0, -1, 0))
    return {"phone": BR_LOC * extrude(pl * RectangleRounded(w, h, 9), t)}


def m2_csk(length=None):
    """M2 countersunk screw along +z, head top at z = 0 (overall length, as countersunk screws are sold)."""
    length = length or w6.SCREW_L
    return Cone(2.0, 1.0, 1.0, align=MIN) + Cylinder(1.0, length, align=MIN)


def fasteners():
    """The ten M2 x 5 countersunk screws, threaded into pilot holes (for the renders). The head's 4.0 mm rim sits
    0.2 mm inside each 4.4 mm countersink."""
    out = {}
    for i, (x, y) in enumerate(w6.PLATE_BOSSES):
        out[f"scr_plate{i}"] = Pos(x, y, 1.5 + 0.2) * m2_csk()
    for i, (x, y) in enumerate(w6.TENON_SCREWS):
        out[f"scr_tenon{i}"] = Pos(x, y, w6.SOCKET_Z - 2 + 0.5 + 0.2) * m2_csk()
    for i, (u, v) in enumerate(w6.RET_BOSSES):
        out[f"scr_face{i}"] = FACE * Pos(u, v, -w6.RET_FRONT - 2 + 0.2) * m2_csk()
    for i, (x, z) in enumerate(w6.COVER_SCREWS):
        out[f"scr_cover{i}"] = BR_LOC * Pos(x, w6.BR_T - 0.2, z) * Rot(90, 0, 0) * m2_csk()
    return out


def all_parts():
    d = {}
    for f in (display, qi, modules, phone):
        d.update(f())
    return d
