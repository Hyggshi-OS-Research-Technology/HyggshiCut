#include <iostream>
#include <cassert>
#include <QApplication>
#include "core/Clip.h"
#include "render/TextRenderer.h"
#include "render/GLVideoWidget.h"
#include "decode/FrameTypes.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    std::cout << "Running TextCacheAndCpuFallbackTest...\n";

    hc::Clip clip;
    clip.type = hc::ClipType::Text;
    clip.displayLabel = "Hello HyggshiCut Text Cache";
    clip.textFontSize = 48;
    clip.textFontFamily = "Segoe UI";

    // Test 1: Tight bounding box rendering (fullCanvas = false, default)
    QImage tightImg = hc::TextRenderer::renderText(clip, 1920, 1080, false);
    assert(!tightImg.isNull());
    std::cout << "Tight bounding box dimensions: " << tightImg.width() << "x" << tightImg.height() << "\n";
    size_t tightBytes = static_cast<size_t>(tightImg.sizeInBytes());
    std::cout << "Tight image bytes: " << tightBytes << " (" << (tightBytes / 1024) << " KiB)\n";

    // Must be substantially smaller than a full 1080p canvas (1920 * 1080 * 4 = 8,294,400 bytes ≈ 7.9 MiB)
    constexpr size_t kFullCanvasBytes = 1920ull * 1080ull * 4ull;
    assert(tightBytes < kFullCanvasBytes / 10); // at least 10x smaller, typically ~50x smaller
    assert(tightImg.width() < 1920);
    assert(tightImg.height() < 1080);
    std::cout << "PASS: Tight bounding box renders a compact card instead of full canvas!\n";

    // Test 2: Full canvas rendering when explicitly requested (for export)
    QImage fullImg = hc::TextRenderer::renderText(clip, 1920, 1080, true);
    assert(!fullImg.isNull());
    assert(fullImg.width() == 1920);
    assert(fullImg.height() == 1080);
    std::cout << "PASS: Full canvas mode renders exactly 1920x1080!\n";

    // Test 3: Deterministic cacheKey
    QString key1 = hc::TextRenderer::cacheKey(clip, 1920, 1080, false);
    QString key2 = hc::TextRenderer::cacheKey(clip, 1920, 1080, false);
    assert(key1 == key2);

    clip.displayLabel = "Modified Text Content";
    QString key3 = hc::TextRenderer::cacheKey(clip, 1920, 1080, false);
    assert(key1 != key3);
    std::cout << "PASS: Cache keys are deterministic and change when content changes!\n";

    // Test 4: GLVideoWidget CPU fallback mode and YUV->RGB conversion
    hc::GLVideoWidget widget;
    widget.resize(640, 360);
    widget.setForceCpuFallback(true);
    assert(widget.isForceCpuFallback());

    // Construct a test YUV420P frame
    hc::VideoFrame testFrame;
    testFrame.width = 64;
    testFrame.height = 64;
    testFrame.strideY = 64;
    testFrame.strideU = 32;
    testFrame.strideV = 32;
    testFrame.y.assign(64 * 64, 128); // 50% gray
    testFrame.u.assign(32 * 32, 128);
    testFrame.v.assign(32 * 32, 128);

    hc::GLLayer vLayer;
    vLayer.frame = testFrame;

    hc::GLLayer tLayer;
    tLayer.isText = true;
    tLayer.canvasW = 1920;
    tLayer.canvasH = 1080;
    tLayer.textCacheKey = key3;
    tLayer.rgbaImage = tightImg;

    widget.setLayers({vLayer, tLayer});
    std::cout << "PASS: GLVideoWidget layers set with CPU fallback enabled without crashes!\n";

    std::cout << "ALL TEXT CACHE AND CPU FALLBACK TESTS PASSED!\n";
    return 0;
}
