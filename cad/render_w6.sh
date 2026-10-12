#!/bin/sh
# Render W6 views from the CAD. Usage: render_w6.sh [preview] [views...]
cd "$(dirname "$0")"
BLENDER=${BLENDER:-$HOME/Applications/blender-4.0.2-linux-x64/blender}
if [ "$1" = preview ]; then shift; RES=800; SAMPLES=16; else RES=2400; SAMPLES=${SAMPLES:-64}; fi
mkdir -p renders
for v in ${@:-hero exploded_front exploded_back section}; do
  VIEW=$v "$BLENDER" -b --python render_w6.py -- "$PWD/renders/w6_$v.png" $RES $SAMPLES denoise 2>&1 | grep -E "Error|WROTE|^Time:|Traceback|line [0-9]"
done
