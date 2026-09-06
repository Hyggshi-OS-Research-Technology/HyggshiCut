#include "PlaybackController.h"
#include "../render/GLVideoWidget.h"
#include "../render/TextRenderer.h"
#include "../audio/AudioFilterDesc.h"
#include "../core/SystemInfo.h"
#include <QDebug>
#include <algorithm>
#include <cmath>

namespace hc {

namespace {
// If the next requested source time is within this window *ahead* of the
// last decoded frame's pts, we decode sequentially instead of reseeking —
// reseeking is a keyframe-granularity operation and far more expensive.
constexpr Ticks kSequentialDecodeWindow = 2 * kTicksPerSecond;
constexpr int kMaxSequentialSteps = 90; // safety cap so a stuck stream can't hang the UI thread
// Combined decode budget across ALL layers in one renderFrameAt() call.
constexpr int kMaxTotalSequentialStepsPerTick = 150;
constexpr size_t kAudioChunkFrames = 1024; // ~21.3ms per chunk at 48kHz
// Frames larger than this (raw YUV420P bytes, ~1.5x pixel count) are not
// put into the frame cache: a handful of uncached 4K/8K frames would blow
// through the whole cache budget and start evicting everything else on
// every insert. This is a non-issue for proxy-sourced decode (proxies are
// capped well under this) and for 1080p-and-below originals.
constexpr size_t kMaxCacheableFrameBytes = 8ull * 1024 * 1024;
}

PlaybackController::PlaybackController(Project* project, GLVideoWidget* videoOutput,
                                         ProxyManager* proxyManager, QObject* parent)
    : QObject(parent), m_project(project), m_videoOutput(videoOutput), m_proxyManager(proxyManager) {
    // Wire up the shared texture cache so GLVideoWidget can bind cached textures.
    if (m_videoOutput) m_videoOutput->setTextureCache(&m_textureCache);

    connect(&m_timer, &QTimer::timeout, this, &PlaybackController::onTick);
    connect(&m_audioTimer, &QTimer::timeout, this, &PlaybackController::feedAudio);
    initAudio();

    // Size the frame/texture caches to the machine. The defaults assume a
    // mid-range workstation; on weak/old machines they'd either pin far too
    // much RAM (texture/image tile caches) or be starved of it. Probed once
    // here; unknown RAM (probe returned 0) keeps the defaults.
    const uint64_t ram = systeminfo::totalMemoryBytes();
    if (ram > 0) {
        if (ram <= 3ull * 1024ull * 1024ull * 1024ull) {          // <= 3 GiB
            m_textureCache.setGpuBudgetBytes(64ull * 1024ull * 1024ull);
            m_textureCache.setCpuBudgetBytes(48ull * 1024ull * 1024ull);
            m_frameCache.setMaxBytes(24ull * 1024ull * 1024ull);
        } else if (ram <= 8ull * 1024ull * 1024ull * 1024ull) {   // <= 8 GiB
            m_textureCache.setGpuBudgetBytes(128ull * 1024ull * 1024ull);
            m_textureCache.setCpuBudgetBytes(64ull * 1024ull * 1024ull);
            m_frameCache.setMaxBytes(48ull * 1024ull * 1024ull);
        }
    }

    // Do not render from the constructor. MainWindow may rebuild this controller
    // before QOpenGLWidget has an initialized context (especially while opening
    // a project). The first frame is scheduled by MainWindow after the event
    // loop has a chance to initialize the GL widget.
}

PlaybackController::~PlaybackController() {
    pause();
    // Stop all decoding before the cache/object disappears. Do not force a
    // QOpenGLWidget context switch from a destructor: opening/replacing a
    // project can happen while Qt is processing widget events, and makeCurrent()
    // here can crash on some Qt/driver combinations. GPU cache resources are
    // owned by the preview widget and are reclaimed with its GL context.
    m_decoders.clear();
    m_decoderOpenPath.clear();
    m_lastDecodedSourceTime.clear();
    m_frameCache.clear();
}

void PlaybackController::initAudio() {
    // See AlsaAudioOutput.h for why this goes straight to ALSA instead of
    // through QAudioSink/QMediaDevices: Qt Multimedia's native-pipewire
    // backend fails to enumerate any device at all on this Qt/PipeWire
    // combination, unrelated to whether audio hardware/PipeWire itself is
    // actually working.
    m_audioOutput = std::make_unique<AlsaAudioOutput>();
    if (!m_audioOutput->openBestAvailable(kAudioSampleRate, kAudioChannels)) {
        // openBestAvailable() already logged why. Keep the object around
        // (isOpen() == false) rather than resetting to nullptr, so a future
        // retry could reuse it if we ever add one — feedAudio() etc. all
        // guard on isOpen() already.
    }
}


QString PlaybackController::videoSourcePathFor(const QString& assetId) const {
    auto asset = m_project->findAsset(assetId);
    if (!asset) return QString();
    if (m_useProxy && m_proxyManager) {
        const QString proxyPath = m_proxyManager->proxyPathForAsset(asset);
        if (!proxyPath.isEmpty()) return proxyPath;
    }
    return asset->filePath;
}

Decoder* PlaybackController::decoderFor(const QString& assetId) {
    const QString wantPath = videoSourcePathFor(assetId);
    if (wantPath.isEmpty()) return nullptr;

    auto it = m_decoders.find(assetId);
    if (it != m_decoders.end()) {
        // Reuse the open decoder UNLESS a proxy became ready (or the proxy
        // toggle changed) since it was opened — the path it's actually
        // reading from no longer matches what we want.
        auto pathIt = m_decoderOpenPath.find(assetId);
        if (pathIt != m_decoderOpenPath.end() && pathIt->second == wantPath) {
            return it->second.get();
        }
        // Source changed under us: drop the stale decoder and any frames
        // cached from it, then fall through to reopen against wantPath.
        m_decoders.erase(it);
        m_lastDecodedSourceTime.erase(assetId);
        m_frameCache.invalidateAsset(assetId);
    }

    auto decoder = std::make_unique<Decoder>();
    QString err;
    if (!decoder->open(wantPath, &err)) {
        qWarning("PlaybackController: khong mo duoc decoder cho %s: %s",
                 qPrintable(wantPath), qPrintable(err));
        return nullptr;
    }
    Decoder* raw = decoder.get();
    m_decoders.emplace(assetId, std::move(decoder));
    m_decoderOpenPath[assetId] = wantPath;
    return raw;
}

void PlaybackController::setUseProxy(bool useProxy) {
    if (m_useProxy == useProxy) return;
    m_useProxy = useProxy;
    // Every open video decoder may now be reading the "wrong" (proxy vs
    // original) source — decoderFor() will lazily reopen each one against
    // the right path the next time it's needed, but the frame cache holds
    // frames from the old source's pixel dimensions, so drop it wholesale
    // rather than per-asset.
    m_decoders.clear();
    m_decoderOpenPath.clear();
    m_lastDecodedSourceTime.clear();
    m_frameCache.clear();
    renderFrameAt(m_currentTime);
}

void PlaybackController::onProxyReady(const QString& assetId) {
    // Nothing to do if proxy use is off — decoderFor() will pick the proxy
    // up automatically next time it's turned on.
    if (!m_useProxy) return;
    auto it = m_decoders.find(assetId);
    if (it != m_decoders.end()) {
        m_decoders.erase(it);
        m_decoderOpenPath.erase(assetId);
        m_lastDecodedSourceTime.erase(assetId);
        m_frameCache.invalidateAsset(assetId);
    }
    renderFrameAt(m_currentTime);
}

Decoder* PlaybackController::audioDecoderFor(const QString& assetId) {
    auto it = m_audioDecoders.find(assetId);
    if (it != m_audioDecoders.end()) return it->second.get();

    auto asset = m_project->findAsset(assetId);
    if (!asset || !asset->hasAudio()) return nullptr;

    auto decoder = std::make_unique<Decoder>();
    QString err;
    if (!decoder->open(asset->filePath, &err)) {
        qWarning("PlaybackController: khong mo duoc audio decoder cho %s: %s",
                 qPrintable(asset->filePath), qPrintable(err));
        return nullptr;
    }
    Decoder* raw = decoder.get();
    m_audioDecoders.emplace(assetId, std::move(decoder));
    return raw;
}

void PlaybackController::resetAudioState() {
    m_audioBuffers.clear();
    m_audioDecoderPts.clear();
    // A seek breaks stream continuity, and these graphs carry internal
    // state (FFT windows, compressor envelope) that's now stale relative to
    // the jump — drop them so they rebuild fresh on the next feedAudio()
    // call rather than glitch.
    m_audioFilterChains.clear();
    for (auto& pair : m_audioDecoders) {
        if (pair.second) {
            pair.second->seek(0);
        }
    }
}

void PlaybackController::pullAudioSamples(const QString& assetId, Ticks srcTimeStart, size_t numFrames, std::vector<int16_t>& out) {
    out.assign(numFrames * kAudioChannels, 0);
    Decoder* decoder = audioDecoderFor(assetId);
    if (!decoder) return;

    auto& queue = m_audioBuffers[assetId];
    auto lastPtsIt = m_audioDecoderPts.find(assetId);
    const bool ptsKnown = (lastPtsIt != m_audioDecoderPts.end());
    const Ticks lastPts = ptsKnown ? lastPtsIt->second : -1;

    // If queue is empty or the required time is far from where the decoder is positioned:
    const bool needSeek = !ptsKnown || srcTimeStart < lastPts || (srcTimeStart - lastPts > 500'000);
    if (needSeek) {
        queue.clear();
        decoder->seek(srcTimeStart);
        m_audioDecoderPts[assetId] = srcTimeStart;
    }

    // Decode until we have enough samples in the queue
    size_t bufferedSamples = 0;
    for (const auto& entry : queue) {
        bufferedSamples += (entry.samples.size() - entry.readOffset);
    }

    const size_t neededSamples = numFrames * kAudioChannels;
    int maxPumps = 20;
    while (bufferedSamples < neededSamples && maxPumps-- > 0) {
        auto frame = decoder->decodeNextAudioFrame(kAudioSampleRate, kAudioChannels);
        if (!frame || !frame->isValid()) break; // EOF
        bufferedSamples += frame->samples.size();
        m_audioDecoderPts[assetId] = frame->pts;
        queue.push_back(AudioBufferEntry{frame->pts, std::move(frame->samples), 0});
    }

    // Drain from queue into out
    size_t written = 0;
    while (written < neededSamples && !queue.empty()) {
        auto& entry = queue.front();
        size_t available = entry.samples.size() - entry.readOffset;
        size_t toCopy = std::min(available, neededSamples - written);
        std::copy_n(entry.samples.data() + entry.readOffset, toCopy, out.data() + written);
        entry.readOffset += toCopy;
        written += toCopy;
        if (entry.readOffset >= entry.samples.size()) {
            queue.erase(queue.begin());
        }
    }
}

void PlaybackController::feedAudio() {
    if (!m_playing || !m_audioOutput || !m_audioOutput->isOpen()) return;

    const Ticks totalDur = duration();
    if (totalDur > 0 && m_audioTimelineTime >= totalDur) return;

    int maxChunks = 12; // limit per call to avoid locking UI; buffer is ~500ms now (see AlsaAudioOutput)
    while (maxChunks-- > 0) {
        const long framesFree = m_audioOutput->framesAvailable();
        if (framesFree < static_cast<long>(kAudioChunkFrames)) break;

        const size_t N = kAudioChunkFrames;
        const Ticks dt = static_cast<Ticks>(std::llround(N * 1'000'000.0 / kAudioSampleRate));
        const Ticks t0 = m_audioTimelineTime;
        const Ticks t1 = t0 + dt;

        std::vector<float> mixBuffer(N * kAudioChannels, 0.0f);
        std::vector<int16_t> clipSamples;

        for (const auto& track : m_project->timeline().tracks()) {
            if (track.muted) continue;
            for (const auto& clip : track.clips()) {
                if (clip.muted || clip.volume <= 0.0001) continue;
                if (clip.timelineEnd() <= t0 || clip.timelineStart >= t1) continue;

                auto asset = m_project->findAsset(clip.assetId);
                if (!asset || !asset->hasAudio()) continue;

                const Ticks ovStart = std::max(t0, clip.timelineStart);
                const Ticks ovEnd = std::min(t1, clip.timelineEnd());
                if (ovEnd <= ovStart) continue;

                const size_t frameStart = std::clamp<size_t>(
                    static_cast<size_t>((ovStart - t0) * kAudioSampleRate / 1'000'000), 0, N - 1);
                const size_t frameEnd = std::clamp<size_t>(
                    static_cast<size_t>((ovEnd - t0) * kAudioSampleRate / 1'000'000), frameStart, N);
                const size_t framesNeeded = frameEnd - frameStart;
                if (framesNeeded == 0) continue;

                const Ticks srcStart = clip.timelineTimeToSourceTime(ovStart);
                pullAudioSamples(clip.assetId, srcStart, framesNeeded, clipSamples);

                // EQ / denoise / compressor, if this clip has any set — see
                // audio/AudioFilterChain.h for why this is a persistent
                // per-clip graph instead of a one-shot call.
                const QString filterDesc = buildAudioFilterDescription(clip.audioFilters);
                if (!filterDesc.isEmpty()) {
                    auto it = m_audioFilterChains.find(clip.id);
                    if (it == m_audioFilterChains.end() || it->second->description() != filterDesc) {
                        auto chain = std::make_unique<AudioFilterChain>(filterDesc, kAudioSampleRate, kAudioChannels);
                        it = m_audioFilterChains.insert_or_assign(clip.id, std::move(chain)).first;
                    }
                    if (it->second->isValid()) {
                        clipSamples = it->second->process(clipSamples.data(), framesNeeded);
                    }
                }

                const float baseVol = static_cast<float>(clip.volume);
                for (size_t k = 0; k < framesNeeded; ++k) {
                    float fade = 1.0f;
                    if (clip.fadeInDuration > 0 || clip.fadeOutDuration > 0) {
                        const Ticks curT = t0 + static_cast<Ticks>((frameStart + k) * 1'000'000.0 / kAudioSampleRate);
                        if (clip.fadeInDuration > 0 && curT < clip.timelineStart + clip.fadeInDuration) {
                            fade = std::clamp(static_cast<float>(curT - clip.timelineStart) / clip.fadeInDuration, 0.0f, 1.0f);
                        }
                        if (clip.fadeOutDuration > 0 && curT > clip.timelineEnd() - clip.fadeOutDuration) {
                            fade = std::min(fade, std::clamp(static_cast<float>(clip.timelineEnd() - curT) / clip.fadeOutDuration, 0.0f, 1.0f));
                        }
                    }
                    const float vol = baseVol * fade;
                    mixBuffer[(frameStart + k) * 2 + 0] += clipSamples[k * 2 + 0] * vol;
                    mixBuffer[(frameStart + k) * 2 + 1] += clipSamples[k * 2 + 1] * vol;
                }
            }
        }

        std::vector<int16_t> outPcm(N * kAudioChannels);
        float leftPeak = 0.0f;
        float rightPeak = 0.0f;
        for (size_t i = 0; i < N; ++i) {
            float l = std::abs(mixBuffer[i * 2]) / 32768.0f;
            float r = std::abs(mixBuffer[i * 2 + 1]) / 32768.0f;
            if (l > leftPeak) leftPeak = l;
            if (r > rightPeak) rightPeak = r;
        }

        for (size_t i = 0; i < outPcm.size(); ++i) {
            const float s = mixBuffer[i];
            if (s > 32767.0f) outPcm[i] = 32767;
            else if (s < -32768.0f) outPcm[i] = -32768;
            else outPcm[i] = static_cast<int16_t>(s);
        }

        emit audioLevelsChanged(leftPeak, rightPeak);

        if (!m_audioOutput->write(outPcm.data(), N)) break;
        m_audioTimelineTime += dt;
    }
}

// ── CORE RENDERING: unified visual compositing ─────────────────────
// This is where the image-decode-once optimization lives.
void PlaybackController::renderFrameAt(Ticks t) {
    // Every visual clip (video, image, text) active at `t`, bottom-to-top.
    const auto activeClips = m_project->timeline().activeVisualClipsAt(t);

    if (activeClips.empty()) {
        if (m_videoOutput) m_videoOutput->clearFrame();
        return;
    }

    std::vector<GLLayer> layers;
    layers.reserve(activeClips.size());
    int remainingDecodeBudget = kMaxTotalSequentialStepsPerTick;

    for (const auto& visLayer : activeClips) {
        const Clip* activeClip = visLayer.clip;
        try {
            GLLayer layer;
            layer.transform = activeClip->transformAt(t);
            layer.opacity = activeClip->opacityAt(t) * visLayer.weight;
            layer.blendMode = activeClip->blendMode;
            layer.effects = activeClip->effects;

            if (activeClip->type == ClipType::Text) {
                // ━━━ TEXT CLIP: rasterize styled text with font, color, outline, etc. ━━━
                const int cw = m_project->timeline().videoWidth > 0 ? m_project->timeline().videoWidth : 1920;
                const int ch = m_project->timeline().videoHeight > 0 ? m_project->timeline().videoHeight : 1080;
                layer.isText = true;
                layer.canvasW = cw;
                layer.canvasH = ch;
                layer.textCacheKey = TextRenderer::cacheKey(*activeClip, cw, ch, false);
                layer.rgbaImage = TextRenderer::renderText(*activeClip, cw, ch, false);
            } else if (activeClip->type == ClipType::Image) {
                // ━━━ IMAGE CLIP: asynchronous RGBA8 image cache ━━━
                // Never decode an image on the playback/UI thread. requestAsset()
                // schedules FFmpeg decode on TextureCache's worker pool. The GL
                // widget uploads a bounded amount of ready data on paintGL().
                auto asset = m_project->findAsset(activeClip->assetId);
                if (!asset) continue;

                if (!m_textureCache.contains(activeClip->assetId)) {
                    m_textureCache.requestAsset(*asset);
                }

                // Keep the layer reference even while the image is loading.
                // GLVideoWidget will simply skip it until the async decode is
                // ready, then its upload/pump timer causes the next frame to
                // appear without another timeline seek.
                layer.cachedAssetId = activeClip->assetId;
            } else if (activeClip->type == ClipType::Video) {
                // ━━━ VIDEO CLIP: decode per-frame (unchanged from before) ━━━
                if (remainingDecodeBudget <= 0) {
                    continue;
                }

                const Ticks sourceTime = activeClip->timelineTimeToSourceTime(t);

                // ─ Frame cache: skip FFmpeg entirely on a hit ─
                // Revisiting a recently-decoded position (scrubbing back
                // over an edit point, replaying the last few seconds) is
                // the common case while editing, and used to always cost a
                // fresh seek+decode. quantize() needs a frame rate, which
                // we only have once the decoder is open, so an asset seen
                // for the first time this session still falls through to
                // decoderFor() below (that's fine — it's the same cost as
                // before this cache existed).
                auto assetForRate = m_project->findAsset(activeClip->assetId);
                bool cacheHit = false;
                if (assetForRate && assetForRate->frameRate > 0.0) {
                    const int64_t frameIdx = FrameCache::quantize(sourceTime, assetForRate->frameRate);
                    VideoFrame cached;
                    if (m_frameCache.get(activeClip->assetId, frameIdx, cached)) {
                        m_lastDecodedSourceTime[activeClip->assetId] = cached.pts;
                        layer.frame = std::move(cached);
                        cacheHit = true;
                    }
                }

                if (!cacheHit) {
                    Decoder* decoder = decoderFor(activeClip->assetId);
                    if (!decoder || !decoder->hasVideo()) continue;

                    const auto lastIt = m_lastDecodedSourceTime.find(activeClip->assetId);
                    const bool canDecodeSequentially =
                        lastIt != m_lastDecodedSourceTime.end() &&
                        sourceTime >= lastIt->second &&
                        (sourceTime - lastIt->second) <= kSequentialDecodeWindow;

                    if (!canDecodeSequentially) {
                        decoder->seek(sourceTime);
                    }

                    std::optional<VideoFrame> frame;
                    const int stepBudget = std::min(kMaxSequentialSteps, remainingDecodeBudget);
                    int stepsTaken = 0;
                    for (; stepsTaken < stepBudget; ++stepsTaken) {
                        auto f = decoder->decodeNextVideoFrame();
                        if (!f) break; // EOF
                        frame = f;
                        if (f->pts >= sourceTime) break;
                    }
                    remainingDecodeBudget -= stepsTaken;

                    if (frame && frame->isValid()) {
                        m_lastDecodedSourceTime[activeClip->assetId] = frame->pts;
                        const size_t frameBytes = frame->y.size() + frame->u.size() + frame->v.size();
                        if (decoder->frameRate() > 0.0 && frameBytes <= kMaxCacheableFrameBytes) {
                            const int64_t frameIdx = FrameCache::quantize(frame->pts, decoder->frameRate());
                            m_frameCache.put(activeClip->assetId, frameIdx, *frame);
                        }
                        layer.frame = std::move(*frame);
                        // cachedAssetId left empty → GLVideoWidget uploads normally.
                    }
                }
            } else {
                // Audio-only clip on visual track (shouldn't happen but guard).
                continue;
            }

            layers.push_back(std::move(layer));
        } catch (const std::exception& e) {
            qWarning() << "[PlaybackController] decode failed for clip" << activeClip->id << ":" << e.what();
        } catch (...) {
            qWarning() << "[PlaybackController] decode failed for clip" << activeClip->id << ": unknown exception";
        }
    }

    if (m_videoOutput) {
        if (!layers.empty()) {
            m_videoOutput->setLayers(layers);
        } else {
            m_videoOutput->clearFrame();
        }
    }
}

void PlaybackController::play() {
    if (m_playing) return;
    if (duration() <= 0) return;
    if (m_currentTime >= duration()) {
        m_currentTime = 0;
    }
    m_playStartTimelineTime = m_currentTime;
    m_audioTimelineTime = m_currentTime;
    resetAudioState();

    if (m_audioOutput && m_audioOutput->isOpen()) {
        // Clear out anything left over from before (e.g. a previous
        // pause), same role QAudioSink::start() used to serve.
        m_audioOutput->dropPending();
    }

    m_wallClock.start();
    const double fps = m_project->timeline().frameRate > 0 ? m_project->timeline().frameRate : 30.0;
    m_timer.start(static_cast<int>(1000.0 / fps));
    // Independent of the video-rate timer above (see m_audioTimer's
    // declaration in the header for why) — short interval so a slow video
    // frame doesn't starve the ALSA buffer.
    m_audioTimer.start(10);
    m_playing = true;

    feedAudio();
    emit playingChanged(true);
}

void PlaybackController::pause() {
    if (!m_playing) return;
    m_timer.stop();
    m_audioTimer.stop();
    if (m_audioOutput && m_audioOutput->isOpen()) {
        m_audioOutput->dropPending();
    }
    resetAudioState();
    m_playing = false;
    emit playingChanged(false);
}

void PlaybackController::togglePlay() {
    if (m_playing) pause(); else play();
}

void PlaybackController::seek(Ticks t) {
    const Ticks maxSeek = std::max<Ticks>(duration() + secondsToTicks(600), secondsToTicks(3600));
    t = std::clamp<Ticks>(t, 0, maxSeek);
    m_currentTime = t;
    m_audioTimelineTime = t;
    resetAudioState();

    if (m_playing) {
        m_playStartTimelineTime = t;
        m_wallClock.restart();
        if (m_audioOutput && m_audioOutput->isOpen()) {
            m_audioOutput->dropPending();
        }
        feedAudio();
    }
    renderFrameAt(t);
    emit positionChanged(t);
}

void PlaybackController::onTick() {
    feedAudio();

    const Ticks elapsed = static_cast<Ticks>(m_wallClock.elapsed()) * 1000; // ms -> ticks
    Ticks t = m_playStartTimelineTime + elapsed;
    const Ticks total = duration();
    if (total > 0 && t >= total) {
        t = total;
        m_currentTime = t;
        renderFrameAt(t);
        emit positionChanged(t);
        pause();
        return;
    }
    m_currentTime = t;
    renderFrameAt(t);
    emit positionChanged(t);
}

} // namespace hc
