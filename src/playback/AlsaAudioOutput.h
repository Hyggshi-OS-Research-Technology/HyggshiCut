#pragma once
#include <cstddef>
#include <cstdint>

// Forward-declared instead of including <alsa/asoundlib.h> here, so that
// pulling in AlsaAudioOutput.h doesn't drag ALSA's C headers (and their
// macro pollution) into every translation unit that includes
// PlaybackController.h.
typedef struct _snd_pcm snd_pcm_t;

namespace hc {

// Minimal direct-ALSA PCM output used for realtime timeline-preview audio.
//
// Why this exists instead of QAudioSink: on this project's target system
// (Qt 6.10.2 + PipeWire 1.6.2), Qt Multimedia's native-pipewire QAudioSink
// backend fails to enumerate ANY audio device — QMediaDevices::audioOutputs()
// comes back empty every time, with
//   spaVisitChoice: parse error "Object: ... Spa:Pod:Object:Param:Format ..."
// printed to stderr under QT_DEBUG_PLUGINS=1. That's a bug in Qt's own SPA
// pod parser used to query device capabilities, not a system audio problem —
// `aplay -L` on the same machine shows a working ALSA PCM named "pipewire"
// (the ALSA-to-PipeWire bridge every PipeWire install ships), completely
// independent of Qt's broken enumeration path. This class opens that PCM
// (or "pulse"/"default" as fallbacks, for portability to machines without
// PipeWire) directly via libasound, sidestepping QMediaDevices entirely.
//
// This is a fixed-format blocking-ish PCM writer: signed 16-bit interleaved
// only, at whatever sampleRate/channels the caller requests — it does not
// negotiate or resample. PlaybackController's mixer already produces exactly
// that layout (see kAudioSampleRate/kAudioChannels there), so no conversion
// is needed on the caller's side.
class AlsaAudioOutput {
public:
    AlsaAudioOutput();
    ~AlsaAudioOutput();

    AlsaAudioOutput(const AlsaAudioOutput&) = delete;
    AlsaAudioOutput& operator=(const AlsaAudioOutput&) = delete;

    // Tries a short list of well-known ALSA PCM names ("pipewire", "pulse",
    // "default", in that order) and opens the first one that supports
    // exactly sampleRate/channels/S16_LE. Logs why each candidate failed.
    // Returns false if none worked — caller falls back to silent preview,
    // same as the old QAudioSink path did.
    bool openBestAvailable(unsigned sampleRate, unsigned channels);

    void close();
    bool isOpen() const { return m_pcm != nullptr; }

    // How many frames can currently be written without blocking. Mirrors
    // QAudioSink::bytesFree()'s role in the old feedAudio() gating logic —
    // callers should check this before writing a chunk so a full ALSA ring
    // buffer never stalls the UI thread. Returns -1 on error.
    long framesAvailable();

    // Writes exactly `numFrames` interleaved frames (numFrames * channels
    // int16_t samples) from `data`. Internally retries on recoverable
    // errors (buffer underrun / device suspend). Returns false on an
    // unrecoverable error, at which point the caller should stop feeding
    // (device likely disappeared).
    bool write(const int16_t* data, size_t numFrames);

    // Drops any buffered-but-not-yet-played audio and re-arms the stream.
    // Call this on pause/seek so stale audio doesn't play after a scrub,
    // mirroring what QAudioSink::reset()/stop() used to do.
    void dropPending();

private:
    bool tryOpen(const char* deviceName, unsigned sampleRate, unsigned channels);

    snd_pcm_t* m_pcm = nullptr;
    unsigned m_channels = 0;
};

} // namespace hc
