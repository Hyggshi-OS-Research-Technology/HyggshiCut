# Changelog

All notable changes to HyggshiCut are documented in this file.

---

## [Unreleased]

### New Features
- **Clip copy / paste / duplicate**: copy the selected clip (`Ctrl+C`), paste it at the playhead on a compatible track (`Ctrl+V`), or duplicate it right after itself (`Ctrl+D`) — also available from the clip's right-click menu. Pasted/duplicated clips keep their full styling, transform, effects, fades, and keyframes, and get a fresh id so undo history stays sane.
- **Ripple delete**: remove a clip and automatically close the gap by shifting every later clip on the track left (Edit menu + clip right-click menu).
- **Frame-accurate clip nudge**: move the selected clip one frame left/right with `,` / `.` (or the Edit menu), for precise sync adjustments.
- **Snap toggle**: edge/playhead/keyframe snapping during drags can now be switched on/off from the View menu and is remembered across sessions.
- **Standard timeline navigation**: `Ctrl`/`Alt` + mouse wheel now zooms the timeline around the cursor, plain mouse wheel pans horizontally (`Shift` + wheel scrolls vertically), `Home`/`End` jump to the start/end, and a **Zoom to fit** action (`Shift+Z`) fits the whole timeline into the window. The playhead auto-scrolls into view during playback and edge scrubbing, with a live zoom readout in the status bar.
- **Transition types (Wipe, Slide, Dip to Color)**: transitions between adjacent clips on a Visual track now go beyond the classic cross-dissolve. Right-click the transition marker to pick the type — **Cross Dissolve**, **Wipe**, **Slide**, or **Dip to Color** — plus its direction, duration, and (for dip) the colour it fades through. The timeline marker is colour-coded by type, and every transition renders identically in the GL preview, the CPU fallback preview, and the ffmpeg export.

### Bug Fixes
- **HD thumbnail color accuracy**: Media-pool/timeline thumbnails now honor each source's BT.601 vs BT.709 matrix (previously always converted as BT.601, so HD thumbnails could differ slightly from Preview/Export).
- **Media pool thumbnails after reopening a project**: image/video thumbnails (and audio waveforms) are derived in-memory at import time and are not stored in the `.hcproj`, so reopening a saved project used to show every media row with a blank preview. They are now regenerated when a project is opened, so the media pool looks exactly as it did when it was saved.
- **Removed developer-machine paths**: dropped the hardcoded `/home/hyggshi/Downloads/...` language/plugin search entries, so bundled assets are only discovered from portable locations.
- **Screen recorder cleanup**: a failed audio multiplex now removes the leftover raw-video temp file instead of leaking it in `/tmp`.
- **Export low-memory log**: the "low-memory mode" message now reports the real trigger thresholds.

### Performance & Memory Optimizations (weak / older machines)
- **Adaptive decode threading**: `Decoder` now scales FFmpeg worker threads with the detected core count, and drops to slice-only threading (much lower RAM) on machines with ≤ 3 GiB.
- **Adaptive cache budgets**: preview frame/texture caches are sized from detected physical RAM (≤ 3 GiB and ≤ 8 GiB tiers) instead of always using workstation-sized defaults.
- **Adaptive export threads**: single-core machines render with one FFmpeg thread instead of two.
- **Smart proxy resolution**: proxy transcoding now tiers by source size — 4K/8K footage gets a 720p proxy, HD gets a 480p proxy, and smaller sources are never upscaled (AUTO mode, the new default). Explicit 360p/480p/540p/720p presets remain available in Settings.

---

## [1.1.0] - 2026-09-04

### New Features
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

### Performance & Memory Optimizations
- **Text RAM-Bomb Elimination**:
  - Replaced full-canvas text rasterization (1920x1080 ~7.9 MiB / 3840x2160 ~31.6 MiB per entry) with **Tight Bounding Box Cards** (`cardW x cardH`, ~100–300 KiB).
  - Reduced text CPU cache entry count from 32 to 8, slashing resident CPU RAM usage from **~253 MiB down to < 1.5 MiB** (> 98% reduction).
  - Full-canvas rendering retained for FFmpeg export filter graphs to preserve 100% backward compatibility.
- **GPU Text Texture Reuse & PCIe Bandwidth Reduction**:
  - Added OpenGL texture caching in `GLVideoWidget`.
  - Reuses GPU textures directly on every frame for static text layers, eliminating repeated `glTexSubImage2D` calls and dropping PCIe bandwidth from **~237 MiB/s to 0 B/s**.
  - Quad scaling via vertex shader (`uTileScale = vec2(cardW / canvasW, cardH / canvasH)`) eliminates transparent overdraw and maintains pixel-perfect alignment.

### Packaging & CI/CD Fixes
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
