#!/usr/bin/env bash
set -e

DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$DIR"

echo "=========================================================="
echo "  HyggshiCut - Launching GUI in Docker"
echo "=========================================================="

# 1. Build runtime image if needed
echo "--> Checking/Building hyggshicut:runtime image..."
docker build -t hyggshicut:runtime --target runtime \
    --build-arg UID="$(id -u)" \
    --build-arg GID="$(id -g)" \
    .

# 2. Prepare X11 permissions
if command -v xhost >/dev/null 2>&1; then
    xhost +local:root >/dev/null 2>&1 || true
    xhost +local:"$(whoami)" >/dev/null 2>&1 || true
    xhost +SI:localuser:"$(whoami)" >/dev/null 2>&1 || true
fi

# 3. Assemble Docker run flags
CONTAINER_RUNTIME_DIR="/tmp/runtime-hyggshi"

DOCKER_ARGS=(
    --rm
    -it
    --network=host
    --ipc=host
    --user "$(id -u):$(id -g)"
    -e DISPLAY="${DISPLAY:-:0}"
    -e QT_X11_NO_MITSHM=1
    -v /tmp/.X11-unix:/tmp/.X11-unix:rw
)

# Wayland display socket forwarding (if active)
if [ -n "$WAYLAND_DISPLAY" ] && [ -S "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}" ]; then
    echo "--> Forwarding Wayland display: ${WAYLAND_DISPLAY}"
    DOCKER_ARGS+=(
        -e WAYLAND_DISPLAY="${WAYLAND_DISPLAY}"
        -e XDG_RUNTIME_DIR="${CONTAINER_RUNTIME_DIR}"
        -v "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}:${CONTAINER_RUNTIME_DIR}/${WAYLAND_DISPLAY}:rw"
    )
fi

# PulseAudio / PipeWire socket forwarding (for low-latency audio playback)
USER_PULSE_SOCK="/run/user/$(id -u)/pulse/native"
if [ -S "$USER_PULSE_SOCK" ]; then
    echo "--> Forwarding PulseAudio / PipeWire audio stream"
    DOCKER_ARGS+=(
        -e PULSE_SERVER=unix:/tmp/pulse-socket
        -v "$USER_PULSE_SOCK:/tmp/pulse-socket:rw"
    )
fi

# Direct ALSA audio device passthrough
if [ -d "/dev/snd" ]; then
    DOCKER_ARGS+=(
        --device /dev/snd:/dev/snd
        --group-add audio
    )
fi

# Direct GPU hardware acceleration (Mesa / Intel / AMD / NVIDIA DRI)
if [ -d "/dev/dri" ]; then
    echo "--> Passing GPU acceleration (/dev/dri)"
    DOCKER_ARGS+=(
        --device /dev/dri:/dev/dri
        --group-add video
    )
    RENDER_GID=$(getent group render | cut -d: -f3 || true)
    if [ -n "$RENDER_GID" ]; then
        DOCKER_ARGS+=(--group-add "$RENDER_GID")
    fi
fi

# Host directories mapping for project persistence & media import
mkdir -p "${HOME}/.config/HyggshiCut" "${HOME}/Videos"
DOCKER_ARGS+=(
    -v "${HOME}/.config/HyggshiCut:/home/hyggshi/.config/HyggshiCut:rw"
    -v "${HOME}/Videos:/home/hyggshi/Videos:rw"
    -v "$DIR:/workspace:rw"
    -w /workspace
)

echo "--> Launching HyggshiCut GUI..."
echo "=========================================================="
exec docker run "${DOCKER_ARGS[@]}" hyggshicut:runtime "$@"
