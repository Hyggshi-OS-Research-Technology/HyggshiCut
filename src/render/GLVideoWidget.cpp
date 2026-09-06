#include "GLVideoWidget.h"
#include "TextureCache.h"
#include <QMutexLocker>
#include <QVector2D>
#include <QVector4D>
#include <QThread>
#include <QDebug>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <memory>

namespace hc {
namespace {

const char* kVertexShader = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
out vec2 vTexCoord;
uniform vec2 uFit;
uniform vec2 uScale;
uniform float uRotationDeg;
uniform float uAspect;
uniform vec2 uOffset;
uniform vec2 uTileScale;
uniform vec2 uTileCenter;
uniform float uSourceRotationDeg;

void main() {
    vec2 p = aPos * uTileScale + uTileCenter;

    // Apply media-letterboxing fit and user scale
    p *= uFit * uScale;

    // Source orientation (e.g. 90/180/270 deg metadata from mobile videos)
    if (abs(uSourceRotationDeg) > 0.001) {
        float sourceRad = radians(uSourceRotationDeg);
        float sc = cos(sourceRad);
        float ss = sin(sourceRad);
        p = mat2(sc, ss, -ss, sc) * p;
    }

    // Convert X into isotropic screen coordinates
    p.x *= uAspect;

    // 2D Isotropic Rotation (pure 2D rotation without shearing/2.5D perspective distortion)
    float rad = radians(uRotationDeg);
    float c = cos(rad);
    float s = sin(rad);
    p = mat2(c, s, -s, c) * p;

    // Convert back from isotropic space to NDC coordinates
    p.x /= uAspect;

    // Translation in NDC
    p += uOffset;

    gl_Position = vec4(p, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

const char* kFragmentShader = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 FragColor;
uniform sampler2D texY;
uniform sampler2D texU;
uniform sampler2D texV;
uniform float uOpacity;
uniform float uUseBt709;

uniform float uBrightness;
uniform float uContrast;
uniform float uSaturation;
uniform float uHueRotate;
uniform float uSepia;
uniform float uInvert;
uniform float uVignetteStrength;
uniform float uVignetteRadius;
uniform float uBlur;
uniform float uSharpen;
uniform vec3 uLift;
uniform vec3 uGamma;
uniform vec3 uGain;
uniform vec4 uCrop;

vec3 applyEffects(vec3 rgb, vec2 uv) {
    rgb += vec3(uBrightness);
    rgb = (rgb - 0.5) * uContrast + 0.5;
    float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    rgb = mix(vec3(luma), rgb, uSaturation);
    if (abs(uHueRotate) > 0.001) {
        float angle = radians(uHueRotate);
        float cosA = cos(angle);
        float sinA = sin(angle);
        const vec3 k = vec3(0.57735, 0.57735, 0.57735);
        rgb = rgb * cosA + cross(k, rgb) * sinA + k * dot(k, rgb) * (1.0 - cosA);
    }
    if (uSepia > 0.001) {
        vec3 sepiaRGB = vec3(
            dot(rgb, vec3(0.393, 0.769, 0.189)),
            dot(rgb, vec3(0.349, 0.686, 0.168)),
            dot(rgb, vec3(0.272, 0.534, 0.131))
        );
        rgb = mix(rgb, sepiaRGB, uSepia);
    }
    if (uInvert > 0.5) {
        rgb = 1.0 - rgb;
    }
    rgb = rgb + uLift * (1.0 - rgb);
    rgb = pow(max(rgb, vec3(0.0001)), vec3(1.0 / max(vec3(0.01), 1.0 + uGamma)));
    rgb = rgb * (1.0 + uGain);
    if (uVignetteStrength > 0.001) {
        float d = distance(uv, vec2(0.5));
        float vig = smoothstep(uVignetteRadius, uVignetteRadius - 0.5 * uVignetteStrength, d);
        rgb *= vig;
    }
    return clamp(rgb, 0.0, 1.0);
}

void main() {
    if (vTexCoord.x < uCrop.x || vTexCoord.y < uCrop.y || vTexCoord.x > (1.0 - uCrop.z) || vTexCoord.y > (1.0 - uCrop.w)) {
        discard;
    }
    float y = texture(texY, vTexCoord).r;
    if (uBlur > 0.5) {
        vec2 off = vec2(uBlur * 0.0008);
        float yB = (texture(texY, vTexCoord + vec2(-off.x, 0.0)).r +
                    texture(texY, vTexCoord + vec2( off.x, 0.0)).r +
                    texture(texY, vTexCoord + vec2(0.0, -off.y)).r +
                    texture(texY, vTexCoord + vec2(0.0,  off.y)).r +
                    texture(texY, vTexCoord + vec2(-off.x, -off.y)).r +
                    texture(texY, vTexCoord + vec2( off.x,  off.y)).r) / 6.0;
        y = mix(y, yB, min(1.0, uBlur / 10.0));
    }
    if (uSharpen > 0.05) {
        vec2 off = vec2(0.0008);
        float yN = (texture(texY, vTexCoord + vec2(-off.x, 0.0)).r +
                    texture(texY, vTexCoord + vec2( off.x, 0.0)).r +
                    texture(texY, vTexCoord + vec2(0.0, -off.y)).r +
                    texture(texY, vTexCoord + vec2(0.0,  off.y)).r) * 0.25;
        y = clamp(y + (y - yN) * uSharpen * 1.5, 0.0, 1.0);
    }
    float u = texture(texU, vTexCoord).r - 0.5;
    float v = texture(texV, vTexCoord).r - 0.5;
    // BT.601 (SD) vs BT.709 (HD) luma/chroma coefficients — must match
    // whichever matrix the source was actually encoded with, or Preview
    // colors visibly diverge from what Export (ffmpeg) produces.
    vec3 rgb601 = vec3(y + 1.402 * v, y - 0.344136 * u - 0.714136 * v, y + 1.772 * u);
    vec3 rgb709 = vec3(y + 1.5748 * v, y - 0.1873 * u - 0.4681 * v, y + 1.8556 * u);
    vec3 baseRgb = clamp(mix(rgb601, rgb709, uUseBt709), 0.0, 1.0);
    FragColor = vec4(applyEffects(baseRgb, vTexCoord), uOpacity);
}
)";

const char* kFragmentShaderRGBA = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 FragColor;
uniform sampler2D texRGBA;
uniform float uOpacity;

uniform float uBrightness;
uniform float uContrast;
uniform float uSaturation;
uniform float uHueRotate;
uniform float uSepia;
uniform float uInvert;
uniform float uVignetteStrength;
uniform float uVignetteRadius;
uniform float uBlur;
uniform float uSharpen;
uniform vec3 uLift;
uniform vec3 uGamma;
uniform vec3 uGain;
uniform vec4 uCrop;

vec3 applyEffects(vec3 rgb, vec2 uv) {
    rgb += vec3(uBrightness);
    rgb = (rgb - 0.5) * uContrast + 0.5;
    float luma = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    rgb = mix(vec3(luma), rgb, uSaturation);
    if (abs(uHueRotate) > 0.001) {
        float angle = radians(uHueRotate);
        float cosA = cos(angle);
        float sinA = sin(angle);
        const vec3 k = vec3(0.57735, 0.57735, 0.57735);
        rgb = rgb * cosA + cross(k, rgb) * sinA + k * dot(k, rgb) * (1.0 - cosA);
    }
    if (uSepia > 0.001) {
        vec3 sepiaRGB = vec3(
            dot(rgb, vec3(0.393, 0.769, 0.189)),
            dot(rgb, vec3(0.349, 0.686, 0.168)),
            dot(rgb, vec3(0.272, 0.534, 0.131))
        );
        rgb = mix(rgb, sepiaRGB, uSepia);
    }
    if (uInvert > 0.5) {
        rgb = 1.0 - rgb;
    }
    rgb = rgb + uLift * (1.0 - rgb);
    rgb = pow(max(rgb, vec3(0.0001)), vec3(1.0 / max(vec3(0.01), 1.0 + uGamma)));
    rgb = rgb * (1.0 + uGain);
    if (uVignetteStrength > 0.001) {
        float d = distance(uv, vec2(0.5));
        float vig = smoothstep(uVignetteRadius, uVignetteRadius - 0.5 * uVignetteStrength, d);
        rgb *= vig;
    }
    return clamp(rgb, 0.0, 1.0);
}

void main() {
    if (vTexCoord.x < uCrop.x || vTexCoord.y < uCrop.y || vTexCoord.x > (1.0 - uCrop.z) || vTexCoord.y > (1.0 - uCrop.w)) {
        discard;
    }
    vec4 c = texture(texRGBA, vTexCoord);
    if (uBlur > 0.5) {
        vec2 off = vec2(uBlur * 0.0008);
        vec4 cB = (texture(texRGBA, vTexCoord + vec2(-off.x, 0.0)) +
                   texture(texRGBA, vTexCoord + vec2( off.x, 0.0)) +
                   texture(texRGBA, vTexCoord + vec2(0.0, -off.y)) +
                   texture(texRGBA, vTexCoord + vec2(0.0,  off.y)) +
                   texture(texRGBA, vTexCoord + vec2(-off.x, -off.y)) +
                   texture(texRGBA, vTexCoord + vec2( off.x,  off.y))) / 6.0;
        c = mix(c, cB, min(1.0, uBlur / 10.0));
    }
    if (uSharpen > 0.05) {
        vec2 off = vec2(0.0008);
        vec4 cN = (texture(texRGBA, vTexCoord + vec2(-off.x, 0.0)) +
                   texture(texRGBA, vTexCoord + vec2( off.x, 0.0)) +
                   texture(texRGBA, vTexCoord + vec2(0.0, -off.y)) +
                   texture(texRGBA, vTexCoord + vec2(0.0,  off.y))) * 0.25;
        c.rgb = clamp(c.rgb + (c.rgb - cN.rgb) * uSharpen * 1.5, 0.0, 1.0);
    }
    FragColor = vec4(applyEffects(c.rgb, vTexCoord), c.a * uOpacity);
}
)";

const char* kPresentVertexShader = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aTexCoord;
out vec2 vTexCoord;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    // In OpenGL FBO textures, v=1.0 corresponds to the top of the rendered scene.
    // Screen quad vertex y=+1.0 has aTexCoord.y=0.0 in verts, so we map to 1.0 - aTexCoord.y.
    vTexCoord = vec2(aTexCoord.x, 1.0 - aTexCoord.y);
}
)";

const char* kPresentFragmentShader = R"(
#version 330 core
in vec2 vTexCoord;
out vec4 FragColor;
uniform sampler2D texScene;
void main() {
    FragColor = texture(texScene, vTexCoord);
}
)";

QVector2D tileScaleFor(int x, int y, int tileSize, int width, int height) {
    const int tw = std::min(tileSize, width - x * tileSize);
    const int th = std::min(tileSize, height - y * tileSize);
    return QVector2D(static_cast<float>(tw) / width,
                     static_cast<float>(th) / height);
}

QVector2D tileCenterFor(int x, int y, int tileSize, int width, int height) {
    const int tw = std::min(tileSize, width - x * tileSize);
    const int th = std::min(tileSize, height - y * tileSize);
    const float cx = -1.0f + (2.0f * x * tileSize + tw) / static_cast<float>(width);
    const float cy = 1.0f - (2.0f * y * tileSize + th) / static_cast<float>(height);
    return QVector2D(cx, cy);
}

} // namespace

GLVideoWidget::GLVideoWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setMinimumSize(160, 90);
}

GLVideoWidget::~GLVideoWidget() {
    makeCurrent();
    for (auto& pair : m_textTextureCache) {
        if (pair.second.texture) glDeleteTextures(1, &pair.second.texture);
    }
    m_textTextureCache.clear();
    for (auto& layer : m_gpuLayers) destroyLayerTextures(layer);
    m_sceneFbo.reset();
    m_vbo.destroy();
    delete m_program;
    delete m_programRGBA;
    delete m_presentProgram;
    doneCurrent();
}

static QString s_glRenderer = "OpenGL (Initializing)";
static QString s_glVersion = "Core Profile";
static QString s_glslVersion = "330 core";

QString GLVideoWidget::rendererString() {
    return s_glRenderer;
}

QString GLVideoWidget::versionString() {
    return s_glVersion;
}

QString GLVideoWidget::glslVersionString() {
    return s_glslVersion;
}

void GLVideoWidget::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.06f, 0.06f, 0.07f, 1.0f);

    const char* r = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* v = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* s = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
    if (r) s_glRenderer = QString::fromUtf8(r);
    if (v) s_glVersion = QString::fromUtf8(v);
    if (s) s_glslVersion = QString::fromUtf8(s);

    qInfo() << "HyggshiCut Graphics Renderer:"
            << s_glRenderer
            << "| Version:" << s_glVersion
            << "| GLSL:" << s_glslVersion;

    if (qEnvironmentVariableIsSet("HYGGSHICUT_FORCE_CPU_RENDER")) {
        qWarning() << "HyggshiCut: HYGGSHICUT_FORCE_CPU_RENDER is set. Activating CPU preview fallback mode.";
        m_forceCpuFallback = true;
    }

    m_program = new QOpenGLShaderProgram(this);
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader);
    bool okYUV = m_program->link();
    if (!okYUV) qWarning() << "HyggshiCut YUV shader:" << m_program->log();

    m_programRGBA = new QOpenGLShaderProgram(this);
    m_programRGBA->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader);
    m_programRGBA->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShaderRGBA);
    bool okRGBA = m_programRGBA->link();
    if (!okRGBA) qWarning() << "HyggshiCut RGBA shader:" << m_programRGBA->log();

    m_presentProgram = new QOpenGLShaderProgram(this);
    m_presentProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, kPresentVertexShader);
    m_presentProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, kPresentFragmentShader);
    bool okPresent = m_presentProgram->link();
    if (!okPresent) qWarning() << "HyggshiCut present shader:" << m_presentProgram->log();

    if (!okYUV || !okRGBA || !okPresent) {
        m_gpuAvailable = false;
        qWarning() << "HyggshiCut: OpenGL 3.3 core shaders could not be linked! Activating CPU preview renderer.";
        return;
    }
    m_gpuAvailable = true;

    const float verts[] = {
        -1.0f, -1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 1.0f,
         1.0f,  1.0f,  1.0f, 0.0f,
        -1.0f, -1.0f,  0.0f, 1.0f,
         1.0f,  1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f,  0.0f, 0.0f,
    };

    m_vao.create();
    m_vao.bind();
    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(verts, sizeof(verts));

    auto setupAttributes = [this](QOpenGLShaderProgram* p) {
        p->bind();
        p->enableAttributeArray(0);
        p->setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));
        p->enableAttributeArray(1);
        p->setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));
        p->release();
    };
    setupAttributes(m_program);
    setupAttributes(m_programRGBA);
    setupAttributes(m_presentProgram);

    m_vao.release();
    m_vbo.release();
    ensureSceneFbo();
}


void GLVideoWidget::ensureSceneFbo() {
    if (width() <= 0 || height() <= 0) return;
    if (m_sceneFbo && m_sceneFbo->size() == QSize(width() * devicePixelRatioF(), height() * devicePixelRatioF())) return;

    const QSize size(qMax(1, static_cast<int>(width() * devicePixelRatioF())),
                     qMax(1, static_cast<int>(height() * devicePixelRatioF())));
    QOpenGLFramebufferObjectFormat fmt;
    fmt.setAttachment(QOpenGLFramebufferObject::NoAttachment);
    fmt.setInternalTextureFormat(GL_RGBA8);
    fmt.setTextureTarget(GL_TEXTURE_2D);
    m_sceneFbo = std::make_unique<QOpenGLFramebufferObject>(size, fmt);
}

void GLVideoWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w * devicePixelRatioF(), h * devicePixelRatioF());
    ensureSceneFbo();
}

void GLVideoWidget::ensureLayerTextures(GpuLayer& layer, int width, int height) {
    if (width == layer.texWidth && height == layer.texHeight && layer.texY && !layer.usesCache) return;
    if (layer.usesCache) return;

    if (!layer.texY) {
        auto makeTex = [&]() {
            unsigned int tex = 0;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            return tex;
        };
        layer.texY = makeTex();
        layer.texU = makeTex();
        layer.texV = makeTex();
    }

    layer.texWidth = width;
    layer.texHeight = height;
    const int chromaW = (width + 1) / 2;
    const int chromaH = (height + 1) / 2;
    glBindTexture(GL_TEXTURE_2D, layer.texY);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, layer.texU);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, chromaW, chromaH, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, layer.texV);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, chromaW, chromaH, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
}

void GLVideoWidget::uploadLayer(GpuLayer& layer, const VideoFrame& frame) {
    if (layer.usesCache || layer.isRGBA || layer.texWidth != frame.width || layer.texHeight != frame.height || !layer.texY) {
        destroyLayerTextures(layer);
        ensureLayerTextures(layer, frame.width, frame.height);
    }
    const int chromaW = (frame.width + 1) / 2;
    const int chromaH = (frame.height + 1) / 2;

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame.strideY);
    glBindTexture(GL_TEXTURE_2D, layer.texY);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, frame.width, frame.height, GL_RED, GL_UNSIGNED_BYTE, frame.y.data());
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame.strideU);
    glBindTexture(GL_TEXTURE_2D, layer.texU);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, chromaW, chromaH, GL_RED, GL_UNSIGNED_BYTE, frame.u.data());
    glPixelStorei(GL_UNPACK_ROW_LENGTH, frame.strideV);
    glBindTexture(GL_TEXTURE_2D, layer.texV);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, chromaW, chromaH, GL_RED, GL_UNSIGNED_BYTE, frame.v.data());
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);

    layer.isRGBA = false;
    layer.sourceRotationDeg = frame.displayRotationDeg;
    layer.useBt709 = frame.colorMatrixBt709;
    layer.tiled = false;
    layer.tileSize = 0;
    layer.hasFrame = true;
    layer.usesCache = false;
}

void GLVideoWidget::uploadRgbaLayer(GpuLayer& layer, const QImage& image) {
    if (layer.usesCache || !layer.isRGBA || layer.texWidth != image.width() || layer.texHeight != image.height() || !layer.texRGBA) {
        destroyLayerTextures(layer);
        unsigned int tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.width(), image.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        layer.texRGBA = tex;
        layer.texWidth = image.width();
        layer.texHeight = image.height();
    }

    glBindTexture(GL_TEXTURE_2D, layer.texRGBA);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    QImage glImg = image.format() == QImage::Format_RGBA8888_Premultiplied ? image : image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, glImg.width(), glImg.height(), GL_RGBA, GL_UNSIGNED_BYTE, glImg.constBits());

    layer.isRGBA = true;
    layer.sourceRotationDeg = 0.0;
    layer.tiled = false;
    layer.tileSize = 0;
    layer.hasFrame = true;
    layer.usesCache = false;
}

unsigned int GLVideoWidget::uploadNewRgbaTexture(const QImage& image) {
    if (image.isNull()) return 0;
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    QImage glImg = (image.format() == QImage::Format_RGBA8888_Premultiplied)
                       ? image
                       : image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, glImg.width(), glImg.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, glImg.constBits());
    return tex;
}

QImage GLVideoWidget::convertYuvToRgb(const VideoFrame& frame) {
    if (!frame.isValid() || frame.width <= 0 || frame.height <= 0) return {};

    QImage rgb(frame.width, frame.height, QImage::Format_RGB888);
    const bool useBt709 = frame.colorMatrixBt709;

    for (int y = 0; y < frame.height; ++y) {
        const uint8_t* yRow = frame.y.data() + y * frame.strideY;
        const uint8_t* uRow = frame.u.data() + (y / 2) * frame.strideU;
        const uint8_t* vRow = frame.v.data() + (y / 2) * frame.strideV;
        uchar* dst = rgb.scanLine(y);

        for (int x = 0; x < frame.width; ++x) {
            const int Y = yRow[x];
            const int U = uRow[x / 2] - 128;
            const int V = vRow[x / 2] - 128;

            int r, g, b;
            if (useBt709) {
                r = qBound(0, static_cast<int>(Y + 1.5748 * V), 255);
                g = qBound(0, static_cast<int>(Y - 0.1873 * U - 0.4681 * V), 255);
                b = qBound(0, static_cast<int>(Y + 1.8556 * U), 255);
            } else {
                r = qBound(0, static_cast<int>(Y + 1.402 * V), 255);
                g = qBound(0, static_cast<int>(Y - 0.344136 * U - 0.714136 * V), 255);
                b = qBound(0, static_cast<int>(Y + 1.772 * U), 255);
            }

            dst[x * 3 + 0] = static_cast<uchar>(r);
            dst[x * 3 + 1] = static_cast<uchar>(g);
            dst[x * 3 + 2] = static_cast<uchar>(b);
        }
    }
    return rgb;
}

void GLVideoWidget::bindCachedLayer(GpuLayer& layer) {
    if (!m_textureCache) return;
    const auto info = m_textureCache->snapshot(layer.cachedAssetId);
    if (!info || !info->valid) {
        layer.hasFrame = false;
        return;
    }

    // Cache owns these resources; clear our previous non-cache resources first.
    if (!layer.usesCache) destroyLayerTextures(layer);
    layer.texRGBA = m_textureCache->acquireTexture(layer.cachedAssetId);
    layer.texWidth = info->width;
    layer.texHeight = info->height;
    layer.tileSize = info->tileSize;
    layer.tiled = info->isTiled;
    layer.isRGBA = true;
    layer.isText = false;
    layer.sourceRotationDeg = 0.0;
    layer.hasFrame = true;
    layer.usesCache = true;
}

void GLVideoWidget::destroyLayerTextures(GpuLayer& layer) {
    if (!layer.usesCache) {
        if (layer.texY) glDeleteTextures(1, &layer.texY);
        if (layer.texU) glDeleteTextures(1, &layer.texU);
        if (layer.texV) glDeleteTextures(1, &layer.texV);
        if (layer.texRGBA) glDeleteTextures(1, &layer.texRGBA);
    }
    layer.texY = layer.texU = layer.texV = layer.texRGBA = 0;
    layer.isRGBA = false;
    layer.isText = false;
    layer.textCacheKey.clear();
    layer.cpuImage = QImage();
    layer.sourceRotationDeg = 0.0;
    layer.tiled = false;
    layer.tileSize = 0;
    layer.texWidth = layer.texHeight = 0;
    layer.canvasW = layer.canvasH = 0;
    layer.hasFrame = false;
    layer.usesCache = false;
}


void GLVideoWidget::setFrame(const VideoFrame& frame) {
    GLLayer layer;
    layer.frame = frame;
    setLayers({layer});
}

void GLVideoWidget::setLayers(const std::vector<GLLayer>& layers) {
    {
        QMutexLocker locker(&m_pendingMutex);
        m_pendingLayers = layers;
        m_hasPendingLayers = true;
        m_pendingClear = false;
    }
    if (QThread::currentThread() == thread()) update();
    else QMetaObject::invokeMethod(this, QOverload<>::of(&QOpenGLWidget::update), Qt::QueuedConnection);
}

void GLVideoWidget::clearFrame() {
    {
        QMutexLocker locker(&m_pendingMutex);
        m_pendingLayers.clear();
        m_hasPendingLayers = false;
        m_pendingClear = true;
    }
    if (QThread::currentThread() == thread()) update();
    else QMetaObject::invokeMethod(this, QOverload<>::of(&QOpenGLWidget::update), Qt::QueuedConnection);
}

void GLVideoWidget::uploadPendingLayers() {
    std::vector<GLLayer> layers;
    bool doClear = false;
    {
        QMutexLocker locker(&m_pendingMutex);
        if (!m_hasPendingLayers && !m_pendingClear) return;
        layers = std::move(m_pendingLayers);
        m_pendingLayers.clear();
        doClear = m_pendingClear;
        m_hasPendingLayers = false;
        m_pendingClear = false;
    }

    if (doClear) {
        for (auto& gl : m_gpuLayers) destroyLayerTextures(gl);
        m_gpuLayers.clear();
        return;
    }

    while (m_gpuLayers.size() < layers.size()) m_gpuLayers.emplace_back();
    while (m_gpuLayers.size() > layers.size()) {
        destroyLayerTextures(m_gpuLayers.back());
        m_gpuLayers.pop_back();
    }

    for (size_t i = 0; i < layers.size(); ++i) {
        auto& gpu = m_gpuLayers[i];
        gpu.transform = layers[i].transform;
        gpu.opacity = layers[i].opacity;
        gpu.blendMode = layers[i].blendMode;
        gpu.effects = layers[i].effects;
        gpu.isText = layers[i].isText;
        gpu.canvasW = layers[i].canvasW;
        gpu.canvasH = layers[i].canvasH;

        if (!m_gpuAvailable || m_forceCpuFallback) {
            if (!layers[i].rgbaImage.isNull()) {
                gpu.cpuImage = layers[i].rgbaImage;
                gpu.hasFrame = true;
            } else if (layers[i].frame.isValid()) {
                gpu.cpuImage = convertYuvToRgb(layers[i].frame);
                gpu.hasFrame = true;
            } else {
                gpu.cpuImage = QImage();
                gpu.hasFrame = false;
            }
            continue;
        }

        if (layers[i].isText && !layers[i].textCacheKey.isEmpty()) {
            gpu.cachedAssetId.clear();
            auto it = m_textTextureCache.find(layers[i].textCacheKey);
            if (it != m_textTextureCache.end() && it->second.texture != 0) {
                // CACHE HIT: Re-use existing GL texture across frames without uploading!
                if (!gpu.usesCache) destroyLayerTextures(gpu);
                gpu.texRGBA = it->second.texture;
                gpu.texWidth = it->second.width;
                gpu.texHeight = it->second.height;
                gpu.canvasW = it->second.canvasW;
                gpu.canvasH = it->second.canvasH;
                gpu.isRGBA = true;
                gpu.isText = true;
                gpu.textCacheKey = layers[i].textCacheKey;
                gpu.hasFrame = true;
                gpu.usesCache = true;
                it->second.lastUsed = ++m_textTextureCounter;
            } else if (!layers[i].rgbaImage.isNull()) {
                // CACHE MISS: Upload new tight text texture once
                if (!gpu.usesCache) destroyLayerTextures(gpu);

                // Evict LRU if text texture cache full
                if (m_textTextureCache.size() >= kMaxTextTextures) {
                    auto oldestIt = m_textTextureCache.begin();
                    for (auto cit = m_textTextureCache.begin(); cit != m_textTextureCache.end(); ++cit) {
                        if (cit->second.lastUsed < oldestIt->second.lastUsed) {
                            oldestIt = cit;
                        }
                    }
                    if (oldestIt != m_textTextureCache.end()) {
                        for (auto& gl : m_gpuLayers) {
                            if (gl.texRGBA == oldestIt->second.texture) {
                                gl.texRGBA = 0;
                                gl.hasFrame = false;
                            }
                        }
                        glDeleteTextures(1, &oldestIt->second.texture);
                        m_textTextureCache.erase(oldestIt);
                    }
                }

                unsigned int newTex = uploadNewRgbaTexture(layers[i].rgbaImage);
                CachedTextTexture entry;
                entry.texture = newTex;
                entry.width = layers[i].rgbaImage.width();
                entry.height = layers[i].rgbaImage.height();
                entry.canvasW = layers[i].canvasW;
                entry.canvasH = layers[i].canvasH;
                entry.lastUsed = ++m_textTextureCounter;
                m_textTextureCache[layers[i].textCacheKey] = entry;

                gpu.texRGBA = newTex;
                gpu.texWidth = entry.width;
                gpu.texHeight = entry.height;
                gpu.canvasW = entry.canvasW;
                gpu.canvasH = entry.canvasH;
                gpu.isRGBA = true;
                gpu.isText = true;
                gpu.textCacheKey = layers[i].textCacheKey;
                gpu.hasFrame = true;
                gpu.usesCache = true;
            } else {
                gpu.hasFrame = false;
            }
        } else if (!layers[i].rgbaImage.isNull()) {
            gpu.cachedAssetId.clear();
            uploadRgbaLayer(gpu, layers[i].rgbaImage);
        } else if (!layers[i].cachedAssetId.isEmpty()) {
            if (gpu.cachedAssetId != layers[i].cachedAssetId) {
                destroyLayerTextures(gpu);
                gpu.cachedAssetId = layers[i].cachedAssetId;
            }
            bindCachedLayer(gpu);
        } else if (layers[i].frame.isValid()) {
            gpu.cachedAssetId.clear();
            uploadLayer(gpu, layers[i].frame);
        } else {
            gpu.hasFrame = false;
        }
    }
}


QVector2D GLVideoWidget::fitFor(int sourceW, int sourceH) const {
    if (sourceW <= 0 || sourceH <= 0 || width() <= 0 || height() <= 0) return {1.0f, 1.0f};
    const float widgetAspect = static_cast<float>(width()) / static_cast<float>(height());
    const float sourceAspect = static_cast<float>(sourceW) / static_cast<float>(sourceH);
    if (sourceAspect > widgetAspect) return {1.0f, widgetAspect / sourceAspect};
    return {sourceAspect / widgetAspect, 1.0f};
}

void setEffectUniforms(QOpenGLShaderProgram* prog, const std::vector<Effect>& effects) {
    float brightness = 0.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    float hueRotate = 0.0f;
    float sepia = 0.0f;
    float invert = 0.0f;
    float vignetteStrength = 0.0f;
    float vignetteRadius = 0.75f;
    float blurRadius = 0.0f;
    float sharpenAmount = 0.0f;
    QVector3D lift(0.0f, 0.0f, 0.0f);
    QVector3D gamma(0.0f, 0.0f, 0.0f);
    QVector3D gain(0.0f, 0.0f, 0.0f);
    QVector4D crop(0.0f, 0.0f, 0.0f, 0.0f);

    for (const auto& eff : effects) {
        if (!eff.enabled) continue;
        if (eff.type == "brightness") {
            brightness += static_cast<float>(eff.paramValue("amount", 0.0));
        } else if (eff.type == "contrast") {
            contrast *= static_cast<float>(eff.paramValue("amount", 1.0));
        } else if (eff.type == "saturation") {
            saturation *= static_cast<float>(eff.paramValue("amount", 1.0));
        } else if (eff.type == "hue_rotate") {
            hueRotate += static_cast<float>(eff.paramValue("degrees", 0.0));
        } else if (eff.type == "sepia") {
            sepia = std::max(sepia, static_cast<float>(eff.paramValue("amount", 0.8)));
        } else if (eff.type == "invert") {
            invert = 1.0f;
        } else if (eff.type == "blur") {
            blurRadius = std::max(blurRadius, static_cast<float>(eff.paramValue("radius", 5.0)));
        } else if (eff.type == "sharpen") {
            sharpenAmount = std::max(sharpenAmount, static_cast<float>(eff.paramValue("amount", 1.0)));
        } else if (eff.type == "vignette") {
            vignetteStrength = static_cast<float>(eff.paramValue("strength", 0.5));
            vignetteRadius = static_cast<float>(eff.paramValue("radius", 0.75));
        } else if (eff.type == "color_grade") {
            float lr = static_cast<float>(eff.paramValue("lift_r", eff.paramValue("lift", 0.0)));
            float lg = static_cast<float>(eff.paramValue("lift_g", eff.paramValue("lift", 0.0)));
            float lb = static_cast<float>(eff.paramValue("lift_b", eff.paramValue("lift", 0.0)));
            float gr = static_cast<float>(eff.paramValue("gamma_r", eff.paramValue("gamma", 0.0)));
            float gg = static_cast<float>(eff.paramValue("gamma_g", eff.paramValue("gamma", 0.0)));
            float gb = static_cast<float>(eff.paramValue("gamma_b", eff.paramValue("gamma", 0.0)));
            float gar = static_cast<float>(eff.paramValue("gain_r", eff.paramValue("gain", 0.0)));
            float gag = static_cast<float>(eff.paramValue("gain_g", eff.paramValue("gain", 0.0)));
            float gab = static_cast<float>(eff.paramValue("gain_b", eff.paramValue("gain", 0.0)));
            lift += QVector3D(lr, lg, lb);
            gamma += QVector3D(gr, gg, gb);
            gain += QVector3D(gar, gag, gab);
        } else if (eff.type == "crop") {
            float cl = static_cast<float>(eff.paramValue("left", 0.0));
            float ct = static_cast<float>(eff.paramValue("top", 0.0));
            float cr = static_cast<float>(eff.paramValue("right", 0.0));
            float cb = static_cast<float>(eff.paramValue("bottom", 0.0));
            crop = QVector4D(std::clamp(crop.x() + cl, 0.0f, 0.99f),
                             std::clamp(crop.y() + ct, 0.0f, 0.99f),
                             std::clamp(crop.z() + cr, 0.0f, 0.99f),
                             std::clamp(crop.w() + cb, 0.0f, 0.99f));
        }
    }

    prog->setUniformValue("uBrightness", brightness);
    prog->setUniformValue("uContrast", contrast);
    prog->setUniformValue("uSaturation", saturation);
    prog->setUniformValue("uHueRotate", hueRotate);
    prog->setUniformValue("uSepia", sepia);
    prog->setUniformValue("uInvert", invert);
    prog->setUniformValue("uBlur", blurRadius);
    prog->setUniformValue("uSharpen", sharpenAmount);
    prog->setUniformValue("uVignetteStrength", vignetteStrength);
    prog->setUniformValue("uVignetteRadius", vignetteRadius);
    prog->setUniformValue("uLift", lift);
    prog->setUniformValue("uGamma", gamma);
    prog->setUniformValue("uGain", gain);
    prog->setUniformValue("uCrop", crop);
}

void GLVideoWidget::applyBlendMode(BlendMode mode) {
    switch (mode) {
        case BlendMode::Normal: glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glBlendEquation(GL_FUNC_ADD); break;
        case BlendMode::Multiply: glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA); glBlendEquation(GL_FUNC_ADD); break;
        case BlendMode::Screen: glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR); glBlendEquation(GL_FUNC_ADD); break;
        case BlendMode::Overlay: glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR); glBlendEquation(GL_FUNC_ADD); break;
        case BlendMode::Add: glBlendFunc(GL_SRC_ALPHA, GL_ONE); glBlendEquation(GL_FUNC_ADD); break;
        case BlendMode::Subtract: glBlendFunc(GL_SRC_ALPHA, GL_ONE); glBlendEquation(GL_FUNC_SUBTRACT); break;
        case BlendMode::Darken: glBlendFunc(GL_ONE, GL_ONE); glBlendEquation(GL_MIN); break;
        case BlendMode::Lighten: glBlendFunc(GL_ONE, GL_ONE); glBlendEquation(GL_MAX); break;
        case BlendMode::Difference: glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_SRC_COLOR); glBlendEquation(GL_FUNC_ADD); break;
        case BlendMode::Exclusion: glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_SRC_COLOR); glBlendEquation(GL_FUNC_ADD); break;
        case BlendMode::HardLight:
        case BlendMode::SoftLight:
        case BlendMode::Dodge:
        case BlendMode::Burn:
        case BlendMode::Saturate:
        case BlendMode::HSLHue:
        case BlendMode::HSLSaturation:
        case BlendMode::HSLColor:
        case BlendMode::HSLLuminosity:
        default: glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glBlendEquation(GL_FUNC_ADD); break;
    }
}

static void orientedDimensions(int width, int height, double rotationDeg, int& outW, int& outH) {
    outW = width;
    outH = height;
    double a = std::fmod(std::abs(rotationDeg), 360.0);
    if (a > 45.0 && a < 135.0) {
        outW = height;
        outH = width;
    } else if (a >= 225.0 && a < 315.0) {
        outW = height;
        outH = width;
    }
}

void GLVideoWidget::renderCachedTiledLayer(const GpuLayer& layer) {
    if (!m_textureCache || layer.tileSize <= 0 || width() <= 0 || height() <= 0) return;
    QOpenGLShaderProgram* prog = m_programRGBA;
    prog->bind();
    m_vao.bind();
    const float aspect = static_cast<float>(width()) / static_cast<float>(height());
    prog->setUniformValue("uFit", fitFor(layer.texWidth, layer.texHeight));
    prog->setUniformValue("uScale", QVector2D(static_cast<float>(layer.transform.scaleX), static_cast<float>(layer.transform.scaleY)));
    prog->setUniformValue("uRotationDeg", static_cast<float>(layer.transform.rotationDeg));
    prog->setUniformValue("uAspect", aspect);
    prog->setUniformValue("uSourceRotationDeg", 0.0f);
    prog->setUniformValue("uOffset", QVector2D(static_cast<float>(layer.transform.x), static_cast<float>(-layer.transform.y)));
    prog->setUniformValue("uOpacity", static_cast<float>(std::clamp(layer.opacity, 0.0, 1.0)));
    prog->setUniformValue("texRGBA", 0);
    setEffectUniforms(prog, layer.effects);

    const int cols = (layer.texWidth + layer.tileSize - 1) / layer.tileSize;
    const int rows = (layer.texHeight + layer.tileSize - 1) / layer.tileSize;
    const double rad = layer.transform.rotationDeg * M_PI / 180.0;
    const double c = std::cos(rad), s = std::sin(rad);
    const QVector2D fit = fitFor(layer.texWidth, layer.texHeight);

    glActiveTexture(GL_TEXTURE0);
    applyBlendMode(layer.blendMode);

    for (int ty = 0; ty < rows; ++ty) {
        for (int tx = 0; tx < cols; ++tx) {
            const QVector2D scale = tileScaleFor(tx, ty, layer.tileSize, layer.texWidth, layer.texHeight);
            const QVector2D center = tileCenterFor(tx, ty, layer.tileSize, layer.texWidth, layer.texHeight);

            // Conservative screen-space culling. Rotated tiles use a four-corner
            // bounding box in isotropic screen coordinates.
            const float hx = scale.x() * fit.x() * static_cast<float>(std::abs(layer.transform.scaleX));
            const float hy = scale.y() * fit.y() * static_cast<float>(std::abs(layer.transform.scaleY));
            const float cx = center.x() * fit.x() * static_cast<float>(layer.transform.scaleX);
            const float cy = center.y() * fit.y() * static_cast<float>(layer.transform.scaleY);
            float minX = 10.0f, maxX = -10.0f, minY = 10.0f, maxY = -10.0f;
            const float corners[4][2] = {{-hx,-hy},{hx,-hy},{hx,hy},{-hx,hy}};
            for (const auto& p : corners) {
                const float lx = (cx + p[0]) * aspect;
                const float ly = (cy + p[1]);
                const float rx = static_cast<float>(c * lx - s * ly);
                const float ry = static_cast<float>(s * lx + c * ly);
                const float x = rx / aspect + static_cast<float>(layer.transform.x);
                const float y = ry - static_cast<float>(layer.transform.y);
                minX = std::min(minX, x); maxX = std::max(maxX, x);
                minY = std::min(minY, y); maxY = std::max(maxY, y);
            }
            if (maxX < -1.0f || minX > 1.0f || maxY < -1.0f || minY > 1.0f) continue;

            const unsigned int tex = m_textureCache->acquireTile(layer.cachedAssetId, tx, ty);
            if (!tex) continue;
            glBindTexture(GL_TEXTURE_2D, tex);
            prog->setUniformValue("uTileScale", scale);
            prog->setUniformValue("uTileCenter", center);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
    }

    m_vao.release();
    prog->release();
}

void GLVideoWidget::renderLayer(const GpuLayer& layer) {
    if (!layer.hasFrame || width() <= 0 || height() <= 0) return;
    if (layer.usesCache && layer.tiled) {
        renderCachedTiledLayer(layer);
        return;
    }

    QOpenGLShaderProgram* prog = layer.isRGBA ? m_programRGBA : m_program;
    prog->bind();
    m_vao.bind();
    int displayW = layer.texWidth;
    int displayH = layer.texHeight;
    if (!layer.isRGBA) {
        orientedDimensions(layer.texWidth, layer.texHeight, layer.sourceRotationDeg, displayW, displayH);
    }
    const float aspect = static_cast<float>(width()) / static_cast<float>(height());

    QVector2D tileScale(1.0f, 1.0f);
    QVector2D fit;
    if (layer.isText && layer.canvasW > 0 && layer.canvasH > 0) {
        tileScale = QVector2D(static_cast<float>(layer.texWidth) / static_cast<float>(layer.canvasW),
                              static_cast<float>(layer.texHeight) / static_cast<float>(layer.canvasH));
        fit = fitFor(layer.canvasW, layer.canvasH);
    } else {
        fit = fitFor(displayW, displayH);
    }

    prog->setUniformValue("uFit", fit);
    prog->setUniformValue("uScale", QVector2D(static_cast<float>(layer.transform.scaleX), static_cast<float>(layer.transform.scaleY)));
    prog->setUniformValue("uRotationDeg", static_cast<float>(layer.transform.rotationDeg));
    prog->setUniformValue("uAspect", aspect);
    prog->setUniformValue("uSourceRotationDeg", static_cast<float>(layer.isRGBA ? 0.0 : layer.sourceRotationDeg));
    prog->setUniformValue("uOffset", QVector2D(static_cast<float>(layer.transform.x), static_cast<float>(-layer.transform.y)));
    prog->setUniformValue("uTileScale", tileScale);
    prog->setUniformValue("uTileCenter", QVector2D(0.0f, 0.0f));
    prog->setUniformValue("uOpacity", static_cast<float>(std::clamp(layer.opacity, 0.0, 1.0)));
    setEffectUniforms(prog, layer.effects);

    if (layer.isRGBA) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, layer.texRGBA);
        prog->setUniformValue("texRGBA", 0);
    } else {
        prog->setUniformValue("uUseBt709", layer.useBt709 ? 1.0f : 0.0f);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, layer.texY); prog->setUniformValue("texY", 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, layer.texU); prog->setUniformValue("texU", 1);
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, layer.texV); prog->setUniformValue("texV", 2);
    }

    applyBlendMode(layer.blendMode);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    m_vao.release();
    prog->release();
}

void GLVideoWidget::paintCpuFallback() {
    uploadPendingLayers();

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const int w = width();
    const int h = height();
    if (w <= 0 || h <= 0) return;

    p.fillRect(rect(), QColor(15, 15, 18));

    for (const auto& layer : m_gpuLayers) {
        if (!layer.hasFrame || layer.cpuImage.isNull()) continue;

        const QImage& img = layer.cpuImage;
        p.save();
        p.setOpacity(std::clamp(layer.opacity, 0.0, 1.0));

        const qreal cx = w / 2.0 + layer.transform.x * (w / 2.0);
        const qreal cy = h / 2.0 + layer.transform.y * (h / 2.0);

        p.translate(cx, cy);
        if (std::abs(layer.transform.rotationDeg) > 0.001) {
            p.rotate(layer.transform.rotationDeg);
        }

        qreal drawW = img.width();
        qreal drawH = img.height();

        if (layer.isText && layer.canvasW > 0 && layer.canvasH > 0) {
            const qreal widgetAspect = static_cast<qreal>(w) / h;
            const qreal canvasAspect = static_cast<qreal>(layer.canvasW) / layer.canvasH;
            qreal fitScale = (canvasAspect > widgetAspect)
                ? (static_cast<qreal>(w) / layer.canvasW)
                : (static_cast<qreal>(h) / layer.canvasH);
            drawW = img.width() * fitScale * std::abs(layer.transform.scaleX);
            drawH = img.height() * fitScale * std::abs(layer.transform.scaleY);
        } else {
            const qreal widgetAspect = static_cast<qreal>(w) / h;
            const qreal imgAspect = static_cast<qreal>(img.width()) / img.height();
            qreal baseW, baseH;
            if (imgAspect > widgetAspect) {
                baseW = w;
                baseH = w / imgAspect;
            } else {
                baseH = h;
                baseW = h * imgAspect;
            }
            drawW = baseW * std::abs(layer.transform.scaleX);
            drawH = baseH * std::abs(layer.transform.scaleY);
        }

        const QRectF dstRect(-drawW / 2.0, -drawH / 2.0, drawW, drawH);
        p.drawImage(dstRect, img);
        p.restore();
    }

    // Informational overlay badge
    p.save();
    const QString badgeText = QStringLiteral("CPU Fallback Mode (OpenGL 3.3 Unavailable)");
    QFont badgeFont("Segoe UI", 9, QFont::Bold);
    p.setFont(badgeFont);
    QFontMetrics fm(badgeFont);
    const int badgeW = fm.horizontalAdvance(badgeText) + 16;
    const int badgeH = fm.height() + 8;
    const QRect badgeRect(w - badgeW - 12, 12, badgeW, badgeH);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(30, 30, 35, 200));
    p.drawRoundedRect(badgeRect, 4, 4);

    p.setPen(QColor(240, 180, 40));
    p.drawText(badgeRect, Qt::AlignCenter, badgeText);
    p.restore();
}

void GLVideoWidget::paintGL() {
    if (!m_gpuAvailable || m_forceCpuFallback) {
        paintCpuFallback();
        return;
    }

    if (m_textureCache) m_textureCache->pumpUploads(2);
    uploadPendingLayers();
    ensureSceneFbo();

    if (m_textureCache && m_textureCache->hasPendingWork()) {
        QMetaObject::invokeMethod(this, QOverload<>::of(&QOpenGLWidget::update), Qt::QueuedConnection);
    }

    if (!m_sceneFbo || !m_program || !m_programRGBA || !m_presentProgram) {
        glClear(GL_COLOR_BUFFER_BIT);
        return;
    }

    m_sceneFbo->bind();
    glViewport(0, 0, m_sceneFbo->width(), m_sceneFbo->height());
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    for (const auto& layer : m_gpuLayers) renderLayer(layer);
    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
    m_sceneFbo->release();

    glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    glViewport(0, 0, width() * devicePixelRatioF(), height() * devicePixelRatioF());
    glDisable(GL_BLEND);
    glClear(GL_COLOR_BUFFER_BIT);
    m_presentProgram->bind();
    m_vao.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneFbo->texture());
    m_presentProgram->setUniformValue("texScene", 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    m_vao.release();
    m_presentProgram->release();
}


} // namespace hc
