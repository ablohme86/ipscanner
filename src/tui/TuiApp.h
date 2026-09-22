#pragma once

#include <QObject>
#include <QCoreApplication>
#include <QSocketNotifier>
#include <QTimer>
#include <QElapsedTimer>
#include <QProcess>
#include <QList>
#include <QString>
#include <QPair>

#include "TerminalScreen.h"
#include "TuiTheme.h"
#include "../core/HostItem.h"
#include "../scanner/NetworkScanner.h"
#include "../scanner/PortScanner.h"

enum class ModalMode {
    None = 0,
    Search,
    RangeConfig,
    PortScan,
    Wol,
    Ping,
    Export,
    Theme,
    Help
};

struct InterfaceItem {
    QString name;
    QString ip;
    int prefix = 24;
    QString startIp;
    QString endIp;
    QString bcastIp;
};

class TuiApp : public QObject {
    Q_OBJECT
public:
    explicit TuiApp(QCoreApplication *app, QObject *parent = nullptr);
    ~TuiApp() override;

    void setAutoScan(bool autoScan) { m_autoScanOnStart = autoScan; }
    void setPaletteTheme(TuiPaletteId palette) { m_palette = TuiPalette::get(palette); }
    void setRange(const QString &startIp, const QString &endIp);
    void setExitAfter(int seconds);

    bool start();

private slots:
    void onInputReady(int socket);
    void onTick();

    // Scanner callbacks
    void onScanStarted(int totalHosts);
    void onHostDiscovered(const HostItem &host);
    void onScanProgress(int completed, int total, const QString &currentIp);
    void onScanFinished();

    // Port scanner callbacks
    void onPortFound(const QString &ip, const PortInfo &info);
    void onPortProgress(int scanned, int total);
    void onPortScanFinished();

    // Ping callbacks
    void onPingReadyRead();
    void onPingFinished(int exitCode);

private:
    void populateInterfaces();
    void applyInterface(int index);
    void startScan();
    void pauseScan();
    void resumeScan();
    void stopScan();
    void clearScan();

    void applyFilter();
    void appendLog(const QString &msg, const QString &tag = "SYS");

    void handleKey(const TuiKeyEvent &ev);
    void handleNormalKey(const TuiKeyEvent &ev);
    void handleSearchKey(const TuiKeyEvent &ev);
    void handleRangeKey(const TuiKeyEvent &ev);
    void handlePortScanKey(const TuiKeyEvent &ev);
    void handleWolKey(const TuiKeyEvent &ev);
    void handlePingKey(const TuiKeyEvent &ev);
    void handleExportKey(const TuiKeyEvent &ev);
    void handleThemeKey(const TuiKeyEvent &ev);
    void handleHelpKey(const TuiKeyEvent &ev);

    void render();
    void renderMainView();
    void renderHeader(int x, int y, int w);
    void renderMetrics(int x, int y, int w);
    void renderHostTable(int x, int y, int w, int h);
    void renderInspector(int x, int y, int w, int h);
    void renderLogPane(int x, int y, int w, int h);
    void renderFooter(int x, int y, int w);

    // Modal renderers
    void renderSearchModal();
    void renderRangeModal();
    void renderPortScanModal();
    void renderWolModal();
    void renderPingModal();
    void renderExportModal();
    void renderThemeModal();
    void renderHelpModal();

    void copySelectedToClipboard();
    void exportCurrentData(int format, const QString &filePath);

    HostItem selectedHost() const;

private:
    QCoreApplication *m_app;
    TerminalScreen m_screen;
    TuiPalette m_palette;
    ModalMode m_modal = ModalMode::None;

    QSocketNotifier *m_inputNotifier = nullptr;
    QTimer *m_tickTimer = nullptr;

    // Scanner
    NetworkScanner *m_scanner = nullptr;
    PortScanner *m_portScanner = nullptr;

    // Ping process
    QProcess *m_pingProcess = nullptr;
    QStringList m_pingOutput;
    QString m_pingTargetIp;

    // Network interfaces
    QList<InterfaceItem> m_interfaces;
    int m_selectedIfaceIndex = 0;
    QString m_startIp = "192.168.1.1";
    QString m_endIp = "192.168.1.254";
    QString m_bcastIp = "192.168.1.255";
    int m_concurrency = 45;
    double m_timeoutSec = 0.4;
    bool m_scanPorts = true;
    bool m_onlyAlive = true;

    // Hosts data
    QList<HostItem> m_hosts;
    QList<HostItem> m_filteredHosts;
    int m_selectedHostIndex = 0;
    int m_tableScrollOffset = 0;

    // Scan metrics
    bool m_isScanning = false;
    bool m_isPaused = false;
    int m_totalHosts = 0;
    int m_completedHosts = 0;
    int m_aliveCount = 0;
    int m_offlineCount = 0;
    int m_servicesCount = 0;
    QString m_currentProbingIp = "-";
    QElapsedTimer m_scanTimer;
    qint64 m_scanElapsedMs = 0;

    // Logs
    QStringList m_logs;
    QString m_statusMessage = "SYS.READY // Press [SPACE] to start subnet reconnaissance.";

    // Port scan modal state
    int m_portPreset = 0;
    QList<PortInfo> m_discoveredPorts;
    int m_portScanTotal = 0;
    int m_portScanCompleted = 0;
    bool m_portScanActive = false;

    // WOL modal state
    QString m_wolStatus;

    // Export modal state
    int m_exportFormat = 0; // 0=CSV, 1=JSON, 2=TXT
    QString m_exportPath = "recon_dump.csv";

    // Range config modal state
    int m_rangeField = 0; // 0=iface, 1=startIp, 2=endIp, 3=threads, 4=timeout, 5=services
    QString m_editStartIp;
    QString m_editEndIp;
    int m_editThreads = 45;
    int m_editTimeoutMs = 400;
    bool m_editScanPorts = true;

    // Search query
    QString m_searchQuery;

    // Flags
    bool m_autoScanOnStart = false;
    int m_exitAfterSeconds = 0;
    uint32_t m_animFrame = 0;
};
