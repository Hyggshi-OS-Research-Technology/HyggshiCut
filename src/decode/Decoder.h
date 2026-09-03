#pragma once
#include <QString>
#include <QImage>
#include <optional>
#include <vector>
#include "FrameTypes.h"

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;
struct SwrContext;

namespace hc {

// Decoder wraps a single open FFmpeg decode session for one source file.
// One Decoder instance is created per active clip during playback (the
// PlaybackController owns a small pool, see PlaybackController.h) and one
// short-lived instance is used for thumbnail grabs in the media pool.
//
// Not thread-safe: each Decoder must be used from a single thread.
class Decoder {
public:
    Decoder();
    ~Decoder();
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    bool open(const QString& filePath, QString* errorOut = nullptr);
    void close();
    bool isOpen() const { return m_fmt != nullptr; }

    bool hasVideo() const { return m_videoStreamIndex >= 0; }
    bool hasAudio() const { return m_audioStreamIndex >= 0; }

    // Seeks the underlying demuxer to the nearest keyframe at-or-before `t`
    // and flushes decoder buffers. Call before decodeNext* after a scrub.
    void seek(Ticks t);

    // Pulls the next video frame in decode order. Returns nullopt at EOF.
    std::optional<VideoFrame> decodeNextVideoFrame();

    // Pulls the next audio frame, resampled to `outSampleRate`/stereo S16.
    std::optional<AudioFrame> decodeNextAudioFrame(int outSampleRate = 48000, int outChannels = 2);

    // Convenience for the media pool / timeline thumbnails: seeks to `t`,
    // decodes forward until a video frame is available, and returns it
    // scaled to fit within maxSize as an RGB QImage. Leaves the decoder
    // positioned right after the grabbed frame.
    QImage grabThumbnail(Ticks t, int maxWidth = 160, int maxHeight = 90);

    // Decodes the ENTIRE audio stream once (mono, 48kHz internally) and
    // reduces it to one peak-amplitude value per time bucket, for drawing
    // the timeline waveform. Returns an empty vector if the file has no
    // audio. Like grabThumbnail, this is meant for a short-lived Decoder
    // instance used once at import time — it seeks to 0 first and leaves
    // the decoder positioned at EOF afterwards.
    std::vector<float> computeWaveformPeaks(double bucketsPerSecond = 50.0);

    double frameRate() const { return m_frameRate; }
    int width() const { return m_width; }
    int height() const { return m_height; }

private:
    bool decodeVideoPacket(AVPacket* pkt, std::optional<VideoFrame>& out);
    bool decodeAudioPacket(AVPacket* pkt, int outSampleRate, int outChannels, std::optional<AudioFrame>& out);
    VideoFrame convertFrameToYuv420p(AVFrame* frame);

    AVFormatContext* m_fmt = nullptr;
    AVCodecContext* m_videoCodecCtx = nullptr;
    AVCodecContext* m_audioCodecCtx = nullptr;
    int m_videoStreamIndex = -1;
    int m_audioStreamIndex = -1;

    AVFrame* m_decodedFrame = nullptr;
    AVPacket* m_packet = nullptr;
    SwsContext* m_swsCtx = nullptr;   // video -> YUV420P
    SwrContext* m_swrCtx = nullptr;   // audio resample -> S16 stereo

    double m_frameRate = 0.0;
    int m_width = 0;
    int m_height = 0;
    double m_displayRotationDeg = 0.0;
    // Decided once in open() from the source stream's tagged colorspace
    // (falling back to a resolution heuristic) — see FrameTypes.h's
    // VideoFrame::colorMatrixBt709 for why this needs to match what
    // Exporter's ffmpeg filter graph does for the same source.
    bool m_colorMatrixBt709 = false;
};

} // namespace hc
