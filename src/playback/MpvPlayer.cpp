#include "MpvPlayer.h"
#include <QDebug>
#include <QMetaObject>
#include <clocale>
#include <stdexcept>

namespace hc {

namespace {
void checkError(int status) {
    if (status < 0) {
        qWarning("MpvPlayer: mpv error: %s", mpv_error_string(status));
    }
}
} // namespace

MpvPlayer::MpvPlayer(QObject* parent) : QObject(parent) {
    // Defensive re-assertion: libmpv requires LC_NUMERIC == "C" to parse its
    // own numeric option values correctly. main() already sets this right
    // after constructing QApplication (which is what actually breaks it —
    // its ctor calls setlocale(LC_ALL, "") using the system locale), but
    // various Qt calls (dialogs, QLocale, etc.) can silently reset it again
    // later, so we re-assert it right before touching mpv every time.
    std::setlocale(LC_NUMERIC, "C");

    m_mpv = mpv_create();
    if (!m_mpv) {
        throw std::runtime_error(
            "mpv_create() that thất bại (thường do LC_NUMERIC != \"C\", "
            "thiếu libmpv.so đúng phiên bản, hoặc hết tài nguyên hệ thống)");
    }

    // We render ourselves via the render API in MpvVideoWidget, so the
    // client API must not open its own window.
    mpv_set_option_string(m_mpv, "vo", "libmpv");
    // Audio output is left on mpv's default (auto-selects pulse/alsa/
    // wasapi/coreaudio...) — this is exactly the "audio API" HyggshiCut's
    // own decoder pipeline never wired up for source preview.
    mpv_set_option_string(m_mpv, "keep-open", "yes");
    mpv_set_option_string(m_mpv, "hwdec", "auto-safe");
    mpv_set_option_string(m_mpv, "vd-lavc-dr", "yes");
    mpv_set_option_string(m_mpv, "terminal", "no");
    mpv_set_option_string(m_mpv, "input-default-bindings", "no");
    mpv_set_option_string(m_mpv, "input-vo-keyboard", "no");
    mpv_set_option_string(m_mpv, "osc", "no");

    if (mpv_initialize(m_mpv) < 0) {
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
        throw std::runtime_error("MpvPlayer: mpv_initialize() that thất bại");
    }

    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);

    mpv_set_wakeup_callback(m_mpv, &MpvPlayer::wakeupCallback, this);
}

MpvPlayer::~MpvPlayer() {
    if (m_mpv) {
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
}

void MpvPlayer::loadFile(const QString& path) {
    if (!m_mpv) return;
    const QByteArray utf8 = path.toUtf8();
    const char* args[] = {"loadfile", utf8.constData(), nullptr};
    checkError(mpv_command_async(m_mpv, 0, args));
}

void MpvPlayer::play() {
    if (!m_mpv) return;
    int flag = 0; // pause = false
    checkError(mpv_set_property_async(m_mpv, 0, "pause", MPV_FORMAT_FLAG, &flag));
}

void MpvPlayer::pause() {
    if (!m_mpv) return;
    int flag = 1; // pause = true
    checkError(mpv_set_property_async(m_mpv, 0, "pause", MPV_FORMAT_FLAG, &flag));
}

void MpvPlayer::togglePause() {
    if (m_paused) play(); else pause();
}

void MpvPlayer::seek(double seconds) {
    if (!m_mpv) return;
    const QByteArray posStr = QByteArray::number(seconds, 'f', 3);
    const char* args[] = {"seek", posStr.constData(), "absolute", nullptr};
    checkError(mpv_command_async(m_mpv, 0, args));
}

void MpvPlayer::setVolume(int volume0to100) {
    if (!m_mpv) return;
    double vol = static_cast<double>(volume0to100);
    checkError(mpv_set_property_async(m_mpv, 0, "volume", MPV_FORMAT_DOUBLE, &vol));
}

void MpvPlayer::setMuted(bool muted) {
    if (!m_mpv) return;
    int flag = muted ? 1 : 0;
    checkError(mpv_set_property_async(m_mpv, 0, "mute", MPV_FORMAT_FLAG, &flag));
}

void MpvPlayer::wakeupCallback(void* ctx) {
    // Called from an mpv-internal thread — never touch mpv or Qt objects
    // here directly, just hop back to the Qt event loop.
    QMetaObject::invokeMethod(reinterpret_cast<MpvPlayer*>(ctx), "onMpvEvents", Qt::QueuedConnection);
}

void MpvPlayer::onMpvEvents() {
    if (!m_mpv) return;
    while (true) {
        mpv_event* event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE) break;
        handleEvent(event);
    }
}

void MpvPlayer::handleEvent(mpv_event* event) {
    switch (event->event_id) {
    case MPV_EVENT_PROPERTY_CHANGE: {
        auto* prop = static_cast<mpv_event_property*>(event->data);
        if (!prop || !prop->data) break;
        if (prop->name && strcmp(prop->name, "time-pos") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
            emit positionChanged(*static_cast<double*>(prop->data));
        } else if (prop->name && strcmp(prop->name, "duration") == 0 && prop->format == MPV_FORMAT_DOUBLE) {
            emit durationChanged(*static_cast<double*>(prop->data));
        } else if (prop->name && strcmp(prop->name, "pause") == 0 && prop->format == MPV_FORMAT_FLAG) {
            m_paused = (*static_cast<int*>(prop->data)) != 0;
            emit pausedChanged(m_paused);
        }
        break;
    }
    case MPV_EVENT_END_FILE:
        emit endOfFile();
        break;
    case MPV_EVENT_LOG_MESSAGE: {
        auto* msg = static_cast<mpv_event_log_message*>(event->data);
        qWarning("mpv: %s", msg->text);
        break;
    }
    default:
        break;
    }
}

} // namespace hc
