#include "TextPanel.h"
#include "../i18n/LanguageManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QGroupBox>
#include <QColorDialog>

namespace hc {

TextPanel::TextPanel(QWidget* parent) : QWidget(parent) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);

    // 1. Text Content Input
    m_contentGroup = new QGroupBox(this);
    auto* contentLayout = new QVBoxLayout(m_contentGroup);
    m_contentEdit = new QPlainTextEdit(this);
    m_contentEdit->setMaximumHeight(70);
    contentLayout->addWidget(m_contentEdit);
    mainLayout->addWidget(m_contentGroup);

    // 2. Font & Size
    m_fontGroup = new QGroupBox(this);
    m_fontForm = new QFormLayout(m_fontGroup);
    m_fontForm->setSpacing(6);

    m_fontCombo = new QFontComboBox(this);
    m_fontForm->addRow(new QLabel(this), m_fontCombo);

    auto* sizeColorLayout = new QHBoxLayout();
    m_fontSizeSpin = new QSpinBox(this);
    m_fontSizeSpin->setRange(10, 300);
    m_fontSizeSpin->setValue(64);
    m_fontSizeSpin->setSuffix(" px");

    m_textColorBtn = new QPushButton(this);
    m_textColorBtn->setFixedWidth(60);
    updateColorButton(m_textColorBtn, m_textColorHex);

    sizeColorLayout->addWidget(m_fontSizeSpin, 1);
    sizeColorLayout->addWidget(new QLabel(LTR("text.color"), this));
    sizeColorLayout->addWidget(m_textColorBtn);
    m_fontForm->addRow(new QLabel(this), sizeColorLayout);

    // Style toggles (Bold, Italic, Underline) & Alignment
    auto* styleAlignLayout = new QHBoxLayout();
    m_boldBtn = new QPushButton(QStringLiteral("B"), this);
    m_boldBtn->setCheckable(true);
    m_boldBtn->setFixedWidth(32);
    QFont bFont = m_boldBtn->font();
    bFont.setBold(true);
    m_boldBtn->setFont(bFont);

    m_italicBtn = new QPushButton(QStringLiteral("I"), this);
    m_italicBtn->setCheckable(true);
    m_italicBtn->setFixedWidth(32);
    QFont iFont = m_italicBtn->font();
    iFont.setItalic(true);
    m_italicBtn->setFont(iFont);

    m_underlineBtn = new QPushButton(QStringLiteral("U"), this);
    m_underlineBtn->setCheckable(true);
    m_underlineBtn->setFixedWidth(32);
    QFont uFont = m_underlineBtn->font();
    uFont.setUnderline(true);
    m_underlineBtn->setFont(uFont);

    styleAlignLayout->addWidget(m_boldBtn);
    styleAlignLayout->addWidget(m_italicBtn);
    styleAlignLayout->addWidget(m_underlineBtn);
    styleAlignLayout->addSpacing(10);

    m_alignGroup = new QButtonGroup(this);
    m_alignLeftBtn = new QPushButton(QStringLiteral("⇤"), this);
    m_alignLeftBtn->setCheckable(true);
    m_alignLeftBtn->setFixedWidth(32);

    m_alignCenterBtn = new QPushButton(QStringLiteral("≡"), this);
    m_alignCenterBtn->setCheckable(true);
    m_alignCenterBtn->setFixedWidth(32);

    m_alignRightBtn = new QPushButton(QStringLiteral("⇥"), this);
    m_alignRightBtn->setCheckable(true);
    m_alignRightBtn->setFixedWidth(32);

    m_alignGroup->addButton(m_alignCenterBtn, 0);
    m_alignGroup->addButton(m_alignLeftBtn, 1);
    m_alignGroup->addButton(m_alignRightBtn, 2);

    styleAlignLayout->addWidget(m_alignLeftBtn);
    styleAlignLayout->addWidget(m_alignCenterBtn);
    styleAlignLayout->addWidget(m_alignRightBtn);
    styleAlignLayout->addStretch(1);

    m_fontForm->addRow(new QLabel(this), styleAlignLayout);
    mainLayout->addWidget(m_fontGroup);

    // 3. Text Outline / Stroke
    m_outlineGroup = new QGroupBox(this);
    auto* outlineLayout = new QHBoxLayout(m_outlineGroup);
    m_outlineCheck = new QCheckBox(this);
    m_outlineColorBtn = new QPushButton(this);
    m_outlineColorBtn->setFixedWidth(50);
    updateColorButton(m_outlineColorBtn, m_outlineColorHex);

    m_outlineWidthSpin = new QSpinBox(this);
    m_outlineWidthSpin->setRange(1, 20);
    m_outlineWidthSpin->setValue(2);
    m_outlineWidthSpin->setSuffix(" px");

    outlineLayout->addWidget(m_outlineCheck);
    outlineLayout->addWidget(new QLabel(LTR("text.color"), this));
    outlineLayout->addWidget(m_outlineColorBtn);
    outlineLayout->addWidget(new QLabel(LTR("text.outlineWidth"), this));
    outlineLayout->addWidget(m_outlineWidthSpin);
    mainLayout->addWidget(m_outlineGroup);

    // 4. Text Background Box
    m_bgGroup = new QGroupBox(this);
    auto* bgLayout = new QHBoxLayout(m_bgGroup);
    m_bgCheck = new QCheckBox(this);
    m_bgColorBtn = new QPushButton(this);
    m_bgColorBtn->setFixedWidth(50);
    updateColorButton(m_bgColorBtn, m_bgColorHex);

    m_paddingSpin = new QSpinBox(this);
    m_paddingSpin->setRange(0, 100);
    m_paddingSpin->setValue(12);
    m_paddingSpin->setSuffix(" px");

    bgLayout->addWidget(m_bgCheck);
    bgLayout->addWidget(new QLabel(LTR("text.color"), this));
    bgLayout->addWidget(m_bgColorBtn);
    bgLayout->addWidget(new QLabel(LTR("text.padding"), this));
    bgLayout->addWidget(m_paddingSpin);
    mainLayout->addWidget(m_bgGroup);

    mainLayout->addStretch(1);

    retranslateUi();
    connect(&LanguageManager::instance(), &LanguageManager::languageChanged, this, [this]() {
        retranslateUi();
    });

    // Connect signals
    connect(m_contentEdit, &QPlainTextEdit::textChanged, this, &TextPanel::onContentChanged);
    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this, &TextPanel::onFontFamilyChanged);
    connect(m_fontSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &TextPanel::onFontSizeChanged);
    connect(m_textColorBtn, &QPushButton::clicked, this, &TextPanel::onTextColorClicked);

    connect(m_boldBtn, &QPushButton::toggled, this, &TextPanel::onBoldToggled);
    connect(m_italicBtn, &QPushButton::toggled, this, &TextPanel::onItalicToggled);
    connect(m_underlineBtn, &QPushButton::toggled, this, &TextPanel::onUnderlineToggled);
    connect(m_alignGroup, &QButtonGroup::idClicked, this, &TextPanel::onAlignChanged);

    connect(m_outlineCheck, &QCheckBox::toggled, this, &TextPanel::onOutlineToggled);
    connect(m_outlineColorBtn, &QPushButton::clicked, this, &TextPanel::onOutlineColorClicked);
    connect(m_outlineWidthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &TextPanel::onOutlineWidthChanged);

    connect(m_bgCheck, &QCheckBox::toggled, this, &TextPanel::onBackgroundToggled);
    connect(m_bgColorBtn, &QPushButton::clicked, this, &TextPanel::onBackgroundColorClicked);
    connect(m_paddingSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &TextPanel::onPaddingChanged);

    clearSelection();
}

void TextPanel::updateColorButton(QPushButton* btn, const QString& hexColor) {
    QColor col(hexColor);
    if (!col.isValid()) col = Qt::white;
    btn->setStyleSheet(QString("background-color: %1; border: 1px solid #666; border-radius: 3px;").arg(col.name(QColor::HexArgb)));
}

Clip* TextPanel::getSelectedClip() {
    if (!m_project || m_trackId.isEmpty() || m_clipId.isEmpty()) return nullptr;
    auto track = m_project->timeline().findTrack(m_trackId);
    if (!track) return nullptr;
    return track->findClip(m_clipId);
}

void TextPanel::setSelectedClip(Project* project, const QString& trackId, const QString& clipId) {
    m_project = project;
    m_trackId = trackId;
    m_clipId = clipId;

    Clip* clip = getSelectedClip();
    if (!clip || clip->type != ClipType::Text) {
        clearSelection();
        return;
    }

    setEnabled(true);
    updateUiFromClip();
}

void TextPanel::clearSelection() {
    m_trackId.clear();
    m_clipId.clear();
    setEnabled(false);
}

void TextPanel::updateUiFromClip() {
    Clip* clip = getSelectedClip();
    if (!clip) return;

    m_updatingUi = true;
    m_contentEdit->setPlainText(clip->displayLabel);
    m_fontCombo->setCurrentFont(QFont(clip->textFontFamily));
    m_fontSizeSpin->setValue(clip->textFontSize > 0 ? clip->textFontSize : 64);

    m_textColorHex = clip->textFontColor.isEmpty() ? QStringLiteral("#FFFFFF") : clip->textFontColor;
    updateColorButton(m_textColorBtn, m_textColorHex);

    m_boldBtn->setChecked(clip->textBold);
    m_italicBtn->setChecked(clip->textItalic);
    m_underlineBtn->setChecked(clip->textUnderline);

    if (clip->textAlignment == 1) m_alignLeftBtn->setChecked(true);
    else if (clip->textAlignment == 2) m_alignRightBtn->setChecked(true);
    else m_alignCenterBtn->setChecked(true);

    m_outlineCheck->setChecked(clip->textOutlineEnabled);
    m_outlineColorHex = clip->textOutlineColor.isEmpty() ? QStringLiteral("#000000") : clip->textOutlineColor;
    updateColorButton(m_outlineColorBtn, m_outlineColorHex);
    m_outlineWidthSpin->setValue(clip->textOutlineWidth > 0 ? clip->textOutlineWidth : 2);

    m_bgCheck->setChecked(clip->textBackgroundEnabled);
    m_bgColorHex = clip->textBackgroundColor.isEmpty() ? QStringLiteral("#00000080") : clip->textBackgroundColor;
    updateColorButton(m_bgColorBtn, m_bgColorHex);
    m_paddingSpin->setValue(clip->textPadding >= 0 ? clip->textPadding : 12);

    m_updatingUi = false;
}

void TextPanel::onContentChanged() {
    if (m_updatingUi) return;
    Clip* clip = getSelectedClip();
    if (!clip) return;
    clip->displayLabel = m_contentEdit->toPlainText();
    emit textEdited();
}

void TextPanel::onFontFamilyChanged(const QFont& font) {
    if (m_updatingUi) return;
    Clip* clip = getSelectedClip();
    if (!clip) return;
    clip->textFontFamily = font.family();
    emit textEdited();
}

void TextPanel::onFontSizeChanged(int val) {
    if (m_updatingUi) return;
    Clip* clip = getSelectedClip();
    if (!clip) return;
    clip->textFontSize = val;
    emit textEdited();
}

void TextPanel::onTextColorClicked() {
    Clip* clip = getSelectedClip();
    if (!clip) return;
    QColor cur(clip->textFontColor.isEmpty() ? "#FFFFFF" : clip->textFontColor);
    QColor picked = QColorDialog::getColor(cur, this, tr("Chọn màu chữ"), QColorDialog::ShowAlphaChannel);
    if (picked.isValid()) {
        clip->textFontColor = picked.name(QColor::HexRgb);
        m_textColorHex = clip->textFontColor;
        updateColorButton(m_textColorBtn, m_textColorHex);
        emit textEdited();
    }
}

void TextPanel::onBoldToggled(bool checked) {
    if (m_updatingUi) return;
    Clip* clip = getSelectedClip();
    if (!clip) return;
    clip->textBold = checked;
    emit textEdited();
}

void TextPanel::onItalicToggled(bool checked) {
    if (m_updatingUi) return;
    Clip* clip = getSelectedClip();
    if (!clip) return;
    clip->textItalic = checked;
    emit textEdited();
}

void TextPanel::onUnderlineToggled(bool checked) {
    if (m_updatingUi) return;
    Clip* clip = getSelectedClip();
    if (!clip) return;
    clip->textUnderline = checked;
    emit textEdited();
}

void TextPanel::onAlignChanged(int id) {
    if (m_updatingUi) return;
    Clip* clip = getSelectedClip();
    if (!clip) return;
    clip->textAlignment = id;
    emit textEdited();
}

void TextPanel::onOutlineToggled(bool checked) {
    if (m_updatingUi) return;
    Clip* clip = getSelectedClip();
    if (!clip) return;
    clip->textOutlineEnabled = checked;
    emit textEdited();
}

void TextPanel::onOutlineColorClicked() {
    Clip* clip = getSelectedClip();
    if (!clip) return;
    QColor cur(clip->textOutlineColor.isEmpty() ? "#000000" : clip->textOutlineColor);
    QColor picked = QColorDialog::getColor(cur, this, tr("Chọn màu viền chữ"), QColorDialog::ShowAlphaChannel);
    if (picked.isValid()) {
        clip->textOutlineColor = picked.name(QColor::HexRgb);
        m_outlineColorHex = clip->textOutlineColor;
        updateColorButton(m_outlineColorBtn, m_outlineColorHex);
        emit textEdited();
    }
}

void TextPanel::onOutlineWidthChanged(int val) {
    if (m_updatingUi) return;
    Clip* clip = getSelectedClip();
    if (!clip) return;
    clip->textOutlineWidth = val;
    emit textEdited();
}

void TextPanel::onBackgroundToggled(bool checked) {
    if (m_updatingUi) return;
    Clip* clip = getSelectedClip();
    if (!clip) return;
    clip->textBackgroundEnabled = checked;
    emit textEdited();
}

void TextPanel::onBackgroundColorClicked() {
    Clip* clip = getSelectedClip();
    if (!clip) return;
    QColor cur(clip->textBackgroundColor.isEmpty() ? "#00000080" : clip->textBackgroundColor);
    QColor picked = QColorDialog::getColor(cur, this, tr("Chọn màu nền chữ"), QColorDialog::ShowAlphaChannel);
    if (picked.isValid()) {
        clip->textBackgroundColor = picked.name(QColor::HexArgb);
        m_bgColorHex = clip->textBackgroundColor;
        updateColorButton(m_bgColorBtn, m_bgColorHex);
        emit textEdited();
    }
}

void TextPanel::onPaddingChanged(int val) {
    if (m_updatingUi) return;
    Clip* clip = getSelectedClip();
    if (!clip) return;
    clip->textPadding = val;
    emit textEdited();
}

void TextPanel::retranslateUi() {
    if (m_contentGroup) m_contentGroup->setTitle(LTR("text.contentGroup"));
    if (m_contentEdit) m_contentEdit->setPlaceholderText(LTR("text.placeholder"));
    if (m_fontGroup) m_fontGroup->setTitle(LTR("text.fontGroup"));
    if (m_outlineGroup) m_outlineGroup->setTitle(LTR("text.outlineGroup"));
    if (m_outlineCheck) m_outlineCheck->setText(LTR("text.outlineEnable"));
    if (m_bgGroup) m_bgGroup->setTitle(LTR("text.bgGroup"));
    if (m_bgCheck) m_bgCheck->setText(LTR("text.bgEnable"));

    if (m_fontForm) {
        if (auto* l = qobject_cast<QLabel*>(m_fontForm->labelForField(m_fontCombo))) l->setText(LTR("text.font"));
    }
}

} // namespace hc
