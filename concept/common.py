"""Shared parts for the wireless charger stand concept renders (Blender 4.0, Cycles).
Copied from desk-gadget/concept/photoreal/common.py. All sizes in mm; screen and OLED content is
illustrative only (concept render, not live data)."""
import math
import os
import sys

import bmesh
import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
TEX = os.path.join(HERE, "tex")
FONT = "/usr/share/fonts/truetype/lato/Lato-Black.ttf"
MM = 0.001
M = {}  # materials, filled by reset()


def args():
    a = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    return (a[0] if a else os.path.join(HERE, "out.png"),
            int(a[1]) if len(a) > 1 else 1920, int(a[2]) if len(a) > 2 else 128,
            len(a) > 3 and a[3] == "denoise")


# ---------- materials ----------
def principled(name, color, rough=0.5, metal=0.0, **kw):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*color, 1)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    for k, v in kw.items():
        if isinstance(v, tuple) and len(b.inputs[k].default_value) == 4:
            v = (*v, 1)
        b.inputs[k].default_value = v
    return m


def add_bump(m, scale, strength, kind="noise"):
    nt = m.node_tree
    if kind == "noise":
        t = nt.nodes.new("ShaderNodeTexNoise")
        t.inputs["Detail"].default_value = 4
    else:
        t = nt.nodes.new("ShaderNodeTexWave")
        t.wave_type = "RINGS" if kind == "rings" else "BANDS"
        if kind == "brushed":
            t.bands_direction = "X"
            t.inputs["Distortion"].default_value = 2
            t.inputs["Detail"].default_value = 10
    t.inputs["Scale"].default_value = scale
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = strength
    nt.links.new(t.outputs["Fac"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], nt.nodes["Principled BSDF"].inputs["Normal"])
    return m


def image_emission(name, img, strength, rough=0.03):
    m = principled(name, (0, 0, 0), rough, **{"Coat Weight": 0.5})
    nt = m.node_tree
    t = nt.nodes.new("ShaderNodeTexImage")
    t.image = bpy.data.images.load(os.path.join(TEX, img))
    t.extension = "EXTEND"
    nt.links.new(t.outputs["Color"], nt.nodes["Principled BSDF"].inputs["Emission Color"])
    nt.nodes["Principled BSDF"].inputs["Emission Strength"].default_value = strength
    return m


def image_diffuse(name, img, rough=0.5):
    m = principled(name, (1, 1, 1), rough)
    nt = m.node_tree
    t = nt.nodes.new("ShaderNodeTexImage")
    t.image = bpy.data.images.load(os.path.join(TEX, img))
    nt.links.new(t.outputs["Color"], nt.nodes["Principled BSDF"].inputs["Base Color"])
    return m


def wood(name, dark, light, grain=(1.0, 14.0, 1.0), rough=0.42, bands="Y"):
    m = principled(name, light, rough, **{"Coat Weight": 0.3})
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    tc = nt.nodes.new("ShaderNodeTexCoord")
    mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = grain
    noise = nt.nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 6
    noise.inputs["Detail"].default_value = 8
    noise.inputs["Distortion"].default_value = 1.5
    wave = nt.nodes.new("ShaderNodeTexWave")
    wave.bands_direction = bands
    wave.inputs["Scale"].default_value = 3
    wave.inputs["Distortion"].default_value = 8
    wave.inputs["Detail"].default_value = 6
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = (*dark, 1)
    ramp.color_ramp.elements[1].color = (*light, 1)
    mix = nt.nodes.new("ShaderNodeMath")
    mix.operation = "MULTIPLY_ADD"
    mix.inputs[1].default_value = 0.6
    nt.links.new(tc.outputs["Object"], mp.inputs["Vector"])
    nt.links.new(mp.outputs["Vector"], noise.inputs["Vector"])
    nt.links.new(mp.outputs["Vector"], wave.inputs["Vector"])
    nt.links.new(wave.outputs["Fac"], mix.inputs[0])
    nt.links.new(noise.outputs["Fac"], mix.inputs[2])
    nt.links.new(mix.outputs[0], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], b.inputs["Base Color"])
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.06
    nt.links.new(mix.outputs[0], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], b.inputs["Normal"])
    return m


def diffuser():
    m = principled("diffuser", (0.9, 0.9, 0.88), 0.35)
    nt = m.node_tree
    tr = nt.nodes.new("ShaderNodeBsdfTranslucent")
    mx = nt.nodes.new("ShaderNodeMixShader")
    mx.inputs["Fac"].default_value = 0.85
    nt.links.new(nt.nodes["Principled BSDF"].outputs[0], mx.inputs[1])
    nt.links.new(tr.outputs[0], mx.inputs[2])
    nt.links.new(mx.outputs[0], nt.nodes["Material Output"].inputs["Surface"])
    return m


def glow(name, color, strength):
    return principled(name, (0, 0, 0), 0.5, **{"Emission Color": color, "Emission Strength": strength})


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    M.clear()
    M.update(
        trim=principled("trim", (0.012, 0.012, 0.014), 0.35),
        glass=principled("phone_glass", (0.004, 0.004, 0.005), 0.03, **{"Coat Weight": 1.0}),
        alu=add_bump(principled("aluminium", (0.66, 0.67, 0.69), 0.32, 1.0), 900, 0.15, "rings"),
        chrome=principled("chrome", (0.9, 0.9, 0.92), 0.08, 1.0),
        pcb=principled("pcb", (0.01, 0.01, 0.01), 0.4),
        copper=principled("copper", (0.80, 0.42, 0.25), 0.28, 1.0),
        ferrite=principled("ferrite", (0.03, 0.03, 0.03), 0.7),
        diffuser=diffuser(),
        rubber=principled("cable", (0.02, 0.02, 0.02), 0.6),
        oak=wood("oak", (0.20, 0.10, 0.045), (0.42, 0.24, 0.11)),
        wall=principled("wall", (0.55, 0.52, 0.48), 0.9),
    )


# ---------- geometry ----------
def coll():
    return bpy.context.scene.collection


def link(obj, mat, parent=None, smooth=True):
    coll().objects.link(obj)
    if mat:
        obj.data.materials.append(mat)
    if parent:
        obj.parent = parent
    if smooth and obj.type == "MESH":
        for p in obj.data.polygons:
            p.use_smooth = True
        obj.data.use_auto_smooth = True
        obj.data.auto_smooth_angle = math.radians(35)
    return obj


def bevel(obj, width, segs=4, angle=35):
    m = obj.modifiers.new("bevel", "BEVEL")
    m.width = width * MM
    m.segments = segs
    m.limit_method = "ANGLE"
    m.angle_limit = math.radians(angle)
    m.harden_normals = True
    return obj


def mesh_from(name, verts, faces):
    me = bpy.data.meshes.new(name)
    me.from_pydata([Vector(v) * MM for v in verts], [], faces)
    me.update()
    return bpy.data.objects.new(name, me)


BOX_FACES = [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)]


def box(name, x0, y0, z0, sx, sy, sz, mat, parent=None, bev=0, segs=4):
    v = [(x0 + dx * sx, y0 + dy * sy, z0 + dz * sz) for dz in (0, 1) for dy in (0, 1) for dx in (0, 1)]
    o = mesh_from(name, v, BOX_FACES)
    if bev:
        bevel(o, bev, segs)
    return link(o, mat, parent)


def hexa(name, v8, mat, parent=None, bev=0, segs=4):
    """Arbitrary convex 8-vertex solid, vertices ordered like box(): z0 (y0 x0,x1; y1 x0,x1), then z1."""
    o = mesh_from(name, v8, BOX_FACES)
    if bev:
        bevel(o, bev, segs)
    return link(o, mat, parent)


def prism(name, pts, z0, h, mat, parent=None):
    n = len(pts)
    v = [(x, y, z0) for x, y in pts] + [(x, y, z0 + h) for x, y in pts]
    f = [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
    f += [tuple(reversed(range(n))), tuple(range(n, 2 * n))]
    return link(mesh_from(name, v, f), mat, parent)


def circle(r, n=96, cx=0, cy=0):
    return [(cx + r * math.cos(2 * math.pi * i / n), cy + r * math.sin(2 * math.pi * i / n)) for i in range(n)]


def cyl(name, r, h, mat, parent=None, loc=(0, 0, 0), n=96, bev=0):
    o = prism(name, circle(r, n, loc[0], loc[1]), loc[2], h, mat, parent)
    if bev:
        bevel(o, bev, 3)
    return o


def ring(name, r_in, r_out, h, mat, parent=None, z0=0, n=96):
    ci = circle(r_in, n)
    co = circle(r_out, n)
    v = [(x, y, z0) for x, y in co] + [(x, y, z0) for x, y in ci] + \
        [(x, y, z0 + h) for x, y in co] + [(x, y, z0 + h) for x, y in ci]
    f = []
    for i in range(n):
        j = (i + 1) % n
        f += [(i, j, 2 * n + j, 2 * n + i), (n + j, n + i, 3 * n + i, 3 * n + j),
              (2 * n + i, 2 * n + j, 3 * n + j, 3 * n + i), (j, i, n + i, n + j)]
    return link(mesh_from(name, v, f), mat, parent)


def empty(name, loc=(0, 0, 0), rot=(0, 0, 0), parent=None):
    e = bpy.data.objects.new(name, None)
    e.location = Vector(loc) * MM
    e.rotation_euler = [math.radians(a) for a in rot]
    coll().objects.link(e)
    if parent:
        e.parent = parent
    return e


def face_frame(name, x, y, z, parent=None):
    """Frame on a front face: local +Z points at the viewer (-Y), local +Y points up."""
    return empty(name, (x, y, z), (90, 0, 0), parent)


def plane(name, x0, z0, sx, sz, y, mat, parent=None):
    """Vertical plane facing -Y, UV 0..1 upright."""
    o = mesh_from(name, [(x0, y, z0), (x0 + sx, y, z0), (x0 + sx, y, z0 + sz), (x0, y, z0 + sz)], [(0, 1, 2, 3)])
    uv = o.data.uv_layers.new()
    for li, (u, v) in enumerate([(0, 0), (1, 0), (1, 1), (0, 1)]):
        uv.data[li].uv = (u, v)
    return link(o, mat, parent, smooth=False)


def flat(name, x0, y0, sx, sy, z, mat, parent=None):
    """Horizontal plane facing +Z, UV 0..1."""
    o = mesh_from(name, [(x0, y0, z), (x0 + sx, y0, z), (x0 + sx, y0 + sy, z), (x0, y0 + sy, z)], [(0, 1, 2, 3)])
    uv = o.data.uv_layers.new()
    for li, (u, v) in enumerate([(0, 0), (1, 0), (1, 1), (0, 1)]):
        uv.data[li].uv = (u, v)
    return link(o, mat, parent, smooth=False)


def label(text, x, y, parent, size=4.2, mat=None, align="CENTER", font=FONT):
    cu = bpy.data.curves.new(text, "FONT")
    cu.body = text
    cu.font = bpy.data.fonts.load(font)
    cu.size = size * MM
    cu.align_x = align
    cu.extrude = 0.1 * MM
    o = bpy.data.objects.new(text, cu)
    o.location = Vector((x, y, 0.1)) * MM
    coll().objects.link(o)
    o.data.materials.append(mat or M["label"])
    o.parent = parent
    return o


def sphere(name, r, mat, parent, loc, scale=(1, 1, 1)):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=96, ring_count=48, radius=r * MM)
    o = bpy.context.active_object
    for c in list(o.users_collection):
        c.objects.unlink(o)
    link(o, mat, parent)
    o.location = Vector(loc) * MM
    o.scale = scale
    return o


# ---------- charger stand parts ----------
def rrect(w, h, r, n=12, cx=0, cy=0):
    """Rounded rectangle outline centred on (cx, cy)."""
    pts = []
    for qx, qy, a0 in ((1, 1, 0), (-1, 1, 90), (-1, -1, 180), (1, -1, 270)):
        ox, oy = cx + qx * (w / 2 - r), cy + qy * (h / 2 - r)
        for i in range(n + 1):
            a = math.radians(a0 + 90 * i / n)
            pts.append((ox + r * math.cos(a), oy + r * math.sin(a)))
    return pts


def printed(name, color, rough=0.55, layer=0.2, strength=0.08, **kw):
    """FDM plastic: principled + horizontal layer lines (bands along object Z)."""
    m = principled(name, color, rough, **kw)
    nt = m.node_tree
    tc = nt.nodes.new("ShaderNodeTexCoord")
    t = nt.nodes.new("ShaderNodeTexWave")
    t.bands_direction = "Z"
    t.inputs["Scale"].default_value = 0.628 / (layer * MM)
    t.inputs["Distortion"].default_value = 0.2
    bump = nt.nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = strength
    bump.inputs["Distance"].default_value = 0.02 * MM
    nt.links.new(tc.outputs["Object"], t.inputs["Vector"])
    nt.links.new(t.outputs["Fac"], bump.inputs["Height"])
    nt.links.new(bump.outputs["Normal"], nt.nodes["Principled BSDF"].inputs["Normal"])
    return m


def clear(name, tint=(0.8, 0.82, 0.85), rough=0.12):
    return principled(name, tint, rough, **{"Transmission Weight": 1.0, "IOR": 1.5})


def disc_uv(name, r, z, mat, parent=None, n=96):
    """Horizontal disc facing +Z with planar UVs covering the square around it."""
    pts = circle(r, n)
    o = mesh_from(name, [(x, y, z) for x, y in pts], [tuple(range(n))])
    uv = o.data.uv_layers.new()
    for li, vi in enumerate(o.data.polygons[0].vertices):
        x, y = pts[vi]
        uv.data[li].uv = (0.5 + x / (2 * r), 0.5 + y / (2 * r))
    return link(o, mat, parent, smooth=False)


def arc(name, r_in, r_out, a0, a1, z0, h, mat, parent=None, n=48):
    """Annular sector from angle a0 to a1 (degrees, CCW from +X)."""
    pts = [(r_out * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
            r_out * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]
    pts += [(r_in * math.cos(math.radians(a1 - (a1 - a0) * i / n)),
             r_in * math.sin(math.radians(a1 - (a1 - a0) * i / n))) for i in range(n + 1)]
    return prism(name, pts, z0, h, mat, parent)


def phone(parent, img="lock_portrait.png", landscape=False):
    """75 x 160 x 8.4 mm slab lying in local XY, screen facing +Z, origin at the centre of the back."""
    w, h = (160, 75) if landscape else (75, 160)
    body = prism("phone_body", rrect(w, h, 9), 0, 8.4, principled("phone_frame", (0.05, 0.05, 0.06), 0.25, 0.8), parent)
    bevel(body, 1.2, 4, 50)
    flat("phone_glass", -w / 2 + 0.8, -h / 2 + 0.8, w - 1.6, h - 1.6, 8.42, M["glass"], parent)
    flat("phone_screen", -w / 2 + 2.2, -h / 2 + 2.2, w - 4.4, h - 4.4, 8.45,
         image_emission("lock_" + img, img, 1.2), parent)


def oled(parent, img="oled_blue.png", strength=6):
    """0.91in 128x32 OLED in local XY facing +Z: glass 30 x 11.5, active area 22.4 x 5.6."""
    flat("oled_glass", -15, -5.75, 30, 11.5, 0.05, principled("oled_glass", (0.005, 0.005, 0.006), 0.05,
                                                              **{"Coat Weight": 1.0}), parent)
    flat("oled_px", -11.2, -2.8, 22.4, 5.6, 0.08, image_emission("oled", img, strength), parent)


def round_display(parent, img="dial.png", bezel_mat=None, bezel_r=21, strength=1.6):
    """1.28in GC9A01 module (38 mm round PCB, glass ~36 mm, active 32.4 mm) behind a printed bezel.
    Local XY facing +Z; glass surface at z = 0, bezel front at z = 1.6 with a 33.6 mm aperture."""
    ring("rd_bezel", 16.8, bezel_r, 1.6, bezel_mat or M["trim"], parent)
    cyl("rd_glass", 18, 0.2, principled("rd_glass", (0.004, 0.004, 0.005), 0.05, **{"Coat Weight": 1.0}), parent,
        loc=(0, 0, -0.2))
    disc_uv("rd_px", 16.2, 0.02, image_emission("dial_" + img, img, strength), parent)


def qi_coil(parent, r_out=24, turns=11, ferrite=True):
    """Visible Qi TX coil: ferrite disc plus concentric copper turns, local XY."""
    if ferrite:
        cyl("ferrite", r_out + 2, 1.0, M["ferrite"], parent)
    for i in range(turns):
        r = r_out - i * 1.35
        if r < 6:
            break
        ring(f"turn{i}", r - 0.55, r, 0.9, M["copper"], parent, z0=1.0, n=72)


def pcb(name, x, y, w, h, parent, chips=(), color=(0.02, 0.18, 0.08)):
    board = box(name, x, y, 0, w, h, 1.6, principled(name + "_fr4", color, 0.45), parent, bev=0.3, segs=1)
    for i, (cx, cy, cw, ch, ht) in enumerate(chips):
        box(f"{name}_ic{i}", x + cx, y + cy, 1.6, cw, ch, ht, M["pcb"], parent, bev=0.2, segs=1)
    return board


def led_ring(parent, n, r, lit, color, strength=60, r_in=None, r_out=None, dim=0.0, z=0, cap=1.5):
    """WS2812B 5050 LEDs on a ring PCB at radius r, under a printed white diffuser annulus `cap` mm
    thick. Lit LEDs fill clockwise from 12 o'clock (+Y). Returns nothing; all in local XY facing +Z."""
    r_in = r_in if r_in is not None else r - 5
    r_out = r_out if r_out is not None else r + 5
    ring("led_pcb", r - 4.5, r + 4.5, 1.6, M["pcb"], parent, z0=z - 3.6)
    on = glow("led_lit", color, strength)
    part = glow("led_part", color, strength * dim) if dim else principled("led_off", (0.85, 0.85, 0.8), 0.3)
    for i in range(n):
        a = math.radians(90 - i * 360 / n)
        m = on if i < lit else (part if i == lit and dim else principled("led_off", (0.85, 0.85, 0.8), 0.3))
        led = box(f"led{i}", -2.5, -2.5, z - 2.0, 5, 5, 1.6, m, parent, 0.3, 2)
        led.location = Vector((r * math.cos(a), r * math.sin(a), 0)) * MM
        led.rotation_euler = (0, 0, a)
    ring("led_diffuser", r_in, r_out, cap, M["diffuser"], parent, z0=z - 0.2)


STRIP_PITCH = 1000 / 60  # WS2812B 60 LED/m tape: 16.67 mm pitch, 10 mm wide


def _basis(t, n):
    from mathutils import Matrix
    t, n = Vector(t).normalized(), Vector(n).normalized()
    y = n.cross(t)
    return Matrix(((t.x, y.x, n.x), (t.y, y.y, n.y), (t.z, y.z, n.z)))


def led_strip(parent, leds, lit, color, strength=45, dim=0.35, tape=True):
    """WS2812B tape. `leds` is a list of (position, normal, tangent) in local mm; LEDs face `normal`.
    The first `lit` LEDs are on, the next one is dimmed (partial step), the rest are off."""
    on = glow("strip_on", color, strength)
    part = glow("strip_part", color, strength * dim)
    off = principled("strip_off", (0.85, 0.85, 0.8), 0.3)
    tape_mat = principled("strip_tape", (0.80, 0.80, 0.78), 0.5)
    for i, (p, n, t) in enumerate(leds):
        m = on if i < lit else (part if i == lit else off)
        mat = _basis(t, n).to_4x4()
        mat.translation = Vector(p) * MM
        led = box(f"sled{i}", -2.5, -2.5, 0, 5, 5, 1.6, m, parent, 0.3, 2)
        led.matrix_basis = mat
        if tape:
            tp = box(f"stape{i}", -STRIP_PITCH / 2, -5, -0.3, STRIP_PITCH, 10, 0.3, tape_mat, parent)
            tp.matrix_basis = mat


def strip_line(start, direction, normal, count, pitch=STRIP_PITCH):
    d = Vector(direction).normalized()
    return [(tuple(Vector(start) + d * (pitch * (i + 0.5))), tuple(normal), tuple(d)) for i in range(count)]


def strip_arc(radius, a0, count, z=0.0, inward=False, clockwise=True, pitch=STRIP_PITCH):
    """Tape curled around the local Z axis (bend axis parallel to the tape width), starting at angle a0."""
    step = math.degrees(pitch / radius) * (-1 if clockwise else 1)
    out = []
    for i in range(count):
        a = math.radians(a0 + step * (i + 0.5))
        r = Vector((math.cos(a), math.sin(a), 0))
        tang = Vector((-math.sin(a), math.cos(a), 0)) * (-1 if clockwise else 1)
        out.append((tuple(r * radius + Vector((0, 0, z))), tuple(-r if inward else r), tuple(tang)))
    return out


def sweep_rect(name, path, w, t, mat, parent=None):
    """Rectangular section (w across X, t across the path normal) swept along a 2D path of (y, z) points."""
    verts, faces = [], []
    n = len(path)
    for i, (y, z) in enumerate(path):
        y0, z0 = path[max(i - 1, 0)]
        y1, z1 = path[min(i + 1, n - 1)]
        dy, dz = y1 - y0, z1 - z0
        L = math.hypot(dy, dz)
        ny, nz = -dz / L * t / 2, dy / L * t / 2
        for sx in (-w / 2, w / 2):
            verts += [(sx, y + ny, z + nz), (sx, y - ny, z - nz)]
    for i in range(n - 1):
        a, b = 4 * i, 4 * (i + 1)
        faces += [(a, a + 2, b + 2, b), (a + 1, b + 1, b + 3, a + 3), (a, b, b + 1, a + 1), (a + 2, a + 3, b + 3, b + 2)]
    faces += [(0, 1, 3, 2), (4 * n - 4, 4 * n - 2, 4 * n - 1, 4 * n - 3)]
    return link(mesh_from(name, verts, faces), mat, parent)


def silk(name, color, rough=0.28):
    """Silk PLA: satin metallic sheen, never a mirror."""
    return printed(name, color, rough, strength=0.12, metal=0.55, **{"Coat Weight": 0.2})


def wood_pla(name, color=(0.16, 0.09, 0.045)):
    """Wood-fill PLA: matte, slightly speckled, strong layer lines."""
    m = printed(name, color, 0.78, strength=0.16)
    nt = m.node_tree
    t = nt.nodes.new("ShaderNodeTexNoise")
    t.inputs["Scale"].default_value = 4000
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = (*[v * 0.8 for v in color], 1)
    ramp.color_ramp.elements[1].color = (*[min(1, v * 1.15) for v in color], 1)
    tc = nt.nodes.new("ShaderNodeTexCoord")
    nt.links.new(tc.outputs["Object"], t.inputs["Vector"])
    nt.links.new(t.outputs["Fac"], ramp.inputs["Fac"])
    nt.links.new(ramp.outputs["Color"], nt.nodes["Principled BSDF"].inputs["Base Color"])
    return m


def cut(obj, cutter):
    """Boolean-subtract `cutter` from `obj` (cutter is hidden from the render)."""
    m = obj.modifiers.new("cut", "BOOLEAN")
    m.operation = "DIFFERENCE"
    m.object = cutter
    cutter.hide_render = True
    cutter.hide_viewport = True
    # cut first, then bevel, so the bevel also rounds the opening and shading stays clean
    obj.modifiers.move(len(obj.modifiers) - 1, 0)
    return obj


MONO = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"


def acrylic(name="smoked_acrylic", tint=(0.80, 0.80, 0.82)):
    """Bought laser-cut cast acrylic sheet, light smoked grey (about 80% transmission): glossy, see-through."""
    return principled(name, tint, 0.03, **{"Transmission Weight": 1.0, "IOR": 1.49})


def screw_m3(parent, x, y, z=0.0, mat=None):
    """M3 button-head hex screw seen from the head side (head 5.7 mm dia, 1.65 mm tall), local +Z out."""
    mat = mat or principled("black_oxide", (0.03, 0.03, 0.035), 0.35, 0.8)
    head = empty(f"screw_{x}_{y}", (x, y, z), (0, 0, 0), parent)
    sphere("screw_head", 2.85, mat, head, (0, 0, 0), (1, 1, 0.58))
    cyl("hex", 1.15, 0.2, principled("socket", (0.005, 0.005, 0.005), 0.6), head, loc=(0, 0, 1.5), n=6)


def wire(pts, color, radius=0.65):
    cu = bpy.data.curves.new("wire", "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth = radius * MM
    cu.bevel_resolution = 4
    sp = cu.splines.new("BEZIER")
    sp.bezier_points.add(len(pts) - 1)
    for bp, p in zip(sp.bezier_points, pts):
        bp.co = Vector(p) * MM
        bp.handle_left_type = bp.handle_right_type = "AUTO"
    return link(bpy.data.objects.new("wire", cu), principled("wire_" + str(color), color, 0.45), smooth=False)


def c3_supermini(parent, x, y, z=0.0, rot=0):
    """ESP32-C3 SuperMini: 22.5 x 18 mm black PCB, USB-C at one end, ceramic antenna, shield can."""
    f = empty("c3", (x, y, z), (0, 0, rot), parent)
    box("c3_pcb", -11.25, -9, 0, 22.5, 18, 1.0, principled("c3_black", (0.02, 0.02, 0.025), 0.45), f, bev=0.3, segs=1)
    box("c3_can", -4, -5, 1.0, 9, 9, 0.9, principled("c3_can", (0.5, 0.5, 0.5), 0.35, 0.9), f, bev=0.2, segs=1)
    box("c3_usb", -12.2, -4.5, 1.0, 7.4, 9, 3.2, principled("usb_shell", (0.6, 0.6, 0.62), 0.3, 1.0), f, bev=0.8, segs=3)
    box("c3_ant", 7.5, -3, 1.0, 3, 6, 1.0, principled("ant", (0.1, 0.1, 0.12), 0.4), f)
    for i in range(8):
        for side in (-1, 1):
            cyl(f"pad{i}{side}", 0.7, 0.05, principled("gold", (0.8, 0.6, 0.3), 0.3, 1.0), f,
                loc=(-8.9 + i * 2.54, side * 7.6, 1.0), n=12)
    return f


def qi_board(parent, x, y, z=0.0, rot=0):
    """Generic Qi TX board (Electrobot NAW15 class): green PCB ~52 x 24 mm (ASSUMED until measured)."""
    f = empty("qi_pcb", (x, y, z), (0, 0, rot), parent)
    box("qi_fr4", -26, -12, 0, 52, 24, 1.6, principled("qi_green", (0.02, 0.20, 0.08), 0.45), f, bev=0.3, segs=1)
    box("qi_ic", -14, -5, 1.6, 7, 7, 1.0, principled("ic", (0.02, 0.02, 0.02), 0.4), f)
    box("qi_mosfets", -3, 3, 1.6, 10, 5, 1.3, principled("ic2", (0.02, 0.02, 0.02), 0.4), f)
    cyl("qi_cap", 3.2, 6.5, principled("cap", (0.25, 0.05, 0.05), 0.4), f, loc=(14, -4, 1.6))
    box("qi_usb", 20, -4.5, 1.6, 7.4, 9, 3.2, principled("usb_shell", (0.6, 0.6, 0.62), 0.3, 1.0), f, bev=0.8, segs=3)
    return f


def cable(pts, radius=1.8):
    cu = bpy.data.curves.new("cable", "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth = radius * MM
    cu.bevel_resolution = 6
    sp = cu.splines.new("BEZIER")
    sp.bezier_points.add(len(pts) - 1)
    for bp, p in zip(sp.bezier_points, pts):
        bp.co = Vector(p) * MM
        bp.handle_left_type = bp.handle_right_type = "AUTO"
    link(bpy.data.objects.new("cable", cu), M["rubber"], smooth=False)


def room(desk_mat=None, wall_y=750, wall_mat=None, desk_z=0.0):
    d = flat("desk", -700, -500, 1600, 1500, desk_z, desk_mat or M["oak"])
    d.data.uv_layers.remove(d.data.uv_layers[0])
    plane("wall", -900, 0, 2000, 1200, wall_y, wall_mat or M["wall"])


# ---------- lights, camera, render ----------
def area(name, loc, target, size, power, color=(1, 1, 1)):
    ld = bpy.data.lights.new(name, "AREA")
    ld.shape = "RECTANGLE"
    ld.size, ld.size_y = size
    ld.energy = power
    ld.color = color
    o = bpy.data.objects.new(name, ld)
    o.location = loc
    o.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    coll().objects.link(o)
    return o


def studio(key=160, fill=20, rim=60, world=0.35, world_color=(0.20, 0.21, 0.23)):
    w = bpy.data.worlds.new("world")
    bpy.context.scene.world = w
    w.use_nodes = True
    w.node_tree.nodes["Background"].inputs["Color"].default_value = (*world_color, 1)
    w.node_tree.nodes["Background"].inputs["Strength"].default_value = world
    area("window_key", (-0.9, -0.3, 0.9), (0.1, 0.1, 0.05), (1.4, 1.0), key, (1.0, 0.96, 0.9))
    area("fill", (0.9, -0.7, 0.4), (0.1, 0.1, 0.05), (0.8, 0.8), fill, (0.85, 0.9, 1.0))
    area("rim", (0.35, 0.6, 0.55), (0.1, 0.1, 0.1), (0.5, 0.5), rim, (1.0, 0.9, 0.85))


def render(cam_loc, target, focus, lens=55, fstop=3.5, exposure=-0.4, aspect=2 / 3, look="AgX - Medium High Contrast"):
    out, res_x, samples, denoise = args()
    scene = bpy.context.scene
    cd = bpy.data.cameras.new("cam")
    cd.lens = lens
    cd.dof.use_dof = True
    cd.dof.aperture_fstop = fstop
    cam = bpy.data.objects.new("cam", cd)
    cam.location = cam_loc
    cam.rotation_euler = (Vector(target) - Vector(cam_loc)).to_track_quat("-Z", "Y").to_euler()
    cd.dof.focus_distance = (Vector(focus) - Vector(cam_loc)).length
    coll().objects.link(cam)
    scene.camera = cam
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = samples
    scene.cycles.use_adaptive_sampling = True
    scene.cycles.max_bounces = 10
    scene.cycles.transmission_bounces = 10
    # distro Blender has no OIDN: render large and downscale. With the official build (~/Applications),
    # pass "denoise" to use OIDN with albedo + normal guides so layer lines and texture survive.
    scene.cycles.use_denoising = denoise
    if denoise:
        scene.cycles.denoiser = "OPENIMAGEDENOISE"
        scene.cycles.denoising_input_passes = "RGB_ALBEDO_NORMAL"
        scene.cycles.denoising_prefilter = "ACCURATE"
    scene.cycles.sample_clamp_indirect = 3.0
    scene.render.resolution_x = res_x
    scene.render.resolution_y = int(res_x * aspect)
    scene.render.threads_mode = "FIXED"
    scene.render.threads = 5
    scene.view_settings.view_transform = "AgX"
    scene.view_settings.look = look
    scene.view_settings.exposure = exposure
    scene.render.filepath = out
    scene.render.image_settings.file_format = "PNG"
    bpy.ops.render.render(write_still=True)
    print("WROTE", out)
