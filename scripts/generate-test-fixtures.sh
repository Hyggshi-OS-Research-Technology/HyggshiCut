#!/usr/bin/env bash
set -euo pipefail

build_dir="$(realpath "$1")"
project_generator="$2"
fixture_dir="$build_dir/build"
mkdir -p "$fixture_dir"

ffmpeg -hide_banner -loglevel error -y \
  -f lavfi -i 'testsrc2=size=640x360:rate=30' \
  -f lavfi -i 'sine=frequency=880:sample_rate=48000' \
  -t 8 -c:v libx264 -preset ultrafast -pix_fmt yuv420p \
  -c:a aac -b:a 96k -movflags +faststart \
  "$fixture_dir/test_sample.mp4"

ffmpeg -hide_banner -loglevel error -y \
  -f lavfi -i 'color=c=0x2d7dd2:s=320x180:d=1' \
  -frames:v 1 -update 1 "$fixture_dir/test_image.png"

"$project_generator" \
  "$fixture_dir/test_sample.mp4" \
  "$fixture_dir/user_project.hcproj"
