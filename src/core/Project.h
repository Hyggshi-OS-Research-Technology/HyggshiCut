#pragma once
#include <QObject>
#include <QString>
#include <memory>
#include <vector>
#include "MediaAsset.h"
#include "Timeline.h"

namespace hc {

// Project bundles the imported media pool with the editable Timeline and
// handles serialization to a JSON ".hcproj" file. Media files themselves
// stay on disk at their original paths (referenced, not copied) — matching
// how lightweight editors like this are expected to behave.
class Project : public QObject {
    Q_OBJECT
public:
    explicit Project(QObject* parent = nullptr);

    QString name = "Untitled Project";
    QString filePath; // path to the .hcproj file, empty if never saved

    Timeline& timeline() {
        if (!m_timeline) {
            static Timeline s_empty;
            return s_empty;
        }
        return *m_timeline;
    }
    const Timeline& timeline() const {
        if (!m_timeline) {
            static const Timeline s_empty;
            return s_empty;
        }
        return *m_timeline;
    }

    const std::vector<MediaAssetPtr>& assets() const { return m_assets; }

    // Probes and adds a media file to the pool. Returns nullptr + error on failure.
    MediaAssetPtr importMedia(const QString& filePath, QString* errorOut = nullptr);
    MediaAssetPtr findAsset(const QString& assetId) const;
    bool removeAsset(const QString& assetId);

    // Re-probes `assetId` against a new file on disk (used when the original
    // media was moved/renamed). The asset keeps its id so existing clips stay
    // linked. Returns the new MediaAsset, or nullptr + error on failure.
    MediaAssetPtr relinkAsset(const QString& assetId, const QString& newPath, QString* errorOut = nullptr);

    // --- Undo/redo (snapshot-based: the whole timeline, including tracks
    // and clip placement, is captured per edit). Route every mutating UI
    // operation through pushHistory() BEFORE it changes the timeline. ---
    void pushUndoSnapshot();
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;
    void clearUndoHistory();

    bool saveToFile(const QString& path, QString* errorOut = nullptr);
    bool loadFromFile(const QString& path, QString* errorOut = nullptr);

signals:
    void assetsChanged();
    void projectLoaded();

private:
    std::unique_ptr<Timeline> m_timeline;
    std::vector<MediaAssetPtr> m_assets;

    // Snapshot-based undo history (vector<Track> is cheap to copy — clips are
    // plain data, no heap handles).
    std::vector<std::vector<Track>> m_undoStack;
    std::vector<std::vector<Track>> m_redoStack;
    static constexpr int kMaxUndoDepth = 100;
};

} // namespace hc
