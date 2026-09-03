#pragma once
#include <QWidget>
#include <QTimer>

namespace hc {

class AudioMeterWidget : public QWidget {
    Q_OBJECT
public:
    explicit AudioMeterWidget(QWidget* parent = nullptr);

public slots:
    // Levels in linear range [0.0, 2.0] or dB
    void setLevels(float leftPeak, float rightPeak);
    void reset();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    float m_leftLevel = 0.0f;
    float m_rightLevel = 0.0f;
    float m_leftPeakHold = 0.0f;
    float m_rightPeakHold = 0.0f;
    int m_leftPeakHoldCounter = 0;
    int m_rightPeakHoldCounter = 0;

    QTimer* m_decayTimer = nullptr;
};

} // namespace hc
