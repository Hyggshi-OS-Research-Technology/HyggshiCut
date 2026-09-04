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
    QString textCacheKey;       // cache key for static text GL texture reuse
    int canvasW = 1920;         // original timeline canvas width
    int canvasH = 1080;         // original timeline canvas height
    bool isText = false;
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
//   Text: rasterized to tight bounding box and cached as GL textures, reused
//   every frame without per-frame glTexSubImage2D uploads.
//
// CPU Fallback:
//   When OpenGL 3.3 / shaders are unavailable or when HYGGSHICUT_FORCE_CPU_RENDER=1,
//   a pure software compositor (QPainter + CPU YUV->RGB) renders preview smoothly.
class GLVideoWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit GLVideoWidget(QWidget* parent = nullptr);
    ~GLVideoWidget() override;

    void setFrame(const VideoFrame& frame);
    void setLayers(const std::vector<GLLayer>& layers);
    void clearFrame();
    void setTextureCache(TextureCache* cache) { m_textureCache = cache; }

    bool isGpuAvailable() const { return m_gpuAvailable; }
    void setForceCpuFallback(bool force) { m_forceCpuFallback = force; update(); }
    bool isForceCpuFallback() const { return m_forceCpuFallback; }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    struct GpuLayer {
        unsigned int texY = 0, texU = 0, texV = 0;
        unsigned int texRGBA = 0;
        bool isRGBA = false;
        bool isText = false;
        bool hasFrame = false;
        bool usesCache = false;
        bool tiled = false;
        QString cachedAssetId;
        QString textCacheKey;
        int texWidth = 0, texHeight = 0;
        int canvasW = 1920, canvasH = 1080;
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
        // CPU fallback copy of decoded/rasterized image
        QImage cpuImage;
    };

    struct CachedTextTexture {
        unsigned int texture = 0;
        int width = 0;
        int height = 0;
        int canvasW = 1920;
        int canvasH = 1080;
        uint64_t lastUsed = 0;
    };

    void ensureLayerTextures(GpuLayer& layer, int width, int height);
    void uploadLayer(GpuLayer& layer, const VideoFrame& frame);
    void uploadRgbaLayer(GpuLayer& layer, const QImage& image);
    unsigned int uploadNewRgbaTexture(const QImage& image);
    void bindCachedLayer(GpuLayer& layer);
    void destroyLayerTextures(GpuLayer& layer);
    void uploadPendingLayers();

    void ensureSceneFbo();
    void renderLayer(const GpuLayer& layer);
    void renderCachedTiledLayer(const GpuLayer& layer);
    void applyBlendMode(BlendMode mode);
    QVector2D fitFor(int sourceW, int sourceH) const;

    void paintCpuFallback();
    static QImage convertYuvToRgb(const VideoFrame& frame);

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

    // Text GL texture cache
    static constexpr size_t kMaxTextTextures = 16;
    std::unordered_map<QString, CachedTextTexture> m_textTextureCache;
    uint64_t m_textTextureCounter = 0;

    // CPU fallback mode
    bool m_gpuAvailable = true;
    bool m_forceCpuFallback = false;
};

} // namespace hc
