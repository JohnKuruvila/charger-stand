# W6 firmware

Firmware for the W6 charger stand's round screen (ESP32-C3 SuperMini + 1.28" GC9A01 240 × 240 IPS display).

- **Clock** with the date. Nineteen faces, picked in the app or with `w6 face`:
  - **classic**: digital; the ring fills once a minute
  - **analog**: hands and hour ticks, with the date on the dial
  - **stack**: big hours over minutes, the day on the left and the month and AM/PM on the right; **stack_calm**: the same in cream and brown with nothing else, the seconds filling a ring of dots
  - **orbit**: 60 dots round the edge light up through the hour; **orbit_night**: the same in dim red, for a dark room
  - **segment**, **segment_teal**: seven-segment digits like an old bedside clock, the unlit segments faintly showing, and the seconds underneath
  - **words**: the time in words to the nearest five minutes ("nearly ten past ten"), with a dot on the edge where the hour hand would be
  - **walnut**: analog with numerals, a date window at 3 and a red seconds hand; **minimal**: two hands, four marks and an amber track for the minutes
  - **terminal**, **terminal_amber**: a shell prompt: the time in dot-matrix digits, then the last job sent to `POST /api/progress` and how long ago it finished (the uptime before there has been one)

  - Living faces, where every pixel is computed, like the original display demo:
    - **wormhole**: a tunnel flying toward you in amber, magenta, violet and cyan, the time in its dark middle (10 frames a second, about 55 % busy)
    - **aurora**: slow plasma in colours that follow the time of day (10 a second, about 45 %)
    - **galaxy**: a spiral galaxy of 1800 stars turning under the time (12 a second, about 50 %)
    - **silk**: folded bands of light drifting across (10 a second, about 35 %)
    - **sky**: the sky for the time of day, the sun or a crescent moon on its arc, stars at night, walnut-brown hills (redrawn once a minute, about 5 %)
    - **walnut_grain**: an analog dial in generated walnut wood with brass markers and shadowed hands (about 6 %: the wood is a picture in flash, made by `tools/make_wood.sh`, so the face needs no RAM tables)

    The C3 has no floating-point unit, so per-pixel work is whole numbers and tables; they take 19 KB (sky) to 36 KB (galaxy), only while that face is on. The busy figures are measured on the stand (0.6.4). Against 0.6.3 that is 14–23 points less, and each frame's longest loop is 13–24 ms shorter: the painting code moved to `src/living_fx.cpp`, built with `-O2`, with a loop for each face and no floating point in the per-frame setup.

  Only the chosen face exists in memory (classic and analog are always built), so more faces cost flash, not RAM.
- **Time zone** from the phone: the setup page takes it from the phone's browser, and the W6 app sends the phone's whenever it differs (after a trip, or a daylight-saving change). There is no weather or location: the stand only talks to the home network and a time server.
- **Timer**: a countdown, e.g. a 25-minute focus timer; the ring empties, then "Time's up" blinks for a minute.
- **Progress ring** for anything that can send an HTTP request: a `terraform apply`, a GitHub Actions run, a backup script. It is green when the job is done and red when it fails.
- **Notifications**: a short message for a few seconds.
- **Phone battery**: an automation app on the phone sends its battery level (and charging power, if it has it). While the phone charges, a charging screen replaces the clock: a green ring and a big percentage, the power in watts and a small clock; its light streak moves 15 times a second. Two animated styles, picked in the app or with `w6 charging`: **Warp** (flying through a star tunnel towards a portal in the charge colour, faster the more power the phone takes; at 100 % it flashes on arrival) and **Swarm** (the percentage is made of particles that buzz round their places and stream in from the rim, more with more power; at 100 % they burst and re-form). Like the living faces, they compute every pixel at about 10 frames a second and take about 22 KB only while they show. See "Phone battery" below.
- **Now playing**: round album art, the title and artist, and the track's position on the ring, while music plays on the phone. Sent by the W6 Android app.
- **A small daily surprise.** It's in `src/ui.cpp` if you want spoilers.
- **Pairing the app by QR code**: the app asks the stand to show a one-time code and scans it, so nobody types the token.
- **Start history**: the last 20 starts with the reason and how long each ran (`w6 log`), to tell a power cut from a crash.
- **Setup from a phone**: scan the QR code on the screen, pick the Wi-Fi network, type the password.
- **Updates over Wi-Fi.** The USB port is sealed inside the stand, so every update after the first one goes over the network.

The stand charges the phone (Qi) and drives its screen over Wi-Fi. The W6 Android app ([`../../android`](../../android/README.md)) is the easy way to feed it the phone's charge and music; anything that can send HTTP works too.

![Screens](docs/screens.png)

## Setting it up

1. Plug the stand in. The screen shows a QR code and the name and password of a setup network (`W6-setup-XXXX`).
2. Scan the code with the phone camera to join that network. The setup page opens by itself; if it doesn't, browse to `http://192.168.4.1`.
3. Pick the home Wi-Fi network (2.4 GHz only) and type its password. The clock takes the phone's time zone.
4. When it says *Connected*, press **Finish**.
5. The page shows the stand's address and the **API token**. Copy the token; scripts need it.

**To open setup again**, e.g. for a new router, or to see the token again: plug the stand in and unplug it within five seconds, do that once more, then plug it in to stay. (Three power-ups, each within a few seconds of the last.) If the stand can't find its Wi-Fi for 45 seconds after it starts, it opens setup by itself. It keeps trying the old network in the background and goes back to it as soon as it is there again. Setup opened on purpose closes after 10 minutes if no phone joins it, and the stand goes back to its saved network.

## The API

Plain HTTP on port 80, on the local network only. The stand is at `http://w6.local`, or at its IP address if `.local` names don't resolve on your machine. Every `/api/` request needs the token, and request bodies are JSON with `Content-Type: application/json` (without it, curl's `-d` sends a form and the stand says so):

```sh
export W6_HOST=w6.local W6_TOKEN=...   # or put both in ~/.config/w6/env as KEY=value lines

curl -s -H "Authorization: Bearer $W6_TOKEN" http://$W6_HOST/api/status
curl -s -H "Authorization: Bearer $W6_TOKEN" -H 'Content-Type: application/json' -d '{"title":"Nightly backup","percent":40,"detail":"4 / 10 databases"}' \
     http://$W6_HOST/api/progress
curl -s -H "Authorization: Bearer $W6_TOKEN" -H 'Content-Type: application/json' -d '{"text":"Deploy finished","level":"success"}' http://$W6_HOST/api/notify
```

| Request | Body | What it does |
|---|---|---|
| `GET /api/status` | | Version, uptime, why it last started (`reset_reason`: power on, restart, crash, watchdog, brownout), IP, Wi-Fi signal, free memory, screen rotation, face, `faces` (every face this firmware has, in order), the ESP32's chip temperature (`chip_temp_c`, the die, so 15-30 °C above the air in the base), `utc_offset_s`, the clock's time zone (absent until one is set), and `living_frames`, frames drawn by living faces since the start (two readings give their frame rate). Also `features` (what this firmware can do: `phone`, `timer`, `faces`, `rotation`, `log`, `media`, `pair`, `cpu`, `fun`, `clock_24h`, `screenshot_rle`, `http`, `utc_offset`), `timer_left_s` while a timer runs, `clock_24h`, and `media_last`: the last now-playing update as received (its age, `playing`, `position_ms` and `duration_ms`, -1 when the app didn't send one), to tell why the progress ring is empty. `lvgl_mem` is LVGL's own 56 KB pool for screens and objects (free, biggest free block, peak use; drawing fails if it fills), and `http` the count, average and longest time per route since the start, reply included (nothing else runs meanwhile, so that is how long the screen stood still). Then `min_free_heap` (lowest since the start), `largest_free_block`, and `cpu`: how busy the chip is (`busy_pct` for the last second, `busy_pct_10s`, `peak_pct_60s`), the 10 s split between the main loop and the system, the longest loop pass (`loop_max_ms_10s`: how long the screen and web server stalled), and per-second `history_pct`, `history_heap_kb` and `history_loop_max_ms` for the last minute. The CPU share is sampled ~730 times a second by a hardware timer, so a single second is good to a few points |
| `GET /api/log` | | The last 20 starts, newest first: when each began, why (`reason`: power on, restart, crash, watchdog, brownout), how long it ran (`ran_s`; `ran_exact: false` means the stand lost power, so the run lasted at least that long), whether it got online, Wi-Fi drops, hottest chip reading, lowest free memory, and `restarted_for` when the firmware restarted itself (`update`, `reboot`, `reset`, `set up`, `rollback`, `setup idle`, `setup fail`, `wifi back`). Also this start's Wi-Fi events, with failed attempts while joining marked `while_joining`. Kept in flash and across a reset; it holds no Wi-Fi names or tokens |
| `POST /api/pair` | none, **no token** | Shows a one-time pairing code for 2 minutes, as a QR code (`w6://pair?host=<ip>&code=<code>`) and as text. A new code can start only 30 s after the last; while one shows, this returns it again. Replies `{"expires_s": n}` |
| `POST /api/pair/finish` | `code`, **no token** | With the code from the screen (any case, spaces ignored), replies `{"token": "..."}` and shows "Paired". Five wrong codes end it. Only someone who can see the screen can pair; the token itself is never shown |
| `POST /api/media` | `title`, `artist`, `playing` (default `true`), `position_ms`, `duration_ms`, `art_id` (any short string naming the track's art), `ttl` (seconds, default 300) | Shows now playing while `playing` is true, and for 8 s after a pause. Replies `{"art": "need" \| "have" \| "none"}`: `need` means the stand doesn't have the art named `art_id` yet |
| `POST /api/media/art?id=<art_id>` | multipart form, one file of exactly 25 088 bytes: 112 × 112 pixels of RGB565, high byte first, row by row | The album art, drawn in a circle |
| `DELETE /api/media` | | Takes now playing off |
| `POST /api/progress` | `title`, `percent` (0–100, or `null` when the total isn't known: the ring spins), `state` (`running`, `success`, `failed`), `detail`, `icon` (`terminal`, `terraform`, `github`, `download`, `sync`, `info`, `warning`), `ttl` (seconds) | Shows the progress ring. It goes away when `ttl` passes without an update: 600 s while running, 60 s after `success` or `failed`. A new `title` restarts the elapsed time. |
| `DELETE /api/progress` | | Removes the ring now |
| `POST /api/phone` | `percent` (0–100, required), `charging` (default `true`), `watts` (optional), `ttl` (seconds, default 900). Numbers may be sent as text | Shows the charging screen while `charging` is true, until `ttl` passes without an update |
| `DELETE /api/phone` | | Takes it off now |
| `POST /api/timer` | `minutes` or `seconds` (up to 24 h), `label` (default "Timer") | Starts a countdown screen |
| `DELETE /api/timer` | | Cancels it |
| `POST /api/notify` | `text`, `level` (`info`, `success`, `warning`, `error`), `seconds` (default 10) | Shows a message over everything else |
| `POST /api/settings` | `{"face": "stack"}`: one of `faces` in `GET /api/status`. Or `{"charging_style": "warp"}`: one of `charging_styles` (`classic`, `warp`, `swarm`). Or `{"rotation": 0-3}`: quarter turns clockwise, applied at once and kept across a reset. Or `{"clock_24h": true}` for 21:30 instead of 9:30 PM (12-hour is the default). Or `{"utc_offset_s": 19800}`: the time zone, in seconds east of UTC (+05:30 here); the app sends the phone's | One setting per request; the reply echoes it |
| `GET /api/screenshot` | `?format=rle` for the small version | The screen as a 240 × 240 BMP (115 KB), or run-length coded (about 14 KB): `W6R1`, width and height as 16-bit little-endian, then packets: a byte `h`; if its top bit is set, one RGB565 pixel (little-endian) repeated `(h & 0x7F) + 1` times, otherwise `h + 1` pixels follow. The screen stands still while it is sent: about 0.2 s for the small one on the stand's own network, 1 s for the BMP, and about 1 s even for the small one on a living face, whose pixels hardly repeat. `503` when there isn't the memory to send it |
| `POST /api/ota` | multipart form, field `firmware` | Installs new firmware and restarts (see below) |
| `POST /api/reboot` | | Restarts |
| `POST /api/setup` | | Leaves the home network and opens setup |
| `POST /api/reset` | | Forgets Wi-Fi, time zone and token, then restarts into setup. The screen rotation is kept: it belongs to the hardware |

Errors come back as `{"error": "..."}` with a 4xx or 5xx status.

## Phone battery

The stand can't see the phone's battery, so the phone has to send it. The simplest way is the W6 Android app ([`android/`](../../android/README.md)): pair it once and it sends the charge and now playing by itself. Without the app, a free automation app such as **MacroDroid** (or Tasker) does it without any custom app. The phone must be on the same Wi-Fi as the stand.

1. **Find the stand's address.** Android doesn't always resolve `w6.local`, so use its IP address: the setup page shows it at the end. Reserve that address for the stand in the router's DHCP settings, so it never changes.
2. **Make three macros** in MacroDroid, each with one **HTTP Request** action:
   - URL `http://<stand IP>/api/phone`, method `POST`
   - header `Authorization: Bearer <token>`
   - content type `application/json`

   | Trigger | Body |
   |---|---|
   | Power connected | `{"percent": <battery level>, "charging": true}` |
   | Battery level change | `{"percent": <battery level>, "charging": true}`, with a **constraint** "power connected" |
   | Power disconnected | `{"percent": <battery level>, "charging": false, "ttl": 60}` |

   For `<battery level>`, use the app's magic-text / variable picker and choose the battery level. The exact name differs between app versions, so pick it from the list rather than typing it. Add a "Wi-Fi SSID is the home network" constraint so it doesn't try from elsewhere.
3. **A focus-timer button (optional).** Add a fourth macro with a **Shortcut / Widget** trigger: HTTP Request `POST http://<stand IP>/api/timer` with body `{"minutes": 25, "label": "Focus"}`. Put its widget on the home screen and one tap starts the timer on the stand.
4. **Charging power (optional).** If the phone and app can read the battery current or power, add `"watts": <value>`. Many phones don't expose this; the stand then shows the percentage only.

Screen order, from the top: notification, timer, progress ring, setup, charging screen, clock face.

Test from a computer first: `w6 phone 86 --watts 2.8` should show the charging screen with "86%" and "2.8 W"; `w6 phone --clear 0` takes it off. A phone that reports `"charging": false` doesn't take over the screen. On iPhone, the Shortcuts app can send the battery level the same way (a personal automation on "charger connected", with a "Get contents of URL" action), but not the charging power.

## The command line tool

[`tools/w6.py`](tools/w6.py) wraps the API. It needs Python 3.8+ and nothing else.

```sh
alias w6='python3 path/to/tools/w6.py'
w6 status
w6 log                           # recent starts: why, how long each ran; Wi-Fi drops
w6 notify "Coffee's ready" --level info
w6 progress "Nightly backup" 40 --detail "4 / 10 databases"
w6 tf apply -auto-approve        # runs terraform apply -json and fills the ring as resources finish
w6 gh https://github.com/OWNER/REPO/actions/runs/123456789
w6 gh --repo OWNER/REPO --branch main     # the newest run on main
w6 screenshot screen.bmp       # what LVGL drew; it does not show the panel's rotation
w6 rotate 1                      # 0-3 quarter turns clockwise
w6 phone 86 --watts 2.8          # what a phone automation sends; --not-charging, --clear
w6 face words                    # any of "faces" in w6 status: classic, analog, stack, orbit, words, ...
w6 charging warp                 # the charging screen: classic (the ring), warp or swarm
w6 timezone                      # this computer's time zone; or w6 timezone +05:30
w6 timer 25 --label Focus        # a countdown; w6 timer --cancel
w6 --dry-run tf plan             # prints what it would send, sends nothing
```

- **`w6 tf`** adds `-json` to the Terraform command, prints Terraform's messages as normal text, and works out the percentage from the plan: each create, update or delete is one step, a replace is two. It returns Terraform's exit code, so it can sit in a script or a Makefile. `apply -json` needs `-auto-approve` or a saved plan file.
- **`w6 gh`** polls the GitHub API every 10 seconds and shows finished steps out of all steps in the run. It reads a token from `GH_TOKEN` or `GITHUB_TOKEN`, which private repos need (`GH_TOKEN=$(gh auth token) w6 gh ...`). Without a token GitHub allows 60 requests an hour, which is about 3 minutes of watching at 2 requests per check.
- If the stand is off or unreachable, `tf` and `gh` warn once and carry on, so a job never fails because of the stand.

## Building and flashing

Install [PlatformIO](https://platformio.org) (`pipx install platformio`). Then:

```sh
pio run -e c3 -t upload          # first time, over USB: also writes the partition table for over-the-air updates
pio run -e ota -t upload         # afterwards, over Wi-Fi (reads W6_HOST and W6_TOKEN)
pio device monitor               # USB serial log; type token, status, setup or reset
```

Wiring (display pad → ESP32-C3 SuperMini pin): SCL → 4, SDA → 6, CS → 7, DC → 5, RST → 10, VCC → 3V3, GND → G. The display has no backlight pin, so brightness can't be changed.

**Desktop preview.** `pio run -e preview && .pio/build/preview/program` renders every screen into `preview/*.bmp` on your computer, using the same `ui.cpp` as the stand. Design changes don't need the hardware. The real panel is smaller, brighter and less saturated than the image.

## Living with a sealed case

Inside the finished stand the ESP32's USB-C port is unreachable, and **its USB port must never be used while the stand's own supply is connected** (the buck converter feeds the 5V pin; two supplies on one rail can damage either). Everything below works without opening it:

| Need | How |
|---|---|
| Restart | Unplug and plug in again, or `POST /api/reboot` |
| Find out why it restarted or looped | `w6 log`. A run of short `power on` starts, each "<10s" or another range rather than an exact time, means the supply is cutting out (cable, plug, adapter or a joint inside); `crash` or `watchdog` means firmware (a main loop stuck for 20 s restarts as `watchdog` rather than freezing the screen); `brownout` means the supply sagged without cutting out |
| New Wi-Fi (new router, new home) | Nothing to do: if the saved network isn't found for 45 s after power-up, setup opens by itself. Or power-cycle three times quickly (see "Setting it up") |
| See the API token again | Open setup (power-cycle three times); the setup page shows it |
| Wipe it for a new owner | `POST /api/reset`, or open setup and set it up again |
| Picture the wrong way round | `w6 rotate 0-3` |
| New firmware | `pio run -e ota -t upload` |

**If an update breaks it.** Updates go into the second app slot, and the old firmware stays in the first one. The stand goes back to the old one by itself if the new firmware:
- crashes three times in a row before it has run for a minute, or
- doesn't get back onto the home network within five minutes.

If the router happens to be down right after an update, that costs the update, not the stand. Anything worse, e.g. firmware that joins Wi-Fi but never answers on the network, means taking the bottom plate off (4 × M2 screws) and flashing over USB, with the stand's own USB supply **unplugged**.

**Setup network and the ESP32-C3 SuperMini.** This board's antenna is poorly matched. At full transmit power its setup network appears in a phone's list, but neither a phone nor a laptop can join it. So setup runs at 8.5 dBm, checks that the network really started, and shows "Setup network failed" and restarts instead of a QR code nobody can use. **Before a reset, check that setup works**: a reset with a broken setup network leaves a sealed stand with no way onto any Wi-Fi. So before an OTA update: keep changes small, build and run the desktop preview, never touch `net.cpp`, `web.cpp` or the OTA code casually, and test the result over the network straight after flashing while the old version is still in the other slot.

**Screen rotation.** The panel's orientation depends on how the display is mounted. In the W6 base its header tab points to the right (seen from the front), which needs one quarter turn clockwise; that is the default. It is stored apart from the other settings (`w6hw` namespace) so `reset` doesn't undo it.

## Code map

| File | What it does |
|---|---|
| `src/main.cpp` | Startup, the main loop, the clock, USB serial commands, rollback after a bad update |
| `src/ui.cpp` | All the screens (LVGL 8.3). No hardware in here, so it also builds for the desktop preview |
| `src/living_fx.cpp`, `src/charging_fx.cpp` | The living faces and the Warp and Swarm charging screens, painted pixel by pixel. Both are built with `-O2` (`tools/fast_paint.py`); everything else with the framework's `-Os` |
| `src/wood.cpp` | walnut_grain's wood, 91 KB, generated by `tools/make_wood.sh` from `tools/wood.cpp`; don't edit it |
| `src/display.cpp`, `src/panel.h` | The GC9A01 over SPI with DMA: LVGL draws one 20-row band while the previous one is being sent |
| `src/net.cpp` | Wi-Fi, the setup network and its captive portal, mDNS (`w6.local`), network time |
| `src/web.cpp` | The setup page and the API |
| `src/settings.cpp` | What is kept in flash (NVS) |
| `src/cpuload.cpp` | The CPU figures in `GET /api/status`: a hardware timer interrupt every 1373 µs notes which task it stopped (idle, main loop, other) and closes a bucket each second; 60 seconds are kept. FreeRTOS run-time stats aren't compiled into this Arduino core |
| `src/bootlog.cpp` | The start history behind `GET /api/log`: one 640-byte record set in its own NVS namespace (`w6log`), written at each start, at 10 s and 60 s, on getting online or network time, every 10 minutes and before a deliberate restart. A RAM copy that survives resets (not power cuts) gives the exact length of a run that ended in a crash or restart |
| `src/fonts/` | Montserrat and Font Awesome glyphs, made by `tools/make_fonts.sh` |

The time comes from a network time server; the time zone is a fixed offset from UTC, set from the setup page or the app. Until both are there, the clock shows `--:--` rather than a time that might be wrong.

## Security

- **The API is plain HTTP with a bearer token, so keep it on a home network.** Anyone who can watch traffic on that network can read the token, and with it they can change the screen or install firmware. Don't forward port 80 to the internet.
- **Firmware updates aren't signed.** Whoever has the token can install any firmware.
- **The Wi-Fi password and the token are stored unencrypted** in the ESP32's flash. Anyone with the stand open and a USB cable can read them.
- **The setup network** is WPA2 with a random password that only appears on the screen. It exists only during setup, and the setup page only answers on it, never on the home network.
- **Outgoing connections:** only network time (NTP). The stand fetches nothing else from the internet.
- **Work laptops:** posting job names to the stand is posting data off a work machine. Check that your employer is fine with it.

## Ideas for later

- **Reach the stand from outside the home network.** The work laptop can only call `w6.local` from the same Wi-Fi. Have the stand subscribe to a topic on [ntfy](https://ntfy.sh) or an MQTT broker, and post progress there from anywhere, GitHub Actions runners included. The stand only makes outgoing connections, so no ports are opened. Use a long random topic name or a self-hosted server, because anyone who knows the topic can post to it.
- **A step in a GitHub workflow** that posts its own progress to that relay.
- **Charging indicator**: wire the Qi board's charge LED to a free GPIO through a resistor. Check the voltage across that LED first.
