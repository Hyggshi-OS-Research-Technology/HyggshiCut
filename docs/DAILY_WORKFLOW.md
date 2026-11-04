# Daily editing and export workflow

HyggshiCut is organized as a standard editor workspace: **Media** on the left, **Preview** in the center, **Properties** on the right, and the multi-track **Timeline** below. Drag media to tracks, use **Add Layer** for additional video/audio/title/effect tracks, and save projects as `.hcproj` files that reference media in place.

## A safe everyday loop

1. **Create / open a project.** Set canvas size and frame rate to the delivery target before assembling a long timeline. Keep the `.hcproj` and source media in a stable folder; the project stores media references rather than embedding large source files.
2. **Build the edit with multiple tracks.** Use separate video tracks for overlays and picture-in-picture, audio tracks for music/voice, and title/effect layers as needed. Lock, mute or hide a track with its track controls. Use snapping, split-at-playhead and Undo/Redo; save at major milestones with `Ctrl+S`.
3. **Use proxies when decoding is slow.** Generate proxies from the media menu or Settings → Generate Proxies. Enable **Use Proxy (Low-res Preview)** for timeline playback and scrubbing. Proxy media is cached outside the project; it is video-only, while preview audio and every final export use the original source. Clearing the cache does not delete source media.
4. **Let autosave protect unsaved edits.** Autosave is enabled by default and stores recovery snapshots outside the project folder. After an abnormal exit, choose Recover at startup and immediately save the recovered project to a real `.hcproj`. A normal exit removes that session's recovery snapshot.
5. **Check the export environment before delivery.** Help → Check Environment reports FFmpeg and encoder availability. Select an output path, inspect resolution, frame rate, video/audio codecs and pixel format, then export a short sample before a long deliverable. If a selected encoder is missing, install an FFmpeg build that provides it or choose an available codec; the export dialog cannot add codecs to FFmpeg.

## Low-memory export and predictable validation

The exporter deliberately limits filter-graph threads, bounds codec worker threads, uses a constrained VBV buffer, and switches to a conservative single-thread profile on systems with little available/total RAM. In that mode it caps a single FFmpeg allocation at 128 MiB and substitutes H.264 for H.265 when the automatic profile detects low memory. These are safeguards, not a hard cap on total process memory. Large resolutions, high-bit-depth formats and complex effects can still need more RAM than a small machine has.

To validate the conservative argument profile on a high-memory development machine, set `HYGGSHICUT_FORCE_LOW_MEMORY=1` when building the FFmpeg arguments. The export smoke test does this automatically, inspects the thread/allocation limits, then runs a real short export and verifies the output duration with `ffprobe`. This verifies the selected profile and the end-to-end pipeline; it is not a substitute for testing on the lowest-RAM hardware you support.

Recommended release checks:

- Run the complete CTest suite (`ctest --preset tests`).
- Run `HyggshiCutExportSmokeTest` with a short H.264/AAC source and inspect the output in an independent player.
- Repeat at the lowest supported RAM and target resolution, watching peak resident memory and confirming the desktop remains responsive.
- Test each optional codec on the actual system FFmpeg build; missing encoders should produce a clear error rather than a mysterious blank file.
- Reopen the saved project and verify media relinking, track placement, effects, title layers and audio before delivering.

## Quy trình dựng và xuất hằng ngày

HyggshiCut có bố cục quen thuộc: **Media** bên trái, **Xem trước** ở giữa, **Thuộc tính** bên phải và **Timeline nhiều track** phía dưới. Kéo media vào track, chọn **Thêm lớp** để tạo thêm track video/audio/văn bản/hiệu ứng, rồi lưu dự án `.hcproj`. File dự án giữ đường dẫn tham chiếu đến media, không nhúng các file nguồn lớn.

### Vòng làm việc an toàn

1. **Tạo hoặc mở dự án.** Đặt kích thước canvas và FPS theo đầu ra trước khi dựng timeline dài. Giữ `.hcproj` và media trong thư mục ổn định.
2. **Dựng trên nhiều track.** Tách video overlay/PIP, nhạc/giọng nói, tiêu đề và lớp hiệu ứng theo track riêng. Dùng nút khóa, tắt tiếng hoặc ẩn track; bật bắt dính, cắt tại playhead và Undo/Redo. Lưu bằng `Ctrl+S` ở các mốc quan trọng.
3. **Dùng proxy khi giải mã chậm.** Tạo proxy qua menu media hoặc Settings → Generate Proxies. Bật **Use Proxy (Low-res Preview)** để tua/xem timeline mượt hơn. Proxy chỉ chứa hình ảnh; âm thanh xem trước và toàn bộ bản xuất cuối cùng vẫn dùng media gốc. Xóa cache không xóa media nguồn.
4. **Để autosave bảo vệ chỉnh sửa.** Autosave mặc định bật và lưu snapshot phục hồi bên ngoài thư mục dự án. Nếu ứng dụng thoát bất thường, chọn Recover khi mở lại rồi lưu ngay thành `.hcproj` thật. Thoát bình thường sẽ xóa snapshot của phiên đó.
5. **Kiểm tra môi trường trước khi giao file.** Help → Check Environment hiển thị FFmpeg và codec đang có. Chọn đường dẫn lưu, kiểm tra độ phân giải, FPS, codec video/audio và pixel format; xuất thử đoạn ngắn trước khi xuất bản dài. Nếu thiếu encoder, hãy cài FFmpeg có encoder đó hoặc chọn codec đang có.

### Xuất trên máy ít RAM và kiểm chứng

Exporter giới hạn luồng filter graph, giới hạn worker của codec, dùng bộ đệm VBV có giới hạn và tự chuyển sang cấu hình bảo thủ khi phát hiện RAM khả dụng/tổng thấp. Chế độ này đặt giới hạn 128 MiB cho một lần cấp phát FFmpeg và chuyển H.265 sang H.264 khi tự nhận diện máy ít RAM. Đây là biện pháp giảm rủi ro, **không phải** giới hạn cứng tổng RAM của tiến trình. Độ phân giải cao, pixel format nhiều bit và hiệu ứng nặng vẫn có thể vượt khả năng máy yếu.

Để kiểm tra profile bảo thủ trên máy phát triển nhiều RAM, đặt `HYGGSHICUT_FORCE_LOW_MEMORY=1` khi tạo tham số FFmpeg. Test export smoke tự bật cờ này, xác minh giới hạn thread/cấp phát, sau đó xuất một đoạn thật và dùng `ffprobe` kiểm tra thời lượng. Việc này xác minh profile và quy trình đầu-cuối, nhưng không thay thế kiểm thử trên phần cứng ít RAM nhất mà dự án hỗ trợ.

Trước khi phát hành: chạy toàn bộ CTest, xuất thử H.264/AAC, kiểm tra video bằng trình phát độc lập, đo RAM cực đại trên cấu hình yếu nhất, thử từng codec tùy chọn trên đúng bản FFmpeg mục tiêu, rồi mở lại dự án để xác nhận media, track, hiệu ứng, tiêu đề và âm thanh.
