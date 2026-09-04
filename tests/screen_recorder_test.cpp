#include <iostream>
#include <cassert>
#include <QCoreApplication>
#include <QTimer>
#include <QDir>
#include <QFileInfo>
#include "capture/ScreenRecorder.h"

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);

    std::cout << "Running ScreenRecorderTest...\n";

    bool gnomeAvailable = hc::ScreenRecorder::isGnomeScreencastAvailable();
    std::cout << "GNOME Screencast D-Bus available: " << (gnomeAvailable ? "YES" : "NO") << "\n";

    hc::ScreenRecorder recorder;
    assert(recorder.state() == hc::ScreenRecorderState::Idle);

    hc::ScreenRecordSettings settings;
    settings.areaMode = hc::CaptureAreaMode::FullScreen;
    settings.audioMode = hc::AudioRecordMode::Microphone;
    settings.fps = 30;
    settings.countdownSeconds = 0; // Immediate
    settings.outputFolder = QDir::tempPath();

    bool startedSignalReceived = false;
    bool finishedSignalReceived = false;
    QString resultPath;

    QObject::connect(&recorder, &hc::ScreenRecorder::recordingStarted, [&](const QString& path) {
        startedSignalReceived = true;
        std::cout << "Signal recordingStarted received: " << path.toStdString() << "\n";
    });

    QObject::connect(&recorder, &hc::ScreenRecorder::recordingFinished, [&](const QString& path, int duration, qint64 size) {
        finishedSignalReceived = true;
        resultPath = path;
        std::cout << "Signal recordingFinished received: " << path.toStdString()
                  << " (duration=" << duration << "s, size=" << size << " bytes)\n";
        app.quit();
    });

    QObject::connect(&recorder, &hc::ScreenRecorder::recordingError, [&](const QString& err) {
        std::cout << "Recording error: " << err.toStdString() << "\n";
        app.quit();
    });

    bool ok = recorder.start(settings);
    assert(ok);
    assert(recorder.state() == hc::ScreenRecorderState::Recording);
    assert(startedSignalReceived);
    std::cout << "PASS: ScreenRecorder started cleanly!\n";

    // Stop recording after 1.5 seconds
    QTimer::singleShot(1500, [&]() {
        std::cout << "Triggering recorder.stop()...\n";
        recorder.stop();
    });

    // Timeout safety fallback
    QTimer::singleShot(10000, [&]() {
        std::cerr << "Timeout waiting for recorder to finish\n";
        app.quit();
    });

    app.exec();

    assert(finishedSignalReceived);
    assert(!resultPath.isEmpty());
    assert(QFileInfo::exists(resultPath));
    assert(QFileInfo(resultPath).size() > 0);
    std::cout << "Recorded file verified: " << resultPath.toStdString()
              << " size=" << QFileInfo(resultPath).size() << " bytes\n";

    // Clean up test file
    QFile::remove(resultPath);

    std::cout << "ALL SCREEN RECORDER TESTS PASSED!\n";
    return 0;
}
