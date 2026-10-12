"""Slice print files with OrcaSlicer's CLI (Bambu Lab A1, 0.4 nozzle, 0.20 mm Standard) and report what the
slicer says: print time, filament, layers, and any warnings. Nothing is sent to the printer.

    python3 slice.py out/print/base.3mf[:filament] ...      filament: pla (default) | tpu
    python3 slice.py --all                                   every part and coupon with its material

Output G-code and logs go to out/sliced/. The system presets inherit from each other; OrcaSlicer's CLI wants
complete files, so they are flattened first."""
import glob, json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ORCA_DIR = os.path.expanduser("~/Applications/orcaslicer")
ORCA = os.path.join(ORCA_DIR, "squashfs-root", "AppRun")
PROFILES = os.path.join(ORCA_DIR, "squashfs-root", "resources", "profiles", "BBL")
OUT = os.path.join(HERE, "out", "sliced")
MACHINE = "Bambu Lab A1 0.4 nozzle"
PROCESS = "0.20mm Standard @BBL A1"
BED = "Textured PEI Plate"     # the CLI otherwise slices for the Cool Plate (bed at 35 C, and parts come loose)
FILAMENTS = {"pla": "Bambu PLA Basic @BBL A1", "tpu": "Bambu TPU 95A @BBL A1"}
# per-part material and process overrides (same as BUILD.md): all plain PLA except the optional TPU feet
PARTS = {
    "base": ("pla", {"wall_loops": "3"}), "cap": ("pla", {"sparse_infill_density": "100%"}), "backrest": ("pla", {"wall_loops": "3"}),
    "coil_cover": ("pla", {}), "lip": ("pla", {}), "feet": ("tpu", {"sparse_infill_density": "100%"}),
    "plate": ("pla", {"wall_loops": "4"}), "retainer": ("pla", {}),
    "coupon_screws": ("pla", {}), "coupon_glass": ("pla", {}), "coupon_display_face": ("pla", {}), "coupon_socket": ("pla", {}),
    "coupon_tenon": ("pla", {}), "coupon_coil_pocket": ("pla", {}),
}


def flatten(kind, name):
    """Resolve the `inherits` chain of a system preset into one dict."""
    path = os.path.join(PROFILES, kind, name + ".json")
    if not os.path.exists(path):
        hits = glob.glob(os.path.join(PROFILES, kind, "**", name + ".json"), recursive=True)
        path = hits[0]
    d = json.load(open(path))
    parent = d.pop("inherits", "")
    if parent:
        base = flatten(kind, parent)
        base.update(d)
        d = base
    return d


def preset(kind, name, overrides=None):
    d = flatten(kind, name)
    d.update(overrides or {})
    if kind == "process":
        d["curr_bed_type"] = BED
    d["from"] = "system"
    d["name"] = name
    os.makedirs(OUT, exist_ok=True)
    p = os.path.join(OUT, "_%s_%s.json" % (kind, re.sub(r"\W+", "_", name)))
    json.dump(d, open(p, "w"), indent=1)
    return p


def slice_one(path, fil=None):
    name = os.path.splitext(os.path.basename(path))[0]
    mat, over = PARTS.get(name, ("pla", {}))
    mat = fil or mat
    d = os.path.join(OUT, name)
    os.makedirs(d, exist_ok=True)
    for old in glob.glob(os.path.join(d, "*")):
        os.remove(old)
    machine, process = preset("machine", MACHINE), preset("process", PROCESS, over)
    filament = preset("filament", FILAMENTS[mat])
    cmd = [ORCA, "--load-settings", machine + ";" + process, "--load-filaments", filament, "--arrange", "1",
           "--slice", "0", "--outputdir", d, "--debug", "2", os.path.abspath(path)]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=1800)
    log = r.stdout + r.stderr
    open(os.path.join(d, "slice.log"), "w").write(log)
    res = {"part": name, "material": mat, "ok": r.returncode == 0, "rc": r.returncode}
    gcodes = glob.glob(os.path.join(d, "*.gcode"))
    if gcodes:
        text = open(gcodes[0], errors="ignore").read()
        text = text[:20000] + text[-60000:]          # header and footer comments
        for key, pat in (("time", r"total estimated time: ([^\n;]+)"), ("layers", r"total layer number: (\d+)"),
                         ("filament_g", r"filament used \[g\] = ([\d.]+)"), ("filament_m", r"filament used \[mm\] = ([\d.]+)")):
            m = re.search(pat, text)
            if m:
                res[key] = m.group(1).strip()
        if "filament_m" in res:
            res["filament_m"] = round(float(res["filament_m"]) / 1000, 2)
    rj = os.path.join(d, "result.json")
    if os.path.exists(rj):
        j = json.load(open(rj))
        res["error"] = j.get("error_string", "")
        res["warnings"] = [p.get("warning_message") for p in j.get("sliced_plates", []) if p.get("warning_message")]
    warn = sorted(set(re.findall(r"\[(?:warning|error)\][^\n]*", log)))
    res["log_warnings"] = warn[:8]
    return res


if __name__ == "__main__":
    args = sys.argv[1:]
    if args == ["--all"]:
        args = sorted(glob.glob(os.path.join(HERE, "out", "print", "*.3mf")) + glob.glob(os.path.join(HERE, "out", "coupons", "*.3mf")))
    for a in args:
        path, _, fil = a.partition(":")
        print(json.dumps(slice_one(path, fil or None)))
