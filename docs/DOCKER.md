# HyggshiCut - Docker Environment & Build Infrastructure

HyggshiCut cung cấp môi trường Docker hoàn chỉnh cho **100% reproducible builds**, **đóng gói package `.deb` độc lập**, và **chạy ứng dụng GUI trực tiếp** với GPU passthrough và âm thanh.

---

## Tính năng nổi bật

- **Chạy GUI trực tiếp từ Docker**: Hỗ trợ hiển thị mượt mà trên cả **Wayland** và **X11** với tăng tốc phần cứng GPU (`/dev/dri`) và âm thanh thời gian thực (PulseAudio / PipeWire / ALSA).
- **Đóng gói `.deb` sạch sẽ**: Không phụ thuộc vào môi trường hay thư viện máy chủ (host), tránh xung đột phiên bản hoặc thiếu thư viện.
- **Render Headless CLI**: Hỗ trợ render dự án video tự động trong Docker container không cần màn hình đồ họa (dành cho CI/CD hoặc server).
- **Lưu trữ dữ liệu liên tục**: Tự động mount thư mục cấu hình `~/.config/HyggshiCut`, video `~/Videos`, và thư mục làm việc hiện tại vào container.

---

## Hướng dẫn sử dụng nhanh

### 1. Chạy giao diện GUI HyggshiCut trong Docker

Khởi chạy trực tiếp trình biên tập video đồ họa với GPU passthrough (Mesa Intel / AMD / NVIDIA) và âm thanh:

```bash
# Cách 1: Sử dụng helper script tự động cấu hình Wayland/X11 & Audio (Khuyến nghị)
./scripts/docker-run.sh

# Cách 2: Sử dụng Docker Compose
docker compose run --rm app
```

> **Lưu ý**: Script `./scripts/docker-run.sh` sẽ tự động:
> - Cấp quyền hiển thị X11 / Wayland qua socket.
> - Kết nối luồng âm thanh qua PipeWire / PulseAudio socket (`/run/user/$UID/pulse/native`).
> - Gắn thiết bị tăng tốc GPU phần cứng (`/dev/dri`).
> - Map thư mục `$HOME/Videos` và cấu hình của bạn để lưu lại thiết lập.

---

### 2. Đóng gói file `.deb` độc lập trong Docker

Tạo gói cài đặt Debian `.deb` chuẩn, sạch sẽ và hoàn toàn độc lập với hệ thống máy chủ:

```bash
# Cách 1: Dùng helper script
./scripts/docker-build-deb.sh

# Cách 2: Dùng docker compose
docker compose run --rm build-deb
```

Gói `.deb` sau khi đóng gói thành công sẽ tự động xuất ra thư mục `./dist/`:
- `dist/hyggshicut_1.0.0-1_amd64.deb`

Cài đặt gói `.deb` lên máy chủ:
```bash
sudo apt-get install -y ./dist/hyggshicut_1.0.0-1_amd64.deb
```

---

### 3. Render video dòng lệnh (Headless CLI) trong Docker

Render video trong container mà không cần giao diện đồ họa (headless) với codec tùy chọn:

```bash
docker run --rm \
    -v $(pwd):/workspace \
    -w /workspace \
    -e QT_QPA_PLATFORM=offscreen \
    hyggshicut:runtime \
    --render /workspace/my_project.hcproj -o /workspace/rendered.mp4 --progress
```

---

## Cấu trúc kỹ thuật Docker

1. **Multi-stage Dockerfile**:
   - `builder`: Đầy đủ toolchain C++20, Qt6, FFmpeg libav*, OpenGL, ALSA để biên dịch mã nguồn, chạy test suite tự động với chế độ headless (`offscreen`), và đóng gói `.deb`.
   - `runtime`: Image Ubuntu 24.04 tối ưu dung lượng chứa các thư viện chia sẻ runtime, font chữ Unicode/Cjk, driver Mesa DRI/Vulkan, và FFmpeg CLI.
2. **GPU & Display Passthrough**:
   - Tự động chuyển tiếp socket `/tmp/.X11-unix` và `wayland-0`.
   - Truyền trực tiếp `/dev/dri` cho Mesa Intel Iris Xe / AMD / NVIDIA.
   - Truyền socket âm thanh PulseAudio/PipeWire và `/dev/snd`.
3. **User Isolation**:
   - Tự động map user UID/GID (1000) bên trong container khớp với tài khoản máy host để tránh lỗi quyền sở hữu file khi lưu project hay export video.
