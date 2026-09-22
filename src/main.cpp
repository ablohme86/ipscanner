#include <QApplication>
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QTimer>
#include <QPixmap>
#include <iostream>
#include <string>
#include <unistd.h>

#include "ui/MainWindow.h"
#include "tui/TuiApp.h"
#include "tui/TuiTheme.h"

int main(int argc, char *argv[])
{
    // Pre-parse command-line arguments to determine UI mode
    bool explicitGui = false;
    bool explicitTui = false;
    bool showHelp = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--gui" || arg == "-g") {
            explicitGui = true;
        } else if (arg == "--tui" || arg == "-t") {
            explicitTui = true;
        } else if (arg == "--help" || arg == "-h") {
            showHelp = true;
        }
    }

    bool isInteractiveTerminal = (isatty(STDIN_FILENO) != 0) && (isatty(STDOUT_FILENO) != 0);
    bool hasDisplay = !qEnvironmentVariableIsEmpty("DISPLAY") || !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY");

    bool runTui = false;
    if (explicitTui) {
        runTui = true;
    } else if (explicitGui) {
        runTui = false;
    } else {
        // As configured: automatically launch TUI when started from an interactive terminal or headless environment
        if (isInteractiveTerminal || !hasDisplay) {
            runTui = true;
        } else {
            runTui = false;
        }
    }

    // Fallback if GUI was requested or defaulted, but no display server exists
    if (!runTui && !hasDisplay) {
        if (isInteractiveTerminal) {
            std::cerr << "[WARN] No graphical display server detected ($DISPLAY / $WAYLAND_DISPLAY unset)." << std::endl;
            std::cerr << "[WARN] Automatically launching Terminal UI (TUI) instead." << std::endl;
            runTui = true;
        } else {
            std::cerr << "[ERROR] Cannot launch GUI: No graphical display server found ($DISPLAY / $WAYLAND_DISPLAY unset)." << std::endl;
            return 1;
        }
    }

    // ------------------------------------------------------------------------
    // TERMINAL UI (TUI) MODE
    // ------------------------------------------------------------------------
    if (runTui) {
        QCoreApplication app(argc, argv);
        app.setApplicationName("IP Scanner");
        app.setOrganizationName("IPScanner");
        app.setApplicationVersion("2.4.0");

        QCommandLineParser parser;
        parser.setApplicationDescription("Network IP Scanner // Terminal Reconnaissance Console (TUI)");
        parser.addHelpOption();
        parser.addVersionOption();

        QCommandLineOption tuiOpt(QStringList() << "t" << "tui", "Force interactive Terminal UI mode (TUI)");
        QCommandLineOption guiOpt(QStringList() << "g" << "gui", "Force Graphical User Interface mode (GUI)");
        QCommandLineOption autoScanOpt("auto-scan", "Automatically initiate subnet reconnaissance on launch");
        QCommandLineOption exitAfterOpt("exit-after", "Automatically terminate execution after specified seconds", "seconds");
        QCommandLineOption themeOpt("theme", "Palette theme (tokyo, dracula, gruvbox, green, cyan, amber, dark, light)", "name");
        QCommandLineOption startIpOpt("start-ip", "Initial target IPv4 address", "ip");
        QCommandLineOption endIpOpt("end-ip", "Final target IPv4 address", "ip");

        parser.addOption(tuiOpt);
        parser.addOption(guiOpt);
        parser.addOption(autoScanOpt);
        parser.addOption(exitAfterOpt);
        parser.addOption(themeOpt);
        parser.addOption(startIpOpt);
        parser.addOption(endIpOpt);

        parser.process(app);

        TuiApp tui(&app);

        if (parser.isSet(themeOpt)) {
            tui.setPaletteTheme(TuiPalette::parseName(parser.value(themeOpt)));
        }

        if (parser.isSet(startIpOpt) || parser.isSet(endIpOpt)) {
            QString sIp = parser.value(startIpOpt);
            QString eIp = parser.value(endIpOpt);
            tui.setRange(sIp, eIp);
        }

        if (parser.isSet(autoScanOpt)) {
            tui.setAutoScan(true);
        }

        if (parser.isSet(exitAfterOpt)) {
            tui.setExitAfter(parser.value(exitAfterOpt).toInt());
        }

        if (!tui.start()) {
            std::cerr << "[ERROR] Failed to initialize terminal UI." << std::endl;
            return 1;
        }

        return app.exec();
    }

    // ------------------------------------------------------------------------
    // GRAPHICAL USER INTERFACE (GUI) MODE
    // ------------------------------------------------------------------------
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
    app.setApplicationVersion("2.4.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Network IP Scanner in Qt5 / C++");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption tuiOpt(QStringList() << "t" << "tui", "Force interactive Terminal UI mode (TUI)");
    QCommandLineOption guiOpt(QStringList() << "g" << "gui", "Force Graphical User Interface mode (GUI)");
    QCommandLineOption autoScanOpt("auto-scan", "Automatically start scanning on launch");
    QCommandLineOption screenshotOpt("screenshot", "Save a window screenshot to file", "file");
    QCommandLineOption exitAfterOpt("exit-after", "Exit after specified seconds", "seconds");
    QCommandLineOption themeOpt("theme", "Palette theme (tokyo, dracula, gruvbox, cyan, green, amber, dark, light)", "name");

    parser.addOption(tuiOpt);
    parser.addOption(guiOpt);
    parser.addOption(autoScanOpt);
    parser.addOption(screenshotOpt);
    parser.addOption(exitAfterOpt);
    parser.addOption(themeOpt);

    parser.process(app);

    MainWindow window;

    if (parser.isSet(themeOpt)) {
        QString t = parser.value(themeOpt).toLower();
        if (t == "dracula") window.setPaletteTheme(MainWindow::ThemeBtopDracula);
        else if (t == "gruvbox") window.setPaletteTheme(MainWindow::ThemeBtopGruvbox);
        else if (t == "cyan") window.setPaletteTheme(MainWindow::ThemeRetroCyan);
        else if (t == "green") window.setPaletteTheme(MainWindow::ThemeRetroGreen);
        else if (t == "amber") window.setPaletteTheme(MainWindow::ThemeRetroAmber);
        else if (t == "dark") window.setPaletteTheme(MainWindow::ThemeDark);
        else if (t == "light") window.setPaletteTheme(MainWindow::ThemeLight);
        else window.setPaletteTheme(MainWindow::ThemeBtopTokyo);
    }

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
