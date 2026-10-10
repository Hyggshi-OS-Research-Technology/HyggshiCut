# Changelog

All notable changes to HyggshiCut are documented in this file.

---

## [Unreleased]

### New Features
- **Linux AppImage build**: add an x86_64 AppImage build using linuxdeploy and its Qt plugin. The image bundles the app's detected Qt/native runtime dependencies and `ffmpeg`; a custom `AppRun` puts the bundled executable first on `PATH`, so export, proxy generation, and screen recording all use the packaged version. Pull-request builds upload an `hyggshicut-linux-appimage` artifact, and version-tag releases attach both `.deb` and `.AppImage` assets.
- **Autosave & crash recovery** — the one thing an editor must not get wrong. HyggshiCut previously wrote a `.hcproj` only when you pressed Ctrl+S, so a GPU driver reset, an OOM-killed ffmpeg export or any crash lost everything since the last manual save. The open project is now snapshotted every 2 minutes (configurable 15 s – 30 min, or off, in **Settings → Window**), and only when it actually has unsaved changes.
  - Snapshots live in `~/.local/share/HyggshiCut/recovery/`, never in your project folder, and are plain `.hcproj` files — recovery is just "open this file", so there is no second format to keep in sync.
  - A clean exit deletes the snapshot, so anything present at startup is by definition a session that did not exit cleanly. That is when the work is offered back (**Recover** / **Discard** / **Decide later**).
  - Ownership is tracked by pid, and a pid only counts as alive if it is still a running HyggshiCut — so a second instance open right now is not reported as a crash, and a recycled pid is not mistaken for a live app.
  - A recovered project is pointed back at your original file and marked modified, so Ctrl+S writes where you expect rather than into the recovery directory.
- **C++23**: the build now targets C++23 where the toolchain supports it, falling back to C++20 (the configure output reports which). Because accepting `-std=c++23` says nothing about library availability — GCC 12 compiles as C++23 and ships `<expected>` but has no `std::format`, `std::print` or C++23 ranges adaptors — `src/core/CxxFeatures.h` provides one `HC_HAS_*` macro per facility, each checking the specific `__cpp_lib_*` macro, so nothing branches on a bare `__cplusplus` comparison.
- **Environment check**: a new diagnostics page that verifies everything HyggshiCut resolves at *run* time, so a broken setup is identified up front instead of as a confusing mid-edit failure. Open it from **Settings → Check Environment…** or **Help → Check Environment…** and press *Run check*; each row is colour-coded and carries a suggested fix, and *Copy report* puts the whole thing on the clipboard for bug reports.
  - **ffmpeg binary** — resolved with the exact same search order the exporter uses (PATH, then `/usr/bin`, `/usr/local/bin`, `/snap/bin`, `/bin`), so it reports the binary that would really be used rather than just what is on PATH.
  - **ffmpeg encoders** — checks the encoders the export presets actually reference (`libx264`, `aac`, `libx265`, `libvpx-vp9`, `libsvtav1`, `prores_ks`, `libmp3lame`, `pcm_s16le`). Previously a missing encoder only surfaced as a failed export after it had already been running for a while. H.264/AAC are errors; the rest are warnings.
  - **OpenGL** — reports the live driver strings and distinguishes three states: healthy GPU, shader-link failure (silently falls back to the slow CPU renderer), and Mesa's software rasterisers (`llvmpipe`/`softpipe`/`swrast`), which work but make preview inexplicably slow.
  - **Audio** — opens the ALSA `default` PCM device exactly as the timeline preview does, so an unusable device is detected rather than merely a present one.
  - **Bundled assets** — `.langhc` / `.plhc` discovery across the app's real search path, catching the "UI shows raw translation keys" and "Effects panel is empty" cases.
  - **System** — Qt and libav\* build-vs-runtime versions (a major-version mismatch is flagged as an ABI hazard), CPU/RAM against the thresholds that trigger low-memory mode, and a genuine write probe of the proxy cache directory.
- **`--check-env` CLI flag**: prints the same report as plain text and exits 0/1 so install scripts and CI can gate on it. It runs *before* any GUI is constructed, so it works over SSH and in containers with no display — which is precisely where you need it.
- **Effect Layers (adjustment layers)**: a new media-less timeline layer that carries an effect stack and lends it to every visual clip on the tracks **below** it, for as long as the layer overlaps them. Add one with `Ctrl+Shift+L`, *Timeline → Add Layer → Effect Layer*, the timeline's right-click menu, or the new **Effect Layer** card at the top of the Explorer's Effects page (double-click to span the whole timeline, drag onto the timeline to place it at the drop point).
  - Build the stack from the Inspector's **Effects** tab, by dragging effect cards onto the layer, or from the layer's own double-click / right-click menu (which also offers *clear all effects* and *rename*).
  - Stacks compose in classic NLE order: the clip's own effects first, then each layer above it bottom-to-top, so several layers can be stacked for a shot-by-shot grade plus an overall look.
  - Preview and export stay pixel-consistent: the GL renderer and the ffmpeg exporter resolve the same inherited stack (`Timeline::mergedEffects` / `Timeline::effectLayerRangesFor`). A layer that only partially overlaps a clip is time-gated in the filter graph (`split` + `overlay:enable=between(t,…)`), so the look switches on and off on exactly the frames the preview shows; a layer covering the whole clip simply extends that clip's chain at no extra cost.
  - Layers are invisible by design: they never composite a frame, never become an ffmpeg input, are skipped by transitions/thumbnails/transform editing, and hiding their track bypasses them. Stretching a layer past the end of the footage does **not** lengthen the timeline, so exports never grow a black tail.
  - On the timeline they are drawn as hatched orange blocks with an always-visible `Fx N` badge, and selecting one tints the span of the tracks below that it affects. They split, trim, move, copy/paste, duplicate, ripple-delete and undo/redo like any other clip, and are saved in `.hcproj` as `"type": "effect_layer"`.
- **Clip copy / paste / duplicate**: copy the selected clip (`Ctrl+C`), paste it at the playhead on a compatible track (`Ctrl+V`), or duplicate it right after itself (`Ctrl+D`) — also available from the clip's right-click menu. Pasted/duplicated clips keep their full styling, transform, effects, fades, and keyframes, and get a fresh id so undo history stays sane.
- **Ripple delete**: remove a clip and automatically close the gap by shifting every later clip on the track left (Edit menu + clip right-click menu).
- **Frame-accurate clip nudge**: move the selected clip one frame left/right with `,` / `.` (or the Edit menu), for precise sync adjustments.
- **Snap toggle**: edge/playhead/keyframe snapping during drags can now be switched on/off from the View menu and is remembered across sessions.
- **Standard timeline navigation**: `Ctrl`/`Alt` + mouse wheel now zooms the timeline around the cursor, plain mouse wheel pans horizontally (`Shift` + wheel scrolls vertically), `Home`/`End` jump to the start/end, and a **Zoom to fit** action (`Shift+Z`) fits the whole timeline into the window. The playhead auto-scrolls into view during playback and edge scrubbing, with a live zoom readout in the status bar.
- **Transition types (Wipe, Slide, Dip to Color)**: transitions between adjacent clips on a Visual track now go beyond the classic cross-dissolve. Right-click the transition marker to pick the type — **Cross Dissolve**, **Wipe**, **Slide**, or **Dip to Color** — plus its direction, duration, and (for dip) the colour it fades through. The timeline marker is colour-coded by type, and every transition renders identically in the GL preview, the CPU fallback preview, and the ffmpeg export.
- **Unified Inspector panel**: the four separate right-hand docks (Transform, Video Effects, Text, Audio Filters) are now a single tabbed **Properties** dock that switches to the relevant tab when you select a clip, and includes a read-only **Media** tab showing the selected asset's name, type, location, duration, resolution, frame rate, bitrate, sample rate, channels, and on-disk file size.
- **Explorer grid & search**: the Media Pool is now a thumbnail **grid** (bigger previews, name + type + resolution/duration card labels) with a live **search box** that filters by file name or media type, an item counter, and click-to-inspect — single-clicking a card shows its properties in the Inspector without stealing focus during playback.

### Bug Fixes
- **Saving a project was not atomic and could destroy it**: `Project::saveToFile()` opened the real `.hcproj` with `Truncate` and wrote straight into it, so a crash, a full disk or a power cut part-way through left a truncated, unparseable file — with the previous good version already gone. It now writes to a temporary file and renames it into place, so the original survives any failure. A short write (disk full / over quota) was also ignored, with the function returning success for a corrupt file; it is now detected and reported.
- **`LanguageManager.cpp` used `std::sort` without including `<algorithm>`**: it compiled only by accident, via a transitive Qt include. A Qt or libstdc++ header reshuffle would have broken the build.
- **CMakeLists.txt duplication**: ten test targets were near-identical 10-line copy-paste blocks, and three had already drifted to different include-directory sets for no reason. Replaced with `add_engine_test()` / `add_gui_test()` helpers, cutting the file from 304 to 216 lines with a byte-identical target set and source lists.
- **Transform bounding-box drag lag ("image moves on its own")**: dragging the white transform frame over an image/video used to fire one synchronous seek + re-composite per mouse-move, so the GL image lagged behind the overlay box and then lurched forward to catch up. The clip transform still updates on every move (and the box tracks the cursor 1:1), but the preview re-render is now throttled to ~40/s during the drag, with a final un-throttled re-render on release. Undo now snapshots at drag *start* rather than release, so Undo restores the pre-drag position.
- **Preview seek bar scrub glitch & audio sputter**: dragging the preview seek bar ("lever") while playing used to (a) fire one synchronous seek per pixel of movement, stalling the UI thread, and (b) re-seed audio output on every movement, so sound sputtered and the playhead fought the cursor. Scrubbing now pauses playback (silent, CapCut-style), throttles seeks to ~40/s, keeps the timecode readout live under the cursor, and resumes playback when the lever is released if it was playing before.
- **HD thumbnail color accuracy**: Media-pool/timeline thumbnails now honor each source's BT.601 vs BT.709 matrix (previously always converted as BT.601, so HD thumbnails could differ slightly from Preview/Export).
- **Media pool thumbnails after reopening a project**: image/video thumbnails (and audio waveforms) are derived in-memory at import time and are not stored in the `.hcproj`, so reopening a saved project used to show every media row with a blank preview. They are now regenerated when a project is opened, so the media pool looks exactly as it did when it was saved.
- **Removed developer-machine paths**: dropped the hardcoded `/home/hyggshi/Downloads/...` language/plugin search entries, so bundled assets are only discovered from portable locations.
- **Screen recorder cleanup**: a failed audio multiplex now removes the leftover raw-video temp file instead of leaking it in `/tmp`.
- **Export low-memory log**: the "low-memory mode" message now reports the real trigger thresholds.
- **Proxy generation ignored the ffmpeg search path**: `ProxyManager` hard-coded `setProgram("ffmpeg")` while `Exporter` resolved the binary through PATH plus the usual install prefixes. On a machine where ffmpeg lives in `/usr/local/bin` or `/snap/bin` but is not on the GUI process's PATH, exporting worked while every proxy silently failed. Both now use the same resolver.
- **Keyboard shortcuts silently did nothing (`S`, `Delete`, `Shift+Delete`, `Ctrl+Shift+P`, `Ctrl+,`)**: these actions were each created *twice* — once in Edit and again in the Timeline/File/Settings menu — as two separate `QAction`s carrying the same `QKeySequence`. Qt treats that as an *ambiguous* shortcut and fires **neither**, so the keys did nothing while the menu entries kept working and still displayed the shortcut. Each action is now created once and the same instance is added to every menu that shows it.
- **Editing shortcuts hijacked typing**: `S`, `C`, `Delete`, `Ctrl+C`, `Ctrl+V`, `Ctrl+D`, `Ctrl+A` and `Esc` were window-global, so typing in the Explorer search box or the Text panel triggered timeline edits — pressing `s` split a clip, and `Ctrl+C` copied the selected clip instead of the selected text. They are now scoped to the timeline (`Qt::WidgetWithChildrenShortcut`) and only fire while it has focus.
- **Shortcuts stopped working after switching projects**: the timeline-scoped actions are attached to the `TimelineWidget`, which is recreated on every New/Open, so they are now re-attached whenever the timeline or the menus are rebuilt.
- **Menu rebuilds leaked duplicate shortcuts**: every language change clears the menu bar and re-runs `buildMenus()`, which previously created a fresh set of window-owned actions each time; the stale ones stayed alive and re-introduced the ambiguity above. They are now destroyed before the rebuild.
- **`+` / `-` timeline zoom was documented but never implemented**: the menu only bound `QKeySequence::ZoomIn`/`ZoomOut` (i.e. `Ctrl++` / `Ctrl+-`). The bare keys now zoom while the timeline has focus, and the README documents both forms.
- **Shortcut table corrections**: the README listed `+`/`-` for zoom (wrong, see above) and omitted `C`, `Ctrl+A`, `Esc`, `Ctrl+N`, `Ctrl+O`, `Ctrl+Q`, `Ctrl+,` and `Ctrl+Shift+R`. It now also marks which shortcuts require timeline focus.
- **New `ShortcutTest`**: an offscreen regression test asserting that no two actions share a shortcut in the same context, that each documented shortcut is bound exactly once, that text-shadowing shortcuts stay timeline-scoped, and that rebuilding the menus does not duplicate actions.
- **Bundled languages & plugins were never installed**: `cmake --install` (and therefore the `.deb`) shipped only the executable, so an installed HyggshiCut found no `.langhc`/`.plhc` files and fell back to raw translation keys with an empty Effects list. They are now installed to `share/hyggshicut/{languages,plugins}`, and both `main.cpp` and `LanguageManager` search that location (plus `/usr/local/share`) in addition to the paths next to the binary. The Docker runtime image now copies `plugins/` too.
- **Opening a project by double-click / "Open With"**: the `.desktop` entry launches `HyggshiCut %F`, but the CLI only accepted `-p/--project`, so a file passed positionally was ignored and a blank editor opened. A positional `[project.hcproj]` argument is now accepted (with `--project` still taking precedence).
- **`.hcproj` MIME type was never registered**: the `.desktop` file declared `application/x-hyggshicut-project` but the package installed no MIME definition for it, so no file manager ever associated `.hcproj` files with HyggshiCut. The package now ships `usr/share/mime/packages/hyggshicut.xml`.
- **Test wrote to a hardcoded developer path**: `settings_dialog_test` asserted on saving preview PNGs into an absolute `/home/hyggshi/.gemini/...` directory, so it failed on every machine but one. It now renders into a `QTemporaryDir`.
- **Version drift**: `CMakeLists.txt` declared 0.1.0 while `--version` and `debian/changelog` said 1.0.0. The project version is now the single source of truth and is injected into the binary via `HYGGSHICUT_VERSION`.
- **Malformed `debian/copyright`**: the `License:` field contained the full licence text instead of a short name, which is not valid DEP-5. Rewritten with a proper `Expat` stanza.
- **Repository hygiene**: removed the checked-in `dd.zip` build artefact (a 2 MB duplicate snapshot of the tree), renamed `license` to `LICENSE-HOSL-1.3.md` so it no longer collides with `LICENSE` on case-insensitive filesystems (where a clone would silently lose one of the two), and extended `.gitignore` to cover Debian build artefacts and archives.
- **CI release notes**: the tagged-release step emitted a literal `\n` in the release body and hid every failure behind `|| true`; it now writes real newlines and reports failures. The offline test step also builds and runs `SegmentBoundTest` and `ScreenRecorderTest` alongside `EffectLayerTest`.

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
