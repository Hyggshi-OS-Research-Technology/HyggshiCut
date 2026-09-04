# Changelog

All notable changes to HyggshiCut are documented in this file.

---

## [1.1.0] - 2026-09-04

### 🚀 New Features
- **Native Screen Recording (Ghi Màn Hình)**:
  - Added dedicated Screen Recorder supporting Linux desktop environments.
  - **Wayland (Ubuntu / GNOME)**: Uses native `org.gnome.Shell.Screencast` D-Bus interface for smooth 1080p/4K 60fps hardware-accelerated capture with zero window flickering.
  - **X11 / Fallback**: Automatic fallback to `ffmpeg -f x11grab` with ultrafast H.264 encoding.
  - **Synchronized Audio**: Records Microphone or Desktop Audio via PipeWire/PulseAudio (`-f pulse`) and losslessly multiplexes streams upon completion.
  - **Deep Editor Integration**: Quick-record button in Media Pool, menu shortcut (`Ctrl+Shift+R`), with auto-import into Media Pool and instant insertion into the timeline at the playhead.

- **Graphics Backend Selection in Settings**:
  - Added backend selection in `Settings -> Graphics Backend` (`Cài đặt -> Bộ dựng đồ họa`):
    - **OpenGL 3.3 Core Profile** (Recommended / Default)
    - **Latest OpenGL Core Profile** (Negotiates up to OpenGL 4.6)
    - **Vulkan** (Experimental RHI)
  - Preferences persist across application restarts via `QSettings`.

- **CPU Software Preview Fallback**:
  - Automatically activates software compositor when OpenGL 3.3 or GLSL shaders fail to compile, or when forced via `HYGGSHICUT_FORCE_CPU_RENDER=1`.
  - Implemented CPU YUV-to-RGB conversion (supporting BT.601 and BT.709 matrices) and `QPainter` layer compositing, preventing black screens on legacy hardware and VMs.

- **Window Settings (Cài đặt cửa sổ)**:
  - Added dedicated Window Settings dialog in `Settings -> Window Settings…` (`Cài đặt -> Cài đặt cửa sổ…`).
  - Supports configurable startup window modes: Remember last size & position, Maximized, Fullscreen, or Default (1280x720).
  - Toggles for Always on Top, Lock Panels (prevent accidental detaching/floating), Toolbar and Status Bar visibility, and window opacity slider.
  - Added quick action to Reset Dock Layout to the clean default arrangement.
  - All preferences persist across application sessions via `QSettings`.

### ⚡ Performance & Memory Optimizations
- **Text RAM-Bomb Elimination**:
  - Replaced full-canvas text rasterization (1920x1080 ~7.9 MiB / 3840x2160 ~31.6 MiB per entry) with **Tight Bounding Box Cards** (`cardW x cardH`, ~100–300 KiB).
  - Reduced text CPU cache entry count from 32 to 8, slashing resident CPU RAM usage from **~253 MiB down to < 1.5 MiB** (> 98% reduction).
  - Full-canvas rendering retained for FFmpeg export filter graphs to preserve 100% backward compatibility.
- **GPU Text Texture Reuse & PCIe Bandwidth Reduction**:
  - Added OpenGL texture caching in `GLVideoWidget`.
  - Reuses GPU textures directly on every frame for static text layers, eliminating repeated `glTexSubImage2D` calls and dropping PCIe bandwidth from **~237 MiB/s to 0 B/s**.
  - Quad scaling via vertex shader (`uTileScale = vec2(cardW / canvasW, cardH / canvasH)`) eliminates transparent overdraw and maintains pixel-perfect alignment.

### 🛠️ Packaging & CI/CD Fixes
- **Debian Packaging (`.deb`)**:
  - Fixed Debhelper compat level conflict (`debhelper-compat (= 13)` in `debian/control` vs redundant `debian/compat`).
  - Added executable permissions (`+x`) to `debian/rules`.
  - Fixed icon path to install `hyggshicut.png` to `/usr/share/pixmaps/`.
  - Added `ffmpeg` as a runtime dependency in `debian/control`.
  - Updated OpenGL development dependencies to `libgl-dev | libgl1-mesa-dev` for Ubuntu 24.04 compatibility in `.github/workflows/build.yml`.

---

## [1.0.0] - 2026-08-16

### Initial Release
- Multi-track timeline with non-linear video, audio, image, and text clips.
- Real-time GPU compositing with custom GLSL shaders and visual effects.
- Free Transform with keyframe animation support (position, scale, rotation, opacity).
- Audio mixing, 3-band EQ, compressor, noise reduction, and live VU meters.
- Multi-format video export via FFmpeg (MP4, MOV, MKV, WebM, MP3, WAV).
- Low-latency timeline audio playback via ALSA PCM (bypassing Qt multimedia).
- Raw source clip preview via libmpv client API.
- Multilingual interface support (Vietnamese and English).
