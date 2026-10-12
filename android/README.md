# W6 for Android

The companion app for the W6 stand. It keeps the stand up to date with the phone and works as a remote for it:

- **Charge on the stand.** While the phone charges wirelessly, the stand shows its battery as a ring, a big percentage
  and the power going into the battery, measured by the phone. Under "When the phone charges on the stand" on Home,
  pick how it looks: the ring, Warp or Swarm (shown when the stand's firmware has them). Replaces the MacroDroid recipe in
  `firmware/w6/README.md`.
- **Now playing.** Song, artist and round album art, with the track's progress on the ring. It shows while music plays
  and goes away a few seconds after a pause.
- **Remote.**
  - A live picture of the stand's screen; pull down on Home to refresh it.
  - Clock faces: a row to swipe through on Home (tap one to use it), and "See all" for big pictures of every face the
    stand's firmware lists, still ones first, then the animated ones.
  - Focus timer, screen rotation and 12- or 24-hour time.
- **Time zone.** The stand's clock follows the phone's time zone: the app sends it whenever the stand's differs, e.g.
  after a trip or a daylight-saving change (while the app is open).
  - The start history (`GET /api/log`), for diagnosing restarts.
  - Firmware updates from a file on the phone, and restarts.

The app greys out anything the stand's firmware can't do (`features` in `GET /api/status`).

## Installing

Sideload the APK (no Play Store listing).

**"App blocked to protect your device" (some countries).** Google Play Protect's enhanced fraud
protection blocks apps that ask for notification access when they are installed from a browser, a messaging app or a
file manager, and offers no way past it on the phone. W6 needs notification access,
so it is caught. Two ways to install it anyway:
- **Over USB or wireless debugging with ADB** (`adb install w6-x.y.z.apk`), which isn't an internet sideloading source.
- **Turn off "Scan apps with Play Protect"** (Play Store → profile → Play Protect → ⚙), install, then turn it back on.

On first start:

1. **Pair.** The app lists stands it finds on the Wi-Fi (mDNS); tap one, or type its address. The stand shows a QR code
   with an 8-character code under it for two minutes. Tap **Scan the code** (Google's scanner from Play services, so
   W6 needs no camera permission) or type the code, and the stand hands the app its token. Scanning the QR with the
   phone's camera app opens W6 with it too; the app asks before using a link and accepts only home-network addresses.
   "Use a token instead" takes the token from the stand's setup page.
2. **Notification access.** Android keeps a notification listener running, which lets W6 update the stand with the
   app closed. It is also the only way an app may see what's playing. W6 ignores the notifications themselves: it
   doesn't read, keep or send them (`LinkService.kt` overrides nothing that receives them).
   - **"Restricted setting".** Android 13+ blocks this permission for some sideloaded apps. Open App info, tap ⋮,
     choose "Allow restricted settings", then turn the access on. The app links to both screens.
   - **Without the access,** W6 updates the stand only while the app is open, and now playing stays off.

Nothing leaves the home network. The app talks only to the stand, over plain HTTP: the stand has no certificate. The
token is stored encrypted with a key in the Android Keystore, and backups are off.

## How it works

| What | How |
|---|---|
| Charging | A receiver for battery changes. It sends `POST /api/phone` when the percentage or charging state changes, and every 20 s while charging (TTL 120 s), so the stand drops the screen if the phone leaves. "On the stand" means wireless charging (`BATTERY_PLUGGED_WIRELESS`); a switch lets any charger count |
| Watts | Current × voltage from `BatteryManager`. Phones disagree on the current's sign and some report mA, so the app learns the sign while the phone runs on battery (`Logic.chargeWatts`). It's net of what the phone is using, and is left out when the reading isn't believable |
| Now playing | `MediaSessionManager` sessions, through the notification-listener permission. `POST /api/media`; the stand replies `need` when it doesn't have the track's art yet, and the app then uploads it once (112 × 112 RGB565, `POST /api/media/art`). It resends every 60 s while playing |
| Network | Calls are pinned to the Wi-Fi network, so they don't leave over mobile data when the Wi-Fi has no internet. If the stand stops answering, the app looks for it by its mDNS name straight away, then every 30 s, doubling to 5 minutes while it stays missing (the router may have given it a new address); Settings → Stand address searches on demand. When the stand answers again after an outage, the app resends the charge and the music at once, since a restarted stand has forgotten them |
| Time zone | Each status poll compares the stand's `utc_offset_s` with the phone's current offset (`Logic.utcOffsetSeconds`) and sends the phone's when they differ |
| Screen picture | While the app is open: the status every 2.5 s. The small run-length-coded screenshot only while Home shows (it is the only screen with the picture), every 5 s and at once after a change, about 0.2 s of the stand's time each. A living face barely compresses (about a second to send, and the animation stalls meanwhile), so after a picture over 40 KB the next one waits 10 s. One request at a time |
| Text | The stand's fonts have ASCII plus ° · – …. Accents are folded (é → e) and other scripts dropped (`Logic.standText`) |

## Building

Toolchain: JDK 21, Android SDK with platform 37 and build-tools 36.0.0, Gradle 9.7.1 (wrapper), AGP 9.4.1, Kotlin
2.4.20, Compose BOM 2026.09.00 (Material 3). `minSdk` is 26. `targetSdk` is 36 on purpose: 37 brings local-network
permission rules that need testing on a phone first.

```sh
export ANDROID_HOME=/path/to/android/sdk      # or: echo "sdk.dir=..." > local.properties (git-ignored)
scripts/gradle.sh :app:testDebugUnitTest :app:lintDebug :app:assembleDebug   # or ./gradlew ... without systemd
scripts/screenshots.sh        # renders every screen to app/build/screenshots (opt-in, never in CI)
```

`scripts/gradle.sh` runs Gradle in a memory-capped systemd scope at low priority with no daemons left behind. On a
machine without systemd, use `./gradlew` directly.

- **Screenshots** use the firmware's desktop preview renders as the stand picture when they exist (build them with
  `pio run -e preview` in `firmware/w6` first).
- **Release builds** are signed when `~/.config/w6-android/signing.properties` (or the file named by `W6_SIGNING`)
  holds `storeFile`, `storePassword`, `keyAlias` and `keyPassword`. Keep the keystore out of the repo. An app signed
  with a different key can't update an installed one; the user has to uninstall first.
- **Debug builds** install alongside as `app.w6.companion.debug`.
- **Face pictures** (`res/drawable-nodpi/face_*.webp`) are the firmware preview's `30_face_*` renders, cropped round.
- **Charging screen pictures** (`res/drawable-nodpi/charging_*.webp`) are the preview's `35_charging_*_70` renders, cropped round.
  Redo them when a face changes; a face the app has no picture for shows its name.

## Source map (`app/src/main/java/app/w6/companion/`)

| File | Does |
|---|---|
| `Logic.kt` | Pure decisions and conversions, unit-tested (`LogicTest`): watts, the time zone offset, when to send, text folding, RGB565, screenshot decoding, address checks, pairing links and codes |
| `Model.kt` | The stand's JSON (`/api/status`, `/api/log`, settings replies) |
| `StandApi.kt` | One function per API call (OkHttp), cancelled with the coroutine; errors as `StandException` |
| `Link.kt` | The background engine: battery, media sessions, Wi-Fi, finding the stand again |
| `LinkService.kt` | The notification listener that keeps `Link` running with the app closed |
| `Discovery.kt` | mDNS discovery of `_http._tcp` services named `w6*` |
| `Prefs.kt` | Settings; token encryption |
| `AppViewModel.kt` | Screen state, polling while visible (status every 2.5 s, the picture while Home shows), actions that show their result at once |
| `MainActivity.kt` | Navigation: pairing → access → Home ↔ Clock faces, Home ↔ Settings → Log / Firmware |
| `HomeScreen.kt`, `FacesScreen.kt`, `SettingsScreens.kt`, `PairScreen.kt`, `Components.kt`, `Theme.kt` | Material 3 UI. Dynamic colour on Android 12+, otherwise a walnut and amber scheme matching the stand |

## Status

Used daily on a Pixel 9a: the charge on the stand, and now playing from Spotify with album art and the progress ring.
Unit-tested (`LogicTest`) and lint-clean. Not yet checked:

- other phones' battery-current sign (the watts);
- music apps other than Spotify;
- Android versions other than the one on that phone.
