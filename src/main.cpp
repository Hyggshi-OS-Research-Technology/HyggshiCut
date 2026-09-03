#include <QApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QFileInfo>
#include <QDir>
#include <iostream>
#include <clocale>
#if defined(__linux__)
#include <csignal>
#include <unistd.h>
#include <execinfo.h>
#include <cstdlib>
#include <cstring>
#endif
#include "ui/MainWindow.h"
#include "core/Project.h"
#include "export/Exporter.h"
#include "i18n/LanguageManager.h"
#include "plugin/PluginManager.h"

namespace {

#if defined(__linux__)
// Print a backtrace to stderr on a native crash (SEGV/ABRT/FPE/ILL), then
// restore the default handler and re-raise so the OS still dumps a core.
// Only async-signal-safe calls are allowed inside a signal handler, so we
// use ::write()/::backtrace_symbols_fd() directly rather than iostream.
// This is a diagnostics aid for tracing hard-to-reproduce crashes (the
// GUI crash at startup). It is guarded by __linux__ and does not change
// program behaviour in the normal case.
void crashHandler(int sig) {
    const char banner[] =
        "\n================================================================\n"
        "HyggshiCut crashed\n"
        "================================================================\n";
    ::write(STDERR_FILENO, banner, static_cast<ssize_t>(::strlen(banner)));

    void* frames[64];
    const int count = ::backtrace(frames, 64);
    ::write(STDERR_FILENO, "\nBacktrace:\n", 12);
    ::backtrace_symbols_fd(frames, count, STDERR_FILENO);
    ::write(STDERR_FILENO, "\n", 1);

    ::signal(sig, SIG_DFL);
    ::raise(sig);
}
#endif

void loadBundledAssets(const QApplication& app) {
    // 1. Discover Languages
    QStringList langDirs = {
        QDir(app.applicationDirPath()).filePath("languages"),
        QDir(app.applicationDirPath()).filePath("../languages"),
        QDir::current().filePath("languages"),
        "/home/hyggshi/Downloads/HyggshiCut/languages"
    };

    for (const auto& dPath : langDirs) {
        QDir dir(dPath);
        if (dir.exists()) {
            const auto files = dir.entryInfoList(QStringList() << "*.langhc", QDir::Files);
            for (const auto& fi : files) {
                hc::LanguageManager::instance().loadFromFile(fi.absoluteFilePath());
            }
            break;
        }
    }
    hc::LanguageManager::instance().loadPreference();

    // 2. Discover Plugins
    QStringList pluginDirs = {
        QDir(app.applicationDirPath()).filePath("plugins"),
        QDir(app.applicationDirPath()).filePath("../plugins"),
        QDir::current().filePath("plugins"),
        "/home/hyggshi/Downloads/HyggshiCut/plugins"
    };

    for (const auto& dPath : pluginDirs) {
        QDir dir(dPath);
        if (dir.exists()) {
            const auto files = dir.entryInfoList(QStringList() << "*.plhc", QDir::Files);
            for (const auto& fi : files) {
                hc::PluginManager::instance().loadFromFile(fi.absoluteFilePath());
            }
            break;
        }
    }
    hc::PluginManager::instance().loadState();
}

void printProgressBar(double progress, const QString& statusText = "") {
    const int barWidth = 20;
    const double clamped = std::clamp(progress, 0.0, 1.0);
    const int pct = static_cast<int>(std::round(clamped * 100.0));
    const int pos = static_cast<int>(std::round(barWidth * clamped));

    std::cout << "\r" << QString("%1%").arg(pct, 3).toStdString() << " [";
    for (int i = 0; i < barWidth; ++i) {
        if (i < pos) std::cout << "=";
        else std::cout << "-";
    }
    std::cout << "]";
    if (!statusText.isEmpty()) {
        std::cout << " " << statusText.toStdString();
    }
    std::cout << "    " << std::flush;
}

} // namespace

int main(int argc, char* argv[]) {
#if defined(__linux__)
    // Install crash-backtrace handlers so a startup segfault prints a stack
    // we can act on instead of only "xuát ra core". See crashHandler() above.
    ::signal(SIGSEGV, crashHandler);
    ::signal(SIGABRT, crashHandler);
    ::signal(SIGFPE, crashHandler);
    ::signal(SIGILL, crashHandler);
#endif

    // Normalize aliases like -ot to --ot so Qt doesn't treat it as clustered flags -o -t
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "-ot") == 0) {
            argv[i] = const_cast<char*>("--ot");
        }
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName("HyggshiCut");
    QApplication::setOrganizationName("Hyggshi OS Foundation");
    QApplication::setApplicationVersion("1.0.0");

    loadBundledAssets(app);

    std::setlocale(LC_NUMERIC, "C");

    QCommandLineParser parser;
    parser.setApplicationDescription("HyggshiCut - Video Editor & Headless Render CLI");
    parser.addHelpOption();
    parser.addVersionOption();

    // -p, --project <file>
    QCommandLineOption projectOption(
        QStringList() << "p" << "project",
        "Đường dẫn file dự án .hcproj",
        "file");
    parser.addOption(projectOption);

    // -r, --render
    QCommandLineOption renderOption(
        QStringList() << "r" << "render",
        "Chạy render dự án ở chế độ dòng lệnh (headless)");
    parser.addOption(renderOption);

    // -o, -ot, --output, --output-target <file>
    QCommandLineOption outputOption(
        QStringList() << "o" << "ot" << "output" << "output-target",
        "Đường dẫn file video xuất ra",
        "file");
    parser.addOption(outputOption);

    // --no-gui
    QCommandLineOption noGuiOption(
        "no-gui",
        "Chế độ không giao diện GUI (tự động bật render)");
    parser.addOption(noGuiOption);

    // --preset <name>
    QCommandLineOption presetOption(
        "preset",
        "Preset xuất video: youtube-1080p, youtube-4k, tiktok-9-16, instagram-1-1, prores, audio-mp3, audio-wav",
        "name");
    parser.addOption(presetOption);

    // --codec <name>
    QCommandLineOption codecOption(
        "codec",
        "Video codec: h264, hevc / h265, vp9, av1, prores, none",
        "name");
    parser.addOption(codecOption);

    // --crf <val>
    QCommandLineOption crfOption(
        "crf",
        "Constant Quality CRF (0 - 51)",
        "val");
    parser.addOption(crfOption);

    // --bitrate <val>
    QCommandLineOption bitrateOption(
        "bitrate",
        "Target video bitrate in kbps (e.g. 8000k, 12000)",
        "val");
    parser.addOption(bitrateOption);

    // --width <val>
    QCommandLineOption widthOption(
        "width",
        "Độ rộng khung hình (width)",
        "px");
    parser.addOption(widthOption);

    // --height <val>
    QCommandLineOption heightOption(
        "height",
        "Chiều cao khung hình (height)",
        "px");
    parser.addOption(heightOption);

    // --fps <val>
    QCommandLineOption fpsOption(
        "fps",
        "Tốc độ khung hình (frame rate)",
        "val");
    parser.addOption(fpsOption);

    // --progress
    QCommandLineOption progressOption(
        "progress",
        "Hiển thị thanh tiến trình render trên terminal");
    parser.addOption(progressOption);

    parser.process(app);

    const bool isRenderMode = parser.isSet(renderOption) || parser.isSet(noGuiOption);
    const QString projectPath = parser.value(projectOption);

    // ━━━ HEADLESS CLI RENDER MODE ━━━
    if (isRenderMode) {
        if (projectPath.isEmpty()) {
            std::cerr << "Lỗi: Cần chỉ định file dự án với -p hoặc --project <file.hcproj>\n";
            return 1;
        }

        if (!QFileInfo::exists(projectPath)) {
            std::cerr << "Lỗi: Không tìm thấy file dự án: " << projectPath.toStdString() << "\n";
            return 1;
        }

        auto project = std::make_unique<hc::Project>();
        QString err;
        if (!project->loadFromFile(projectPath, &err)) {
            std::cerr << "Lỗi mở dự án: " << err.toStdString() << "\n";
            return 1;
        }

        // Setup Exporter Settings
        hc::Exporter::Settings settings;
        settings.width = project->timeline().videoWidth > 0 ? project->timeline().videoWidth : 1920;
        settings.height = project->timeline().videoHeight > 0 ? project->timeline().videoHeight : 1080;
        settings.frameRate = project->timeline().frameRate > 0 ? project->timeline().frameRate : 30.0;
        settings.videoCodec = "libx264";
        settings.rateControlMode = "crf";
        settings.crf = 23;
        settings.videoBitrateKbps = 8000;
        settings.preset = "medium";
        settings.pixelFormat = "yuv420p";
        settings.audioCodec = "aac";
        settings.audioBitrateKbps = 192;
        settings.audioSampleRate = 48000;
        settings.audioChannels = 2;

        QString ext = "mp4";

        // Apply Preset overrides
        if (parser.isSet(presetOption)) {
            const QString p = parser.value(presetOption).toLower();
            if (p.contains("youtube-1080p") || p.contains("1080p")) {
                settings.width = 1920; settings.height = 1080; settings.frameRate = 30.0;
                settings.videoCodec = "libx264"; settings.rateControlMode = "bitrate"; settings.videoBitrateKbps = 10000;
            } else if (p.contains("youtube-4k") || p.contains("4k")) {
                settings.width = 3840; settings.height = 2160; settings.frameRate = 60.0;
                settings.videoCodec = "libx264"; settings.rateControlMode = "bitrate"; settings.videoBitrateKbps = 35000;
            } else if (p.contains("tiktok") || p.contains("9-16") || p.contains("shorts")) {
                settings.width = 1080; settings.height = 1920; settings.frameRate = 30.0;
                settings.videoCodec = "libx264"; settings.rateControlMode = "crf"; settings.crf = 20;
            } else if (p.contains("instagram") || p.contains("1-1")) {
                settings.width = 1080; settings.height = 1080; settings.frameRate = 30.0;
                settings.videoCodec = "libx264"; settings.rateControlMode = "crf"; settings.crf = 20;
            } else if (p.contains("prores")) {
                ext = "mov"; settings.videoCodec = "prores_ks"; settings.pixelFormat = "yuv422p10le";
                settings.audioCodec = "pcm_s16le";
            } else if (p.contains("mp3")) {
                ext = "mp3"; settings.videoCodec = "none"; settings.audioCodec = "libmp3lame";
            } else if (p.contains("wav")) {
                ext = "wav"; settings.videoCodec = "none"; settings.audioCodec = "pcm_s16le";
            }
        }

        // Apply CLI Overrides
        if (parser.isSet(codecOption)) {
            const QString c = parser.value(codecOption).toLower();
            if (c == "h264" || c == "x264") settings.videoCodec = "libx264";
            else if (c == "hevc" || c == "h265" || c == "x265") settings.videoCodec = "libx265";
            else if (c == "vp9") settings.videoCodec = "libvpx-vp9";
            else if (c == "av1") settings.videoCodec = "libsvtav1";
            else if (c == "prores") settings.videoCodec = "prores_ks";
            else if (c == "none") settings.videoCodec = "none";
            else settings.videoCodec = c;
        }
        if (parser.isSet(crfOption)) {
            settings.rateControlMode = "crf";
            settings.crf = parser.value(crfOption).toInt();
        }
        if (parser.isSet(bitrateOption)) {
            settings.rateControlMode = "bitrate";
            QString b = parser.value(bitrateOption).toLower();
            b.remove('k');
            settings.videoBitrateKbps = b.toInt();
        }
        if (parser.isSet(widthOption)) settings.width = parser.value(widthOption).toInt();
        if (parser.isSet(heightOption)) settings.height = parser.value(heightOption).toInt();
        if (parser.isSet(fpsOption)) settings.frameRate = parser.value(fpsOption).toDouble();

        // Output path
        QString outputPath = parser.value(outputOption);
        if (outputPath.isEmpty()) {
            outputPath = QFileInfo(projectPath).completeBaseName() + "." + ext;
        }
        settings.outputPath = outputPath;

        std::cout << "====================================================\n";
        std::cout << " HyggshiCut Headless Render Engine\n";
        std::cout << "====================================================\n";
        std::cout << " Dự án:      " << projectPath.toStdString() << " (" << project->name.toStdString() << ")\n";
        std::cout << " Thời lượng: " << (project->timeline().totalDuration() / 1'000'000.0) << "s\n";
        std::cout << " Khung hình: " << settings.width << "x" << settings.height << " @ " << settings.frameRate << " fps\n";
        std::cout << " Codec:      Video: " << settings.videoCodec.toStdString() << " | Audio: " << settings.audioCodec.toStdString() << "\n";
        std::cout << " Xuất ra:    " << settings.outputPath.toStdString() << "\n";
        std::cout << "----------------------------------------------------\n";

        auto exporter = std::make_unique<hc::Exporter>(project.get());

        QObject::connect(exporter.get(), &hc::Exporter::progress, [](double p, const QString& eta) {
            printProgressBar(p, eta);
        });

        int exitCode = 0;
        QObject::connect(exporter.get(), &hc::Exporter::finished, [&](bool success, const QString& message) {
            std::cout << "\n----------------------------------------------------\n";
            if (success) {
                std::cout << " [SUCCESS] " << message.toStdString() << "\n";
                exitCode = 0;
            } else {
                std::cerr << " [FAILED]  " << message.toStdString() << "\n";
                exitCode = 1;
            }
            app.exit(exitCode);
        });

        exporter->start(settings);
        return app.exec();
    }

    // ━━━ GUI MODE ━━━
    hc::MainWindow window;

    // If a project was given without -r, open it in the GUI window. (This
    // used to load into a discarded local Project and silently drop the
    // result, so `HyggshiCut project.hcproj` always opened a blank editor.)
    if (!projectPath.isEmpty() && QFileInfo::exists(projectPath)) {
        QString err;
        if (!window.openProjectFromFile(projectPath, &err)) {
            std::cerr << "Không mở được dự án (" << projectPath.toStdString()
                      << "): " << err.toStdString() << "\n";
        }
    }

    window.show();
    return app.exec();
}
