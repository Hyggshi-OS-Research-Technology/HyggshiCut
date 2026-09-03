#pragma once
#include <QString>
#include <QImage>
#include <memory>
#include <vector>
#include "TimeTypes.h"

namespace hc {

enum class MediaKind { Unknown, Video, Audio, Image };

// Immutable-ish description of a source file on disk, probed once on import.
// The Decoder opens a fresh decode session per-clip from this description;
// MediaAsset itself never holds an open FFmpeg context.
class MediaAsset {
public:
    QString id;                 // stable UUID, used to reference from Clip
    QString filePath;
    QString displayName;
    MediaKind kind = MediaKind::Unknown;

    Ticks duration = 0;         // full source duration in ticks
    int width = 0;
    int height = 0;
    double frameRate = 0.0;     // nominal fps, 0 for audio-only
    int videoStreamIndex = -1;
    int audioStreamIndex = -1;
    int sampleRate = 0;
    int channels = 0;
    int64_t bitRate = 0;        // bitrate in bits/sec (e.g. 320000 for 320 kbps)

    QImage thumbnail;           // small preview grabbed at t=0 (or mid-point)

    // Downsampled peak-per-bucket waveform for the audio stream (empty for
    // assets with no audio, or before generateWaveform() has run). Bucket i
    // covers source time [i, i+1) / kWaveformBucketsPerSecond seconds;
    // value is the loudest |sample| in that bucket, normalized to [0, 1].
    // Populated by Decoder::computeWaveformPeaks() at import time — see
    // MainWindow::generateWaveform(), mirroring how thumbnails are grabbed.
    std::vector<float> waveformPeaks;
    static constexpr double kWaveformBucketsPerSecond = 50.0;

    bool hasVideo() const { return videoStreamIndex >= 0; }
    bool hasAudio() const { return audioStreamIndex >= 0; }

    // Probes the file with avformat/avcodec and fills in the fields above.
    // Returns false (and sets errorOut) if the file can't be opened/probed.
    static std::shared_ptr<MediaAsset> probe(const QString& filePath, QString* errorOut = nullptr);
};

using MediaAssetPtr = std::shared_ptr<MediaAsset>;

} // namespace hc
