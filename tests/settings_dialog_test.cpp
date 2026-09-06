#include <QApplication>
#include <QDebug>
#include <cassert>
#include <iostream>
#include "../src/i18n/LanguageManager.h"
#include "../src/ui/ThemeManager.h"
#include "../src/ui/WindowSettingsDialog.h"
#include "../src/ui/MainWindow.h"
#include "../src/ui/TimelineWidget.h"
#include "../src/playback/PlaybackController.h"
#include <QPainter>
#include <QImage>
#include <QMouseEvent>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    std::cout << "[TEST] 1. Initializing LanguageManager with 'vi'..." << std::endl;
    hc::LanguageManager::instance().setLanguage("vi");

    std::cout << "[TEST] 2. Verifying translations..." << std::endl;
    QString okText = hc::LanguageManager::instance().translate("dialog.ok");
    QString applyText = hc::LanguageManager::instance().translate("dialog.apply");
    QString cancelText = hc::LanguageManager::instance().translate("dialog.cancel");
    std::cout << "  dialog.ok: " << okText.toStdString() << std::endl;
    std::cout << "  dialog.apply: " << applyText.toStdString() << std::endl;
    std::cout << "  dialog.cancel: " << cancelText.toStdString() << std::endl;

    assert(!okText.isEmpty() && okText != "dialog.ok");
    assert(!applyText.isEmpty() && applyText != "dialog.apply");
    assert(!cancelText.isEmpty() && cancelText != "dialog.cancel");

    std::cout << "[TEST] 3. Testing English translations..." << std::endl;
    hc::LanguageManager::instance().setLanguage("en");
    okText = hc::LanguageManager::instance().translate("dialog.ok");
    applyText = hc::LanguageManager::instance().translate("dialog.apply");
    cancelText = hc::LanguageManager::instance().translate("dialog.cancel");
    std::cout << "  dialog.ok (en): " << okText.toStdString() << std::endl;
    std::cout << "  dialog.apply (en): " << applyText.toStdString() << std::endl;
    std::cout << "  dialog.cancel (en): " << cancelText.toStdString() << std::endl;
    assert(okText == "OK");
    assert(applyText == "Apply");
    assert(cancelText == "Cancel");

    // Revert back to vi for dialog testing
    hc::LanguageManager::instance().setLanguage("vi");

    std::cout << "[TEST] 4. Instantiating WindowSettingsDialog..." << std::endl;
    {
        hc::WindowSettingsDialog dialog(nullptr);
        assert(dialog.minimumWidth() >= 700);

        std::cout << "  Dialog minimum size: " << dialog.minimumWidth() << "x" << dialog.minimumHeight() << std::endl;

        // Verify tabs
        dialog.setCurrentTab(hc::SettingsTab::Window);
        dialog.setCurrentTab(hc::SettingsTab::Appearance);
        dialog.setCurrentTab(hc::SettingsTab::Language);
        dialog.setCurrentTab(hc::SettingsTab::Graphics);
        dialog.setCurrentTab(hc::SettingsTab::Proxy);
        dialog.setCurrentTab(hc::SettingsTab::About);

        std::cout << "  Tested all 6 tabs switching successfully." << std::endl;
    }

    std::cout << "[TEST] 5. Testing ThemeManager theme switching..." << std::endl;
    hc::ThemeManager::setTheme("dark");
    hc::ThemeManager::setTheme("light");
    hc::ThemeManager::setTheme("dark");
    std::cout << "  Theme switching completed without errors." << std::endl;

    std::cout << "[TEST] 6. Testing MainWindow lifecycle and clean destruction..." << std::endl;
    {
        auto mainWindow = std::make_unique<hc::MainWindow>();
        // Trigger theme change while MainWindow is alive
        hc::ThemeManager::setTheme("light");
        hc::ThemeManager::setTheme("dark");
        // Destruct mainWindow now - should not trigger unique_ptr assertion
    }
    std::cout << "  MainWindow destroyed cleanly with 0 crashes!" << std::endl;

    std::cout << "[TEST] 7. Testing TimelineWidget rendering & visual verification..." << std::endl;
    {
        auto proj = std::make_unique<hc::Project>();
        proj->timeline().addTrack(hc::TrackType::Visual, "Visual 1");
        proj->timeline().addTrack(hc::TrackType::Audio, "Audio 1");

        auto& vTrack = proj->timeline().tracks()[0];
        auto& aTrack = proj->timeline().tracks()[1];

        hc::Clip clip1;
        clip1.type = hc::ClipType::Video;
        clip1.displayLabel = "Gameplay_Intro.mp4";
        clip1.timelineStart = hc::secondsToTicks(1.0);
        clip1.sourceIn = 0;
        clip1.sourceOut = hc::secondsToTicks(6.0);
        clip1.fadeInDuration = hc::secondsToTicks(0.5);
        vTrack.addClip(std::move(clip1));

        hc::Clip clip2;
        clip2.type = hc::ClipType::Audio;
        clip2.displayLabel = "Background_Beat.mp3";
        clip2.timelineStart = hc::secondsToTicks(0.5);
        clip2.sourceIn = 0;
        clip2.sourceOut = hc::secondsToTicks(8.0);
        clip2.fadeOutDuration = hc::secondsToTicks(1.0);
        aTrack.addClip(std::move(clip2));

        auto timelineWidget = std::make_unique<hc::TimelineWidget>(proj.get());
        timelineWidget->setPlayheadTime(hc::secondsToTicks(3.5));
        timelineWidget->resize(1000, 160);

        QImage img(1000, 160, QImage::Format_ARGB32_Premultiplied);
        img.fill(Qt::transparent);
        QPainter p(&img);
        timelineWidget->render(&p);
        p.end();

        const QString outPath = "/home/hyggshi/.gemini/antigravity-ide/brain/29584d24-aa27-42bc-8c6d-6bfaed7c4155/timeline_preview.png";
        bool saved = img.save(outPath);
        std::cout << "  Timeline preview image saved to " << outPath.toStdString() << " (success=" << saved << ")" << std::endl;
        assert(saved);

        // Test active controls: muted audio track, locked visual track
        aTrack.muted = true;
        vTrack.locked = true;
        img.fill(Qt::transparent);
        QPainter p2(&img);
        timelineWidget->render(&p2);
        p2.end();
        const QString outPath2 = "/home/hyggshi/.gemini/antigravity-ide/brain/29584d24-aa27-42bc-8c6d-6bfaed7c4155/timeline_controls_preview.png";
        img.save(outPath2);
        std::cout << "  Timeline controls preview image saved to " << outPath2.toStdString() << std::endl;
    }

    std::cout << "[TEST] 8. Testing Timeline scrubbing and playhead dragging interaction..." << std::endl;
    {
        // 8a. Test PlaybackController seeking on empty project (should NOT clamp to 0)
        auto emptyProj = std::make_unique<hc::Project>();
        auto playback = std::make_unique<hc::PlaybackController>(emptyProj.get(), nullptr);
        playback->seek(hc::secondsToTicks(2.5));
        std::cout << "  Empty project seek(2.5s) -> currentTime: " << hc::ticksToSeconds(playback->currentTime()) << "s" << std::endl;
        assert(playback->currentTime() == hc::secondsToTicks(2.5));

        // 8b. Test TimelineWidget mouse scrubbing from playhead shield
        auto timelineWidget = std::make_unique<hc::TimelineWidget>(emptyProj.get());
        timelineWidget->resize(1000, 200);
        timelineWidget->show();

        hc::Ticks lastSeekTime = -1;
        QObject::connect(timelineWidget.get(), &hc::TimelineWidget::seekRequested, [&](hc::Ticks t) {
            lastSeekTime = t;
        });

        // Click on playhead shield at time 0 (around x = 220, y = 10)
        const QPointF ptShield(220, 10);
        QMouseEvent pressShield(QEvent::MouseButtonPress, ptShield, ptShield, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(timelineWidget.get(), &pressShield);

        // Drag to x = 400
        const QPointF ptMove(400, 10);
        QMouseEvent moveShield(QEvent::MouseMove, ptMove, ptMove, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(timelineWidget.get(), &moveShield);

        const hc::Ticks expectedTime = timelineWidget->pixelToTime(400);
        std::cout << "  Dragged shield to x=400 -> playheadTime: " << hc::ticksToSeconds(timelineWidget->playheadTime()) << "s" << std::endl;
        assert(timelineWidget->playheadTime() == expectedTime);
        assert(timelineWidget->playheadTime() > 0);

        // Release at x = 400
        QMouseEvent releaseShield(QEvent::MouseButtonRelease, ptMove, ptMove, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(timelineWidget.get(), &releaseShield);
        assert(lastSeekTime == expectedTime);

        // 8c. Test scrubbing from empty track background (e.g. x = 500, y = 80)
        emptyProj->timeline().addTrack(hc::TrackType::Visual, "Visual 1");
        timelineWidget->refresh();
        QImage emptyImg(1000, 160, QImage::Format_ARGB32_Premultiplied);
        emptyImg.fill(Qt::transparent);
        QPainter pEmpty(&emptyImg);
        timelineWidget->render(&pEmpty);
        pEmpty.end();
        const QString emptyPath = "/home/hyggshi/.gemini/antigravity-ide/brain/29584d24-aa27-42bc-8c6d-6bfaed7c4155/timeline_empty_preview.png";
        emptyImg.save(emptyPath);

        const QPointF ptTrack(500, 80);
        QMouseEvent pressTrack(QEvent::MouseButtonPress, ptTrack, ptTrack, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(timelineWidget.get(), &pressTrack);
        const hc::Ticks expectedTrackTime = timelineWidget->pixelToTime(500);
        assert(timelineWidget->playheadTime() == expectedTrackTime);

        std::cout << "  Timeline playhead dragging & empty track scrubbing verified successfully!" << std::endl;
    }

    std::cout << "ALL TESTS PASSED SUCCESSFULLY!" << std::endl;
    return 0;
}
