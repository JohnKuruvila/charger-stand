#!/bin/sh
# Regenerates src/fonts/*.c with lv_font_conv (needs Node.js). Montserrat Medium and Font Awesome 5 come with LVGL,
# so build once first (pio run -e preview) to download it. To add characters, change a range below and rerun.
set -e
cd "$(dirname "$0")/.."
LV=$(ls -d .pio/libdeps/*/lvgl/scripts/built_in_font | head -1)
[ -d "$LV" ] || { echo "LVGL not downloaded yet: run 'pio run -e preview' first" >&2; exit 1; }
cp "$LV/Montserrat-Medium.ttf" "$LV/FontAwesome5-Solid+Brands+Regular.woff" src/fonts/
cd src/fonts
conv() { npx -y lv_font_conv@1.5.2 --bpp 4 --format lvgl --lv-include lvgl.h --no-compress "$@"; }
TEXT=0x20-0x7E,0xB0,0xB7,0x2013,0x2026  # printable ASCII, degree sign, middle dot, en dash, ellipsis
# Font Awesome: bolt wifi check times exclamation-triangle info-circle sync terminal download github cubes music pause
ICONS=0xf0e7,0xf1eb,0xf00c,0xf00d,0xf071,0xf05a,0xf021,0xf120,0xf019,0xf09b,0xf1b2,0xf001,0xf04c
conv --size 68 --font Montserrat-Medium.ttf -r 0x20,0x2D,0x30-0x3A -o w6_clock_68.c
conv --size 50 --font Montserrat-Medium.ttf -r 0x20,0x25,0x2D,0x30-0x3A,0xB0 -o w6_num_50.c
conv --size 24 --font Montserrat-Medium.ttf -r $TEXT -o w6_text_24.c
conv --size 18 --font Montserrat-Medium.ttf -r $TEXT -o w6_text_18.c
conv --size 14 --font Montserrat-Medium.ttf -r $TEXT -o w6_text_14.c
conv --size 36 --font Montserrat-Medium.ttf -r 0x20,0x2D,0x61-0x7A -o w6_words_36.c  # the words face: lowercase
conv --size 48 --font Montserrat-Medium.ttf -r 0x20,0x61-0x7A -o w6_words_48.c
conv --size 34 --font FontAwesome5-Solid+Brands+Regular.woff -r $ICONS -o w6_icons_34.c
conv --size 18 --font FontAwesome5-Solid+Brands+Regular.woff -r $ICONS -o w6_icons_18.c
rm Montserrat-Medium.ttf FontAwesome5-Solid+Brands+Regular.woff
