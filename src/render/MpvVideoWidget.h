#pragma once
#include <QOpenGLWidget>
#include <mpv/render_gl.h>

namespace hc {

class MpvPlayer;

// Renders whatever MpvPlayer is currently playing, using libmpv's *render*
// API (mpv_render_context) directly into this widget's GL framebuffer.
// Unlike GLVideoWidget (which HyggshiCut feeds decoded YUV420P frames one
// by one from PlaybackController), this widget does no decoding itself —
// mpv decodes, scales, color-converts and paces frames on its own, we just
// give it a framebuffer to draw into on every paintGL().
class MpvVideoWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    // player must outlive this widget.
    explicit MpvVideoWidget(MpvPlayer* player, QWidget* parent = nullptr);
    ~MpvVideoWidget() override;

protected:
    void initializeGL() override;
    void paintGL() override;

private slots:
    void maybeUpdate();

private:
    static void renderUpdateCallback(void* ctx);
    static void* getProcAddress(void* ctx, const char* name);

    MpvPlayer* m_player = nullptr;
    mpv_render_context* m_mpvGl = nullptr;
};

} // namespace hc
