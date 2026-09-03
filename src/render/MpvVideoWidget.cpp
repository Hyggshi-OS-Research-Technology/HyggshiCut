#include "MpvVideoWidget.h"
#include "../playback/MpvPlayer.h"
#include <QOpenGLContext>
#include <QMetaObject>
#include <QDebug>
#include <stdexcept>

namespace hc {

MpvVideoWidget::MpvVideoWidget(MpvPlayer* player, QWidget* parent)
    : QOpenGLWidget(parent), m_player(player) {
    setUpdateBehavior(QOpenGLWidget::PartialUpdate);
}

MpvVideoWidget::~MpvVideoWidget() {
    // Must destroy the render context on the GL thread with the context
    // current, before the underlying mpv_handle goes away.
    makeCurrent();
    if (m_mpvGl) {
        mpv_render_context_set_update_callback(m_mpvGl, nullptr, nullptr);
        mpv_render_context_free(m_mpvGl);
        m_mpvGl = nullptr;
    }
    doneCurrent();
}

void* MpvVideoWidget::getProcAddress(void* ctx, const char* name) {
    Q_UNUSED(ctx);
    QOpenGLContext* glctx = QOpenGLContext::currentContext();
    if (!glctx) return nullptr;
    return reinterpret_cast<void*>(glctx->getProcAddress(QByteArray(name)));
}

void MpvVideoWidget::initializeGL() {
    if (!m_player || !m_player->handle()) return;

    mpv_opengl_init_params glInitParams{};
    glInitParams.get_proc_address = &MpvVideoWidget::getProcAddress;
    glInitParams.get_proc_address_ctx = nullptr;

    int advanced = 1; // enables mpv_render_context_render() driven by us, not mpv's own loop
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInitParams},
        {MPV_RENDER_PARAM_ADVANCED_CONTROL, &advanced},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };

    if (mpv_render_context_create(&m_mpvGl, m_player->handle(), params) < 0) {
        qWarning("MpvVideoWidget: khong tao duoc mpv render context");
        m_mpvGl = nullptr;
        return;
    }

    mpv_render_context_set_update_callback(m_mpvGl, &MpvVideoWidget::renderUpdateCallback, this);
}

void MpvVideoWidget::renderUpdateCallback(void* ctx) {
    // Called from an mpv-internal (possibly non-GUI) thread whenever a new
    // frame is ready to be presented; hop to the GUI thread before touching
    // the widget.
    QMetaObject::invokeMethod(reinterpret_cast<MpvVideoWidget*>(ctx), "maybeUpdate", Qt::QueuedConnection);
}

void MpvVideoWidget::maybeUpdate() {
    if (!m_mpvGl) return;
    const uint64_t flags = mpv_render_context_update(m_mpvGl);
    if (flags & MPV_RENDER_UPDATE_FRAME) {
        update(); // schedules paintGL()
    }
}

void MpvVideoWidget::paintGL() {
    if (!m_mpvGl) return;

    const qreal dpr = devicePixelRatioF();
    mpv_opengl_fbo fbo{};
    fbo.fbo = static_cast<int>(defaultFramebufferObject());
    fbo.w = static_cast<int>(width() * dpr);
    fbo.h = static_cast<int>(height() * dpr);
    fbo.internal_format = 0;

    int flipY = 1; // Qt's default FBO is top-left origin like mpv expects when flipped

    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flipY},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    mpv_render_context_render(m_mpvGl, params);
}

} // namespace hc
