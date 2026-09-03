#pragma once
#include <QString>
#include <cstdint>
#include <deque>
#include <vector>

struct AVFilterGraph;
struct AVFilterContext;

namespace hc {

// Wraps a libavfilter graph ("abuffer" -> user filter string -> "abuffersink")
// that processes a continuous stream of interleaved S16 stereo PCM in small
// chunks, for real-time audio preview. This is the preview-side counterpart
// to Exporter's ffmpeg CLI filter_complex — both are built from the exact
// same filter string (see audio/AudioFilterDesc.h), so what you hear while
// scrubbing the timeline matches what ends up in the exported file.
//
// Filters like afftdn (FFT-based) and acompressor (look-ahead) don't emit
// output samples at a fixed 1-in-1-out rate — they buffer internally. This
// class absorbs that by keeping its own output queue: process() always
// returns exactly the number of frames requested. Until the graph has
// accumulated enough filtered output to satisfy a request, it returns
// silence for that call instead of blocking — once the graph reaches
// steady state (typically well under a second) every call after that
// returns real filtered audio. The only user-visible effect is a small,
// fixed startup latency on clips that have a filter enabled (unfiltered
// clips are unaffected; PlaybackController skips this class for them).
class AudioFilterChain {
public:
    // filterDescription must be a non-empty ffmpeg audio filter chain (see
    // buildAudioFilterDescription) — construct one of these only when a
    // clip actually has non-default AudioFilterSettings.
    AudioFilterChain(const QString& filterDescription, int sampleRate, int channels);
    ~AudioFilterChain();

    AudioFilterChain(const AudioFilterChain&) = delete;
    AudioFilterChain& operator=(const AudioFilterChain&) = delete;

    bool isValid() const { return m_valid; }
    // The description this chain was built from — PlaybackController uses
    // this to detect when a clip's filter settings changed and the chain
    // needs to be rebuilt from scratch.
    const QString& description() const { return m_description; }

    // Pushes `frames` interleaved S16 stereo samples (frames*channels
    // int16_t values) through the graph and returns exactly `frames` worth
    // of filtered output (same shape), per the latency note above.
    std::vector<int16_t> process(const int16_t* interleaved, size_t frames);

private:
    QString m_description;
    int m_sampleRate = 48000;
    int m_channels = 2;
    bool m_valid = false;

    AVFilterGraph* m_graph = nullptr;
    AVFilterContext* m_srcCtx = nullptr;
    AVFilterContext* m_sinkCtx = nullptr;

    std::deque<int16_t> m_outputQueue; // interleaved, drained by process()
    int64_t m_nextPts = 0;
};

} // namespace hc
