#include "MediaAsset.h"
#include <QUuid>
#include <QFileInfo>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

namespace hc {

MediaAssetPtr MediaAsset::probe(const QString& filePath, QString* errorOut) {
    AVFormatContext* fmt = nullptr;
    const QByteArray pathUtf8 = filePath.toUtf8();

    if (avformat_open_input(&fmt, pathUtf8.constData(), nullptr, nullptr) < 0) {
        if (errorOut) *errorOut = QStringLiteral("Không mở được file: %1").arg(filePath);
        return nullptr;
    }

    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        if (errorOut) *errorOut = QStringLiteral("Không đọc được thông tin stream: %1").arg(filePath);
        avformat_close_input(&fmt);
        return nullptr;
    }

    auto asset = std::make_shared<MediaAsset>();
    asset->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    asset->filePath = filePath;
    asset->displayName = QFileInfo(filePath).fileName();

    // Overall container duration (fallback to the longest stream if unknown).
    if (fmt->duration != AV_NOPTS_VALUE) {
        asset->duration = fmt->duration; // already in AV_TIME_BASE (microseconds)
    }

    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        AVStream* stream = fmt->streams[i];
        AVCodecParameters* params = stream->codecpar;

        if (params->codec_type == AVMEDIA_TYPE_VIDEO && asset->videoStreamIndex < 0) {
            // Skip embedded cover-art "video" streams (attached_pic) — treat as image only.
            if (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) {
                continue;
            }
            asset->videoStreamIndex = static_cast<int>(i);
            asset->width = params->width;
            asset->height = params->height;
            if (stream->avg_frame_rate.num > 0 && stream->avg_frame_rate.den > 0) {
                asset->frameRate = av_q2d(stream->avg_frame_rate);
            } else if (stream->r_frame_rate.num > 0 && stream->r_frame_rate.den > 0) {
                asset->frameRate = av_q2d(stream->r_frame_rate);
            }
            if (stream->duration != AV_NOPTS_VALUE) {
                const Ticks streamTicks = av_rescale_q(stream->duration, stream->time_base,
                                                        AVRational{1, AV_TIME_BASE});
                asset->duration = std::max(asset->duration, streamTicks);
            }
        } else if (params->codec_type == AVMEDIA_TYPE_AUDIO && asset->audioStreamIndex < 0) {
            asset->audioStreamIndex = static_cast<int>(i);
            asset->sampleRate = params->sample_rate;
            asset->channels = params->ch_layout.nb_channels;
            int64_t br = params->bit_rate;
            if (br <= 0 && stream->codecpar->bit_rate > 0) br = stream->codecpar->bit_rate;
            if (br <= 0 && fmt->bit_rate > 0) br = fmt->bit_rate;
            asset->bitRate = br;
            if (stream->duration != AV_NOPTS_VALUE) {
                const Ticks streamTicks = av_rescale_q(stream->duration, stream->time_base,
                                                        AVRational{1, AV_TIME_BASE});
                asset->duration = std::max(asset->duration, streamTicks);
            }
        }
    }

    if (asset->bitRate <= 0 && fmt->bit_rate > 0) {
        asset->bitRate = fmt->bit_rate;
    }
    if (asset->bitRate <= 0 && asset->duration > 0) {
        const int64_t fileSize = QFileInfo(filePath).size();
        if (fileSize > 0) {
            const double sec = ticksToSeconds(asset->duration);
            if (sec > 0.0) {
                asset->bitRate = static_cast<int64_t>(std::round((fileSize * 8.0) / sec));
            }
        }
    }

    bool isImageFormat = false;
    if (asset->hasVideo()) {
        AVStream* vStream = fmt->streams[asset->videoStreamIndex];
        const AVCodecID codecId = vStream->codecpar->codec_id;
        const QString ext = QFileInfo(filePath).suffix().toLower();
        const QString fmtName = fmt->iformat ? QString::fromLatin1(fmt->iformat->name).toLower() : QString();

        if (codecId == AV_CODEC_ID_PNG || codecId == AV_CODEC_ID_MJPEG ||
            codecId == AV_CODEC_ID_BMP || codecId == AV_CODEC_ID_WEBP ||
            codecId == AV_CODEC_ID_TIFF || codecId == AV_CODEC_ID_GIF ||
            codecId == AV_CODEC_ID_TARGA || codecId == AV_CODEC_ID_SVG ||
            codecId == AV_CODEC_ID_JPEG2000 || codecId == AV_CODEC_ID_JPEGLS ||
            codecId == AV_CODEC_ID_QOI || codecId == AV_CODEC_ID_PAM ||
            codecId == AV_CODEC_ID_PBM || codecId == AV_CODEC_ID_PGM ||
            codecId == AV_CODEC_ID_PPM || codecId == AV_CODEC_ID_XBM ||
            codecId == AV_CODEC_ID_XPM || codecId == AV_CODEC_ID_XWD ||
            fmtName.contains("image2") || fmtName.contains("png") || fmtName.contains("jpeg") ||
            ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "webp" ||
            ext == "bmp" || ext == "tif" || ext == "tiff" || ext == "tga" ||
            ext == "gif" || ext == "svg") {
            isImageFormat = true;
        }
    }

    if (isImageFormat) {
        asset->kind = MediaKind::Image;
        asset->frameRate = 0.0;
        asset->duration = 0; // Still images have no fixed duration limit
    } else if (asset->hasVideo()) {
        asset->kind = MediaKind::Video;
    } else if (asset->hasAudio()) {
        asset->kind = MediaKind::Audio;
    }

    avformat_close_input(&fmt);

    if (asset->kind == MediaKind::Unknown) {
        if (errorOut) *errorOut = QStringLiteral("Không tìm thấy video/audio stream hợp lệ: %1").arg(filePath);
        return nullptr;
    }

    return asset;
}

} // namespace hc
