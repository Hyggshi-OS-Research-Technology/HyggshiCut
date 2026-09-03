#include "AudioFilterChain.h"
#include <cstring>

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/frame.h>
}

namespace hc {

AudioFilterChain::AudioFilterChain(const QString& filterDescription, int sampleRate, int channels)
    : m_description(filterDescription), m_sampleRate(sampleRate), m_channels(channels) {
    m_graph = avfilter_graph_alloc();
    if (!m_graph) return;

    AVChannelLayout chLayout;
    av_channel_layout_default(&chLayout, channels);
    char chLayoutStr[64] = {0};
    av_channel_layout_describe(&chLayout, chLayoutStr, sizeof(chLayoutStr));

    const AVFilter* abuffer = avfilter_get_by_name("abuffer");
    const QString srcArgs = QString("sample_rate=%1:sample_fmt=s16:channel_layout=%2")
        .arg(sampleRate).arg(chLayoutStr);
    int ret = avfilter_graph_create_filter(&m_srcCtx, abuffer, "src",
        srcArgs.toUtf8().constData(), nullptr, m_graph);

    if (ret >= 0) {
        const AVFilter* abuffersink = avfilter_get_by_name("abuffersink");
        ret = avfilter_graph_create_filter(&m_sinkCtx, abuffersink, "sink", nullptr, nullptr, m_graph);
    }

    if (ret >= 0) {
        // Pin the sink to the same S16 format we feed in, so we never have
        // to handle a surprise sample format on the way out.
        static const enum AVSampleFormat kOutFmts[] = {AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE};
        av_opt_set_int_list(m_sinkCtx, "sample_fmts", kOutFmts, AV_SAMPLE_FMT_NONE, AV_OPT_SEARCH_CHILDREN);

        AVFilterInOut* outputs = avfilter_inout_alloc();
        AVFilterInOut* inputs = avfilter_inout_alloc();
        outputs->name = av_strdup("in");
        outputs->filter_ctx = m_srcCtx;
        outputs->pad_idx = 0;
        outputs->next = nullptr;

        inputs->name = av_strdup("out");
        inputs->filter_ctx = m_sinkCtx;
        inputs->pad_idx = 0;
        inputs->next = nullptr;

        ret = avfilter_graph_parse_ptr(m_graph, filterDescription.toUtf8().constData(),
                                        &inputs, &outputs, nullptr);
        if (ret >= 0) ret = avfilter_graph_config(m_graph, nullptr);

        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
    }

    av_channel_layout_uninit(&chLayout);

    m_valid = (ret >= 0);
    if (!m_valid && m_graph) {
        avfilter_graph_free(&m_graph);
        m_graph = nullptr;
        m_srcCtx = nullptr;
        m_sinkCtx = nullptr;
    }
}

AudioFilterChain::~AudioFilterChain() {
    if (m_graph) avfilter_graph_free(&m_graph);
}

std::vector<int16_t> AudioFilterChain::process(const int16_t* interleaved, size_t frames) {
    std::vector<int16_t> out(frames * m_channels, 0);
    if (!m_valid || frames == 0) return out;

    AVFrame* frame = av_frame_alloc();
    if (frame) {
        frame->format = AV_SAMPLE_FMT_S16;
        frame->sample_rate = m_sampleRate;
        av_channel_layout_default(&frame->ch_layout, m_channels);
        frame->nb_samples = static_cast<int>(frames);
        frame->pts = m_nextPts;
        m_nextPts += static_cast<int64_t>(frames);

        if (av_frame_get_buffer(frame, 0) >= 0) {
            std::memcpy(frame->data[0], interleaved, frames * m_channels * sizeof(int16_t));
            if (av_buffersrc_add_frame_flags(m_srcCtx, frame, AV_BUFFERSRC_FLAG_KEEP_REF) >= 0) {
                AVFrame* filtered = av_frame_alloc();
                if (filtered) {
                    while (av_buffersink_get_frame(m_sinkCtx, filtered) >= 0) {
                        const auto* samples = reinterpret_cast<const int16_t*>(filtered->data[0]);
                        const size_t n = static_cast<size_t>(filtered->nb_samples) * m_channels;
                        m_outputQueue.insert(m_outputQueue.end(), samples, samples + n);
                        av_frame_unref(filtered);
                    }
                    av_frame_free(&filtered);
                }
            }
        }
        av_frame_free(&frame);
    }

    // Serve exactly `frames` worth from the queue. If the graph hasn't
    // accumulated that much yet (startup latency — see header comment),
    // this call returns the silence `out` was zero-initialized with and
    // the real audio simply arrives a call or two later, once buffered.
    const size_t needed = frames * m_channels;
    if (m_outputQueue.size() < needed) return out;

    for (size_t i = 0; i < needed; ++i) {
        out[i] = m_outputQueue.front();
        m_outputQueue.pop_front();
    }
    return out;
}

} // namespace hc
