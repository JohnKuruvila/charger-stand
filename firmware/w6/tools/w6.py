#!/usr/bin/env python3
"""Command line for the W6 stand's HTTP API. Python 3.8+, standard library only.

The stand's address and token come from the environment, or from ~/.config/w6/env (KEY=value lines):
    W6_HOST   w6.local, or the stand's IP address
    W6_TOKEN  the API token from setup

    w6.py status
    w6.py log                               recent starts (why, how long each ran) and Wi-Fi drops
    w6.py notify "Deploy finished" --level success
    w6.py progress "Nightly backup" 40 --detail "4 / 10 databases"
    w6.py progress "Nightly backup" 100 --state success
    w6.py clear
    w6.py tf apply -auto-approve            terraform apply with a progress ring (adds -json itself)
    w6.py gh https://github.com/OWNER/REPO/actions/runs/123456789
    w6.py gh --repo OWNER/REPO [--branch main]   watches the newest run
    w6.py screenshot screen.bmp
    w6.py timezone                          the clock takes this computer's time zone (or e.g. +05:30)
    w6.py ota .pio/build/c3/firmware.bin
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request
import uuid


def load_config():
    path = os.path.expanduser("~/.config/w6/env")
    if os.path.exists(path):
        with open(path) as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith("#") and "=" in line:
                    k, v = line.split("=", 1)
                    os.environ.setdefault(k.strip(), v.strip().strip("'\""))
    host = os.environ.get("W6_HOST", "w6.local")
    token = os.environ.get("W6_TOKEN", "")
    return host, token


class Stand:
    def __init__(self, dry_run=False):
        self.host, self.token = load_config()
        self.dry_run = dry_run
        self.warned = False

    def call(self, method, path, body=None, data=None, headers=None, timeout=10):
        if self.dry_run:
            print(f"[dry run] {method} {path} {json.dumps(body) if body is not None else ''}", file=sys.stderr)
            return {}
        if not self.token:
            sys.exit("W6_TOKEN is not set (see the top of this file)")
        h = {"Authorization": f"Bearer {self.token}"}
        if body is not None:
            data = json.dumps(body).encode()
            h["Content-Type"] = "application/json"
        h.update(headers or {})
        req = urllib.request.Request(f"http://{self.host}{path}", data=data, method=method, headers=h)
        # The stand is on the local network: skip any HTTP proxy set in the environment
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        try:
            with opener.open(req, timeout=timeout) as r:
                raw = r.read()
                return raw if r.headers.get("Content-Type", "").startswith("image/") else json.loads(raw or b"{}")
        except urllib.error.HTTPError as e:
            msg = e.read().decode(errors="replace")
            raise SystemExit(f"W6 said {e.code}: {msg}")

    def quiet(self, method, path, body=None):
        """For the watchers: a stand that is off or unreachable must not stop the job being watched."""
        try:
            return self.call(method, path, body, timeout=3)
        except (OSError, SystemExit) as e:
            if not self.warned:
                print(f"w6: could not reach the stand ({e}); carrying on without it", file=sys.stderr)
                self.warned = True


class Throttle:
    """Sends progress at most once a second, but always sends a change of state."""

    def __init__(self, stand, title, icon):
        self.stand, self.title, self.icon = stand, title, icon
        self.last, self.sent = 0.0, None

    def send(self, percent, detail="", state="running", force=False):
        msg = {"title": self.title, "percent": percent, "detail": detail, "state": state, "icon": self.icon}
        now = time.monotonic()
        if msg == self.sent or (not force and state == "running" and now - self.last < 1.0):
            return
        self.last, self.sent = now, msg
        self.stand.quiet("POST", "/api/progress", msg)


# ---- terraform
COUNTED = {"create": 1, "update": 1, "delete": 1, "replace": 2}  # a replace is a delete and a create


def run_terraform(stand, args, title):
    """Runs terraform with -json, prints its messages as text, and drives the ring from the JSON stream."""
    if not args:
        sys.exit("usage: w6.py tf <terraform subcommand> [args], e.g. w6.py tf apply -auto-approve")
    cmd = ["terraform", args[0], "-json", *args[1:]]
    title = title or f"tf {args[0]} · {os.path.basename(os.getcwd())}"
    ring = Throttle(stand, title, "terraform")
    ring.send(None, "planning", force=True)
    planned = total = None
    done = errors = 0
    summary = ""
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, text=True, bufsize=1)
    for line in proc.stdout:
        try:
            m = json.loads(line)
        except ValueError:
            print(line, end="")
            continue
        print(m.get("@message", ""))
        t = m.get("type")
        if t == "planned_change":
            planned = (planned or 0) + COUNTED.get(m["change"].get("action"), 0)
        elif t == "change_summary":
            c = m["changes"]
            if c.get("operation") == "plan":
                total = c.get("add", 0) + c.get("change", 0) + c.get("remove", 0)
            else:
                summary = f"+{c.get('add', 0)}  ~{c.get('change', 0)}  -{c.get('remove', 0)}"
        elif t in ("apply_complete", "apply_errored") and m["hook"].get("action") != "read":
            done += 1
            errors += t == "apply_errored"
        if t in ("planned_change", "change_summary", "apply_start", "apply_complete", "apply_errored"):
            n = total if total is not None else planned
            if n:
                ring.send(min(99, done * 100 // n), f"{done} / {n} changes")
            elif done:
                ring.send(None, f"{done} changes")
    rc = proc.wait()
    ok = rc == 0 and not errors
    n = total if total is not None else planned
    ring.send(100 if ok else (min(99, done * 100 // n) if n else None),
              summary or ("no changes" if ok and not n else f"exit code {rc}"), "success" if ok else "failed", force=True)
    return rc


# ---- GitHub Actions
def gh_get(path):
    h = {"Accept": "application/vnd.github+json", "X-GitHub-Api-Version": "2022-11-28", "User-Agent": "w6-stand"}
    tok = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if tok:
        h["Authorization"] = f"Bearer {tok}"
    with urllib.request.urlopen(urllib.request.Request(f"https://api.github.com{path}", headers=h), timeout=15) as r:
        return json.loads(r.read())


def watch_github(stand, a):
    m = re.search(r"github\.com/([^/]+/[^/]+)/actions/runs/(\d+)", a.run or "")
    repo, run_id = (m.group(1), m.group(2)) if m else (a.repo, a.run)
    if not repo:
        sys.exit("give a run URL, or --repo OWNER/REPO (and optionally a run id)")
    if not run_id or not run_id.isdigit():
        q = f"?per_page=1&branch={a.branch}" if a.branch else "?per_page=1"
        runs = gh_get(f"/repos/{repo}/actions/runs{q}")["workflow_runs"]
        if not runs:
            sys.exit(f"no runs in {repo}")
        run_id = str(runs[0]["id"])
    ring = None
    while True:
        run = gh_get(f"/repos/{repo}/actions/runs/{run_id}")
        jobs, page = [], 1
        while True:  # big matrix workflows have more than one page of jobs
            r = gh_get(f"/repos/{repo}/actions/runs/{run_id}/jobs?per_page=100&page={page}")
            jobs += r["jobs"]
            if len(jobs) >= r["total_count"] or not r["jobs"] or page >= 10:
                break
            page += 1
        if ring is None:
            ring = Throttle(stand, a.title or f"{run['name']} · {run['head_branch']}", "github")
            print(f"watching {run['html_url']}")
        # A queued job lists no steps yet; count it as one step still to do
        steps = [s for j in jobs for s in (j.get("steps") or [{"status": j["status"]}])]
        finished = sum(s["status"] == "completed" for s in steps)
        current = next((f"{j['name']} / {s['name']}" for j in jobs for s in j.get("steps") or []
                        if s["status"] == "in_progress"), "")
        if run["status"] == "completed":
            ok = run["conclusion"] in ("success", "skipped", "neutral")
            failed = next((j["name"] for j in jobs if j["conclusion"] == "failure"), "")
            ring.send(100 if ok else (finished * 100 // len(steps) if steps else None),
                      run["conclusion"] if ok else (failed or run["conclusion"]), "success" if ok else "failed",
                      force=True)
            print(f"{run['conclusion']}")
            return 0 if ok else 1
        pct = min(99, finished * 100 // len(steps)) if steps else None
        ring.send(pct, current or run["status"], force=True)
        print(f"{finished}/{len(steps)} steps  {current}")
        time.sleep(a.every)


# ---- firmware upload
def upload_firmware(stand, path):
    with open(path, "rb") as f:
        fw = f.read()
    b = uuid.uuid4().hex
    body = (f"--{b}\r\nContent-Disposition: form-data; name=\"firmware\"; filename=\"{os.path.basename(path)}\"\r\n"
            f"Content-Type: application/octet-stream\r\n\r\n").encode() + fw + f"\r\n--{b}--\r\n".encode()
    print(f"sending {len(fw)} bytes to {stand.host} ...")
    stand.call("POST", "/api/ota", data=body, headers={"Content-Type": f"multipart/form-data; boundary={b}"},
               timeout=120)
    print("done; the stand restarts into the new firmware")


def lost_power_ran(s):
    """A run cut off by a power loss: the stand saves its uptime at 10 s, 60 s and then every 10 minutes."""
    if s < 10:
        return "<10s"
    if s < 60:
        return "10-60s"
    if s < 600:
        return "1-10m"
    return f"{s // 3600}h{s % 3600 // 60:02d}m+" if s >= 3600 else f"{s // 60}m+"


def print_log(d):
    def ago(s):
        return f"{s // 3600}h{s % 3600 // 60:02d}m" if s >= 3600 else f"{s // 60}m{s % 60:02d}s" if s >= 60 else f"{s}s"

    print("start  began            reason     ran        online  drops  chip  restarted for")
    for b in d["boots"]:
        began = time.strftime("%m-%d %H:%M:%S", time.localtime(b["started"])) if b["started"] else "no time yet"
        ran = ago(b["ran_s"]) if b["ran_exact"] else lost_power_ran(b["ran_s"])
        why = b.get("restarted_for", "") + (" (setup by plugging in)" if b["setup_by_plugging"] else "")
        if b["running"]:
            why = "running now"
        chip = f'{b["max_chip_c"]}C' if "max_chip_c" in b else "--"
        print(f'{b["n"]:>5}  {began:<15}  {b["reason"]:<9}  {ran:<9}  {"yes" if b["got_online"] else "no":<6}  '
              f'{b["wifi_drops"]:>5}  {chip:>4}  {why}')
    print("(<10s, 10-60s, 1-10m or +: the stand lost power, so the end of that run was not seen exactly)")
    if d["wifi_drops"]:
        print(f'\nWi-Fi events since this start ({d["wifi_drops_total"]} in all):')
        for e in d["wifi_drops"]:
            kind = "failed attempt while joining" if e.get("while_joining") else "dropped"
            print(f'  at {ago(e["uptime_s"])} up, {kind}: {e["reason"]} (code {e["code"]})')


def main():
    p = argparse.ArgumentParser(description="Talk to the W6 stand.")
    p.add_argument("--dry-run", action="store_true", help="print what would be sent instead of sending it")
    sub = p.add_subparsers(dest="cmd", required=True)
    sub.add_parser("status")
    lg = sub.add_parser("log", help="recent starts and Wi-Fi drops, to tell a power cut from a crash")
    lg.add_argument("--json", action="store_true", help="print the raw JSON")
    n = sub.add_parser("notify")
    n.add_argument("text")
    n.add_argument("--level", choices=["info", "success", "warning", "error"], default="info")
    n.add_argument("--seconds", type=int, default=10)
    g = sub.add_parser("progress")
    g.add_argument("title")
    g.add_argument("percent", help="0-100, or - when the total is not known")
    g.add_argument("--detail", default="")
    g.add_argument("--state", choices=["running", "success", "failed"], default="running")
    g.add_argument("--icon", choices=["terminal", "terraform", "github", "download", "sync", "info", "warning"],
                   default="terminal")
    g.add_argument("--ttl", type=int, help="seconds the ring stays up without an update")
    sub.add_parser("clear")
    t = sub.add_parser("tf", help="run terraform with a progress ring")
    t.add_argument("--title")
    t.add_argument("args", nargs=argparse.REMAINDER)
    w = sub.add_parser("gh", help="watch a GitHub Actions run")
    w.add_argument("run", nargs="?", help="run URL or id")
    w.add_argument("--repo")
    w.add_argument("--branch")
    w.add_argument("--title")
    w.add_argument("--every", type=int, default=10, help="seconds between checks (60 an hour without a token)")
    s = sub.add_parser("screenshot")
    s.add_argument("file")
    z = sub.add_parser("timezone", help="set the clock's time zone: this computer's, or an offset like +05:30")
    z.add_argument("offset", nargs="?", help="+HH:MM or -HH:MM from UTC")
    ph = sub.add_parser("phone", help="show a phone's battery on the clock (normally sent by an app on the phone)")
    ph.add_argument("percent", type=float)
    ph.add_argument("--not-charging", action="store_true")
    ph.add_argument("--watts", type=float)
    ph.add_argument("--ttl", type=int, default=900, help="seconds until it disappears without an update")
    ph.add_argument("--clear", action="store_true", help="take it off the screen")
    fc = sub.add_parser("face", help="clock face, e.g. classic, analog, stack, orbit, segment, words, walnut, minimal "
                        "or terminal (GET /api/status lists them all)")
    fc.add_argument("name")
    cs = sub.add_parser("charging", help="how the charging screen looks: classic (the ring), warp or swarm")
    cs.add_argument("style", choices=["classic", "warp", "swarm"])
    tm = sub.add_parser("timer", help="countdown on the screen, e.g. w6 timer 25 --label Focus")
    tm.add_argument("minutes", type=float, nargs="?", default=25)
    tm.add_argument("--label", default="Focus")
    tm.add_argument("--cancel", action="store_true")
    r = sub.add_parser("rotate", help="turn the picture: 0-3 quarter turns clockwise (kept across a reset)")
    r.add_argument("quarter_turns", type=int, choices=range(4))
    o = sub.add_parser("ota")
    o.add_argument("firmware")
    a = p.parse_args()

    stand = Stand(a.dry_run)
    if a.cmd == "status":
        print(json.dumps(stand.call("GET", "/api/status"), indent=2))
    elif a.cmd == "log":
        d = stand.call("GET", "/api/log")
        print(json.dumps(d, indent=2)) if a.json else print_log(d)
    elif a.cmd == "notify":
        stand.call("POST", "/api/notify", {"text": a.text, "level": a.level, "seconds": a.seconds})
    elif a.cmd == "progress":
        body = {"title": a.title, "percent": None if a.percent == "-" else float(a.percent), "detail": a.detail,
                "state": a.state, "icon": a.icon}
        if a.ttl:
            body["ttl"] = a.ttl
        stand.call("POST", "/api/progress", body)
    elif a.cmd == "clear":
        stand.call("DELETE", "/api/progress")
    elif a.cmd == "tf":
        sys.exit(run_terraform(stand, [x for x in a.args if x != "--"], a.title))
    elif a.cmd == "gh":
        sys.exit(watch_github(stand, a))
    elif a.cmd == "screenshot":
        with open(a.file, "wb") as f:
            f.write(stand.call("GET", "/api/screenshot"))
        print(f"saved {a.file}")
    elif a.cmd == "timezone":
        if a.offset:
            m = re.fullmatch(r"([+-])(\d{1,2}):?(\d{2})", a.offset)
            if not m:
                sys.exit("offset is +HH:MM or -HH:MM, e.g. +05:30")
            secs = (int(m[2]) * 3600 + int(m[3]) * 60) * (-1 if m[1] == "-" else 1)
        else:
            secs = time.localtime().tm_gmtoff
        print(json.dumps(stand.call("POST", "/api/settings", {"utc_offset_s": secs})))
    elif a.cmd == "phone":
        if a.clear:
            stand.call("DELETE", "/api/phone")
        else:
            body = {"percent": a.percent, "charging": not a.not_charging, "ttl": a.ttl}
            if a.watts is not None:
                body["watts"] = a.watts
            stand.call("POST", "/api/phone", body)
    elif a.cmd == "face":
        print(json.dumps(stand.call("POST", "/api/settings", {"face": a.name})))
    elif a.cmd == "charging":
        print(json.dumps(stand.call("POST", "/api/settings", {"charging_style": a.style})))
    elif a.cmd == "timer":
        if a.cancel:
            stand.call("DELETE", "/api/timer")
        else:
            stand.call("POST", "/api/timer", {"minutes": a.minutes, "label": a.label})
    elif a.cmd == "rotate":
        print(json.dumps(stand.call("POST", "/api/settings", {"rotation": a.quarter_turns})))
    elif a.cmd == "ota":
        upload_firmware(stand, a.firmware)


if __name__ == "__main__":
    main()
