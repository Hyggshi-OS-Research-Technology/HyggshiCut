## Core Feature Set

This document lists the core features of the software as of the version date below.
Only features included in this list (and their direct evolutions) are
considered part of the original Covered Work for the purposes of
Section 6 of the HOSL 1.3 license.

This list is versioned and cumulative. New features may be added in
later versions.

### Timeline & Editing
- **Multi-track timeline:** Support for video, audio, image, and text tracks.
- **Clip manipulation:** Cut, split, trim, move, duplicate, delete, ripple delete.
- **Real-time preview:** Playback from memory-mapped frames with audio synchronization.
- **Snapping:** Magnetic snapping to grid, clips, and timeline markers.
- **Undo/Redo system:** Full history stack for all timeline operations.

### Media Support
- **Input decoding:** Support for major video, audio, and image codecs via FFmpeg (libav\*).
- **Output encoding:** Export with H.264, H.265, VP9, AV1, Prores, AAC, MP3, PCM, WAV, etc.
- **Container formats:** MP4, MKV, MOV, WebM, MP3, WAV.
- **Asset management:** Import/import media files into the project.

### Video Effects & Compositing (OpenGL)
- **Blend modes:** Support for 19 blend modes (Normal, Multiply, Screen, Overlay, Add, etc.).
- **Post-processing chain:** Brightness, Contrast, Saturation, Hue Rotate, Gaussian Blur, Sharpen, Vignette.
- **Color grading:** 3-way color wheels (Lift, Gamma, Gain) + Luma control.
- **Color presets:** Built-in color grading presets.

### Transform & Animation
- **Free Transform:** Position (X, Y), Scale (X, Y), Rotation, Opacity.
- **Keyframes:** Manual keyframe creation and editing for transform properties.
- **Transform Gizmo:** Visual overlay for on-screen manipulation.

### Audio Processing
- **Audio playback:** Direct ALSA audio output with low-latency preview.
- **Audio effects:** 3-band EQ, FFT-based noise reduction (afftdn), Compressor (acompressor).
- **Metering:** Real-time stereo Peak and RMS VU metering.

### Text & Graphics
- **Text layers:** Rich text editing with font, size, color, alignment.
- **Text effects:** Outline, Background Box, Padding.

### Export Features
- **Hardware acceleration:** Optional use of VA-API for encoding/decoding.
- **Export presets:** YouTube (1080p/4K), TikTok (9:16), Instagram (1:1), Ultrafast, Lossless, etc.
- **Headless CLI:** Command-line interface for server-side rendering.

### System & UI
- **Cross-platform:** Linux-first with cross-platform support.
- **Modern GUI:** Qt6-based interface.
- **Theme system:** Light and Dark themes.
- **Localization:** Support for multiple languages.
- **Plugin system:** Extensible plugin architecture.