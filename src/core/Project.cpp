#include "Project.h"
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>

namespace hc {

namespace {

QString clipTypeToString(ClipType t) {
    switch (t) {
        case ClipType::Video: return "video";
        case ClipType::Audio: return "audio";
        case ClipType::Image: return "image";
        case ClipType::Text:  return "text";
    }
    return "video";
}
ClipType clipTypeFromString(const QString& s) {
    if (s == "audio") return ClipType::Audio;
    if (s == "image") return ClipType::Image;
    if (s == "text")  return ClipType::Text;
    return ClipType::Video;
}

// Media paths in a saved project are stored verbatim (as the user imported
// them). If that was a *relative* path, resolving it blindly against the
// current working directory breaks the moment the .hcproj is opened from
// anywhere else (different CWD, double-clicked in a file manager, moved
// alongside its media). To keep projects portable, prefer to resolve a
// relative asset path against the directory *containing the .hcproj file*
// first; fall back to the CWD (legacy files saved that way); and if neither
// resolves, return the stored value unchanged so the caller can still build
// a relink-able placeholder asset instead of failing to open the project.
QString resolveMediaPath(const QString& storedPath, const QString& projectDir) {
    if (storedPath.isEmpty() || QDir::isAbsolutePath(storedPath)) {
        return storedPath;
    }
    if (!projectDir.isEmpty()) {
        const QString relativeToProject =
            QDir::cleanPath(projectDir + QDir::separator() + storedPath);
        if (QFileInfo::exists(relativeToProject)) return relativeToProject;
    }
    if (QFileInfo::exists(storedPath)) return storedPath;
    return storedPath;
}
QString mediaKindToString(MediaKind k) {
    switch (k) {
        case MediaKind::Video: return "video";
        case MediaKind::Audio: return "audio";
        case MediaKind::Image: return "image";
        case MediaKind::Unknown: return "unknown";
    }
    return "unknown";
}
MediaKind mediaKindFromString(const QString& s) {
    if (s == "audio") return MediaKind::Audio;
    if (s == "image") return MediaKind::Image;
    if (s == "video") return MediaKind::Video;
    return MediaKind::Unknown;
}
QString trackTypeToString(TrackType t) {
    switch (t) {
        case TrackType::Visual: return "visual";
        case TrackType::Audio:  return "audio";
    }
    return "visual";
}
TrackType trackTypeFromString(const QString& s) {
    if (s == "audio")  return TrackType::Audio;
    // Backward compat: old projects saved "video" / "text" tracks.
    if (s == "video" || s == "text") return TrackType::Visual;
    return TrackType::Visual;
}

QJsonObject clipToJson(const Clip& c) {
    QJsonObject o;
    o["id"] = c.id;
    o["assetId"] = c.assetId;
    o["type"] = clipTypeToString(c.type);
    o["sourceIn"] = QString::number(c.sourceIn);
    o["sourceOut"] = QString::number(c.sourceOut);
    o["timelineStart"] = QString::number(c.timelineStart);
    o["speed"] = c.speed;
    o["volume"] = c.volume;
    o["opacity"] = c.opacity;
    o["muted"] = c.muted;
    if (!c.transform.isIdentity()) {
        QJsonObject t;
        t["x"] = c.transform.x;
        t["y"] = c.transform.y;
        t["scaleX"] = c.transform.scaleX;
        t["scaleY"] = c.transform.scaleY;
        t["rotationDeg"] = c.transform.rotationDeg;
        o["transform"] = t;
    }
    if (!c.transformKeyframes.isEmpty()) {
        QJsonArray kfs;
        for (const auto& kf : c.transformKeyframes) {
            QJsonObject k;
            k["time"] = QString::number(kf.time);
            k["x"] = kf.value.x;
            k["y"] = kf.value.y;
            k["scaleX"] = kf.value.scaleX;
            k["scaleY"] = kf.value.scaleY;
            k["rotationDeg"] = kf.value.rotationDeg;
            k["opacity"] = kf.value.opacity;
            kfs.append(k);
        }
        o["transformKeyframes"] = kfs;
    }
    o["fadeInDuration"] = QString::number(c.fadeInDuration);
    o["fadeOutDuration"] = QString::number(c.fadeOutDuration);
    if (c.transitionInDuration > 0) {
        o["transitionInDuration"] = QString::number(c.transitionInDuration);
    }
    if (!c.audioFilters.isDefault()) {
        QJsonObject af;
        af["eqLowDb"] = c.audioFilters.eqLowDb;
        af["eqMidDb"] = c.audioFilters.eqMidDb;
        af["eqHighDb"] = c.audioFilters.eqHighDb;
        af["denoiseEnabled"] = c.audioFilters.denoiseEnabled;
        af["denoiseAmountDb"] = c.audioFilters.denoiseAmountDb;
        af["compressorEnabled"] = c.audioFilters.compressorEnabled;
        af["compressorThresholdDb"] = c.audioFilters.compressorThresholdDb;
        af["compressorRatio"] = c.audioFilters.compressorRatio;
        o["audioFilters"] = af;
    }
    o["displayLabel"] = c.displayLabel;
    if (c.type == ClipType::Text) {
        o["textFontFamily"] = c.textFontFamily;
        o["textFontSize"] = c.textFontSize;
        o["textFontColor"] = c.textFontColor;
        o["textBold"] = c.textBold;
        o["textItalic"] = c.textItalic;
        o["textUnderline"] = c.textUnderline;
        o["textAlignment"] = c.textAlignment;
        o["textOutlineEnabled"] = c.textOutlineEnabled;
        o["textOutlineColor"] = c.textOutlineColor;
        o["textOutlineWidth"] = c.textOutlineWidth;
        o["textBackgroundEnabled"] = c.textBackgroundEnabled;
        o["textBackgroundColor"] = c.textBackgroundColor;
        o["textPadding"] = c.textPadding;
    }
    // Blend mode (only serialize if not Normal to keep files clean).
    if (c.blendMode != BlendMode::Normal) {
        o["blendMode"] = static_cast<int>(c.blendMode);
    }
    if (!c.effects.empty()) {
        QJsonArray effArr;
        for (const auto& eff : c.effects) {
            QJsonObject eo;
            eo["type"] = eff.type;
            eo["enabled"] = eff.enabled;
            QJsonObject po;
            for (const auto& p : eff.params) {
                po[p.name] = p.value;
            }
            eo["params"] = po;
            effArr.append(eo);
        }
        o["effects"] = effArr;
    }
    return o;
}

Clip clipFromJson(const QJsonObject& o) {
    Clip c;
    c.id = o["id"].toString();
    c.assetId = o["assetId"].toString();
    c.type = clipTypeFromString(o["type"].toString());
    c.sourceIn = o["sourceIn"].toString().toLongLong();
    c.sourceOut = o["sourceOut"].toString().toLongLong();
    c.timelineStart = o["timelineStart"].toString().toLongLong();
    c.speed = o["speed"].toDouble(1.0);
    c.volume = o["volume"].toDouble(1.0);
    c.opacity = o["opacity"].toDouble(1.0);
    c.muted = o["muted"].toBool(false);
    if (o.contains("transform")) {
        const QJsonObject t = o["transform"].toObject();
        c.transform.x = t["x"].toDouble(0.0);
        c.transform.y = t["y"].toDouble(0.0);
        c.transform.scaleX = t["scaleX"].toDouble(1.0);
        c.transform.scaleY = t["scaleY"].toDouble(1.0);
        c.transform.rotationDeg = t["rotationDeg"].toDouble(0.0);
        c.transform.opacity = t["opacity"].toDouble(c.opacity);
    }
    if (o.contains("transformKeyframes")) {
        const QJsonArray kfs = o["transformKeyframes"].toArray();
        for (const auto& v : kfs) {
            const QJsonObject k = v.toObject();
            TransformKeyframe kf;
            kf.time = k["time"].toString().toLongLong();
            kf.value.x = k["x"].toDouble(0.0);
            kf.value.y = k["y"].toDouble(0.0);
            kf.value.scaleX = k["scaleX"].toDouble(1.0);
            kf.value.scaleY = k["scaleY"].toDouble(1.0);
            kf.value.rotationDeg = k["rotationDeg"].toDouble(0.0);
            kf.value.opacity = k["opacity"].toDouble(1.0);
            c.transformKeyframes.push_back(kf);
        }
        std::sort(c.transformKeyframes.begin(), c.transformKeyframes.end(),
                  [](const TransformKeyframe& a, const TransformKeyframe& b) { return a.time < b.time; });
    }
    c.fadeInDuration = o["fadeInDuration"].toString().toLongLong();
    c.fadeOutDuration = o["fadeOutDuration"].toString().toLongLong();
    c.transitionInDuration = o["transitionInDuration"].toString().toLongLong();
    if (o.contains("audioFilters")) {
        const QJsonObject af = o["audioFilters"].toObject();
        c.audioFilters.eqLowDb = af["eqLowDb"].toDouble(0.0);
        c.audioFilters.eqMidDb = af["eqMidDb"].toDouble(0.0);
        c.audioFilters.eqHighDb = af["eqHighDb"].toDouble(0.0);
        c.audioFilters.denoiseEnabled = af["denoiseEnabled"].toBool(false);
        c.audioFilters.denoiseAmountDb = af["denoiseAmountDb"].toDouble(12.0);
        c.audioFilters.compressorEnabled = af["compressorEnabled"].toBool(false);
        c.audioFilters.compressorThresholdDb = af["compressorThresholdDb"].toDouble(-18.0);
        c.audioFilters.compressorRatio = af["compressorRatio"].toDouble(3.0);
    }
    c.displayLabel = o["displayLabel"].toString();
    if (c.type == ClipType::Text) {
        c.textFontFamily = o["textFontFamily"].toString(c.textFontFamily);
        c.textFontSize = o["textFontSize"].toInt(c.textFontSize);
        c.textFontColor = o["textFontColor"].toString(c.textFontColor);
        c.textBold = o["textBold"].toBool(c.textBold);
        c.textItalic = o["textItalic"].toBool(c.textItalic);
        c.textUnderline = o["textUnderline"].toBool(c.textUnderline);
        c.textAlignment = o["textAlignment"].toInt(c.textAlignment);
        c.textOutlineEnabled = o["textOutlineEnabled"].toBool(c.textOutlineEnabled);
        c.textOutlineColor = o["textOutlineColor"].toString(c.textOutlineColor);
        c.textOutlineWidth = o["textOutlineWidth"].toInt(c.textOutlineWidth);
        c.textBackgroundEnabled = o["textBackgroundEnabled"].toBool(c.textBackgroundEnabled);
        c.textBackgroundColor = o["textBackgroundColor"].toString(c.textBackgroundColor);
        c.textPadding = o["textPadding"].toInt(c.textPadding);
    }
    if (o.contains("blendMode")) {
        c.blendMode = static_cast<BlendMode>(o["blendMode"].toInt(0));
    }
    if (o.contains("effects")) {
        const QJsonArray effArr = o["effects"].toArray();
        for (const auto& v : effArr) {
            const QJsonObject eo = v.toObject();
            Effect eff;
            eff.type = eo["type"].toString();
            eff.enabled = eo["enabled"].toBool(true);
            if (eo.contains("params")) {
                const QJsonObject po = eo["params"].toObject();
                for (auto it = po.begin(); it != po.end(); ++it) {
                    eff.params.push_back(EffectParameter{it.key(), it.value().toDouble()});
                }
            }
            c.effects.push_back(eff);
        }
    }
    return c;
}

} // namespace

Project::Project(QObject* parent) : QObject(parent), m_timeline(std::make_unique<Timeline>(this)) {}

MediaAssetPtr Project::importMedia(const QString& path, QString* errorOut) {
    auto asset = MediaAsset::probe(path, errorOut);
    if (!asset) return nullptr;
    m_assets.push_back(asset);
    emit assetsChanged();
    return asset;
}

MediaAssetPtr Project::findAsset(const QString& assetId) const {
    for (auto& a : m_assets) {
        if (a->id == assetId) return a;
    }
    return nullptr;
}

bool Project::removeAsset(const QString& assetId) {
    const auto before = m_assets.size();
    m_assets.erase(std::remove_if(m_assets.begin(), m_assets.end(),
                                   [&](const MediaAssetPtr& a) { return a->id == assetId; }),
                   m_assets.end());
    const bool changed = m_assets.size() != before;
    if (changed) emit assetsChanged();
    return changed;
}

MediaAssetPtr Project::relinkAsset(const QString& assetId, const QString& newPath, QString* errorOut) {
    auto existing = findAsset(assetId);
    if (!existing) {
        if (errorOut) *errorOut = QStringLiteral("Không tìm thấy media cần liên kết lại.");
        return nullptr;
    }
    auto probed = MediaAsset::probe(newPath, errorOut);
    if (!probed) return nullptr;
    // Keep the original id so every clip referencing this asset stays linked.
    probed->id = existing->id;
    for (auto& a : m_assets) {
        if (a->id == assetId) { a = probed; break; }
    }
    emit assetsChanged();
    return probed;
}

void Project::pushUndoSnapshot() {
    m_undoStack.push_back(m_timeline->tracks());
    if (static_cast<int>(m_undoStack.size()) > kMaxUndoDepth) {
        m_undoStack.erase(m_undoStack.begin());
    }
    m_redoStack.clear();
}

bool Project::undo() {
    if (m_undoStack.empty()) return false;
    m_redoStack.push_back(m_timeline->tracks());
    if (static_cast<int>(m_redoStack.size()) > kMaxUndoDepth) {
        m_redoStack.erase(m_redoStack.begin());
    }
    m_timeline->tracks() = std::move(m_undoStack.back());
    m_undoStack.pop_back();
    emit m_timeline->tracksChanged();
    return true;
}

bool Project::redo() {
    if (m_redoStack.empty()) return false;
    m_undoStack.push_back(m_timeline->tracks());
    if (static_cast<int>(m_undoStack.size()) > kMaxUndoDepth) {
        m_undoStack.erase(m_undoStack.begin());
    }
    m_timeline->tracks() = std::move(m_redoStack.back());
    m_redoStack.pop_back();
    emit m_timeline->tracksChanged();
    return true;
}

bool Project::canUndo() const { return !m_undoStack.empty(); }
bool Project::canRedo() const { return !m_redoStack.empty(); }
void Project::clearUndoHistory() {
    m_undoStack.clear();
    m_redoStack.clear();
}

bool Project::saveToFile(const QString& path, QString* errorOut) {
    if (name.isEmpty() || name == "Untitled Project") {
        name = QFileInfo(path).completeBaseName();
    }
    QJsonObject root;
    root["formatVersion"] = 1;
    root["name"] = name;

    QJsonArray assetsArr;
    // Save media paths relative to the .hcproj's own directory whenever the
    // file lives inside it, so a saved project stays portable (opening it
    // from anywhere, or moving the whole folder with its media, keeps the
    // media findable). Files outside the project folder keep their absolute
    // path. On load, resolveMediaPath() resolves relative entries against
    // the project directory first, then the CWD, so both cases round-trip.
    const QDir projDir = QFileInfo(path).absoluteDir();
    for (auto& a : m_assets) {
        QString assetPath = a->filePath;
        if (!assetPath.isEmpty() && projDir.exists()) {
            const QFileInfo assetInfo(assetPath);
            const QString absPath = assetInfo.absoluteFilePath();
            if (QFileInfo::exists(absPath)) {
                const QString rel = projDir.relativeFilePath(absPath);
                if (!rel.startsWith("..") && !QDir::isAbsolutePath(rel)) {
                    assetPath = rel;
                }
            }
        }
        QJsonObject ao;
        ao["id"] = a->id;
        ao["filePath"] = assetPath;
        ao["displayName"] = a->displayName;
        ao["kind"] = mediaKindToString(a->kind);
        ao["duration"] = QString::number(a->duration);
        ao["width"] = a->width;
        ao["height"] = a->height;
        ao["frameRate"] = a->frameRate;
        ao["videoStreamIndex"] = a->videoStreamIndex;
        ao["audioStreamIndex"] = a->audioStreamIndex;
        ao["sampleRate"] = a->sampleRate;
        ao["channels"] = a->channels;
        ao["bitRate"] = static_cast<qint64>(a->bitRate);
        assetsArr.append(ao);
    }
    root["assets"] = assetsArr;

    QJsonObject tl;
    tl["frameRate"] = m_timeline->frameRate;
    tl["videoWidth"] = m_timeline->videoWidth;
    tl["videoHeight"] = m_timeline->videoHeight;

    QJsonArray tracksArr;
    for (auto& t : m_timeline->tracks()) {
        QJsonObject to;
        to["id"] = t.id;
        to["type"] = trackTypeToString(t.type);
        to["name"] = t.name;
        to["muted"] = t.muted;
        to["hidden"] = t.hidden;
        to["locked"] = t.locked;
        QJsonArray clipsArr;
        for (auto& c : t.clips()) clipsArr.append(clipToJson(c));
        to["clips"] = clipsArr;
        tracksArr.append(to);
    }
    tl["tracks"] = tracksArr;
    root["timeline"] = tl;

    // Ensure the destination directory exists before writing. Without this,
    // saving into a not-yet-created project folder (e.g. a fresh "New
    // Project" location, or a path typed by hand) silently fails with an
    // unhelpful "can't write file" error instead of just creating the folder.
    const QFileInfo pathInfo(path);
    const QDir parentDir = pathInfo.dir();
    if (!parentDir.exists() && !parentDir.mkpath(".")) {
        if (errorOut) *errorOut = QStringLiteral("Không tạo được thư mục để lưu dự án: %1").arg(parentDir.path());
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut) *errorOut = QStringLiteral("Không ghi được file dự án: %1").arg(path);
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    filePath = path;
    return true;
}

bool Project::loadFromFile(const QString& path, QString* errorOut) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorOut) *errorOut = QStringLiteral("Không mở được file dự án: %1").arg(path);
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorOut) *errorOut = QStringLiteral("File dự án bị lỗi định dạng: %1").arg(parseError.errorString());
        return false;
    }

    const QJsonObject root = doc.object();
    name = root["name"].toString(QFileInfo(path).completeBaseName());

    m_assets.clear();
    const QDir projectDir = QFileInfo(path).absoluteDir();
    for (const auto& v : root["assets"].toArray()) {
        const QJsonObject ao = v.toObject();
        const QString mediaPath = ao["filePath"].toString();
        // Prefer a relative asset path resolved against the project's own
        // directory (see resolveMediaPath), not blindly the CWD.
        const QString probePath = resolveMediaPath(mediaPath, projectDir.path());
        QString probeErr;
        auto asset = MediaAsset::probe(probePath, &probeErr);
        if (asset) {
            asset->id = ao["id"].toString(asset->id); // preserve original id for clip refs
            // Point the asset at the resolved (usable) file so this session's
            // preview + export actually open the media, even when the stored
            // string was relative and only resolvable off the project folder.
            asset->filePath = probePath;
            m_assets.push_back(asset);
        } else {
            // Media is missing (file moved/deleted). Keep a placeholder asset
            // with the *same id* so clips stay addressable — the Exporter will
            // render those clips as a black gap, the media pool shows them as
            // "bị mất", and the user can relink the file later without losing
            // their cuts.
            auto stub = std::make_shared<MediaAsset>();
            stub->id = ao["id"].toString();
            stub->filePath = mediaPath;
            stub->displayName = ao["displayName"].toString(mediaPath.isEmpty() ? mediaPath : QFileInfo(mediaPath).fileName());
            stub->kind = mediaKindFromString(ao["kind"].toString());
            stub->duration = ao["duration"].toString().toLongLong();
            stub->width = ao["width"].toInt(0);
            stub->height = ao["height"].toInt(0);
            stub->frameRate = ao["frameRate"].toDouble(0.0);
            stub->videoStreamIndex = ao["videoStreamIndex"].toInt(-1);
            stub->audioStreamIndex = ao["audioStreamIndex"].toInt(-1);
            stub->sampleRate = ao["sampleRate"].toInt(0);
            stub->channels = ao["channels"].toInt(0);
            stub->bitRate = ao["bitRate"].toVariant().toLongLong();
            m_assets.push_back(stub);
        }
    }

    m_timeline = std::make_unique<Timeline>(this);
    const QJsonObject tl = root["timeline"].toObject();
    m_timeline->frameRate = tl["frameRate"].toDouble(30.0);
    m_timeline->videoWidth = tl["videoWidth"].toInt(1920);
    m_timeline->videoHeight = tl["videoHeight"].toInt(1080);

    for (const auto& tv : tl["tracks"].toArray()) {
        const QJsonObject to = tv.toObject();
        Track t;
        t.id = to["id"].toString(t.id);
        t.type = trackTypeFromString(to["type"].toString());
        t.name = to["name"].toString();
        t.muted = to["muted"].toBool(false);
        t.hidden = to["hidden"].toBool(false);
        t.locked = to["locked"].toBool(false);
        for (const auto& cv : to["clips"].toArray()) {
            t.addClip(clipFromJson(cv.toObject()));
        }
        m_timeline->tracks().push_back(std::move(t));
    }

    filePath = path;
    clearUndoHistory();
    emit assetsChanged();
    emit projectLoaded();
    return true;
}

} // namespace hc
