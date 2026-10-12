"""Textures for the charger stand concept renders: phone lock screen, 128x32 OLED readout, round dial.
All content is illustrative only (concept render, not live data)."""
import math
import os
from PIL import Image, ImageDraw, ImageFilter, ImageFont

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tex")
LATO = "/usr/share/fonts/truetype/lato/"
DEJAVU = "/usr/share/fonts/truetype/dejavu/"


def font(path, size):
    return ImageFont.truetype(path, size)


def lock_screen(name, landscape=False):
    # 1080x2400 phone panel, drawn at half size; dark wallpaper, Android-style charging lock screen
    W, H = (1200, 540) if landscape else (540, 1200)
    im = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(im)
    for y in range(H):
        t = y / H
        d.line([(0, y), (W, y)], fill=(int(10 + 18 * t), int(12 + 10 * t), int(24 + 26 * (1 - t))))
    blob = Image.new("L", (W, H), 0)
    ImageDraw.Draw(blob).ellipse((W * 0.1, H * 0.35, W * 0.9, H * 0.75), fill=90)
    blob = blob.filter(ImageFilter.GaussianBlur(120))
    im = Image.composite(Image.new("RGB", (W, H), (40, 90, 120)), im, blob)
    d = ImageDraw.Draw(im)
    white, grey, green = (238, 240, 246), (170, 176, 190), (80, 225, 140)
    if landscape:
        d.text((70, 120), "20:14", font=font(LATO + "Lato-Light.ttf", 190), fill=white)
        d.text((80, 340), "Thu, 24 Sep", font=font(LATO + "Lato-Regular.ttf", 36), fill=grey)
        d.text((W - 70, 420), "Charging wirelessly  •  87%", font=font(LATO + "Lato-Regular.ttf", 32),
               fill=green, anchor="ra")
    else:
        d.text((W / 2, 150), "20", font=font(LATO + "Lato-Light.ttf", 200), fill=white, anchor="ma")
        d.text((W / 2, 360), "14", font=font(LATO + "Lato-Light.ttf", 200), fill=white, anchor="ma")
        d.text((W / 2, 600), "Thu, 24 Sep", font=font(LATO + "Lato-Regular.ttf", 34), fill=grey, anchor="ma")
        d.text((W / 2, 1080), "Charging wirelessly  •  87%", font=font(LATO + "Lato-Regular.ttf", 28),
               fill=green, anchor="ma")
    im.save(os.path.join(OUT, name))


def oled(name, color=(90, 175, 255)):
    # true 128x32 1-bit frame, upscaled with a faint pixel grid so it reads as a real OLED
    fb = Image.new("1", (128, 32), 0)
    d = ImageDraw.Draw(fb)
    d.text((1, -3), "87%", font=font(DEJAVU + "DejaVuSans-Bold.ttf", 30), fill=1)
    small = font(DEJAVU + "DejaVuSansMono-Bold.ttf", 10)
    d.text((76, 0), "CHARGING", font=small, fill=1)
    d.rectangle((76, 13, 122, 19), outline=1)
    d.rectangle((78, 15, 78 + int(42 * 0.87), 17), fill=1)
    d.rectangle((123, 15, 124, 17), fill=1)
    d.text((76, 21), "34°C", font=small, fill=1)
    S = 8
    big = fb.convert("L").resize((128 * S, 32 * S), Image.NEAREST)
    grid = Image.new("L", big.size, 255)
    g = ImageDraw.Draw(grid)
    for i in range(0, big.size[0], S):
        g.line([(i, 0), (i, big.size[1])], fill=120)
    for j in range(0, big.size[1], S):
        g.line([(0, j), (big.size[0], j)], fill=120)
    mask = Image.composite(big, Image.new("L", big.size, 0), grid.point(lambda v: 255 if v > 200 else 0))
    im = Image.new("RGB", big.size, (0, 0, 0))
    im.paste(Image.new("RGB", big.size, color), (0, 0), mask)
    im.save(os.path.join(OUT, name))


def dial(name, accent=(80, 225, 140)):
    # 240x240 round GC9A01, drawn at 2x; everything outside the circle is black
    S = 2
    W = 240 * S
    im = Image.new("RGB", (W, W), (6, 8, 10))
    d = ImageDraw.Draw(im)
    track = (26, 30, 36)
    box = (22 * S, 22 * S, W - 22 * S, W - 22 * S)
    d.arc(box, 0, 360, fill=track, width=14 * S)
    d.arc(box, -90, -90 + 0.87 * 360, fill=accent, width=14 * S)
    d.text((W / 2 - 6 * S, 118 * S), "87", font=font(LATO + "Lato-Black.ttf", 78 * S), fill=(240, 242, 246), anchor="mm")
    d.text((W / 2 + 46 * S, 102 * S), "%", font=font(LATO + "Lato-Bold.ttf", 26 * S), fill=(160, 168, 180), anchor="mm")
    d.text((W / 2, 70 * S), "⚡ CHARGING", font=font(DEJAVU + "DejaVuSans-Bold.ttf", 12 * S), fill=accent, anchor="mm")
    d.text((W / 2, 164 * S), "34°C  ·  +1% / 2 min", font=font(LATO + "Lato-Bold.ttf", 13 * S), fill=(160, 168, 180), anchor="mm")
    d.text((W / 2, 184 * S), "Full ~21:02 Est.", font=font(LATO + "Lato-Regular.ttf", 12 * S), fill=(120, 128, 140), anchor="mm")
    m = Image.new("L", (W, W), 0)
    ImageDraw.Draw(m).ellipse((0, 0, W - 1, W - 1), fill=255)
    out = Image.new("RGB", (W, W), (0, 0, 0))
    out.paste(im, (0, 0), m)
    out.save(os.path.join(OUT, name))


def round_canvas(bg):
    W = 480
    return W, Image.new("RGB", (W, W), bg)


def round_save(im, name):
    W = im.size[0]
    m = Image.new("L", (W, W), 0)
    ImageDraw.Draw(m).ellipse((0, 0, W - 1, W - 1), fill=255)
    out = Image.new("RGB", (W, W), (0, 0, 0))
    out.paste(im, (0, 0), m)
    out.save(os.path.join(OUT, name))


def polar(cx, cy, r, deg):
    a = math.radians(deg)
    return cx + r * math.cos(a), cy - r * math.sin(a)


def fuel(name="fuel.png"):
    # automotive fuel gauge: E..F over a 240 degree sweep, needle at 87%
    W, im = round_canvas((10, 10, 12))
    d = ImageDraw.Draw(im)
    c = W / 2
    amber, white, red = (255, 150, 30), (235, 235, 240), (230, 50, 40)
    a0, a1 = 210, -30
    for i in range(41):
        a = a0 + (a1 - a0) * i / 40
        big = i % 10 == 0
        col = red if i <= 4 else white
        d.line([polar(c, c, 214, a), polar(c, c, 188 if big else 202, a)], fill=col, width=7 if big else 3)
    f = font(LATO + "Lato-Black.ttf", 44)
    d.text(polar(c, c, 150, a0), "E", font=f, fill=red, anchor="mm")
    d.text(polar(c, c, 150, (a0 + a1) / 2), "½", font=f, fill=white, anchor="mm")
    d.text(polar(c, c, 150, a1), "F", font=f, fill=white, anchor="mm")
    na = a0 + (a1 - a0) * 0.87
    tip, tail = polar(c, c, 196, na), polar(c, c, -30, na)
    d.line([tail, tip], fill=amber, width=9)
    d.ellipse((c - 26, c - 26, c + 26, c + 26), fill=(30, 30, 34), outline=(70, 70, 76), width=3)
    d.text((c, c + 110), "87%", font=font(LATO + "Lato-Black.ttf", 58), fill=white, anchor="mm")
    d.text((c, c + 160), "PHONE", font=font(LATO + "Lato-Bold.ttf", 22), fill=(140, 140, 150), anchor="mm")
    round_save(im, name)


def eye(name="eye.png"):
    # EMO-style robot eye: rounded cyan pupil with a highlight, slightly 'happy' (lower lid curve)
    W, im = round_canvas((4, 6, 10))
    d = ImageDraw.Draw(im)
    cyan = (40, 220, 255)
    d.rounded_rectangle((W / 2 - 150, W / 2 - 175, W / 2 + 150, W / 2 + 165), radius=110, fill=cyan)
    d.ellipse((W / 2 - 280, W / 2 + 80, W / 2 + 280, W / 2 + 420), fill=(4, 6, 10))
    d.ellipse((W / 2 + 40, W / 2 - 140, W / 2 + 120, W / 2 - 60), fill=(230, 252, 255))
    glow = im.filter(ImageFilter.GaussianBlur(18))
    im = Image.blend(glow, im, 0.75)
    round_save(im, name)


def watch(name="watch.png"):
    # analogue dial on the LCD: cream face, hands at 20:14, charge sub-dial at 6 o'clock
    W, im = round_canvas((236, 228, 212))
    d = ImageDraw.Draw(im)
    c = W / 2
    ink, red = (30, 30, 34), (190, 40, 30)
    for i in range(60):
        a = 90 - i * 6
        big = i % 5 == 0
        d.line([polar(c, c, 228, a), polar(c, c, 200 if big else 216, a)], fill=ink, width=8 if big else 2)
    f = font(LATO + "Lato-Bold.ttf", 34)
    for n, a in ((12, 90), (3, 0), (9, 180)):
        d.text(polar(c, c, 168, a), str(n), font=f, fill=ink, anchor="mm")
    # charge sub-dial
    sc = (c, c + 95)
    d.ellipse((sc[0] - 62, sc[1] - 62, sc[0] + 62, sc[1] + 62), outline=ink, width=3)
    for i in range(11):
        a = 90 - i * 36
        d.line([polar(sc[0], sc[1], 60, a), polar(sc[0], sc[1], 50, a)], fill=ink, width=2)
    d.text((sc[0], sc[1] + 28), "CHARGE", font=font(LATO + "Lato-Bold.ttf", 14), fill=ink, anchor="mm")
    d.line([sc, polar(sc[0], sc[1], 50, 90 - 0.87 * 360)], fill=red, width=4)
    d.text((c, c - 88), "87%", font=font(LATO + "Lato-Bold.ttf", 22), fill=red, anchor="mm")
    # hands at 20:14
    ha = 90 - (8 * 30 + 14 * 0.5)
    ma = 90 - 14 * 6
    d.line([(c, c), polar(c, c, 120, ha)], fill=ink, width=14)
    d.line([(c, c), polar(c, c, 190, ma)], fill=ink, width=9)
    d.ellipse((c - 14, c - 14, c + 14, c + 14), fill=ink)
    round_save(im, name)


def dial_min(name="dial_min.png"):
    # minimal lamp-head dial: thin white ring, big light numerals
    W, im = round_canvas((8, 8, 10))
    d = ImageDraw.Draw(im)
    c = W / 2
    d.arc((24, 24, W - 24, W - 24), 0, 360, fill=(40, 40, 44), width=8)
    d.arc((24, 24, W - 24, W - 24), -90, -90 + 0.87 * 360, fill=(245, 245, 240), width=8)
    d.text((c, c - 6), "87", font=font(LATO + "Lato-Light.ttf", 170), fill=(245, 245, 240), anchor="mm")
    d.text((c, c + 110), "charging · 34°C", font=font(LATO + "Lato-Regular.ttf", 26), fill=(150, 150, 156), anchor="mm")
    round_save(im, name)


def nest(name="nest.png"):
    # thermostat-style charge page: tick ring lit to 87% in orange, big number, page dots
    W, im = round_canvas((12, 12, 14))
    d = ImageDraw.Draw(im)
    c = W / 2
    orange, dim = (255, 110, 30), (70, 70, 76)
    n = 120
    for i in range(n):
        a = 90 - i * 360 / n
        on = i < 0.87 * n
        d.line([polar(c, c, 228, a), polar(c, c, 196 if on else 206, a)], fill=orange if on else dim, width=4)
    d.text((c, c - 64), "CHARGING", font=font(LATO + "Lato-Bold.ttf", 24), fill=orange, anchor="mm")
    d.text((c, c + 10), "87", font=font(LATO + "Lato-Regular.ttf", 150), fill=(245, 245, 245), anchor="mm")
    d.text((c, c + 96), "full ~21:02 Est.", font=font(LATO + "Lato-Regular.ttf", 24), fill=(150, 150, 156), anchor="mm")
    for i, x in enumerate((-24, 0, 24)):
        d.ellipse((c + x - 6, c + 146 - 6, c + x + 6, c + 146 + 6), fill=(245, 245, 245) if i == 0 else dim)
    round_save(im, name)


def dots_text(d, text, x0, y0, pitch, r, fill, size=9):
    """Render text as a dot matrix (Nothing-style): rasterise small, draw each lit pixel as a dot."""
    f = font(DEJAVU + "DejaVuSansMono-Bold.ttf", size)
    w, h = int(size * 0.62 * len(text)) + 2, size + 3
    fb = Image.new("1", (w, h), 0)
    ImageDraw.Draw(fb).text((0, -1), text, font=f, fill=1)
    for yy in range(h):
        for xx in range(w):
            if fb.getpixel((xx, yy)):
                cx, cy = x0 + xx * pitch, y0 + yy * pitch
                d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=fill)
    return w * pitch


def nothing_dial(name="nothing_dial.png"):
    W, im = round_canvas((0, 0, 0))
    d = ImageDraw.Draw(im)
    c = W / 2
    white, grey, red = (240, 240, 240), (55, 55, 58), (215, 25, 32)
    n = 60
    for i in range(n):
        a = 90 - i * 360 / n
        x, y = polar(c, c, 212, a)
        on = i < 0.87 * n
        rr = 5 if on else 3
        d.ellipse((x - rr, y - rr, x + rr, y + rr), fill=white if on else grey)
    tw = dots_text(d, "87", 0, 0, 0, 0, white, size=16)  # measure only
    pitch = 11
    fw = int(16 * 0.62 * 2 + 2) * pitch
    dots_text(d, "87", c - fw / 2 + 4, c - 100, pitch, 4.2, white, size=16)
    d.ellipse((c + fw / 2 + 2, c - 92, c + fw / 2 + 16, c - 78), fill=red)
    sw = int(10 * 0.62 * 8 + 2) * 4.4
    dots_text(d, "CHARGING", c - sw / 2 + 4, c + 86, 4.4, 1.7, (170, 170, 175), size=10)
    round_save(im, name)


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    lock_screen("lock_portrait.png")
    lock_screen("lock_landscape.png", landscape=True)
    oled("oled_blue.png")
    oled("oled_white.png", (235, 240, 255))
    dial("dial.png")
    fuel()
    eye()
    watch()
    dial_min()
    nest()
    nothing_dial()
