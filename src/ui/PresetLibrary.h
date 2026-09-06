#pragma once
#include <QString>
#include <QStringList>
#include <QColor>
#include <QVector>
#include <QFile>
#include <QDir>
#include <QIODevice>
#include <QDataStream>
#include <cmath>
#include <algorithm>
#include <vector>
#include "../core/Clip.h"

namespace hc {

// ---------------------------------------------------------------------------
// Built-in presets for the Explorer's library pages (CapCut-style left rail).
// These are deliberately header-only data + helpers so both MediaPoolWidget
// (which renders the cards) and MainWindow (which applies a preset) read from
// the same single source of truth.
// ---------------------------------------------------------------------------

// --- Text presets -----------------------------------------------------------
struct TextPreset {
    QString id;
    QString nameKey;   // LTR() key for the display name
    QString sample;    // default label + text drawn on the card
    int fontSize = 64;
    QString fontColor = QStringLiteral("#FFFFFF");
    bool bold = false;
    int alignment = 0; // 0 center, 1 left, 2 right
    bool outlineEnabled = true;
    QString outlineColor = QStringLiteral("#000000");
    int outlineWidth = 2;
    bool backgroundEnabled = false;
    QString backgroundColor = QStringLiteral("#00000080");
    int padding = 12;
};

inline const std::vector<TextPreset>& textPresets() {
    static const std::vector<TextPreset> presets = {
        { QStringLiteral("title"),       QStringLiteral("preset.text.title"),
          QStringLiteral("Title"), 96, QStringLiteral("#FFFFFF"), true, 0,
          true, QStringLiteral("#000000"), 3, false, QStringLiteral("#00000080"), 12 },
        { QStringLiteral("subtitle"),    QStringLiteral("preset.text.subtitle"),
          QStringLiteral("Subtitle"), 48, QStringLiteral("#FFFFFF"), false, 0,
          true, QStringLiteral("#000000"), 2, false, QStringLiteral("#00000080"), 12 },
        { QStringLiteral("lower_third"), QStringLiteral("preset.text.lower_third"),
          QStringLiteral("Lower third"), 44, QStringLiteral("#FFFFFF"), false, 1,
          true, QStringLiteral("#000000"), 2, true, QStringLiteral("#000000C0"), 12 },
        { QStringLiteral("caption"),     QStringLiteral("preset.text.caption"),
          QStringLiteral("Caption"), 40, QStringLiteral("#FFFFFF"), false, 0,
          true, QStringLiteral("#000000"), 2, true, QStringLiteral("#00000080"), 10 },
        { QStringLiteral("headline"),    QStringLiteral("preset.text.headline"),
          QStringLiteral("Headline"), 72, QStringLiteral("#FFD54A"), true, 0,
          true, QStringLiteral("#000000"), 3, false, QStringLiteral("#00000080"), 12 },
        { QStringLiteral("watermark"),   QStringLiteral("preset.text.watermark"),
          QStringLiteral("Watermark"), 32, QStringLiteral("#FFFFFF"), false, 2,
          false, QStringLiteral("#000000"), 0, false, QStringLiteral("#00000080"), 8 },
    };
    return presets;
}

inline const TextPreset* findTextPreset(const QString& id) {
    for (const auto& p : textPresets()) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

// --- Transition presets ------------------------------------------------------
struct TransitionPreset {
    QString id;
    QString nameKey;
    TransitionType type = TransitionType::Dissolve;
    int direction = 0;
    QColor color = QColor(0, 0, 0);
};

inline const std::vector<TransitionPreset>& transitionPresets() {
    static const std::vector<TransitionPreset> presets = {
        { QStringLiteral("dissolve"),  QStringLiteral("preset.transition.dissolve"),
          TransitionType::Dissolve, 0, QColor(0, 0, 0) },
        { QStringLiteral("wipe"),      QStringLiteral("preset.transition.wipe"),
          TransitionType::Wipe, 0, QColor(0, 0, 0) },
        { QStringLiteral("slide"),     QStringLiteral("preset.transition.slide"),
          TransitionType::Slide, 0, QColor(0, 0, 0) },
        { QStringLiteral("dip_black"), QStringLiteral("preset.transition.dip_black"),
          TransitionType::DipToColor, 0, QColor(0, 0, 0) },
        { QStringLiteral("dip_white"), QStringLiteral("preset.transition.dip_white"),
          TransitionType::DipToColor, 0, QColor(255, 255, 255) },
    };
    return presets;
}

inline const TransitionPreset* findTransitionPreset(const QString& id) {
    for (const auto& p : transitionPresets()) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

// --- Synthesized sound effects ----------------------------------------------
// Dependency-free mono 16-bit PCM WAVs generated on demand into the app cache
// dir, then imported like any other audio asset (they have no bundled media to
// ship, so the "Sounds" page is never empty).
struct SfxPreset {
    QString id;
    QString nameKey;
};

inline const std::vector<SfxPreset>& sfxPresets() {
    static const std::vector<SfxPreset> presets = {
        { QStringLiteral("beep"),   QStringLiteral("preset.sfx.beep") },
        { QStringLiteral("click"),  QStringLiteral("preset.sfx.click") },
        { QStringLiteral("pop"),    QStringLiteral("preset.sfx.pop") },
        { QStringLiteral("whoosh"), QStringLiteral("preset.sfx.whoosh") },
        { QStringLiteral("chime"),  QStringLiteral("preset.sfx.chime") },
        { QStringLiteral("tick"),   QStringLiteral("preset.sfx.tick") },
    };
    return presets;
}

inline const SfxPreset* findSfxPreset(const QString& id) {
    for (const auto& p : sfxPresets()) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

namespace {

constexpr int kSfxSampleRate = 44100;

QVector<qint16> synthTone(double dur, double f0, double f1, double decay, double gain) {
    QVector<qint16> s;
    const int n = static_cast<int>(dur * kSfxSampleRate);
    s.reserve(n);
    double phase = 0.0;
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kSfxSampleRate;
        const double freq = f0 + (f1 - f0) * (t / std::max(dur, 0.001));
        phase += 2.0 * M_PI * freq / kSfxSampleRate;
        const double env = std::exp(-decay * t);
        const double v = std::sin(phase) * env * gain;
        s.push_back(static_cast<qint16>(std::clamp(v, -1.0, 1.0) * 32767.0));
    }
    return s;
}

QVector<qint16> synthNoiseSweep(double dur, double gain) {
    QVector<qint16> s;
    const int n = static_cast<int>(dur * kSfxSampleRate);
    s.reserve(n);
    double lp = 0.0; // one-pole low-pass state
    unsigned seed = 0x9e3779b9u;
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / kSfxSampleRate;
        seed = seed * 1103515245u + 12345u;
        const double white = ((seed >> 8) & 0xffff) / 32768.0 - 1.0;
        // Sweep the cutoff up then down, with a soft attack/release envelope.
        const double cutoff = 0.05 + 0.45 * std::sin(M_PI * t / dur);
        lp += cutoff * (white - lp);
        const double env = std::sin(M_PI * t / dur);
        s.push_back(static_cast<qint16>(std::clamp(lp * env * gain, -1.0, 1.0) * 32767.0));
    }
    return s;
}

void mixIn(QVector<qint16>& dst, const QVector<qint16>& src) {
    if (src.size() > dst.size()) dst.resize(src.size());
    for (int i = 0; i < src.size(); ++i) {
        const int mixed = dst[i] + src[i];
        dst[i] = static_cast<qint16>(std::clamp(mixed, -32768, 32767));
    }
}

QVector<qint16> synthSfx(const QString& id) {
    if (id == QStringLiteral("beep"))  return synthTone(0.35, 880, 880, 6.0, 0.8);
    if (id == QStringLiteral("click")) return synthTone(0.08, 2000, 1500, 40.0, 0.9);
    if (id == QStringLiteral("pop"))   return synthTone(0.18, 220, 90, 14.0, 0.9);
    if (id == QStringLiteral("tick"))  return synthTone(0.03, 3000, 3000, 90.0, 0.5);
    if (id == QStringLiteral("whoosh")) return synthNoiseSweep(0.7, 1.0);
    if (id == QStringLiteral("chime")) {
        QVector<qint16> mix;
        mixIn(mix, synthTone(1.0, 523.25, 523.25, 3.0, 0.5));
        mixIn(mix, synthTone(1.0, 659.25, 659.25, 3.0, 0.35));
        mixIn(mix, synthTone(1.0, 783.99, 783.99, 3.0, 0.25));
        return mix;
    }
    return synthTone(0.3, 440, 440, 8.0, 0.7);
}

bool writeWav(const QString& path, const QVector<qint16>& samples) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;

    const quint32 dataSize = static_cast<quint32>(samples.size() * sizeof(qint16));
    const quint32 byteRate = kSfxSampleRate * sizeof(qint16);

    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);

    // RIFF header
    ds.writeRawData("RIFF", 4);
    ds << quint32(36 + dataSize);
    ds.writeRawData("WAVE", 4);

    // fmt chunk (PCM, mono, 16-bit)
    ds.writeRawData("fmt ", 4);
    ds << quint32(16);
    ds << quint16(1);
    ds << quint16(1);
    ds << quint32(kSfxSampleRate);
    ds << quint32(byteRate);
    ds << quint16(2);
    ds << quint16(16);

    // data chunk
    ds.writeRawData("data", 4);
    ds << dataSize;
    for (const qint16 v : samples) {
        ds << v;
    }
    return true;
}

} // namespace

// Writes (if needed) the synthesized WAV for `sfx` into `dir`, creating the
// directory if it doesn't exist yet. Returns the absolute file path, or an
// empty string if the file can't be written.
inline QString ensureSfxFile(const SfxPreset& sfx, const QString& dir) {
    if (dir.isEmpty()) return QString();
    if (!QDir().mkpath(dir)) return QString();
    const QString path = QDir(dir).filePath(sfx.id + QStringLiteral(".wav"));
    if (QFile::exists(path)) return path;
    if (writeWav(path, synthSfx(sfx.id))) return path;
    return QString();
}

} // namespace hc
