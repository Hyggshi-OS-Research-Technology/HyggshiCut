#include "PluginManagerDialog.h"
#include "../plugin/PluginManager.h"
#include "../i18n/LanguageManager.h"
#include <QHBoxLayout>
#include <QSplitter>
#include <QFileDialog>
#include <QMessageBox>
#include <QFrame>
#include <QScrollArea>

namespace hc {

PluginManagerDialog::PluginManagerDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(LTR("plugin.manager.title"));
    setMinimumSize(640, 420);
    resize(720, 480);

    setStyleSheet(R"(
        QDialog {
            background-color: #1a1a22;
            color: #ddd;
        }
        QListWidget {
            background-color: #12121a;
            border: 1px solid #333340;
            border-radius: 5px;
            color: #ddd;
        }
        QListWidget::item { padding: 7px 8px; border-radius: 3px; }
        QListWidget::item:selected { background-color: #2b5b84; color: white; font-weight: bold; }
        QLabel { color: #ccc; }
        QPushButton {
            background-color: #2b2b35;
            color: #eee;
            padding: 5px 14px;
            border-radius: 4px;
            border: 1px solid #444;
        }
        QPushButton:hover { background-color: #38384a; }
        QPushButton:pressed { background-color: #1a2a40; }
    )");

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);
    mainLayout->setSpacing(10);

    // Title
    auto* titleLabel = new QLabel(LTR("plugin.manager.installed"), this);
    titleLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #ff9944;");
    mainLayout->addWidget(titleLabel);

    // Splitter: left list, right details
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setHandleWidth(4);

    // Left: list
    m_list = new QListWidget(splitter);
    connect(m_list, &QListWidget::currentRowChanged, this, &PluginManagerDialog::onSelectionChanged);
    splitter->addWidget(m_list);

    // Right: details panel
    auto* detailsScroll = new QScrollArea(splitter);
    detailsScroll->setWidgetResizable(true);
    detailsScroll->setFrameShape(QFrame::NoFrame);

    m_detailsLabel = new QLabel(LTR("plugin.manager.noPlugins"), detailsScroll);
    m_detailsLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_detailsLabel->setWordWrap(true);
    m_detailsLabel->setStyleSheet("padding: 12px; color: #aaa; font-style: italic;");
    detailsScroll->setWidget(m_detailsLabel);
    splitter->addWidget(detailsScroll);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    mainLayout->addWidget(splitter, 1);

    // Bottom buttons
    auto* btnBar = new QHBoxLayout();
    btnBar->setSpacing(8);

    m_loadBtn = new QPushButton(LTR("plugin.manager.load"), this);
    m_loadBtn->setStyleSheet("background-color: #255d36; color: white; font-weight: bold;");
    connect(m_loadBtn, &QPushButton::clicked, this, &PluginManagerDialog::onLoadPlugin);

    m_toggleBtn = new QPushButton(LTR("plugin.manager.disable"), this);
    m_toggleBtn->setEnabled(false);
    connect(m_toggleBtn, &QPushButton::clicked, this, &PluginManagerDialog::onToggleEnable);

    m_removeBtn = new QPushButton(LTR("plugin.manager.remove"), this);
    m_removeBtn->setStyleSheet("background-color: #5d2525; color: white;");
    m_removeBtn->setEnabled(false);
    connect(m_removeBtn, &QPushButton::clicked, this, &PluginManagerDialog::onRemovePlugin);

    auto* closeBtn = new QPushButton(LTR("btn.close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    btnBar->addWidget(m_loadBtn);
    btnBar->addWidget(m_toggleBtn);
    btnBar->addWidget(m_removeBtn);
    btnBar->addStretch(1);
    btnBar->addWidget(closeBtn);
    mainLayout->addLayout(btnBar);

    // Connect PluginManager changes
    connect(&PluginManager::instance(), &PluginManager::pluginsChanged,
            this, &PluginManagerDialog::refresh);

    refresh();
}

void PluginManagerDialog::refresh() {
    const int prevRow = m_list->currentRow();
    m_list->clear();

    const auto plugins = PluginManager::instance().plugins();
    if (plugins.isEmpty()) {
        m_detailsLabel->setText(LTR("plugin.manager.noPlugins"));
        m_removeBtn->setEnabled(false);
        m_toggleBtn->setEnabled(false);
        return;
    }

    for (const auto& p : plugins) {
        auto* item = new QListWidgetItem(m_list);
        const QString status = p.enabled ? "✅" : "⬜";
        item->setText(QString("%1 %2  [%3]").arg(status, p.name, p.pluginId));
        item->setData(Qt::UserRole, p.pluginId);
    }

    if (prevRow >= 0 && prevRow < m_list->count()) {
        m_list->setCurrentRow(prevRow);
    } else if (m_list->count() > 0) {
        m_list->setCurrentRow(0);
    }
}

void PluginManagerDialog::onSelectionChanged(int row) {
    const auto plugins = PluginManager::instance().plugins();
    if (row < 0 || row >= plugins.size()) {
        m_detailsLabel->setText(LTR("plugin.manager.noPlugins"));
        m_removeBtn->setEnabled(false);
        m_toggleBtn->setEnabled(false);
        return;
    }

    const PluginInfo& p = plugins[row];
    m_removeBtn->setEnabled(true);
    m_toggleBtn->setEnabled(true);
    m_toggleBtn->setText(p.enabled ? LTR("plugin.manager.disable") : LTR("plugin.manager.enable"));

    QString details;
    details += QString("<b style='color:#ffaa44'>%1</b> v%2<br/>").arg(p.name, p.versionStr);
    details += QString("<br/><b>%1</b> %2<br/>").arg(LTR("plugin.manager.pluginId"), p.pluginId);
    details += QString("<b>%1</b> %2<br/>").arg(LTR("plugin.manager.author"), p.author);
    details += QString("<b>%1</b> %2<br/>").arg(LTR("plugin.manager.type"), p.type);
    if (!p.description.isEmpty()) {
        details += QString("<br/><b>%1</b><br/>%2<br/>").arg(LTR("plugin.manager.description"), p.description);
    }
    if (!p.effects.isEmpty()) {
        details += QString("<br/><b>%1</b><ul>").arg(LTR("plugin.manager.effects"));
        for (const auto& eff : p.effects) {
            details += QString("<li>%1 <small style='color:#aaa'>[%2]</small></li>").arg(eff.name, eff.effectType);
        }
        details += "</ul>";
    }
    details += QString("<br/><small style='color:#666'>%1</small>").arg(p.filePath);

    m_detailsLabel->setText(details);
    m_detailsLabel->setTextFormat(Qt::RichText);
}

void PluginManagerDialog::onLoadPlugin() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        LTR("plugin.manager.load"),
        QString(),
        "HyggshiCut Plugin (*.plhc);;All Files (*)");

    if (path.isEmpty()) return;

    QString err;
    if (PluginManager::instance().loadFromFile(path, &err)) {
        refresh();
    } else {
        QMessageBox::warning(this, tr("Plugin Load Failed"), err);
    }
}

void PluginManagerDialog::onRemovePlugin() {
    const int row = m_list->currentRow();
    const auto plugins = PluginManager::instance().plugins();
    if (row < 0 || row >= plugins.size()) return;

    const QString pluginId = plugins[row].pluginId;
    const QString name = plugins[row].name;

    if (QMessageBox::question(this, tr("Xác nhận"),
            tr("Gỡ bỏ plugin '%1'?").arg(name)) != QMessageBox::Yes) return;

    PluginManager::instance().removePlugin(pluginId);
    refresh();
}

void PluginManagerDialog::onToggleEnable() {
    const int row = m_list->currentRow();
    const auto plugins = PluginManager::instance().plugins();
    if (row < 0 || row >= plugins.size()) return;

    const QString pluginId = plugins[row].pluginId;
    const bool curEnabled = plugins[row].enabled;
    PluginManager::instance().setPluginEnabled(pluginId, !curEnabled);
    refresh();
}

} // namespace hc
