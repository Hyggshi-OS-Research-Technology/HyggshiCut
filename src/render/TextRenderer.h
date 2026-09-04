#pragma once
#include <QImage>
#include "../core/Clip.h"

namespace hc {

class TextRenderer {
public:
    // Computes deterministic cache key for text content and styling
    static QString cacheKey(const Clip& clip, int canvasW = 1920, int canvasH = 1080, bool fullCanvas = false);

    // Renders the text in `clip` with full styling (font, color, bold, italic,
    // underline, outline/stroke, background box, alignment) into an RGBA8888 QImage.
    // `canvasW` and `canvasH` represent the target timeline resolution (e.g. 1920x1080).
    // When `fullCanvas` is false (default for UI preview), renders a tight bounding box
    // card (e.g. 350x80) instead of a 1920x1080 canvas, reducing memory by ~98%.
    // When `fullCanvas` is true (used by Exporter PNGs), renders centered on full canvas.
    static QImage renderText(const Clip& clip, int canvasW = 1920, int canvasH = 1080, bool fullCanvas = false);
};

} // namespace hc
