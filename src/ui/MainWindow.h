#pragma once

#include <QMainWindow>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QProgressBar>
#include <QLabel>
#include <QTableView>
#include <QElapsedTimer>
#include <QTimer>
#include <QFrame>

#include "../models/HostTableModel.h"
#include "../models/HostSortFilterProxyModel.h"
#include "../scanner/NetworkScanner.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void startScanDirect() { onStartStopScan(); }
    NetworkScanner* scanner() const { return m_scanner; }

private slots:
    void onRefreshInterfaces();
    void onInterfaceChanged(int index);
    void onCidrPresetChanged(int index);
    void onStartStopScan();
    void onPauseResumeScan();
    void onClearResults();
    void onFilterTextChanged(const QString &text);
    void onShowOnlyAliveToggled(bool checked);

    void onScanStarted(int totalHosts);
    void onHostDiscovered(const HostItem &host);
    void onScanProgress(int completed, int total, const QString &currentIp);
    void onScanFinished();
    void onTimerTick();

    void onTableRowSelected(const QItemSelection &selected, const QItemSelection &deselected);
    void onTableDoubleClicked(const QModelIndex &index);
    void onContextMenuRequested(const QPoint &pos);

    void onOpenHttp();
    void onOpenHttps();
    void onOpenSsh();
    void onPingHost();
    void onDeepPortScan();
    void onWakeOnLan();
    void onCopyIp();
    void onCopyMac();
    void onCopyHostname();
    void onCopyAllInfo();

    void onExportCsv();
    void onExportJson();
    void onExportTxt();
    void onToggleTheme();
    void onAbout();

private:
    void setupUi();
    void setupMenuBar();
    void populateInterfaces();
    void applyTheme(bool dark);
    void updateMissingArpEntries();
    HostItem getSelectedHost() const;
    void updateHostDetailsCard(const HostItem &host);

    // Header Controls
    QComboBox *m_ifaceCombo;
    QPushButton *m_refreshIfaceBtn;
    QLineEdit *m_startIpEdit;
    QLineEdit *m_endIpEdit;
    QComboBox *m_cidrCombo;
    QPushButton *m_scanBtn;
    QPushButton *m_pauseBtn;
    QPushButton *m_clearBtn;

    // Options Bar
    QSpinBox *m_threadsSpin;
    QSpinBox *m_timeoutSpin;
    QCheckBox *m_scanPortsCheck;
    QCheckBox *m_onlyAliveCheck;
    QPushButton *m_portScanToolBtn;
    QPushButton *m_wolToolBtn;
    QPushButton *m_exportBtn;
    QPushButton *m_themeBtn;

    // Filter & Metrics Bar
    QLineEdit *m_filterEdit;
    QLabel *m_aliveBadge;
    QLabel *m_offlineBadge;
    QLabel *m_totalBadge;
    QLabel *m_timeBadge;
    QProgressBar *m_progressBar;
    QLabel *m_statusLabel;

    // Table
    QTableView *m_tableView;
    HostTableModel *m_model;
    HostSortFilterProxyModel *m_proxyModel;

    // Host Details Bottom Card
    QFrame *m_detailsFrame;
    QLabel *m_detailTitle;
    QLabel *m_detailInfo;
    QPushButton *m_btnHttp;
    QPushButton *m_btnHttps;
    QPushButton *m_btnSsh;
    QPushButton *m_btnPing;
    QPushButton *m_btnPortScan;
    QPushButton *m_btnWol;
    QPushButton *m_btnCopy;

    // Scanning Engine
    NetworkScanner *m_scanner;
    QElapsedTimer m_scanTimer;
    QTimer *m_clockTimer;
    bool m_isDarkTheme;
};
