# syntax=docker/dockerfile:1

# ==========================================
# Stage 1: Builder & Packager
# ==========================================
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    dpkg-dev \
    debhelper \
    qt6-base-dev \
    libqt6opengl6-dev \
    qt6-qpa-plugins \
    libavformat-dev \
    libavcodec-dev \
    libavutil-dev \
    libswscale-dev \
    libswresample-dev \
    libavfilter-dev \
    libmpv-dev \
    libasound2-dev \
    libgl-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# Build binary and run offline test suite using offscreen platform
ENV QT_QPA_PLATFORM=offscreen
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DHYGGSHICUT_BUILD_TESTS=ON \
    && cmake --build build -j$(nproc) \
    && ./build/TextCacheAndCpuFallbackTest \
    && ./build/SegmentBoundTest

# Default command: build debian package (.deb)
CMD ["sh", "-c", "dpkg-buildpackage -us -uc -b -j$(nproc) && mkdir -p dist && mv ../hyggshicut_*.deb dist/ && ls -lh dist/"]

# ==========================================
# Stage 2: Production GUI Runtime
# ==========================================
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

RUN apt-get update && apt-get install -y --no-install-recommends \
    libqt6widgets6 \
    libqt6openglwidgets6 \
    libqt6dbus6 \
    libqt6gui6 \
    libqt6core6t64 \
    qt6-qpa-plugins \
    qt6-wayland \
    libxkbcommon-x11-0 \
    libxcb-cursor0 \
    libxcb-icccm4 \
    libxcb-image0 \
    libxcb-keysyms1 \
    libxcb-randr0 \
    libxcb-render-util0 \
    libxcb-shape0 \
    libxcb-sync1 \
    libxcb-xfixes0 \
    libxcb-xinerama0 \
    libxcb-xkb1 \
    libmpv2 \
    libasound2t64 \
    libasound2-plugins \
    pulseaudio-utils \
    libgl1 \
    libgl1-mesa-dri \
    libglx-mesa0 \
    libegl1 \
    mesa-vulkan-drivers \
    fonts-dejavu-core \
    fonts-freefont-ttf \
    ffmpeg \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /src/build/HyggshiCut /usr/local/bin/HyggshiCut
COPY --from=builder /src/languages /usr/local/share/hyggshicut/languages

# Setup non-root desktop user matching UID 1000
ARG UID=1000
ARG GID=1000
RUN (getent group video >/dev/null 2>&1 || groupadd -g 44 video) && \
    (getent group audio >/dev/null 2>&1 || groupadd -g 29 audio) && \
    (getent group render >/dev/null 2>&1 || groupadd -g 990 render) && \
    if id -u ubuntu >/dev/null 2>&1; then \
        usermod -l hyggshi -d /home/hyggshi -m ubuntu && \
        groupmod -n hyggshi ubuntu && \
        usermod -u ${UID} -g ${GID} -aG video,audio,render hyggshi; \
    else \
        (getent group ${GID} >/dev/null 2>&1 || groupadd -g ${GID} hyggshi) && \
        useradd -m -u ${UID} -g ${GID} -G video,audio,render -s /bin/bash hyggshi; \
    fi && \
    mkdir -p /home/hyggshi/.config/HyggshiCut /home/hyggshi/Videos /home/hyggshi/Projects /workspace && \
    chown -R ${UID}:${GID} /home/hyggshi /workspace

ENV HOME=/home/hyggshi
ENV QT_X11_NO_MITSHM=1

USER hyggshi
WORKDIR /home/hyggshi
ENTRYPOINT ["/usr/local/bin/HyggshiCut"]
