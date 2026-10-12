"""Build every W6 part, report size/volume, and check that assembled parts do not collide, with each other or
with the bought parts (display and its header, Qi board, coil, ESP32-C3 with headers, buck)."""
import itertools, sys, time
import w6
import bought

parts = {}
for name, fn in w6.PARTS.items():
    t = time.time()
    p = fn()
    bb = p.bounding_box()
    parts[name] = p
    print(f"{name:11s} valid={p.is_valid} vol={p.volume / 1000:7.1f} cm3  "
          f"size {bb.size.X:6.1f} x {bb.size.Y:6.1f} x {bb.size.Z:6.1f}  ({time.time() - t:.1f}s)")

bad = False
for a, b in itertools.combinations(parts, 2):
    v = (parts[a] & parts[b]).volume
    if v > 0.5:
        bad = True
    print(f"  overlap {a:10s} x {b:10s}: {v:8.2f} mm3")

print("bought parts vs printed parts (only overlaps > 0.05 mm3 are listed):")
extra = bought.all_parts()
extra.pop("phone")                     # rests on the lip and backrest by design
for a, pa in extra.items():
    for b, pb in parts.items():
        v = (pa & pb).volume
        if v > 0.05:
            bad = bad or v > 0.5
            print(f"  overlap {a:10s} x {b:10s}: {v:8.2f} mm3")
sys.exit(1 if bad else 0)
