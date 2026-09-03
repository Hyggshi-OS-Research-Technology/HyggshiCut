#include "AlsaAudioOutput.h"
#include <alsa/asoundlib.h>
#include <QDebug>

namespace hc {

AlsaAudioOutput::AlsaAudioOutput() = default;

AlsaAudioOutput::~AlsaAudioOutput() {
    close();
}

bool AlsaAudioOutput::tryOpen(const char* deviceName, unsigned sampleRate, unsigned channels) {
    snd_pcm_t* pcm = nullptr;
    int err = snd_pcm_open(&pcm, deviceName, SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if (err < 0) {
        qWarning("AlsaAudioOutput: khong mo duoc PCM '%s': %s", deviceName, snd_strerror(err));
        return false;
    }

    snd_pcm_hw_params_t* hwParams = nullptr;
    snd_pcm_hw_params_alloca(&hwParams);

    bool ok = true;
    const char* step = "any";
    snd_pcm_uframes_t periodSize = sampleRate / 50; // ~20ms periods
    do {
        if ((err = snd_pcm_hw_params_any(pcm, hwParams)) < 0) break;

        step = "access";
        if ((err = snd_pcm_hw_params_set_access(pcm, hwParams, SND_PCM_ACCESS_RW_INTERLEAVED)) < 0) break;

        step = "format";
        if ((err = snd_pcm_hw_params_set_format(pcm, hwParams, SND_PCM_FORMAT_S16_LE)) < 0) break;

        step = "channels";
        if ((err = snd_pcm_hw_params_set_channels(pcm, hwParams, channels)) < 0) break;

        step = "rate";
        unsigned actualRate = sampleRate;
        if ((err = snd_pcm_hw_params_set_rate_near(pcm, hwParams, &actualRate, nullptr)) < 0) break;
        if (actualRate != sampleRate) {
            // The mixer in PlaybackController produces PCM at a fixed rate
            // (kAudioSampleRate). Accepting a different device rate here
            // would play back at the wrong speed/pitch instead of actually
            // fixing anything, so treat a rate mismatch as a hard failure
            // and let the caller try the next candidate device.
            qWarning("AlsaAudioOutput: '%s' chi ho tro %u Hz, can dung %u Hz - bo qua.",
                     deviceName, actualRate, sampleRate);
            ok = false;
            break;
        }

        // Explicit period size, not just an overall buffer size: this is
        // what actually determines how much slack we have against a single
        // late/slow feedAudio() call before the device runs dry and clicks.
        // Left to defaults, the pipewire ALSA bridge plugin has been
        // observed to negotiate an oddly small period count, which shows up
        // as audible crackle under normal playback load (video decode
        // sharing the same thread as audio feeding).
        step = "period_size";
        if ((err = snd_pcm_hw_params_set_period_size_near(pcm, hwParams, &periodSize, nullptr)) < 0) break;

        step = "buffer_size";
        // ~500ms total, generous headroom: feedAudio() runs on the same
        // thread as video decode/render, so it can occasionally be a tick
        // late. A bigger ring buffer absorbs that without underrunning;
        // the start_threshold below (one period) keeps startup latency low
        // despite the larger buffer.
        snd_pcm_uframes_t bufferSize = sampleRate / 2;
        if ((err = snd_pcm_hw_params_set_buffer_size_near(pcm, hwParams, &bufferSize)) < 0) break;

        step = "apply";
        if ((err = snd_pcm_hw_params(pcm, hwParams)) < 0) break;

        // Software params: start playback as soon as one period's worth of
        // audio is queued, rather than ALSA's default of waiting for the
        // entire (now much bigger) buffer to fill first. Without this,
        // growing the buffer above would trade crackle for a ~500ms delay
        // before any sound is heard after pressing play/seeking.
        {
            snd_pcm_sw_params_t* swParams = nullptr;
            snd_pcm_sw_params_alloca(&swParams);
            if (snd_pcm_sw_params_current(pcm, swParams) == 0) {
                snd_pcm_sw_params_set_start_threshold(pcm, swParams, periodSize);
                snd_pcm_sw_params_set_avail_min(pcm, swParams, periodSize);
                if (int swErr = snd_pcm_sw_params(pcm, swParams); swErr < 0) {
                    qWarning("AlsaAudioOutput: '%s' ap dung sw_params that bai (khong nghiem trong): %s",
                             deviceName, snd_strerror(swErr));
                }
            }
        }

        step = "prepare";
        if ((err = snd_pcm_prepare(pcm)) < 0) break;

        qInfo("AlsaAudioOutput: da mo PCM '%s' (%u Hz, %u kenh, period=%lu frames, buffer=%lu frames) "
              "truc tiep qua ALSA (bypass Qt Multimedia audio backend).",
              deviceName, actualRate, channels, static_cast<unsigned long>(periodSize),
              static_cast<unsigned long>(bufferSize));
        m_pcm = pcm;
        m_channels = channels;
        return true;
    } while (false);

    if (!ok) {
        // Rate mismatch case already logged above with its own message.
    } else {
        qWarning("AlsaAudioOutput: cau hinh PCM '%s' that bai o buoc '%s': %s",
                 deviceName, step, snd_strerror(err));
    }
    snd_pcm_close(pcm);
    return false;
}

bool AlsaAudioOutput::openBestAvailable(unsigned sampleRate, unsigned channels) {
    close();
    // "pipewire": the ALSA-to-PipeWire bridge PCM every PipeWire install
    // ships (confirmed present via `aplay -L` on the target system).
    // "pulse"/"default": fallbacks for machines running plain PulseAudio
    // or bare ALSA instead, so this isn't PipeWire-specific.
    static const char* kCandidates[] = {"pipewire", "pulse", "default"};
    for (const char* name : kCandidates) {
        if (tryOpen(name, sampleRate, channels)) return true;
    }
    qWarning("AlsaAudioOutput: khong tim thay ALSA PCM output nao hop le "
             "- preview se khong co tieng, nhung video van chay binh thuong.");
    return false;
}

void AlsaAudioOutput::close() {
    if (m_pcm) {
        snd_pcm_drop(m_pcm);
        snd_pcm_close(m_pcm);
        m_pcm = nullptr;
    }
    m_channels = 0;
}

long AlsaAudioOutput::framesAvailable() {
    if (!m_pcm) return -1;
    snd_pcm_sframes_t avail = snd_pcm_avail_update(m_pcm);
    // avail_update() can itself report an xrun/suspend via a negative
    // return; recover once and re-check rather than reporting a bogus
    // negative "space available" up to the caller.
    if (avail < 0) {
        if (snd_pcm_recover(m_pcm, static_cast<int>(avail), 1) < 0) return -1;
        avail = snd_pcm_avail_update(m_pcm);
    }
    return static_cast<long>(avail);
}

bool AlsaAudioOutput::write(const int16_t* data, size_t numFrames) {
    if (!m_pcm || numFrames == 0) return m_pcm != nullptr;

    size_t framesLeft = numFrames;
    const int16_t* cursor = data;
    // Bounded retry count: this is called from the UI-thread playback
    // timer, so we must not spin forever on a device that's stuck.
    int retriesLeft = 8;
    while (framesLeft > 0 && retriesLeft-- > 0) {
        snd_pcm_sframes_t written = snd_pcm_writei(m_pcm, cursor, framesLeft);
        if (written < 0) {
            if (written == -EAGAIN) {
                // Ring buffer briefly full even though framesAvailable()
                // suggested otherwise (can happen under scheduling jitter).
                // Not fatal — caller will pick up the rest next tick.
                return true;
            }
            written = snd_pcm_recover(m_pcm, static_cast<int>(written), 1);
            if (written < 0) {
                qWarning("AlsaAudioOutput: ghi PCM loi khong the phuc hoi: %s",
                         snd_strerror(static_cast<int>(written)));
                return false;
            }
            continue; // retry the write after recovering
        }
        cursor += static_cast<size_t>(written) * m_channels;
        framesLeft -= static_cast<size_t>(written);
    }
    return framesLeft == 0;
}

void AlsaAudioOutput::dropPending() {
    if (!m_pcm) return;
    snd_pcm_drop(m_pcm);
    snd_pcm_prepare(m_pcm);
}

} // namespace hc
