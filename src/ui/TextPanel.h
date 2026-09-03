#pragma once

#include <QWidget>
#include <QGroupBox>
#include <QFormLayout>
#include <QPlainTextEdit>
#include <QFontComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QButtonGroup>
#include "../core/Project.h"

namespace hc {

class TextPanel : public QWidget {
    Q_OBJECT
public:
    explicit TextPanel(QWidget* parent = nullptr);

    void setSelectedClip(Project* project, const QString& trackId, const QString& clipId);
    void clearSelection();

public slots:
    void retranslateUi();

signals:
    void textEdited();

private slots:
    void onContentChanged();
    void onFontFamilyChanged(const QFont& font);
    void onFontSizeChanged(int val);
    void onTextColorClicked();
    void onBoldToggled(bool checked);
    void onItalicToggled(bool checked);
    void onUnderlineToggled(bool checked);
    void onAlignChanged(int id);
    void onOutlineToggled(bool checked);
    void onOutlineColorClicked();
    void onOutlineWidthChanged(int val);
    void onBackgroundToggled(bool checked);
    void onBackgroundColorClicked();
    void onPaddingChanged(int val);

private:
    void updateUiFromClip();
    void updateColorButton(QPushButton* btn, const QString& hexColor);
    Clip* getSelectedClip();

    Project* m_project = nullptr;
    QString m_trackId;
    QString m_clipId;
    bool m_updatingUi = false;

    QGroupBox* m_contentGroup = nullptr;
    QGroupBox* m_fontGroup = nullptr;
    QGroupBox* m_outlineGroup = nullptr;
    QGroupBox* m_bgGroup = nullptr;
    QFormLayout* m_fontForm = nullptr;

    QPlainTextEdit* m_contentEdit = nullptr;
    QFontComboBox* m_fontCombo = nullptr;
    QSpinBox* m_fontSizeSpin = nullptr;
    QPushButton* m_textColorBtn = nullptr;
    QString m_textColorHex = "#FFFFFF";

    QPushButton* m_boldBtn = nullptr;
    QPushButton* m_italicBtn = nullptr;
    QPushButton* m_underlineBtn = nullptr;

    QButtonGroup* m_alignGroup = nullptr;
    QPushButton* m_alignLeftBtn = nullptr;
    QPushButton* m_alignCenterBtn = nullptr;
    QPushButton* m_alignRightBtn = nullptr;

    QCheckBox* m_outlineCheck = nullptr;
    QPushButton* m_outlineColorBtn = nullptr;
    QString m_outlineColorHex = "#000000";
    QSpinBox* m_outlineWidthSpin = nullptr;

    QCheckBox* m_bgCheck = nullptr;
    QPushButton* m_bgColorBtn = nullptr;
    QString m_bgColorHex = "#00000080";
    QSpinBox* m_paddingSpin = nullptr;
};

} // namespace hc
