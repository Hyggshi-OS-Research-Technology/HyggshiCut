#include "TextRenderer.h"
#include <QPainter>
#include <QPainterPath>
#include <QTextLayout>
#include <QTextOption>
#include <QFont>
#include <QFontMetrics>
#include <QColor>
#include <QPen>
#include <QBrush>
#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace hc {

namespace {
static std::mutex s_cacheMutex;
static std::unordered_map<QString, QImage> s_textCache;
constexpr size_t kMaxCacheEntries = 8;
}

QString TextRenderer::cacheKey(const Clip& clip, int canvasW, int canvasH, bool fullCanvas) {
    if (canvasW <= 0) canvasW = 1920;
    if (canvasH <= 0) canvasH = 1080;

    QString content = clip.displayLabel;
    if (content.isEmpty()) content = QStringLiteral("Văn bản");

    return QString(
        "%1|%2|%3|%4|%5|%6|%7|%8|%9|%10|%11|%12|%13|%14|%15|%16|%17")
        .arg(content,
             clip.textFontFamily,
             QString::number(clip.textFontSize),
             clip.textBold ? "1" : "0",
             clip.textItalic ? "1" : "0",
             clip.textUnderline ? "1" : "0",
             QString::number(clip.textAlignment),
             clip.textFontColor,
             clip.textOutlineEnabled ? "1" : "0",
             clip.textOutlineColor,
             QString::number(clip.textOutlineWidth),
             clip.textBackgroundEnabled ? "1" : "0",
             clip.textBackgroundColor,
             QString::number(clip.textPadding),
             QString::number(canvasW),
             QString::number(canvasH),
             fullCanvas ? "1" : "0");
}

QImage TextRenderer::renderText(const Clip& clip, int canvasW, int canvasH, bool fullCanvas) {
    if (canvasW <= 0) canvasW = 1920;
    if (canvasH <= 0) canvasH = 1080;

    const QString key = cacheKey(clip, canvasW, canvasH, fullCanvas);

    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        auto it = s_textCache.find(key);
        if (it != s_textCache.end()) {
            return it->second;
        }
    }

    QString content = clip.displayLabel;
    if (content.isEmpty()) content = QStringLiteral("Văn bản");

    QFont font;
    font.setFamily(clip.textFontFamily.isEmpty() ? QStringLiteral("Segoe UI") : clip.textFontFamily);

    // Scale font size relative to standard 1080p canvas
    const double scaleFactor = static_cast<double>(canvasH) / 1080.0;
    const int pxSize = std::max(10, qRound((clip.textFontSize > 0 ? clip.textFontSize : 64) * scaleFactor));
    font.setPixelSize(pxSize);
    font.setBold(clip.textBold);
    font.setItalic(clip.textItalic);
    font.setUnderline(clip.textUnderline);

    QFontMetrics fm(font);
    const int maxTextWidth = std::max(100, canvasW - 80);
    const QRect textBounding = fm.boundingRect(QRect(0, 0, maxTextWidth, canvasH),
                                               Qt::TextWordWrap | (clip.textAlignment == 1 ? Qt::AlignLeft : clip.textAlignment == 2 ? Qt::AlignRight : Qt::AlignHCenter),
                                               content);

    const int pad = qMax(4, qRound(clip.textPadding * scaleFactor));
    const int outlineW = clip.textOutlineEnabled ? qMax(1, qRound(clip.textOutlineWidth * scaleFactor)) : 0;

    // Dimensions of the rendered text card (natural aspect ratio for transform)
    const int cardW = qMin(canvasW, textBounding.width() + (pad + outlineW) * 2 + 16);
    const int cardH = qMin(canvasH, textBounding.height() + (pad + outlineW) * 2 + 16);

    // If fullCanvas is true (e.g. for Exporter PNG overlay), use full canvas dimensions.
    // If fullCanvas is false (default preview), allocate only the tight bounding box card (~100 KiB vs ~7.9 MiB).
    const int outW = fullCanvas ? canvasW : cardW;
    const int outH = fullCanvas ? canvasH : cardH;
    const int cardX = fullCanvas ? (canvasW - cardW) / 2 : 0;
    const int cardY = fullCanvas ? (canvasH - cardH) / 2 : 0;

    QImage img(outW, outH, QImage::Format_RGBA8888_Premultiplied);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setFont(font);

    const QRect cardRect(cardX, cardY, cardW, cardH);
    const QRect drawRect = cardRect.adjusted(pad + outlineW, pad + outlineW, -(pad + outlineW), -(pad + outlineW));

    // 1. Background box (if enabled)
    if (clip.textBackgroundEnabled && !clip.textBackgroundColor.isEmpty()) {
        QColor bg(clip.textBackgroundColor);
        if (bg.isValid()) {
            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(cardRect.adjusted(1, 1, -1, -1), 8.0 * scaleFactor, 8.0 * scaleFactor);
        }
    }

    Qt::Alignment align = Qt::AlignVCenter;
    if (clip.textAlignment == 1) align |= Qt::AlignLeft;
    else if (clip.textAlignment == 2) align |= Qt::AlignRight;
    else align |= Qt::AlignHCenter;

    QTextOption opt;
    opt.setWrapMode(QTextOption::WordWrap);
    if (clip.textAlignment == 1) opt.setAlignment(Qt::AlignLeft);
    else if (clip.textAlignment == 2) opt.setAlignment(Qt::AlignRight);
    else opt.setAlignment(Qt::AlignHCenter);

    QTextLayout textLayout(content, font);
    textLayout.setTextOption(opt);
    textLayout.beginLayout();
    qreal totalHeight = 0;
    while (true) {
        QTextLine line = textLayout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(drawRect.width());
        totalHeight += line.height();
    }
    textLayout.endLayout();

    const qreal yOffset = (align & Qt::AlignVCenter) ? std::max<qreal>(0, (drawRect.height() - totalHeight) / 2.0) : 0;

    QPainterPath path;
    for (int i = 0; i < textLayout.lineCount(); ++i) {
        QTextLine line = textLayout.lineAt(i);
        QString lineStr = content.mid(line.textStart(), line.textLength());
        while (!lineStr.isEmpty() && (lineStr.endsWith('\n') || lineStr.endsWith('\r'))) {
            lineStr.chop(1);
        }
        if (!lineStr.isEmpty()) {
            qreal x = drawRect.left() + line.x();
            qreal y = drawRect.top() + yOffset + line.y() + line.ascent();
            path.addText(x, y, font, lineStr);
        }
    }

    QColor outColor = QColor(clip.textOutlineColor.isEmpty() ? "#000000" : clip.textOutlineColor);
    QColor textColor = QColor(clip.textFontColor.isEmpty() ? "#FFFFFF" : clip.textFontColor);

    // 2. Text Outline (if enabled) — single-pass vector stroke in O(1), no massive for-loops!
    if (clip.textOutlineEnabled && outlineW > 0) {
        if (outColor.isValid()) {
            QPen pen(outColor, outlineW * 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p.strokePath(path, pen);
        }
    }

    // 3. Text Fill
    p.fillPath(path, QBrush(textColor.isValid() ? textColor : Qt::white));

    // 4. Underline if requested
    if (clip.textUnderline) {
        for (int i = 0; i < textLayout.lineCount(); ++i) {
            QTextLine line = textLayout.lineAt(i);
            qreal x1 = drawRect.left() + line.x();
            qreal x2 = x1 + line.naturalTextWidth();
            qreal y = drawRect.top() + yOffset + line.y() + line.ascent() + 2 * scaleFactor;
            qreal uWidth = std::max<qreal>(1.0, 2.0 * scaleFactor);
            if (clip.textOutlineEnabled && outlineW > 0 && outColor.isValid()) {
                p.setPen(QPen(outColor, (outlineW * 2) + uWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.drawLine(QPointF(x1, y), QPointF(x2, y));
            }
            p.setPen(QPen(textColor.isValid() ? textColor : Qt::white, uWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawLine(QPointF(x1, y), QPointF(x2, y));
        }
    }

    p.end();

    {
        std::lock_guard<std::mutex> lock(s_cacheMutex);
        if (s_textCache.size() >= kMaxCacheEntries) {
            s_textCache.clear();
        }
        s_textCache[key] = img;
    }

    return img;
}

} // namespace hc

