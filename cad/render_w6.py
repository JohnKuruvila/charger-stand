"""Render the W6 CAD (out/assembled/*.stl) in Blender (materials and room from concept/common.py).
VIEW = hero | exploded_front | exploded_back | section | closeup   (env var)
Run: blender -b --python render_w6.py -- out.png 2400 64 denoise"""
import math, os, sys
import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "concept"))
sys.path.insert(0, HERE)
import common as c
from common import MM
import w6_consts as k

VIEW = os.environ.get("VIEW", "hero")
STL = os.path.join(HERE, "out", "assembled")

c.reset()
screw_black = c.principled("screw_black", (0.04, 0.04, 0.045), 0.4, 1.0)
blue_pcb = c.principled("lcd_pcb", (0.02, 0.10, 0.32), 0.45)
green_pcb = c.principled("qi_green", (0.02, 0.20, 0.08), 0.45)
steel = c.principled("usb_shell", (0.6, 0.6, 0.62), 0.3, 1.0)
ic = c.principled("ic", (0.02, 0.02, 0.02), 0.4)
film_cap = c.principled("film_cap", (0.35, 0.05, 0.04), 0.45)
hdr_plastic = c.principled("hdr_plastic", (0.015, 0.015, 0.017), 0.5)
pin_metal = c.principled("pin_metal", (0.75, 0.72, 0.66), 0.3, 1.0)
c3_black = c.principled("c3_black", (0.02, 0.02, 0.025), 0.45)
# plain matte PLA as built: white body, everything else brown (exact shades ASSUMED)
white = c.printed("daisy_white_pla", (0.80, 0.77, 0.70), 0.55)
brown = c.printed("brown_pla", (0.15, 0.06, 0.025), 0.55)
body_m, foot_m, accent_m, inside_m = white, brown, brown, brown
hot_glue = c.principled("hot_glue", (0.80, 0.78, 0.70), 0.35, **{"Transmission Weight": 0.6, "IOR": 1.5})
lcd_glass = c.principled("rd_glass", (0.004, 0.004, 0.005), 0.05, **{"Coat Weight": 1.0})

BR = Matrix.Translation(Vector(k.BR_POS) * MM) @ Matrix.Rotation(math.radians(-16), 4, "X")
FACE = Matrix.Translation(Vector((0, k.FACE_Y, k.FACE_Z)) * MM) @ Matrix.Rotation(math.radians(k.FACE_TILT), 4, "X")
# object frame whose Z is the backrest's local y, so layer lines run parallel to the phone face
BR_LAYERS = BR @ Matrix.Rotation(math.radians(-90), 4, "X")

groups = {}


def group(name):
    if name not in groups:
        groups[name] = c.empty("grp_" + name)
    return groups[name]


def attach(obj, grp):
    g = group(grp)
    obj.parent = g
    obj.matrix_parent_inverse = g.matrix_world.inverted()


def imp(name, mat, grp, frame=None):
    before = set(bpy.data.objects)
    bpy.ops.wm.stl_import(filepath=os.path.join(STL, name + ".stl"), global_scale=MM)
    (o,) = set(bpy.data.objects) - before
    o.name = name
    o.data.transform(o.matrix_basis)           # bake the importer's 0.001 object scale into the mesh
    o.matrix_basis = Matrix.Identity(4)
    for coll in list(o.users_collection):
        coll.objects.unlink(o)
    c.link(o, mat)
    if frame is not None:                      # re-express the mesh in `frame` so layer lines follow it
        o.data.transform(frame.inverted())
        o.matrix_world = frame
    attach(o, grp)
    return o


def child(obj, grp):
    attach(obj, grp)
    return obj


# printed parts
imp("base_body", body_m, "base")
imp("base_foot", foot_m, "base")
imp("cap", accent_m, "cap")
imp("plate", inside_m, "plate")
imp("retainer", inside_m, "retainer")
imp("backrest", accent_m, "backrest", BR_LAYERS)
imp("coil_cover", accent_m, "cover", BR_LAYERS)
imp("lip", brown, "lip")
imp("feet", hot_glue, "plate")
# bought parts
for n in ("scr_plate0", "scr_plate1", "scr_plate2", "scr_plate3"):
    imp(n, screw_black, "plate")
for n in ("scr_face0", "scr_face1"):
    imp(n, screw_black, "retainer")
for n in ("scr_tenon0", "scr_tenon1"):
    imp(n, screw_black, "base")
for n in ("scr_cover0", "scr_cover1"):
    imp(n, screw_black, "cover")
imp("lcd_glass", lcd_glass, "display")
imp("lcd_pcb", blue_pcb, "display")
imp("lcd_hdr", hdr_plastic, "display")
imp("lcd_pins", pin_metal, "display")
imp("qi_pcb", green_pcb, "plate")
imp("qi_usb", steel, "plate")
imp("qi_ic", ic, "plate")
imp("qi_cap", film_cap, "plate")
# screen: emission disc on the glass (real 32.4 mm active area)
disp = c.empty("disp")
disp.matrix_world = FACE @ Matrix.Translation((k.ACTIVE_DX * MM, 0, -k.LCD_FRONT * MM + 0.02 * MM))
c.disc_uv("rd_px", k.ACTIVE_D / 2, 0, c.image_emission("dial", "dial.png", 1.6), disp)
attach(disp, "display")
# Qi coil (ferrite towards the back, copper towards the phone)
coil = c.empty("coil")
coil.matrix_world = BR @ Matrix.Translation(Vector((0, 1.2 + k.COIL_T, k.COIL_Z)) * MM) @ Matrix.Rotation(math.radians(90), 4, "X")
c.qi_coil(coil, r_out=k.COIL_D / 2 - 2, turns=12)
attach(coil, "coil")
# ESP32-C3 (header pins up) and the green MP1584 buck on the plate, measured sizes from bought.py
imp("c3_pcb", c3_black, "plate")
imp("c3_usb", steel, "plate")
imp("c3_hdr", hdr_plastic, "plate")
imp("c3_pins", pin_metal, "plate")
imp("buck", green_pcb, "plate")
imp("buck_l", ic, "plate")
imp("buck_ic", ic, "plate")

if VIEW == "hero" or VIEW == "section":
    ph = c.empty("phone")
    ph.matrix_world = BR @ Matrix.Translation(Vector((0, -0.4, 5 + 80)) * MM) @ Matrix.Rotation(math.radians(90), 4, "X")
    c.phone(ph, "lock_portrait.png")
    # USB-C cable into the Qi board port on the back
    c.box("plug", -6, 45, k.USB_Z - 3.25, 12, 15, 6.5, c.M["rubber"], bev=1.5, segs=3)
    c.cable([(0, 59, k.USB_Z), (6, 95, 1.8), (-140, 220, 1.8), (-320, 260, 1.8)])

if VIEW.startswith("exploded"):
    up = Vector((0, 0, 1))
    back = Vector((0, math.cos(math.radians(16)), -math.sin(math.radians(16))))
    inward = Vector((0, k.N_IN[0], k.N_IN[1]))
    moves = {
        "plate": Vector((0, 0, -62)),
        "display": inward * 8 + Vector((0, 0, -30)),
        "retainer": inward * 18 + Vector((0, 0, -38)),
        "cap": up * 22,
        "backrest": up * 55,
        "coil": up * 55 + back * 32,
        "cover": up * 55 + back * 62,
        "lip": up * 55 - back * 30,
    }
    for g, v in moves.items():
        groups[g].location = v * MM

if VIEW == "section":
    cutter = c.box("cutter", 0, -400, -100, 400, 800, 600, None)
    cutter.hide_render = True
    for o in list(bpy.data.objects):
        if o.type == "MESH" and o.name not in ("cutter", "desk", "wall"):
            m = o.modifiers.new("section", "BOOLEAN")
            m.operation = "DIFFERENCE"
            m.object = cutter
            m.solver = "EXACT"

wall_y = 700 if VIEW == "exploded_back" else 320
desk_z = -80 if VIEW.startswith("exploded") else 0.0   # exploded parts float, so drop the desk out of the way
c.room(desk_mat=c.principled("desk_grey", (0.30, 0.30, 0.30), 0.5), wall_y=wall_y, desk_z=desk_z,
       wall_mat=c.principled("wall", (0.22, 0.23, 0.24), 0.9))
c.studio(key=110, fill=16, rim=60, world=0.2)

CAMS = {
    "hero": dict(cam_loc=(-0.29, -0.46, 0.20), target=(0.0, 0.01, 0.09), focus=(0.0, -0.046, 0.02), lens=45),
    "exploded_front": dict(cam_loc=(-0.40, -0.52, 0.30), target=(0.0, 0.0, 0.07), focus=(0.0, -0.02, 0.05), lens=42),
    "exploded_back": dict(cam_loc=(-0.38, 0.52, 0.30), target=(0.0, 0.02, 0.07), focus=(0.0, 0.03, 0.08), lens=42),
    "closeup": dict(cam_loc=(0.035, -0.145, 0.105), target=(0.0, -0.044, 0.019), focus=(0.0, -0.044, 0.019), lens=85),
    "section": dict(cam_loc=(0.40, -0.07, 0.11), target=(0.0, -0.004, 0.068), focus=(0.0, -0.01, 0.05), lens=58),
}
c.render(**CAMS[VIEW], fstop=8.0 if VIEW != "hero" else 5.0)
