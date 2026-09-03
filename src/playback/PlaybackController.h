#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <memory>
#include <vector>
#include <unordered_map>
#include "../core/Project.h"
#include "../decode/Decoder.h"
#include "../render/TextureCache.h"
#include "../audio/AudioFilterChain.h"
#include "../cache/FrameCache.h"
#include "../cache/ProxyManager.h"
#include "AlsaAudioOutput.h"

namespace hc {

class GLVideoWidget;

// Owns the playback and preview logic for both video and audio.
// Video frames are rendered on GLVideoWidget. Every visual clip (video,
// image, text) that is active at the current time is composited — video
// clips are decoded per-frame via Decoder, while image/text clips use the
// shared TextureCache so their GPU texture is uploaded only once.
// Audio from all active clips across tracks is mixed in real time and
// fed to an AlsaAudioOutput (direct ALSA PCM — see AlsaAudioOutput.h for
// why this doesn't go through Qt Multimedia's QAudioSink).
class PlaybackController : public QObject {
    Q_OBJECT
public:
    // `proxyManager` is optional (nullptr disables proxy-aware decoding
    // entirely) and NOT owned by this class — pass the one MainWindow owns,
    // shared across the whole app session so proxies survive project
    // switches.
    explicit PlaybackController(Project* project, GLVideoWidget* videoOutput,
                                 ProxyManager* proxyManager = nullptr, QObject* parent = nullptr);
    ~PlaybackController() override;

    void play();
    void pause();
    void togglePlay();
    bool isPlaying() const { return m_playing; }

    // Jumps the playhead and immediately redraws the frame under it (scrub).
    void seek(Ticks t);
    Ticks currentTime() const { return m_currentTime; }

    Ticks duration() const { return m_project->timeline().totalDuration(); }

    // Access the texture cache (e.g. for pre-warming or invalidation).
    TextureCache* textureCache() { return &m_textureCache; }

    // When true (default) and a Ready proxy exists for a clip's asset,
    // video decode for PREVIEW uses the proxy file instead of the
    // original. Audio and export are never affected (see ProxyManager.h).
    // Toggling closes and reopens every video decoder so the change takes
    // effect immediately, and drops cached frames from the old source.
    void setUseProxy(bool useProxy);
    bool useProxy() const { return m_useProxy; }

    // Called by MainWindow when the ProxyManager reports a proxy finished
    // generating for `assetId` — reopens that asset's decoder (if proxy
    // use is on) and forces a redraw so the switch is visible right away.
    void onProxyReady(const QString& assetId);

signals:
    void positionChanged(Ticks t);
    void playingChanged(bool playing);
    void audioLevelsChanged(float leftLevel, float rightLevel);

private slots:
    void onTick();

private:
    Decoder* decoderFor(const QString& assetId);
    Decoder* audioDecoderFor(const QString& assetId);
    void renderFrameAt(Ticks t);
    // Path decoderFor() should open for `assetId` right now: the proxy file
    // if proxy use is on and one is Ready, otherwise the original source.
    QString videoSourcePathFor(const QString& assetId) const;

    void initAudio();
    void feedAudio();
    void resetAudioState();
    void pullAudioSamples(const QString& assetId, Ticks srcTimeStart, size_t numFrames, std::vector<int16_t>& out);

    Project* m_project;
    GLVideoWidget* m_videoOutput;
    QTimer m_timer;
    // Feeds the ALSA ring buffer on its own short, fixed-interval cadence,
    // independent of the video frame timer. Audio and video decode/render
    // share this thread; if audio were only fed once per video tick (as it
    // used to be), a single slow video frame is enough to starve the ALSA
    // buffer and produce an audible crackle. A faster, decoupled timer
    // gives feedAudio() more, smaller opportunities to top the buffer back
    // up rather than depending on the video cadence to also be the right
    // cadence for audio.
    QTimer m_audioTimer;
    QElapsedTimer m_wallClock;
    Ticks m_currentTime = 0;
    Ticks m_playStartTimelineTime = 0;
    bool m_playing = false;

    // Video decoders pool keyed by MediaAsset id.
    std::unordered_map<QString, std::unique_ptr<Decoder>> m_decoders;
    // Which path (original or proxy) each currently-open decoder in
    // m_decoders was opened against — lets decoderFor() detect "the proxy
    // for this asset just became ready, but the open decoder is still on
    // the original file" without needing an explicit invalidation call
    // from every caller.
    std::unordered_map<QString, QString> m_decoderOpenPath;
    std::unordered_map<QString, Ticks> m_lastDecodedSourceTime;

    // Shared texture cache for image/text clips.
    TextureCache m_textureCache;

    // Decoded-frame LRU cache shared across all video clips, keyed by
    // (assetId, frame index). Checked before every seek+decode in
    // renderFrameAt() so revisiting a recently-seen timeline position (the
    // common case while trimming/scrubbing) skips FFmpeg entirely.
    FrameCache m_frameCache;

    ProxyManager* m_proxyManager = nullptr; // not owned
    bool m_useProxy = true;

    // Audio playback engine — direct ALSA output, not QAudioSink (see
    // AlsaAudioOutput.h for why).
    std::unique_ptr<AlsaAudioOutput> m_audioOutput;
    Ticks m_audioTimelineTime = 0;
    static constexpr int kAudioSampleRate = 48000;
    static constexpr int kAudioChannels = 2;

    // Audio decoders pool keyed by MediaAsset id.
    std::unordered_map<QString, std::unique_ptr<Decoder>> m_audioDecoders;
    struct AudioBufferEntry {
        Ticks pts = 0;
        std::vector<int16_t> samples;
        size_t readOffset = 0;
    };
    std::unordered_map<QString, std::vector<AudioBufferEntry>> m_audioBuffers;
    std::unordered_map<QString, Ticks> m_audioDecoderPts;

    // Per-clip EQ/denoise/compressor graphs (see audio/AudioFilterChain).
    // Keyed by CLIP id, not asset id — two clips of the same source file
    // can have different filter settings. Only clips whose
    // AudioFilterSettings isn't default get an entry here; everything else
    // skips this path entirely (no libavfilter overhead for the common
    // no-filter case). Rebuilt whenever a clip's filter description string
    // changes (see feedAudio()).
    std::unordered_map<QString, std::unique_ptr<AudioFilterChain>> m_audioFilterChains;
};

} // namespace hc
