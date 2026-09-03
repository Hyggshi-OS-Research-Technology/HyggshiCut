#pragma once
#include <cstdint>
#include <QString>

namespace hc {

// All timeline/media positions are expressed as int64_t microseconds.
// This matches FFmpeg's AV_TIME_BASE (1,000,000) so conversions to/from
// AVStream time bases are cheap and lossless enough for editing purposes.
using Ticks = int64_t;

constexpr Ticks kTicksPerSecond = 1'000'000;

inline Ticks secondsToTicks(double seconds) {
    return static_cast<Ticks>(seconds * static_cast<double>(kTicksPerSecond));
}

inline double ticksToSeconds(Ticks t) {
    return static_cast<double>(t) / static_cast<double>(kTicksPerSecond);
}

// Formats ticks as HH:MM:SS.mmm for UI display / timecodes.
inline QString formatTimecode(Ticks t) {
    if (t < 0) t = 0;
    const qint64 totalMs = t / 1000;
    const qint64 ms = totalMs % 1000;
    const qint64 totalSec = totalMs / 1000;
    const qint64 s = totalSec % 60;
    const qint64 totalMin = totalSec / 60;
    const qint64 m = totalMin % 60;
    const qint64 h = totalMin / 60;
    return QString("%1:%2:%3.%4")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'))
        .arg(ms, 3, 10, QChar('0'));
}

} // namespace hc
