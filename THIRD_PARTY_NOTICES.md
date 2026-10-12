# Third-party notices

Everything in this repository is under the MIT License (`LICENSE`), except the parts below, which keep their own
licences.

## Included in this repository

| What | Where | Licence |
|---|---|---|
| [Montserrat](https://github.com/JulietaUla/Montserrat) glyphs, Copyright 2011 The Montserrat Project Authors | `firmware/w6/src/fonts/w6_clock_68.c`, `w6_num_50.c`, `w6_text_*.c`, `w6_words_*.c` | [SIL Open Font License 1.1](https://openfontlicense.org) |
| [Font Awesome Free 5](https://fontawesome.com) icons, by Fonticons, Inc. | `firmware/w6/src/fonts/w6_icons_*.c` | Icons [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/), font [SIL OFL 1.1](https://openfontlicense.org) ([details](https://fontawesome.com/license/free)) |
| GC9A01 start-up sequence from [Arduino_GFX](https://github.com/moononournation/Arduino_GFX) | `firmware/w6/src/panel.h` | BSD, below |
| [Gradle](https://gradle.org) wrapper | `android/gradlew`, `android/gradlew.bat`, `android/gradle/wrapper/` | [Apache License 2.0](https://www.apache.org/licenses/LICENSE-2.0) |

The fonts were converted to LVGL C arrays with [lv_font_conv](https://github.com/lvgl/lv_font_conv)
(`firmware/w6/tools/make_fonts.sh`); only the characters the stand uses are included.

### Arduino_GFX

```
Software License Agreement (BSD License)

Copyright (c) 2012 Adafruit Industries.  All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice,
  this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.
```

## Downloaded at build time (not included)

The build tools fetch these; each comes with its own licence:
- Firmware: [LVGL](https://github.com/lvgl/lvgl) (MIT), [ArduinoJson](https://github.com/bblanchon/ArduinoJson) (MIT),
  [Arduino_GFX](https://github.com/moononournation/Arduino_GFX) (BSD), [Arduino-ESP32](https://github.com/espressif/arduino-esp32)
  (LGPL 2.1) and ESP-IDF (Apache 2.0).
- Android app: AndroidX, Jetpack Compose, Material 3 and Google Play services code scanner (Apache 2.0 or Google's
  Android SDK terms), [OkHttp](https://github.com/square/okhttp) (Apache 2.0).
- CAD: [build123d](https://github.com/gumyr/build123d) (Apache 2.0), [trimesh](https://github.com/mikedh/trimesh) (MIT),
  lib3mf (BSD).
