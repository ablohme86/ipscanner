#include <QApplication>
#include <QCommandLineParser>
#include <QTimer>
#include <QPixmap>
#include <iostream>
#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
    // Ensure smooth display on Wayland/X11
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
        qputenv("QT_QPA_PLATFORM", "xcb");
    }

    // Enable High DPI scaling for crisp display rendering
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);

    QApplication app(argc, argv);
    app.setApplicationName("IP Scanner");
    app.setApplicationDisplayName("Network IP Scanner");
    app.setOrganizationName("IPScanner");
    app.setApplicationVersion("1.0.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Network IP Scanner in Qt5 / C++");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption autoScanOpt("auto-scan", "Automatically start scanning on launch");
    QCommandLineOption screenshotOpt("screenshot", "Save a window screenshot to file", "file");
    QCommandLineOption exitAfterOpt("exit-after", "Exit after specified seconds", "seconds");
    parser.addOption(autoScanOpt);
    parser.addOption(screenshotOpt);
    parser.addOption(exitAfterOpt);

    parser.process(app);

    MainWindow window;
    window.show();

    if (parser.isSet(autoScanOpt)) {
        QTimer::singleShot(200, &window, [&window]() {
            window.startScanDirect();
        });
    }

    if (parser.isSet(screenshotOpt)) {
        QString path = parser.value(screenshotOpt);
        int delayMs = 3000;
        if (parser.isSet(exitAfterOpt)) {
            delayMs = qMax(500, parser.value(exitAfterOpt).toInt() * 1000 - 500);
        }
        QTimer::singleShot(delayMs, &window, [&window, path, &parser, &app]() {
            QPixmap pix = window.grab();
            if (pix.save(path)) {
                std::cout << "[INFO] Screenshot saved to " << path.toStdString() << std::endl;
            } else {
                std::cerr << "[ERROR] Failed to save screenshot to " << path.toStdString() << std::endl;
            }
            if (parser.isSet("exit-after")) {
                app.quit();
            }
        });
    }

    if (parser.isSet(exitAfterOpt) && !parser.isSet(screenshotOpt)) {
        int sec = parser.value(exitAfterOpt).toInt();
        QTimer::singleShot(sec * 1000, &app, &QApplication::quit);
    }

    return app.exec();
}
