#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLFramebufferObject>
#include <QMutex>
#include <vector>
#include <memory>
#include <QVector2D>
#include <QVector4D>
#include "../decode/FrameTypes.h"
#include "../core/Clip.h"

namespace hc {

class TextureCache;

struct GLLayer {
    VideoFrame frame;
    QImage rgbaImage;           // for text or dynamic raster layers
    Transform transform;
    double opacity = 1.0;
    BlendMode blendMode = BlendMode::Normal;
    std::vector<Effect> effects;
    QString cachedAssetId;
};

// GPU compositor for HyggshiCut preview.
//
// Video:
//   VideoFrame Y/U/V -> per-frame GPU upload -> YUV shader.
//
// Still image/text:
//   TextureCache async decode -> RGBA8 -> either one texture or tiled
//   textures. Huge images are rendered tile-by-tile and only the tiles whose
//   transformed bounds intersect the viewport are requested.
//
// The compositor renders into an intermediate FBO before presenting to the
// QOpenGLWidget default framebuffer. This gives effects/blend passes a stable
// off-screen target and avoids repeatedly reallocating large source textures.
class GLVideoWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit GLVideoWidget(QWidget* parent = nullptr);
    ~GLVideoWidget() override;

    void setFrame(const VideoFrame& frame);
    void setLayers(const std::vector<GLLayer>& layers);
    void clearFrame();
    void setTextureCache(TextureCache* cache) { m_textureCache = cache; }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    struct GpuLayer {
        unsigned int texY = 0, texU = 0, texV = 0;
        unsigned int texRGBA = 0;
        bool isRGBA = false;
        bool hasFrame = false;
        bool usesCache = false;
        bool tiled = false;
        QString cachedAssetId;
        int texWidth = 0, texHeight = 0;
        int tileSize = 0;
        Transform transform;
        double sourceRotationDeg = 0.0;
        double opacity = 1.0;
        BlendMode blendMode = BlendMode::Normal;
        std::vector<Effect> effects;
        // Which YUV->RGB matrix texY/texU/texV were encoded with — see
        // VideoFrame::colorMatrixBt709. Irrelevant for RGBA layers (text/
        // image), which are already RGB and never go through the YUV
        // fragment shader.
        bool useBt709 = false;
    };

    void ensureLayerTextures(GpuLayer& layer, int width, int height);
    void uploadLayer(GpuLayer& layer, const VideoFrame& frame);
    void uploadRgbaLayer(GpuLayer& layer, const QImage& image);
    void bindCachedLayer(GpuLayer& layer);
    void destroyLayerTextures(GpuLayer& layer);
    void uploadPendingLayers();

    void ensureSceneFbo();
    void renderLayer(const GpuLayer& layer);
    void renderCachedTiledLayer(const GpuLayer& layer);
    void applyBlendMode(BlendMode mode);
    QVector2D fitFor(int sourceW, int sourceH) const;

    QOpenGLShaderProgram* m_program = nullptr;
    QOpenGLShaderProgram* m_programRGBA = nullptr;
    QOpenGLShaderProgram* m_presentProgram = nullptr;
    QOpenGLBuffer m_vbo{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject m_vao;
    std::unique_ptr<QOpenGLFramebufferObject> m_sceneFbo;

    QMutex m_pendingMutex;
    std::vector<GLLayer> m_pendingLayers;
    bool m_hasPendingLayers = false;
    bool m_pendingClear = false;

    std::vector<GpuLayer> m_gpuLayers;
    TextureCache* m_textureCache = nullptr;
};

} // namespace hc
