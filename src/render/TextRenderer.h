#pragma once
#include <QImage>
#include "../core/Clip.h"

namespace hc {

class TextRenderer {
public:
    // Renders the text in `clip` with full styling (font, color, bold, italic,
    // underline, outline/stroke, background box, alignment) into an RGBA8888 QImage.
    // `canvasW` and `canvasH` represent the target timeline resolution (e.g. 1920x1080).
    static QImage renderText(const Clip& clip, int canvasW = 1920, int canvasH = 1080);
};

} // namespace hc
