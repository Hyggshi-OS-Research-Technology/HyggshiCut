#pragma once
#include <QDialog>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace hc {

class PluginManagerDialog : public QDialog {
    Q_OBJECT
public:
    explicit PluginManagerDialog(QWidget* parent = nullptr);

private slots:
    void onLoadPlugin();
    void onRemovePlugin();
    void onToggleEnable();
    void onSelectionChanged(int row);
    void refresh();

private:
    QListWidget* m_list = nullptr;
    QLabel* m_detailsLabel = nullptr;
    QPushButton* m_loadBtn = nullptr;
    QPushButton* m_removeBtn = nullptr;
    QPushButton* m_toggleBtn = nullptr;
};

} // namespace hc
