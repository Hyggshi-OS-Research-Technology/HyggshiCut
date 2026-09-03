#include "Decoder.h"
#include <QDebug>
#include <QThread>
#include <cmath>
#include <cstring>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/channel_layout.h>
#include <libavutil/display.h>
#include <libavutil/dict.h>
}

namespace hc {

namespace {
double normalizeDisplayRotation(double degrees) {
    if (!std::isfinite(degrees)) return 0.0;
    // Container matrices are commonly exact multiples of 90 degrees.
    // Snap tiny floating-point errors so the compositor gets stable values.
    double r = std::fmod(degrees, 360.0);
    if (r < -180.0) r += 360.0;
    if (r > 180.0) r -= 360.0;
    const double snapped = std::round(r / 90.0) * 90.0;
    if (std::abs(r - snapped) < 1.0) r = snapped;
    return r;
}
}

Decoder::Decoder() {
    m_decodedFrame = av_frame_alloc();
    m_packet = av_packet_alloc();
}

Decoder::~Decoder() {
    close();
    av_frame_free(&m_decodedFrame);
    av_packet_free(&m_packet);
}

void Decoder::close() {
    if (m_swsCtx) { sws_freeContext(m_swsCtx); m_swsCtx = nullptr; }
    if (m_swrCtx) { swr_free(&m_swrCtx); }
    if (m_videoCodecCtx) { avcodec_free_context(&m_videoCodecCtx); }
    if (m_audioCodecCtx) { avcodec_free_context(&m_audioCodecCtx); }
    if (m_fmt) { avformat_close_input(&m_fmt); }
    m_videoStreamIndex = -1;
    m_audioStreamIndex = -1;
    m_frameRate = 0.0;
    m_width = m_height = 0;
    m_displayRotationDeg = 0.0;
    m_colorMatrixBt709 = false;
}

bool Decoder::open(const QString& filePath, QString* errorOut) {
    close();

    const QByteArray pathUtf8 = filePath.toUtf8();
    if (avformat_open_input(&m_fmt, pathUtf8.constData(), nullptr, nullptr) < 0) {
        if (errorOut) *errorOut = QStringLiteral("avformat_open_input thất bại: %1").arg(filePath);
        return false;
    }
    if (avformat_find_stream_info(m_fmt, nullptr) < 0) {
        if (errorOut) *errorOut = QStringLiteral("avformat_find_stream_info thất bại");
        close();
        return false;
    }

    m_videoStreamIndex = av_find_best_stream(m_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    m_audioStreamIndex = av_find_best_stream(m_fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    if (m_videoStreamIndex >= 0) {
        AVStream* stream = m_fmt->streams[m_videoStreamIndex];
        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!codec) {
            if (errorOut) *errorOut = QStringLiteral("Không tìm thấy video decoder");
            close();
            return false;
        }
        m_videoCodecCtx = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(m_videoCodecCtx, stream->codecpar);
        // Multi-threaded decode: big win for 1080p/4K H.264/HEVC preview scrubbing.
        const int idealThreads = QThread::idealThreadCount();
        m_videoCodecCtx->thread_count = std::clamp(idealThreads / 2, 1, 4);
        m_videoCodecCtx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
        if (avcodec_open2(m_videoCodecCtx, codec, nullptr) < 0) {
            if (errorOut) *errorOut = QStringLiteral("avcodec_open2 (video) thất bại");
            close();
            return false;
        }
        m_width = m_videoCodecCtx->width;
        m_height = m_videoCodecCtx->height;

        // Decide once which YUV->RGB matrix this source's raw planes were
        // encoded with — the GL preview shader needs to pick the same one
        // ffmpeg does when converting for export, or colors visibly shift
        // between Preview and Export for any non-BT.601 source (most HD
        // footage). Prefer an explicit color_space tag; when the source
        // doesn't carry one (very common), fall back to the same
        // resolution heuristic ffmpeg/swscale use: SD content -> BT.601,
        // taller-than-SD content -> BT.709.
        switch (stream->codecpar->color_space) {
            case AVCOL_SPC_BT709:
                m_colorMatrixBt709 = true;
                break;
            case AVCOL_SPC_SMPTE170M:
            case AVCOL_SPC_BT470BG:
            case AVCOL_SPC_SMPTE240M:
                m_colorMatrixBt709 = false;
                break;
            default:
                m_colorMatrixBt709 = (m_height > 576);
                break;
        }

        // Display rotation metadata (smartphones / action cams).
        // Try the modern side-data display matrix first.
        for (int i = 0; i < stream->codecpar->nb_coded_side_data; ++i) {
            const AVPacketSideData& sd = stream->codecpar->coded_side_data[i];
            if (sd.type == AV_PKT_DATA_DISPLAYMATRIX && sd.size >= static_cast<size_t>(sizeof(int32_t) * 9)) {
                const double angle = av_display_rotation_get(reinterpret_cast<const int32_t*>(sd.data));
                if (std::isfinite(angle)) m_displayRotationDeg = normalizeDisplayRotation(-angle);
                break;
            }
        }
        // Fallback for files where rotation is stored only as a stream-level metadata tag.
        if (std::abs(m_displayRotationDeg) < 0.01 && stream->metadata) {
            AVDictionaryEntry* rotateTag = av_dict_get(stream->metadata, "rotate", nullptr, 0);
            if (rotateTag) {
                bool ok = false;
                const double rotation = QString::fromUtf8(rotateTag->value).toDouble(&ok);
                if (ok && std::isfinite(rotation)) m_displayRotationDeg = normalizeDisplayRotation(rotation);
            }
        }
        // Some files/containers expose the legacy rotate tag at the format
        // level instead of the video stream level. Use it as a final fallback.
        if (std::abs(m_displayRotationDeg) < 0.01 && m_fmt) {
            AVDictionaryEntry* rotateTag = av_dict_get(m_fmt->metadata, "rotate", nullptr, 0);
            if (rotateTag) {
                bool ok = false;
                const double rotation = QString::fromUtf8(rotateTag->value).toDouble(&ok);
                if (ok && std::isfinite(rotation)) m_displayRotationDeg = normalizeDisplayRotation(rotation);
            }
        }
        qInfo() << "HyggshiCut: video display rotation =" << m_displayRotationDeg << "deg";

        if (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0) {
            m_frameRate = av_q2d(stream->avg_frame_rate);
        } else if (stream->r_frame_rate.num > 0 && stream->r_frame_rate.den > 0) {
            m_frameRate = av_q2d(stream->r_frame_rate);
        }
    }

    if (m_audioStreamIndex >= 0) {
        AVStream* stream = m_fmt->streams[m_audioStreamIndex];
        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (codec) {
            m_audioCodecCtx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(m_audioCodecCtx, stream->codecpar);
            if (avcodec_open2(m_audioCodecCtx, codec, nullptr) < 0) {
                avcodec_free_context(&m_audioCodecCtx);
                m_audioStreamIndex = -1;
            }
        } else {
            m_audioStreamIndex = -1;
        }
    }

    if (m_videoStreamIndex < 0 && m_audioStreamIndex < 0) {
        if (errorOut) *errorOut = QStringLiteral("File không có stream video/audio hợp lệ");
        close();
        return false;
    }
    return true;
}

void Decoder::seek(Ticks t) {
    if (!m_fmt) return;
    if (t < 0) t = 0;
    av_seek_frame(m_fmt, -1, t, AVSEEK_FLAG_BACKWARD);
    if (m_videoCodecCtx) avcodec_flush_buffers(m_videoCodecCtx);
    if (m_audioCodecCtx) avcodec_flush_buffers(m_audioCodecCtx);
    if (m_swrCtx) { swr_free(&m_swrCtx); m_swrCtx = nullptr; }
}

VideoFrame Decoder::convertFrameToYuv420p(AVFrame* frame) {
    VideoFrame out;
    out.width = frame->width;
    out.height = frame->height;
    out.displayRotationDeg = m_displayRotationDeg;
    out.colorMatrixBt709 = m_colorMatrixBt709;

    AVStream* stream = m_fmt->streams[m_videoStreamIndex];
    const int64_t bestPts = (frame->pts != AV_NOPTS_VALUE) ? frame->pts : frame->pkt_dts;
    out.pts = (bestPts == AV_NOPTS_VALUE)
                  ? 0
                  : av_rescale_q(bestPts, stream->time_base, AVRational{1, AV_TIME_BASE});

    out.strideY = frame->width;
    out.strideU = out.strideV = (frame->width + 1) / 2;
    const int chromaHeight = (frame->height + 1) / 2;

    out.y.resize(static_cast<size_t>(out.strideY) * frame->height);
    out.u.resize(static_cast<size_t>(out.strideU) * chromaHeight);
    out.v.resize(static_cast<size_t>(out.strideV) * chromaHeight);

    // Fast path: If the decoded frame is already native YUV420P, copy directly row-by-row
    // without invoking sws_scale. This eliminates significant CPU conversion overhead.
    if (frame->format == AV_PIX_FMT_YUV420P) {
        const int yH = frame->height;
        for (int r = 0; r < yH; ++r) {
            std::memcpy(out.y.data() + r * out.strideY, frame->data[0] + r * frame->linesize[0], frame->width);
        }
        for (int r = 0; r < chromaHeight; ++r) {
            std::memcpy(out.u.data() + r * out.strideU, frame->data[1] + r * frame->linesize[1], out.strideU);
            std::memcpy(out.v.data() + r * out.strideV, frame->data[2] + r * frame->linesize[2], out.strideV);
        }
        return out;
    }

    m_swsCtx = sws_getCachedContext(
        m_swsCtx,
        frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
        frame->width, frame->height, AV_PIX_FMT_YUV420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    uint8_t* dstData[4] = { out.y.data(), out.u.data(), out.v.data(), nullptr };
    int dstLinesize[4] = { out.strideY, out.strideU, out.strideV, 0 };

    sws_scale(m_swsCtx, frame->data, frame->linesize, 0, frame->height, dstData, dstLinesize);
    return out;
}

// Pumps the demuxer, feeding packets to whichever codec they belong to,
// until a frame of `wantVideo` type is available (or EOF is fully drained).
// Packets for the *other* stream type are still sent to their codec so
// its internal buffer keeps advancing and a later decodeNext*Frame call
// on that stream can receive them without re-reading the file.
static bool pumpDemuxer(AVFormatContext* fmt, AVPacket* pkt,
                         AVCodecContext* videoCtx, int videoStreamIndex,
                         AVCodecContext* audioCtx, int audioStreamIndex,
                         bool wantVideo, AVFrame* outFrame) {
    AVCodecContext* wantCtx = wantVideo ? videoCtx : audioCtx;
    if (!wantCtx) return false;

    // First, see if a frame is already buffered from a previous pump.
    if (avcodec_receive_frame(wantCtx, outFrame) == 0) return true;

    while (true) {
        const int readResult = av_read_frame(fmt, pkt);
        if (readResult < 0) {
            // EOF: flush both codecs and try one last receive.
            if (videoCtx) avcodec_send_packet(videoCtx, nullptr);
            if (audioCtx) avcodec_send_packet(audioCtx, nullptr);
            return avcodec_receive_frame(wantCtx, outFrame) == 0;
        }

        if (pkt->stream_index == videoStreamIndex && videoCtx) {
            avcodec_send_packet(videoCtx, pkt);
        } else if (pkt->stream_index == audioStreamIndex && audioCtx) {
            avcodec_send_packet(audioCtx, pkt);
        }
        av_packet_unref(pkt);

        if (avcodec_receive_frame(wantCtx, outFrame) == 0) return true;
    }
}

std::optional<VideoFrame> Decoder::decodeNextVideoFrame() {
    if (!m_videoCodecCtx) return std::nullopt;
    if (!pumpDemuxer(m_fmt, m_packet, m_videoCodecCtx, m_videoStreamIndex,
                     m_audioCodecCtx, m_audioStreamIndex, /*wantVideo=*/true, m_decodedFrame)) {
        return std::nullopt;
    }
    VideoFrame vf = convertFrameToYuv420p(m_decodedFrame);
    av_frame_unref(m_decodedFrame);
    return vf;
}

std::optional<AudioFrame> Decoder::decodeNextAudioFrame(int outSampleRate, int outChannels) {
    if (!m_audioCodecCtx) return std::nullopt;
    if (!pumpDemuxer(m_fmt, m_packet, m_videoCodecCtx, m_videoStreamIndex,
                     m_audioCodecCtx, m_audioStreamIndex, /*wantVideo=*/false, m_decodedFrame)) {
        return std::nullopt;
    }

    AVFrame* frame = m_decodedFrame;
    AVStream* stream = m_fmt->streams[m_audioStreamIndex];

    if (!m_swrCtx) {
        AVChannelLayout outLayout;
        av_channel_layout_default(&outLayout, outChannels);
        swr_alloc_set_opts2(&m_swrCtx,
                             &outLayout, AV_SAMPLE_FMT_S16, outSampleRate,
                             &frame->ch_layout, static_cast<AVSampleFormat>(frame->format), frame->sample_rate,
                             0, nullptr);
        swr_init(m_swrCtx);
        av_channel_layout_uninit(&outLayout);
    }

    const int64_t maxOutSamples = av_rescale_rnd(
        swr_get_delay(m_swrCtx, frame->sample_rate) + frame->nb_samples,
        outSampleRate, frame->sample_rate, AV_ROUND_UP);

    AudioFrame out;
    out.sampleRate = outSampleRate;
    out.channels = outChannels;
    out.samples.resize(static_cast<size_t>(maxOutSamples) * outChannels);

    uint8_t* outPtr = reinterpret_cast<uint8_t*>(out.samples.data());
    const int converted = swr_convert(m_swrCtx, &outPtr, static_cast<int>(maxOutSamples),
                                       const_cast<const uint8_t**>(frame->data), frame->nb_samples);

    const int64_t bestPts = (frame->pts != AV_NOPTS_VALUE) ? frame->pts : frame->pkt_dts;
    out.pts = (bestPts == AV_NOPTS_VALUE)
                  ? 0
                  : av_rescale_q(bestPts, stream->time_base, AVRational{1, AV_TIME_BASE});

    av_frame_unref(m_decodedFrame);

    if (converted <= 0) return std::nullopt;
    out.samples.resize(static_cast<size_t>(converted) * outChannels);
    return out;
}

QImage Decoder::grabThumbnail(Ticks t, int maxWidth, int maxHeight) {
    if (!hasVideo()) return {};
    seek(t);
    auto frame = decodeNextVideoFrame();
    if (!frame || !frame->isValid()) return {};

    // Manual BT.601 YUV420P -> RGB888 conversion (thumbnail only; GLVideoWidget
    // does the equivalent conversion on the GPU via shader for live playback).
    QImage rgb(frame->width, frame->height, QImage::Format_RGB888);
    for (int y = 0; y < frame->height; ++y) {
        const uint8_t* yRow = frame->y.data() + y * frame->strideY;
        const uint8_t* uRow = frame->u.data() + (y / 2) * frame->strideU;
        const uint8_t* vRow = frame->v.data() + (y / 2) * frame->strideV;
        uchar* dst = rgb.scanLine(y);
        for (int x = 0; x < frame->width; ++x) {
            const int Y = yRow[x];
            const int U = uRow[x / 2] - 128;
            const int V = vRow[x / 2] - 128;
            const int r = qBound(0, static_cast<int>(Y + 1.402 * V), 255);
            const int g = qBound(0, static_cast<int>(Y - 0.344136 * U - 0.714136 * V), 255);
            const int b = qBound(0, static_cast<int>(Y + 1.772 * U), 255);
            dst[x * 3 + 0] = static_cast<uchar>(r);
            dst[x * 3 + 1] = static_cast<uchar>(g);
            dst[x * 3 + 2] = static_cast<uchar>(b);
        }
    }
    return rgb.scaled(maxWidth, maxHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

std::vector<float> Decoder::computeWaveformPeaks(double bucketsPerSecond) {
    std::vector<float> peaks;
    if (!hasAudio() || bucketsPerSecond <= 0.0) return peaks;

    seek(0);

    // Mono is enough for a level display, and halves the decode work
    // compared to resampling to stereo just to throw one channel away.
    constexpr int kSampleRate = 48000;
    constexpr int kChannels = 1;

    while (true) {
        auto frame = decodeNextAudioFrame(kSampleRate, kChannels);
        if (!frame || !frame->isValid()) break; // EOF

        const double samplePeriodUs = 1'000'000.0 / static_cast<double>(frame->sampleRate);
        for (size_t i = 0; i < frame->samples.size(); ++i) {
            const double sampleTimeUs = static_cast<double>(frame->pts) +
                                         static_cast<double>(i) * samplePeriodUs;
            const auto bucket = static_cast<size_t>((sampleTimeUs / 1'000'000.0) * bucketsPerSecond);
            if (bucket >= peaks.size()) peaks.resize(bucket + 1, 0.0f);
            const float amp = std::abs(static_cast<float>(frame->samples[i])) / 32768.0f;
            if (amp > peaks[bucket]) peaks[bucket] = amp;
        }
    }
    return peaks;
}

} // namespace hc
