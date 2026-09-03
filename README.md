# 🎬 HyggshiCut

<div id="NexCode-logo" align="center">
    <br />
    <img src="./debian/hyggshicut.png" alt="HyggshiCut IDE Logo" width="200"/>
    <h1>HyggshiCut</h1>
    <h3>Free/Libre Open Source Software</h3>
</div>

<div id="badges" align="center">

**Modern, Lightweight & High-Performance Professional Video Editor for Linux**

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg?style=flat-square&logo=c%2B%2B)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![Qt6](https://img.shields.io/badge/Qt-6.x-green.svg?style=flat-square&logo=qt)](https://www.qt.io/)
[![FFmpeg](https://img.shields.io/badge/FFmpeg-libav*-red.svg?style=flat-square&logo=ffmpeg)](https://ffmpeg.org/)
[![OpenGL](https://img.shields.io/badge/OpenGL-3.3_Core-orange.svg?style=flat-square&logo=opengl)](https://www.opengl.org/)
[![Platform](https://img.shields.io/badge/Platform-Linux-lightgrey.svg?style=flat-square&logo=linux)](https://kernel.org/)
[![Packaging](https://img.shields.io/badge/Package-.deb-purple.svg?style=flat-square&logo=debian)](https://www.debian.org/)

[**Tính năng (Features)**](#-tính-năng-nổi-bật--key-features) •
[**Cài đặt (Installation)**](#-cài-đặt--installation) •
[**Biên dịch (Build from Source)**](#-biên-dịch-từ-mã-nguồn--building-from-source) •
[**CLI Headless Render**](#-hướng-dẫn-chế-độ-dòng-lệnh-headless-cli-render) •
[**Phím tắt (Shortcuts)**](#-bảng-phím-tắt--keyboard-shortcuts) •
[**Plugin & Ngôn ngữ**](#-hệ-thống-plugin--đa-ngôn-ngữ)

</div>

---

## 📖 Giới thiệu / Overview

**HyggshiCut** là phần mềm chỉnh sửa video chuyên nghiệp, mã nguồn mở, siêu nhẹ và tối ưu hóa hiệu năng cao dành cho Linux. Được xây dựng từ nền tảng **C++20**, **Qt6**, **FFmpeg (libav\*)** và bộ kết xuất **OpenGL 3.3 Core**, HyggshiCut mang lại trải nghiệm biên tập mượt mà với mức chiếm dụng RAM cực thấp.

Ngoài giao diện đồ họa (GUI) trực quan và hiện đại, HyggshiCut còn tích hợp sẵn công cụ kết xuất dòng lệnh **Headless CLI Render Engine**, giúp tự động hóa quá trình xuất video hàng loạt trong môi trường server hoặc CI/CD mà không cần màn hình hiển thị.

---

## ✨ Tính năng nổi bật / Key Features

### 🎞️ 1. Multi-Track Timeline & Biên tập mạnh mẽ
- **Đa layer linh hoạt:** Hỗ trợ không giới hạn các track Video, Audio, Ảnh (Image), và Văn bản (Text).
- **Công cụ cắt dựng chuẩn xác:** Cắt tại con trỏ (`S`), trim 2 đầu clip, kéo giãn thời lượng, ripple delete, snapping thông minh.
- **Hệ thống Lịch sử thao tác:** Hoàn tác (Undo) và Làm lại (Redo) an toàn với toàn bộ thao tác trên timeline.

### ⚡ 2. Real-Time GPU Compositing & Effects (OpenGL 3.3 Core)
- **19 Chế độ hòa trộn (Blend Modes):** Normal, Multiply, Screen, Overlay, Add, Subtract, Darken, Lighten, HardLight, SoftLight, Difference, Exclusion, Dodge, Burn, Saturate, HSL Hue/Saturation/Color/Luminosity.
- **Chuỗi hiệu ứng hậu kỳ thời gian thực (GPU Shader Pipeline):**
  - Brightness (Độ sáng), Contrast (Độ tương phản), Saturation (Độ bão hòa), Hue Rotate (Xoay vòng màu).
  - Multi-pass Gaussian Blur (Làm mờ mượt mà), Sharpen (Làm nét), Vignette (Tối góc), Invert Colors (Đảo màu), Sepia.
- **Chỉnh màu điện ảnh 3-Way Color Grading:**
  - 3 bánh xe màu: **Lift (Shadows)**, **Gamma (Midtones)**, **Gain (Highlights)** cùng thanh trượt độ sáng Luma.
  - Sẵn có các mẫu Preset màu: *Teal & Orange*, *Warm Sunset*, *Cool Nordic*, *Vintage 70s*, *Cyberpunk Neon*, *Bleach Bypass*, *Golden Hour*, *Horror Green*.

### 📐 3. Free Transform & Keyframe Animation (Hoạt ảnh)
- **Biến đổi khung hình tự do:** Điều chỉnh vị trí $(X, Y)$, kích thước Scale $(X, Y)$, góc xoay Rotation (°), độ trong suốt Opacity.
- **Transform Gizmo Overlay trực quan:** Tương tác kéo thả, xoay và căn chỉnh trực tiếp ngay trên khung Preview.
- **Hệ thống Keyframe thông minh:** Đặt keyframe trên dòng thời gian (hiển thị hình thoi `◆`) để tạo chuyển động mượt mà (PIP, zoom, trượt layer, fade).

### 🔀 4. Chuyển cảnh & Fade mượt mà
- **Crossfade Transitions:** Chuyển cảnh hòa tan (Cross-dissolve) mượt mà với thuật toán single-pass siêu tiết kiệm RAM.
- **Fade Handles:** Kéo thả trực tiếp trên clip để tạo Fade-in / Fade-out cho cả hình ảnh và âm thanh.

### 🅣 5. Text & Subtitle Generator
- Tạo layer văn bản với tùy biến font chữ, kích thước, căn lề, màu sắc, chữ đậm/nghiêng/gạch chân.
- Hỗ trợ viền chữ sắc nét (**Outline stroke**) và khung nền (**Background box**) với màu sắc và padding tùy chỉnh.

### 🔊 6. Xử lý Âm thanh chuyên nghiệp (Pro Audio Suite)
- **Đầu ra âm thanh Direct ALSA:** Phát lại âm thanh xem trước cực nhạy, độ trễ cực thấp, không bị lỗi drop device trên PipeWire.
- **Bộ cân bằng 3 dải EQ:** Low (~100Hz), Mid (~1kHz), High (~8kHz).
- **Khử nhiễu nền thông minh:** Thuật toán khử ồn phổ tần số FFT (`afftdn`).
- **Bộ nén động Dynamic Range Compressor:** Giữ âm lượng ổn định với `acompressor` (Threshold & Ratio).
- **Live Stereo VU Meter:** Đo mức âm lượng đỉnh (Peak) và trung bình (RMS) theo thời gian thực.

### 🚀 7. Công cụ Xuất Video Tối ưu (Low-RAM Exporter)
- **Thuật toán Single-Pass:** Xử lý chuỗi filter complex thông minh, không gây spike RAM ngay cả với dự án dài.
- **Đa dạng Codec & Định dạng:**
  - Video: `H.264` (libx264), `H.265 / HEVC` (libx265), `VP9` (libvpx-vp9), `AV1` (libsvtav1), `Apple ProRes 422 HQ` (prores_ks).
  - Audio: `AAC`, `MP3` (libmp3lame), `WAV` (PCM 16/24-bit), `Opus`.
  - Container: `MP4`, `MKV`, `MOV`, `WebM`, `MP3`, `WAV`.
- **Mẫu xuất tối ưu sẵn (Presets):** YouTube 1080p/4K, TikTok / Reels / Shorts 9:16, Instagram 1:1, Máy yếu / Siêu nhanh (Ultrafast), Lossless Master.

### 🖥️ 8. Headless CLI Render Mode (Dòng lệnh không cần GUI)
- Cho phép render dự án video trực tiếp qua Terminal hoặc kịch bản tự động hóa server / CI/CD.
- Tích hợp thanh tiến trình render, ước tính thời gian ETA, và mã phản hồi tiêu chuẩn.

---

## 🛠️ Yêu cầu hệ thống & Thư viện / Dependencies

### Yêu cầu tối thiểu:
- **Hệ điều hành:** Linux (Ubuntu 22.04+, Debian 12+, Fedora 38+, Arch Linux, v.v.)
- **Trình biên dịch:** Hỗ trợ C++20 (`g++ >= 11` hoặc `clang++ >= 14`)
- **Hệ thống build:** `CMake >= 3.20`, `pkg-config`
- **Card đồ họa:** Hỗ trợ OpenGL 3.3 Core Profile trở lên

### Cài đặt thư viện trên Ubuntu / Debian:

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential cmake pkg-config dpkg-dev debhelper \
  qt6-base-dev libqt6opengl6-dev \
  libavformat-dev libavcodec-dev libavutil-dev libswscale-dev \
  libswresample-dev libavfilter-dev \
  libmpv-dev libasound2-dev libgl1-mesa-dev
```

### Cài đặt thư viện trên Arch Linux:

```bash
sudo pacman -S base-devel cmake qt6-base qt6-declarative \
  ffmpeg mpv alsa-lib mesa
```

---

## 🚀 Biên dịch từ mã nguồn / Building from Source

```bash
# 1. Clone repository
git clone https://github.com/hyggshi/HyggshiCut.git
cd HyggshiCut

# 2. Tạo thư mục build
mkdir -p build && cd build

# 3. Cấu hình CMake (Chế độ Release)
cmake .. -DCMAKE_BUILD_TYPE=Release

# 4. Biên dịch (Sử dụng tất cả số nhân CPU)
cmake --build . -j"$(nproc)"

# 5. Khởi chạy HyggshiCut
./HyggshiCut
```

### Biên dịch kèm bộ kiểm thử (Tests):

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug -DHYGGSHICUT_BUILD_TESTS=ON
cmake --build . -j"$(nproc)"

# Chạy smoke test
./HyggshiCutExportSmokeTest
./SegmentBoundTest
./UserProjectExportTest
```

---

## 📦 Đóng gói & Cài đặt gói `.deb` (Debian / Ubuntu)

Bạn có thể tự tạo gói cài đặt `.deb` chuẩn Debian:

```bash
# Đứng tại thư mục gốc của HyggshiCut
dpkg-buildpackage -us -uc -b -j"$(nproc)"

# Cài đặt gói deb vừa tạo
sudo dpkg -i ../hyggshicut_*.deb || sudo apt-get install -f
```

---

## 💻 Hướng dẫn Chế độ Dòng lệnh (Headless CLI Render)

HyggshiCut có thể kết xuất video trực tiếp từ terminal mà không cần khởi động giao diện đồ họa:

```bash
HyggshiCut --render -p <du_an.hcproj> -o <ket_qua.mp4> [tuy_chon]
```

### Danh sách tham số CLI:

| Tham số | Viết tắt | Mô tả |
|---|---|---|
| `--project <file>` | `-p` | Đường dẫn tới file dự án `.hcproj` *(Bắt buộc khi render)* |
| `--render` | `-r` | Chế độ render dòng lệnh |
| `--no-gui` | | Chạy không giao diện (tự động bật chế độ render) |
| `--output <file>` | `-o`, `--ot` | Đường dẫn tệp tin video/audio đầu ra |
| `--preset <name>` | | Chọn preset: `youtube-1080p`, `youtube-4k`, `tiktok-9-16`, `instagram-1-1`, `prores`, `audio-mp3`, `audio-wav` |
| `--codec <name>` | | Video codec: `h264`, `hevc` (h265), `vp9`, `av1`, `prores`, `none` |
| `--crf <0-51>` | | Chất lượng Constant Rate Factor (CRF) |
| `--bitrate <kbps>` | | Target Video Bitrate (ví dụ: `8000k` hoặc `12000`) |
| `--width <px>` | | Độ rộng khung hình xuất |
| `--height <px>` | | Chiều cao khung hình xuất |
| `--fps <fps>` | | Tốc độ khung hình (Frame rate) |
| `--progress` | | Hiển thị thanh tiến trình render realtime |

### Ví dụ sử dụng:

```bash
# 1. Render nhanh với Preset TikTok / Shorts 9:16
HyggshiCut -r -p my_project.hcproj -o shorts_output.mp4 --preset tiktok-9-16 --progress

# 2. Render chuẩn 4K 60FPS với H.265 và CRF 20
HyggshiCut -r -p wedding.hcproj -o wedding_4k.mp4 --codec hevc --crf 20 --width 3840 --height 2160 --fps 60

# 3. Trích xuất chỉ file âm thanh chất lượng cao MP3
HyggshiCut -r -p podcast.hcproj -o podcast_audio.mp3 --preset audio-mp3
```

---

## ⌨️ Bảng Phím tắt / Keyboard Shortcuts

| Phím tắt | Chức năng |
|---|---|
| `Space` | Phát / Dừng phát (Play / Pause) |
| `S` | Cắt đôi clip tại vị trí con trỏ (Split at Playhead) |
| `Delete` / `Backspace` | Xóa clip đang được chọn |
| `Shift + Delete` | Xóa layer/track đang chọn |
| `←` / `→` | Di chuyển lùi / tiến 1 frame |
| `↑` / `↓` | Di chuyển lùi / tiến 5 giây |
| `Ctrl + Z` | Hoàn tác thao tác (Undo) |
| `Ctrl + Y` / `Ctrl + Shift + Z` | Làm lại thao tác (Redo) |
| `Ctrl + I` | Mở hộp thoại Nhập media |
| `Ctrl + T` | Thêm Layer Văn bản mới |
| `Ctrl + E` | Mở hộp thoại Xuất video & âm thanh (Export Dialog) |
| `Ctrl + Shift + P` | Cài đặt khung hình & Canvas dự án |
| `Ctrl + S` | Lưu dự án |
| `Ctrl + Shift + S` | Lưu dự án thành tên khác (Save As) |
| `+` / `-` (hoặc `Ctrl + Scroll`) | Phóng to / Thu nhỏ Timeline (Zoom In / Zoom Out) |

---

## 🔌 Hệ thống Plugin & Đa Ngôn ngữ

### 🧩 Plugin (`.plhc`)
HyggshiCut hỗ trợ hệ thống mở rộng preset màu sắc và hiệu ứng dạng tệp `.plhc` (JSON manifest). Đi kèm sẵn là bộ **Hyggshi Cinematic Color Pack**:
- 🎌 *Anime Vivid* (Hoạt hình sống động)
- 🌸 *K-Drama Warm* (Phim Hàn ấm áp)
- 🎵 *Lo-Fi Pastel Aesthetic*
- ⬛ *Matte Black Cinematic*
- 📷 *Fuji Film Emulation* (Phim Fuji cổ điển)
- 🌙 *Moonlight Blue* (Ánh trăng xanh)
- 🌿 *Forest Green* (Rừng xanh)

Bạn có thể quản lý và nạp thêm plugin tùy chỉnh tại menu `Cài đặt` -> `Quản lý Plugin (.plhc)...`.

### 🌐 Hỗ trợ đa ngôn ngữ (`.langhc`)
Giao diện người dùng hỗ trợ nhiều ngôn ngữ với tính năng chuyển đổi tức thì không cần khởi động lại ứng dụng:
- 🇻🇳 **Tiếng Việt** (`vi.langhc`)
- 🇬🇧 **English** (`en.langhc`)
- 🇯🇵 **日本語** (`ja.langhc`)
- 🇰🇷 **한국어** (`ko.langhc`)
- 🇹🇭 **ไทย** (`th.langhc`)
- 🇨🇳 **简体中文** (`zh.langhc`)

---

## 🏗️ Cấu trúc thư mục dự án / Project Structure

```
HyggshiCut/
├── CMakeLists.txt              # Cấu hình build CMake chính
├── debian/                     # Cấu hình đóng gói .deb cho Linux
├── languages/                  # Các gói tệp ngôn ngữ (.langhc)
├── plugins/                    # Các gói hiệu ứng và preset màu (.plhc)
├── src/
│   ├── main.cpp                # Điểm khởi chạy ứng dụng & Xử lý CLI Headless
│   ├── audio/                  # Xử lý chuỗi bộ lọc âm thanh (EQ, Denoise, Compressor)
│   ├── cache/                  # Bộ nhớ đệm dữ liệu & kết xuất
│   ├── core/                   # Mô hình dữ liệu lõi: Project, Timeline, Track, Clip, MediaAsset
│   ├── decode/                 # FFmpeg media demuxing & decoding
│   ├── export/                 # Bộ xuất render video tối ưu hóa RAM (Single-pass Exporter)
│   ├── i18n/                   # Quản lý ngôn ngữ LanguageManager
│   ├── playback/               # Bộ điều khiển phát lại & ALSA Direct Audio Output
│   ├── plugin/                 # Hệ thống tải và quản lý PluginManager
│   ├── render/                 # Bộ dựng OpenGL 3.3 Core, TextRenderer, TextureCache
│   └── ui/                     # Giao diện Qt6 (Timeline, Transform, Effects, ColorWheel, ExportDialog...)
└── tests/                      # Bộ kiểm thử tính năng và export engine
```

---

## 📄 Bản quyền & Tác giả / License & Authors

- **Tác giả:** Hyggshi OS Foundation / HyggshiCut Team
- **Liên hệ / Hỗ trợ:** [hyggshidev@gmail.com](mailto:hyggshidev@gmail.com)
- **Trang chủ:** [https://hyggshi-os-website.pages.dev/](https://hyggshi-os-website.pages.dev/)
- **Mã nguồn:** Phát hành theo giấy phép mã nguồn mở.

---

<div align="center">
  <sub>Được phát triển với niềm đam mê dành cho cộng đồng sáng tạo nội dung trên Linux.</sub>
</div>
