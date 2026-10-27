#!/usr/bin/env bash
set -euo pipefail

build_dir="$(realpath "$1")"
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

python3 - "$fixture_dir" <<'PY'
import json
import os
import sys

fixture_dir = os.path.abspath(sys.argv[1])
media_path = os.path.join(fixture_dir, "test_sample.mp4")
project = {
    "formatVersion": 1,
    "name": "CI test project",
    "assets": [{
        "id": "ci-test-video",
        "filePath": media_path,
        "displayName": "test_sample.mp4",
        "kind": "video",
        "duration": "8000000",
        "width": 640,
        "height": 360,
        "frameRate": 30.0,
        "videoStreamIndex": 0,
        "audioStreamIndex": 1,
        "sampleRate": 48000,
        "channels": 2,
        "bitRate": 0,
    }],
    "timeline": {
        "frameRate": 30.0,
        "videoWidth": 640,
        "videoHeight": 360,
        "tracks": [{
            "id": "ci-video-track",
            "type": "visual",
            "name": "Video 1",
            "muted": False,
            "hidden": False,
            "locked": False,
            "clips": [{
                "id": "ci-video-clip",
                "assetId": "ci-test-video",
                "type": "video",
                "sourceIn": "0",
                "sourceOut": "8000000",
                "timelineStart": "0",
                "speed": 1.0,
                "volume": 1.0,
                "opacity": 1.0,
                "muted": False,
                "displayLabel": "CI test video",
            }],
        }],
    },
}
with open(os.path.join(fixture_dir, "user_project.hcproj"), "w", encoding="utf-8") as stream:
    json.dump(project, stream, ensure_ascii=False, indent=2)
PY
