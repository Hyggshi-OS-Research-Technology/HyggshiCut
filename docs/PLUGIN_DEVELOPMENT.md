# HyggshiCut effect-pack developer guide

This guide describes the supported `.plhc` format. A `.plhc` is a **declarative preset pack**, not a native-code or shader plugin: it adds named, parameterized presets backed by effects already implemented by HyggshiCut. This deliberate boundary keeps third-party packs portable and prevents untrusted code from executing in the editor.

## Minimal package

```json
{
  "version": 1,
  "pluginId": "studio.example.looks",
  "name": "Example Looks",
  "author": "Example Studio",
  "version_num": "1.0.0",
  "description": "A small set of warm, cool and monochrome looks.",
  "type": "effects",
  "enabled": true,
  "effects": [
    {
      "id": "warm_highlights",
      "name": "Warm Highlights",
      "type": "color_grade",
      "params": {
        "lift_r": 0.02,
        "gamma_r": 0.04,
        "gain_r": 0.12,
        "gain_b": -0.08
      }
    }
  ]
}
```

Save valid UTF-8 JSON with the `.plhc` suffix. `pluginId` and every effect `id` must be 1–96 ASCII letters, digits, dots, underscores or hyphens; IDs must start with a letter or digit. Effect IDs must be unique within their pack. `name` may contain localized or human-readable text. All parameter values must be finite JSON numbers.

## Supported effect types and parameter conventions

Only these types are currently guaranteed to render in both the live preview and FFmpeg export:

| `type` | Parameters | Practical range / default |
|---|---|---|
| `brightness` | `amount` | -1 to 1; 0 is neutral |
| `contrast` | `amount` | 0 to 3; 1 is neutral |
| `saturation` | `amount` | 0 to 3; 1 is neutral |
| `hue_rotate` | `degrees` | 0 to 360; 0 is neutral |
| `blur` | `radius` | 0 to 30; 0 is neutral |
| `sharpen` | `amount` | 0 to 3; 0 is neutral |
| `vignette` | `strength`, `radius` | 0–1; default strength 0.5, radius 0.75 |
| `invert` | none | enabled means full inversion |
| `sepia` | `amount` | 0 to 1; 0 is neutral |
| `crop` | `left`, `top`, `right`, `bottom` | fractions 0–0.9 |
| `color_grade` | `lift_r/g/b`, `gamma_r/g/b`, `gain_r/g/b` | usually -1 to 1; 0 is neutral |

For `color_grade`, the legacy scalar names `lift`, `gamma` and `gain` are accepted as fallback values. Channel-specific names take precedence. Keep presets conservative: effects are cumulative, and large values can clip highlights or crush shadows. Unknown effect types are rejected at load time instead of being silently accepted as no-ops.

## Install and verify

1. Use **Settings → Plugin Manager → Load `.plhc`** and select the file.
2. Select a timeline clip (or an Effect Layer), open **Properties → Effects**, and use **Add** to choose the pack's preset.
3. Verify the preset in preview, save/reopen the project, and run an actual short export. Plugin provenance and the display name are stored with the effect instance; the native effect type and parameters remain the source of rendering behavior.
4. Test both H.264/AAC and the target delivery codec on the FFmpeg build you intend to support. Codec availability is an installation property, not guaranteed by the manifest.

HyggshiCut's `PluginManifestTest` checks valid manifests, rejection of unknown effect types, enabled/disabled behavior, and preset construction. Run it with the normal test suite. The shipped example pack is `plugins/hyggshi_cinematic_pack.plhc`.

## Versioning and compatibility

- Keep `pluginId` stable across compatible releases; bump `version_num` when the pack changes.
- Keep existing effect IDs stable so projects remain understandable when packs are updated or removed.
- `version` is the manifest schema version (currently `1`), not the pack's release version.
- Effects are copied into the project. Removing a pack does not remove an already-applied effect from a project; projects store its display name, provenance, native type and parameter values.
- Packs do not currently provide executable code, custom UI controls, keyframes, custom FFmpeg filters, GPU shaders or external dependencies. Contributions requiring those capabilities must add a first-party engine effect, including matching preview/export implementation and tests, before a pack can reference it.

## Error handling / security

Manifests are parsed as data only. The loader rejects malformed JSON, invalid/duplicate IDs, empty effect lists, unsupported types, non-object parameter maps and non-finite values. Keep package descriptions and parameter sets small and avoid distributing media in a preset pack.

---

# Hướng dẫn nhà phát triển gói hiệu ứng HyggshiCut

Tài liệu này mô tả định dạng `.plhc` được hỗ trợ. `.plhc` là **gói preset khai báo bằng dữ liệu**, không phải plugin mã native hay shader: gói chỉ bổ sung preset có tên và tham số, dựa trên hiệu ứng đã được HyggshiCut cài đặt sẵn. Giới hạn này giúp gói dễ chia sẻ, đồng thời không cho mã bên thứ ba tự chạy trong trình biên tập.

## Gói tối thiểu

```json
{
  "version": 1,
  "pluginId": "studio.example.looks",
  "name": "Example Looks",
  "author": "Example Studio",
  "version_num": "1.0.0",
  "description": "Một bộ preset màu ấm, lạnh và đơn sắc.",
  "type": "effects",
  "enabled": true,
  "effects": [
    {
      "id": "warm_highlights",
      "name": "Vùng sáng ấm",
      "type": "color_grade",
      "params": {
        "lift_r": 0.02,
        "gamma_r": 0.04,
        "gain_r": 0.12,
        "gain_b": -0.08
      }
    }
  ]
}
```

Lưu JSON UTF-8 hợp lệ với đuôi `.plhc`. `pluginId` và mỗi `effect.id` dài 1–96 ký tự ASCII gồm chữ, số, dấu chấm, gạch dưới hoặc gạch nối; ký tự đầu phải là chữ hoặc số. ID hiệu ứng không được trùng trong cùng gói. `name` có thể là tên hiển thị hoặc văn bản bản địa hóa. Mọi tham số phải là số JSON hữu hạn.

## Loại hiệu ứng và quy ước tham số

Chỉ các loại sau được bảo đảm hoạt động đồng nhất trong xem trước và xuất FFmpeg: `brightness` (`amount`, -1..1, mặc định 0), `contrast` (`amount`, 0..3, mặc định 1), `saturation` (`amount`, 0..3, mặc định 1), `hue_rotate` (`degrees`, 0..360), `blur` (`radius`, 0..30), `sharpen` (`amount`, 0..3), `vignette` (`strength`, `radius`, 0..1), `invert` (không có tham số), `sepia` (`amount`, 0..1), `crop` (`left`, `top`, `right`, `bottom`, tỉ lệ 0..0.9), và `color_grade` (`lift_r/g/b`, `gamma_r/g/b`, `gain_r/g/b`, thường -1..1).

Với `color_grade`, tên cũ dạng đơn `lift`, `gamma`, `gain` vẫn được chấp nhận làm giá trị dự phòng; tham số theo kênh được ưu tiên. Hãy đặt giá trị vừa phải vì hiệu ứng cộng dồn và có thể làm cháy vùng sáng hoặc bệt vùng tối. Loại hiệu ứng không hỗ trợ sẽ bị từ chối khi nạp thay vì âm thầm trở thành hiệu ứng rỗng.

## Cài đặt và kiểm tra

1. Chọn **Settings → Plugin Manager → Load `.plhc`** rồi mở file.
2. Chọn clip timeline (hoặc Lớp hiệu ứng), mở **Properties → Effects**, chọn **Add** và áp dụng preset của gói.
3. Kiểm tra xem trước, lưu rồi mở lại dự án, sau đó xuất thử một đoạn ngắn. Tên hiển thị và nguồn gốc plugin được lưu cùng hiệu ứng; loại hiệu ứng native và tham số mới quyết định cách dựng hình.
4. Kiểm thử H.264/AAC và codec bàn giao thực tế trên đúng bản FFmpeg mục tiêu. Codec có sẵn tùy bản cài đặt FFmpeg, không được bảo đảm bởi manifest.

`PluginManifestTest` kiểm tra manifest hợp lệ, từ chối loại hiệu ứng lạ, trạng thái bật/tắt và khởi tạo preset. Hãy chạy cùng bộ test thông thường. Gói ví dụ đi kèm là `plugins/hyggshi_cinematic_pack.plhc`.

## Phiên bản và tương thích

- Giữ `pluginId` ổn định giữa các bản tương thích; tăng `version_num` khi nội dung gói thay đổi.
- Giữ nguyên ID hiệu ứng đã phát hành để dự án vẫn dễ hiểu sau khi cập nhật hoặc gỡ gói.
- `version` là phiên bản schema manifest (hiện tại `1`), không phải phiên bản phát hành.
- Hiệu ứng được sao chép vào dự án. Gỡ gói không xóa hiệu ứng đã áp dụng; dự án lưu tên, nguồn gốc, loại native và tham số.
- Hiện chưa hỗ trợ mã thực thi, UI tùy chỉnh, keyframe, bộ lọc FFmpeg tùy ý, shader GPU hay phụ thuộc ngoài. Muốn có khả năng đó, trước hết cần bổ sung hiệu ứng vào engine chính, cả phần xem trước/xuất và test, rồi mới cho phép gói gọi đến.

## Xử lý lỗi và bảo mật

Manifest chỉ được đọc như dữ liệu. Bộ nạp từ chối JSON lỗi, ID sai hoặc trùng, danh sách hiệu ứng rỗng, loại không hỗ trợ, `params` không phải object và giá trị không hữu hạn. Giữ mô tả và danh sách tham số gọn; không đóng gói media vào preset.
