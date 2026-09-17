#include "MainWindow.h"
#include "PortScanDialog.h"
#include "WolDialog.h"
#include "../core/MacVendorLookup.h"
#include "../core/ArpReader.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QNetworkInterface>
#include <QDesktopServices>
#include <QUrl>
#include <QClipboard>
#include <QApplication>
#include <QFileDialog>
#include <QMessageBox>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QAction>
#include <QMenuBar>
#include <QStatusBar>
#include <QProcess>
#include <QTime>
#include <QScrollBar>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_scanner(new NetworkScanner(this))
    , m_clockTimer(new QTimer(this))
    , m_currentTheme(ThemeRetroGreen)
{
    setWindowTitle("SYS.RECON // CYBERDECK NETWORK RADAR v2.4");
    resize(1180, 750);
    setMinimumSize(900, 560);

    // Initialize vendor lookup and arp reader
    MacVendorLookup::instance().init();
    ArpReader::instance().refreshArpTable();

    m_model = new HostTableModel(this);
    m_proxyModel = new HostSortFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_model);

    setupMenuBar();
    setupUi();
    populateInterfaces();
    applyTheme(ThemeRetroGreen);

    // Scanner signals
    connect(m_scanner, &NetworkScanner::scanStarted, this, &MainWindow::onScanStarted);
    connect(m_scanner, &NetworkScanner::hostDiscovered, this, &MainWindow::onHostDiscovered);
    connect(m_scanner, &NetworkScanner::scanProgress, this, &MainWindow::onScanProgress);
    connect(m_scanner, &NetworkScanner::scanFinished, this, &MainWindow::onScanFinished);

    // Clock timer for scan duration
    m_clockTimer->setInterval(100);
    connect(m_clockTimer, &QTimer::timeout, this, &MainWindow::onTimerTick);

    // Table selection & context menu
    connect(m_tableView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onTableRowSelected);
    connect(m_tableView, &QTableView::doubleClicked, this, &MainWindow::onTableDoubleClicked);
    m_tableView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tableView, &QTableView::customContextMenuRequested, this, &MainWindow::onContextMenuRequested);

    // Stats change updates
    connect(m_model, &HostTableModel::statsChanged, this, [this](int alive, int total) {
        m_aliveBadge->setText(QString("[● ONLINE: %1]").arg(alive));
        m_offlineBadge->setText(QString("[○ OFFLINE: %1]").arg(total - alive));
        m_totalBadge->setText(QString("[TARGETS: %1]").arg(total));
    });

    appendLog("SYSTEM INITIALIZED. Monospace cyberdeck interface ready.", "SYS");
    appendLog(QString("IEEE OUI database loaded: %1 vendor definitions online.").arg("39,850+"), "INTEL");
}

MainWindow::~MainWindow()
{
    m_scanner->stopScan();
}

void MainWindow::setupMenuBar()
{
    QMenuBar *menuBar = this->menuBar();

    // File Menu
    QMenu *fileMenu = menuBar->addMenu("&[FILE]");
    QAction *exportCsvAct = fileMenu->addAction("Dump Targets to CSV...", this, &MainWindow::onExportCsv);
    exportCsvAct->setShortcut(QKeySequence("Ctrl+E"));
    fileMenu->addAction("Dump Targets to JSON...", this, &MainWindow::onExportJson);
    fileMenu->addAction("Dump Targets to TXT...", this, &MainWindow::onExportTxt);
    fileMenu->addSeparator();
    fileMenu->addAction("Exit Cyberdeck", this, &QWidget::close, QKeySequence::Quit);

    // Tools Menu
    QMenu *toolsMenu = menuBar->addMenu("&[TOOLS]");
    toolsMenu->addAction("Deep Port Scanner...", this, &MainWindow::onDeepPortScan, QKeySequence("Ctrl+P"));
    toolsMenu->addAction("Wake-on-LAN Injector...", this, &MainWindow::onWakeOnLan, QKeySequence("Ctrl+W"));
    toolsMenu->addSeparator();
    toolsMenu->addAction("Refresh Network Adapters", this, &MainWindow::onRefreshInterfaces, QKeySequence("F5"));

    // View Menu
    QMenu *viewMenu = menuBar->addMenu("&[VIEW]");
    viewMenu->addAction("Toggle Theme Palette", this, &MainWindow::onToggleTheme, QKeySequence("Ctrl+T"));

    // Help Menu
    QMenu *helpMenu = menuBar->addMenu("&[HELP]");
    helpMenu->addAction("About Cyberdeck Radar", this, &MainWindow::onAbout);
}

void MainWindow::setupUi()
{
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(12, 10, 12, 10);
    mainLayout->setSpacing(8);

    // ==========================================
    // 1. Cyberdeck Title & Control Card
    // ==========================================
    QFrame *controlCard = new QFrame(this);
    controlCard->setObjectName("controlCard");
    QVBoxLayout *cardLayout = new QVBoxLayout(controlCard);
    cardLayout->setContentsMargins(12, 10, 12, 10);
    cardLayout->setSpacing(8);

    // Row 0: Retro Terminal ASCII Title
    QLabel *asciiTitle = new QLabel(
        "┌─[ SYS.NET_RADAR // CYBERDECK SUBNET RECONNAISSANCE CONSOLE v2.4 ]"
        "────────────────────────────────────────────────────────┐", this);
    asciiTitle->setObjectName("titleBanner");
    cardLayout->addWidget(asciiTitle);

    // Row 1: Target Parameters
    QHBoxLayout *row1 = new QHBoxLayout();
    row1->setSpacing(8);

    QLabel *ifaceLbl = new QLabel("IFACE: >", this);
    ifaceLbl->setStyleSheet("font-weight: bold;");
    m_ifaceCombo = new QComboBox(this);
    m_ifaceCombo->setMinimumWidth(210);

    m_refreshIfaceBtn = new QPushButton("REFRESH", this);
    m_refreshIfaceBtn->setToolTip("Query host network interfaces");
    m_refreshIfaceBtn->setFixedWidth(80);
    m_refreshIfaceBtn->setFixedHeight(28);

    QLabel *rangeLbl = new QLabel("RANGE: >", this);
    rangeLbl->setStyleSheet("font-weight: bold; margin-left: 4px;");

    m_startIpEdit = new QLineEdit(this);
    m_startIpEdit->setPlaceholderText("192.168.1.1");
    m_startIpEdit->setFixedWidth(120);

    QLabel *toLbl = new QLabel("..", this);
    toLbl->setStyleSheet("font-weight: bold;");

    m_endIpEdit = new QLineEdit(this);
    m_endIpEdit->setPlaceholderText("192.168.1.254");
    m_endIpEdit->setFixedWidth(120);

    m_cidrCombo = new QComboBox(this);
    m_cidrCombo->addItem("PRESET: /24 (254 NODES)", 24);
    m_cidrCombo->addItem("PRESET: /23 (510 NODES)", 23);
    m_cidrCombo->addItem("PRESET: /22 (1022 NODES)", 22);
    m_cidrCombo->addItem("PRESET: /16 (65534 NODES)", 16);
    m_cidrCombo->addItem("CUSTOM RANGE", 0);
    m_cidrCombo->setMinimumWidth(185);

    m_scanBtn = new QPushButton("▶ EXEC SCAN", this);
    m_scanBtn->setObjectName("primaryBtn");
    m_scanBtn->setMinimumWidth(110);
    m_scanBtn->setFixedHeight(28);

    m_pauseBtn = new QPushButton("⏸ HALT", this);
    m_pauseBtn->setFixedWidth(70);
    m_pauseBtn->setFixedHeight(28);
    m_pauseBtn->setEnabled(false);

    m_clearBtn = new QPushButton("⌫ FLUSH", this);
    m_clearBtn->setFixedWidth(65);
    m_clearBtn->setFixedHeight(28);

    row1->addWidget(ifaceLbl);
    row1->addWidget(m_ifaceCombo);
    row1->addWidget(m_refreshIfaceBtn);
    row1->addWidget(rangeLbl);
    row1->addWidget(m_startIpEdit);
    row1->addWidget(toLbl);
    row1->addWidget(m_endIpEdit);
    row1->addWidget(m_cidrCombo);
    row1->addWidget(m_scanBtn);
    row1->addWidget(m_pauseBtn);
    row1->addWidget(m_clearBtn);
    row1->addStretch();

    cardLayout->addLayout(row1);

    // Row 2: Subsystem Tuning & Tools
    QHBoxLayout *row2 = new QHBoxLayout();
    row2->setSpacing(8);

    QLabel *threadsLbl = new QLabel("WORKERS: >", this);
    m_threadsSpin = new QSpinBox(this);
    m_threadsSpin->setRange(1, 128);
    m_threadsSpin->setValue(45);
    m_threadsSpin->setSuffix(" THREADS");

    QLabel *timeoutLbl = new QLabel("TIMEOUT: >", this);
    m_timeoutSpin = new QSpinBox(this);
    m_timeoutSpin->setRange(100, 3000);
    m_timeoutSpin->setSingleStep(50);
    m_timeoutSpin->setValue(400);
    m_timeoutSpin->setSuffix(" MS");

    m_scanPortsCheck = new QCheckBox("PROBE_SERVICES", this);
    m_scanPortsCheck->setChecked(true);
    m_scanPortsCheck->setToolTip("Probe ports 80, 443, 22, 445, 53 for active services");

    m_onlyAliveCheck = new QCheckBox("ALIVE_ONLY", this);
    m_onlyAliveCheck->setChecked(true);
    m_onlyAliveCheck->setToolTip("Display only active online nodes");

    m_portScanToolBtn = new QPushButton("🔍 PORT_RECON", this);
    m_wolToolBtn = new QPushButton("⚡ WOL_INJECT", this);
    m_exportBtn = new QPushButton("💾 DUMP_DATA", this);

    m_themeCombo = new QComboBox(this);
    m_themeCombo->addItem("PALETTE: MATRIX GREEN", ThemeRetroGreen);
    m_themeCombo->addItem("PALETTE: AMBER CRT", ThemeRetroAmber);
    m_themeCombo->addItem("PALETTE: CYBER CYAN", ThemeRetroCyan);
    m_themeCombo->addItem("PALETTE: CLEAN DARK", ThemeDark);
    m_themeCombo->addItem("PALETTE: CLEAN LIGHT", ThemeLight);
    m_themeCombo->setMinimumWidth(205);

    row2->addWidget(threadsLbl);
    row2->addWidget(m_threadsSpin);
    row2->addWidget(timeoutLbl);
    row2->addWidget(m_timeoutSpin);
    row2->addWidget(m_scanPortsCheck);
    row2->addWidget(m_onlyAliveCheck);
    row2->addStretch();
    row2->addWidget(m_portScanToolBtn);
    row2->addWidget(m_wolToolBtn);
    row2->addWidget(m_exportBtn);
    row2->addWidget(m_themeCombo);

    cardLayout->addLayout(row2);
    mainLayout->addWidget(controlCard);

    // ==========================================
    // 2. Real-Time Telemetry & Filter HUD
    // ==========================================
    QFrame *filterBar = new QFrame(this);
    filterBar->setObjectName("filterBar");
    QHBoxLayout *filterLayout = new QHBoxLayout(filterBar);
    filterLayout->setContentsMargins(10, 6, 10, 6);
    filterLayout->setSpacing(8);

    QLabel *filterPrefix = new QLabel("> FILTER_QUERY:", this);
    filterPrefix->setStyleSheet("font-weight: bold;");
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText("Enter IP, Hostname, MAC, Vendor or Service signature...");
    m_filterEdit->setClearButtonEnabled(true);

    filterLayout->addWidget(filterPrefix);
    filterLayout->addWidget(m_filterEdit, 2);

    m_aliveBadge = new QLabel("[● ONLINE: 0]", this);
    m_aliveBadge->setObjectName("aliveBadge");
    m_aliveBadge->setProperty("class", "badge");

    m_offlineBadge = new QLabel("[○ OFFLINE: 0]", this);
    m_offlineBadge->setObjectName("offlineBadge");
    m_offlineBadge->setProperty("class", "badge");

    m_totalBadge = new QLabel("[TARGETS: 0]", this);
    m_totalBadge->setObjectName("totalBadge");
    m_totalBadge->setProperty("class", "badge");

    m_timeBadge = new QLabel("[⏱ 00:00.0]", this);
    m_timeBadge->setObjectName("timeBadge");
    m_timeBadge->setProperty("class", "badge");

    filterLayout->addWidget(m_aliveBadge);
    filterLayout->addWidget(m_offlineBadge);
    filterLayout->addWidget(m_totalBadge);
    filterLayout->addWidget(m_timeBadge);

    mainLayout->addWidget(filterBar);

    // Segmented Progress Bar & State
    QHBoxLayout *progLayout = new QHBoxLayout();
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);

    m_statusLabel = new QLabel("> STATUS: STANDBY // ENGINE ARMED.", this);
    m_statusLabel->setStyleSheet("font-size: 11px; font-weight: bold;");

    progLayout->addWidget(m_progressBar, 2);
    progLayout->addWidget(m_statusLabel, 1);
    mainLayout->addLayout(progLayout);

    // ==========================================
    // 3. Discovered Node Matrix (Table)
    // ==========================================
    m_tableView = new QTableView(this);
    m_tableView->setModel(m_proxyModel);
    m_tableView->setSortingEnabled(true);
    m_tableView->sortByColumn(HostTableModel::ColIp, Qt::AscendingOrder);
    m_tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tableView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tableView->setAlternatingRowColors(true);
    m_tableView->verticalHeader()->setVisible(false);
    m_tableView->horizontalHeader()->setHighlightSections(false);
    m_tableView->horizontalHeader()->setStretchLastSection(true);

    m_tableView->setColumnWidth(HostTableModel::ColStatus, 90);
    m_tableView->setColumnWidth(HostTableModel::ColIp, 130);
    m_tableView->setColumnWidth(HostTableModel::ColHostname, 170);
    m_tableView->setColumnWidth(HostTableModel::ColPing, 95);
    m_tableView->setColumnWidth(HostTableModel::ColMac, 155);
    m_tableView->setColumnWidth(HostTableModel::ColVendor, 210);
    m_tableView->setColumnWidth(HostTableModel::ColPorts, 180);

    mainLayout->addWidget(m_tableView, 2);

    // ==========================================
    // 4. Tabbed Hacker Stream & Node Inspector
    // ==========================================
    m_bottomTabs = new QTabWidget(this);

    // --- Tab 1: Terminal Log Stream ---
    QWidget *logTab = new QWidget(m_bottomTabs);
    QVBoxLayout *logTabLayout = new QVBoxLayout(logTab);
    logTabLayout->setContentsMargins(8, 6, 8, 6);
    logTabLayout->setSpacing(6);

    m_terminalLog = new QTextEdit(logTab);
    m_terminalLog->setObjectName("terminalLog");
    m_terminalLog->setReadOnly(true);
    logTabLayout->addWidget(m_terminalLog);

    QHBoxLayout *logControlLayout = new QHBoxLayout();
    m_autoScrollCheck = new QCheckBox("[✓] AUTO_SCROLL", logTab);
    m_autoScrollCheck->setChecked(true);
    m_clearLogBtn = new QPushButton("⌫ CLEAR LOG", logTab);
    m_clearLogBtn->setFixedWidth(105);

    logControlLayout->addWidget(m_autoScrollCheck);
    logControlLayout->addStretch();
    logControlLayout->addWidget(m_clearLogBtn);
    logTabLayout->addLayout(logControlLayout);

    m_bottomTabs->addTab(logTab, "[ 💻 SYS_TERMINAL_STREAM ]");

    // --- Tab 2: Selected Node Intel Card ---
    QWidget *intelTab = new QWidget(m_bottomTabs);
    QVBoxLayout *intelTabLayout = new QVBoxLayout(intelTab);
    intelTabLayout->setContentsMargins(12, 8, 12, 8);
    intelTabLayout->setSpacing(8);

    m_detailTitle = new QLabel("[ NO NODE SELECTED ]", intelTab);
    m_detailTitle->setStyleSheet("font-size: 13px; font-weight: bold;");

    m_detailInfo = new QLabel("> Click any active target in the matrix above to extract full intel.", intelTab);
    intelTabLayout->addWidget(m_detailTitle);
    intelTabLayout->addWidget(m_detailInfo);

    QHBoxLayout *actionsLayout = new QHBoxLayout();
    actionsLayout->setSpacing(6);

    m_btnHttp = new QPushButton("🌐 HTTP", intelTab);
    m_btnHttps = new QPushButton("🔒 HTTPS", intelTab);
    m_btnSsh = new QPushButton("💻 SSH", intelTab);
    m_btnPing = new QPushButton("🏓 ICMP PING", intelTab);
    m_btnPortScan = new QPushButton("🔍 DEEP PORTS", intelTab);
    m_btnWol = new QPushButton("⚡ WOL INJECT", intelTab);
    m_btnCopy = new QPushButton("📋 COPY INTEL", intelTab);

    m_btnHttp->setEnabled(false);
    m_btnHttps->setEnabled(false);
    m_btnSsh->setEnabled(false);
    m_btnPing->setEnabled(false);
    m_btnPortScan->setEnabled(false);
    m_btnWol->setEnabled(false);
    m_btnCopy->setEnabled(false);

    actionsLayout->addWidget(m_btnHttp);
    actionsLayout->addWidget(m_btnHttps);
    actionsLayout->addWidget(m_btnSsh);
    actionsLayout->addWidget(m_btnPing);
    actionsLayout->addWidget(m_btnPortScan);
    actionsLayout->addWidget(m_btnWol);
    actionsLayout->addWidget(m_btnCopy);
    actionsLayout->addStretch();

    intelTabLayout->addLayout(actionsLayout);
    m_bottomTabs->addTab(intelTab, "[ 📡 NODE_INTEL // TARGET_DETAILS ]");

    mainLayout->addWidget(m_bottomTabs, 1);

    // ==========================================
    // Event Connections
    // ==========================================
    connect(m_refreshIfaceBtn, &QPushButton::clicked, this, &MainWindow::onRefreshInterfaces);
    connect(m_ifaceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onInterfaceChanged);
    connect(m_cidrCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onCidrPresetChanged);
    connect(m_scanBtn, &QPushButton::clicked, this, &MainWindow::onStartStopScan);
    connect(m_pauseBtn, &QPushButton::clicked, this, &MainWindow::onPauseResumeScan);
    connect(m_clearBtn, &QPushButton::clicked, this, &MainWindow::onClearResults);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &MainWindow::onFilterTextChanged);
    connect(m_onlyAliveCheck, &QCheckBox::toggled, this, &MainWindow::onShowOnlyAliveToggled);
    m_proxyModel->setOnlyAlive(m_onlyAliveCheck->isChecked());

    connect(m_clearLogBtn, &QPushButton::clicked, m_terminalLog, &QTextEdit::clear);
    connect(m_themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onThemeChanged);

    connect(m_portScanToolBtn, &QPushButton::clicked, this, &MainWindow::onDeepPortScan);
    connect(m_wolToolBtn, &QPushButton::clicked, this, &MainWindow::onWakeOnLan);
    connect(m_exportBtn, &QPushButton::clicked, this, &MainWindow::onExportCsv);

    connect(m_btnHttp, &QPushButton::clicked, this, &MainWindow::onOpenHttp);
    connect(m_btnHttps, &QPushButton::clicked, this, &MainWindow::onOpenHttps);
    connect(m_btnSsh, &QPushButton::clicked, this, &MainWindow::onOpenSsh);
    connect(m_btnPing, &QPushButton::clicked, this, &MainWindow::onPingHost);
    connect(m_btnPortScan, &QPushButton::clicked, this, &MainWindow::onDeepPortScan);
    connect(m_btnWol, &QPushButton::clicked, this, &MainWindow::onWakeOnLan);
    connect(m_btnCopy, &QPushButton::clicked, this, &MainWindow::onCopyAllInfo);

    statusBar()->showMessage("> CYBERDECK READY. SELECT INTERFACE AND INITIATE RECON.");
}

void MainWindow::appendLog(const QString &msg, const QString &tag)
{
    QString timestamp = QTime::currentTime().toString("hh:mm:ss.zzz");
    QString line = QString("[%1] [%2] %3").arg(timestamp, tag, msg);
    m_terminalLog->append(line);

    if (m_autoScrollCheck->isChecked()) {
        m_terminalLog->verticalScrollBar()->setValue(m_terminalLog->verticalScrollBar()->maximum());
    }
}

void MainWindow::populateInterfaces()
{
    m_ifaceCombo->clear();

    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    int defaultIndex = -1;
    int curIndex = 0;

    for (const QNetworkInterface &iface : interfaces) {
        if (!iface.isValid() || !(iface.flags() & QNetworkInterface::IsUp) || (iface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }

        const QList<QNetworkAddressEntry> entries = iface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                QString ipStr = entry.ip().toString();
                int prefixLen = entry.prefixLength();
                if (prefixLen <= 0) prefixLen = 24;

                quint32 ipVal = entry.ip().toIPv4Address();
                quint32 maskVal = entry.netmask().toIPv4Address();
                if (maskVal == 0) maskVal = 0xFFFFFF00;

                quint32 netVal = ipVal & maskVal;
                quint32 bcastVal = netVal | (~maskVal);

                quint32 firstHost = netVal + 1;
                quint32 lastHost = (bcastVal > 1) ? (bcastVal - 1) : firstHost;

                QString itemText = QString("%1 - %2/%3").arg(iface.humanReadableName()).arg(ipStr).arg(prefixLen);

                QVariantMap data;
                data["ip"] = ipStr;
                data["startIp"] = QHostAddress(firstHost).toString();
                data["endIp"] = QHostAddress(lastHost).toString();
                data["bcastIp"] = QHostAddress(bcastVal).toString();
                data["prefix"] = prefixLen;

                m_ifaceCombo->addItem(itemText, data);

                if (defaultIndex == -1 && !iface.humanReadableName().contains("virbr") && !iface.humanReadableName().contains("docker")) {
                    defaultIndex = curIndex;
                }
                curIndex++;
            }
        }
    }

    if (m_ifaceCombo->count() > 0) {
        if (defaultIndex >= 0) {
            m_ifaceCombo->setCurrentIndex(defaultIndex);
        } else {
            m_ifaceCombo->setCurrentIndex(0);
        }
        onInterfaceChanged(m_ifaceCombo->currentIndex());
    } else {
        m_startIpEdit->setText("192.168.1.1");
        m_endIpEdit->setText("192.168.1.254");
    }
}

void MainWindow::onRefreshInterfaces()
{
    populateInterfaces();
    appendLog("Network adapter hardware table refreshed.", "IFACE");
    statusBar()->showMessage("> NETWORK INTERFACES REFRESHED.", 3000);
}

void MainWindow::onInterfaceChanged(int index)
{
    if (index < 0 || index >= m_ifaceCombo->count()) return;

    QVariantMap data = m_ifaceCombo->itemData(index).toMap();
    if (!data.isEmpty()) {
        m_startIpEdit->setText(data["startIp"].toString());
        m_endIpEdit->setText(data["endIp"].toString());
        int prefix = data["prefix"].toInt();
        if (prefix == 24) m_cidrCombo->setCurrentIndex(0);
        else if (prefix == 23) m_cidrCombo->setCurrentIndex(1);
        else if (prefix == 22) m_cidrCombo->setCurrentIndex(2);
        else if (prefix == 16) m_cidrCombo->setCurrentIndex(3);
        else m_cidrCombo->setCurrentIndex(4);

        appendLog(QString("Bound to %1 [IP: %2, Targets: %3 - %4]")
                  .arg(m_ifaceCombo->currentText(), data["ip"].toString(), data["startIp"].toString(), data["endIp"].toString()), "IFACE");
    }
}

void MainWindow::onCidrPresetChanged(int index)
{
    int bits = m_cidrCombo->itemData(index).toInt();
    if (bits <= 0) return;

    QHostAddress startAddr(m_startIpEdit->text().trimmed());
    if (startAddr.protocol() != QAbstractSocket::IPv4Protocol) return;

    quint32 startVal = startAddr.toIPv4Address();
    quint32 mask = (bits == 32) ? 0xFFFFFFFF : (0xFFFFFFFF << (32 - bits));
    quint32 netVal = startVal & mask;
    quint32 bcastVal = netVal | (~mask);

    quint32 firstHost = netVal + 1;
    quint32 lastHost = (bcastVal > 1) ? (bcastVal - 1) : firstHost;

    m_startIpEdit->setText(QHostAddress(firstHost).toString());
    m_endIpEdit->setText(QHostAddress(lastHost).toString());
}

void MainWindow::onStartStopScan()
{
    if (m_scanner->isScanning()) {
        m_scanner->stopScan();
        m_clockTimer->stop();
        m_scanBtn->setText("▶ EXEC SCAN");
        m_scanBtn->setObjectName("primaryBtn");
        m_scanBtn->setStyle(m_scanBtn->style());
        m_pauseBtn->setEnabled(false);
        m_pauseBtn->setText("⏸ HALT");
        m_statusLabel->setText("> SCAN SEQUENCE ABORTED BY OPERATOR.");
        appendLog("OPERATOR ABORT: Probe sequence stopped.", "WARN");
        statusBar()->showMessage("> SCAN SEQUENCE CANCELLED.");
        return;
    }

    QString startIp = m_startIpEdit->text().trimmed();
    QString endIp = m_endIpEdit->text().trimmed();

    QHostAddress sAddr(startIp);
    QHostAddress eAddr(endIp);

    if (sAddr.protocol() != QAbstractSocket::IPv4Protocol || eAddr.protocol() != QAbstractSocket::IPv4Protocol) {
        QMessageBox::warning(this, "INVALID RANGE", "Target coordinates must be valid IPv4 addresses.");
        return;
    }

    m_model->clear();
    m_progressBar->setValue(0);

    int concurrency = m_threadsSpin->value();
    double timeoutSec = m_timeoutSpin->value() / 1000.0;
    bool scanPorts = m_scanPortsCheck->isChecked();

    m_scanBtn->setText("■ ABORT");
    m_scanBtn->setObjectName("scanBtnActive");
    m_scanBtn->setStyle(m_scanBtn->style());
    m_pauseBtn->setEnabled(true);
    m_pauseBtn->setText("⏸ HALT");

    m_scanTimer.start();
    m_clockTimer->start();

    appendLog(QString("RECON_START: Scanning targets %1 to %2 (Threads: %3, Timeout: %4ms, PortProbe: %5)")
              .arg(startIp, endIp)
              .arg(concurrency)
              .arg(timeoutSec * 1000.0)
              .arg(scanPorts ? "ENABLED" : "DISABLED"), "SCAN");

    m_scanner->startScan(startIp, endIp, concurrency, timeoutSec, scanPorts);
}

void MainWindow::onPauseResumeScan()
{
    if (m_scanner->isPaused()) {
        m_scanner->resumeScan();
        m_pauseBtn->setText("⏸ HALT");
        m_statusLabel->setText("> RECONNAISSANCE RESUMED.");
        appendLog("SCAN RESUMED.", "SYS");
    } else if (m_scanner->isScanning()) {
        m_scanner->pauseScan();
        m_pauseBtn->setText("▶ RESUME");
        m_statusLabel->setText("> SCAN SUSPENDED [HALT MODE].");
        appendLog("SCAN SUSPENDED.", "SYS");
    }
}

void MainWindow::onClearResults()
{
    if (m_scanner->isScanning()) {
        m_scanner->stopScan();
    }
    m_model->clear();
    m_progressBar->setValue(0);
    m_timeBadge->setText("[⏱ 00:00.0]");
    m_statusLabel->setText("> MATRIX PURGED.");
    m_detailTitle->setText("[ NO NODE SELECTED ]");
    m_detailInfo->setText("> Click any active target in the matrix above to extract full intel.");
    m_btnHttp->setEnabled(false);
    m_btnHttps->setEnabled(false);
    m_btnSsh->setEnabled(false);
    m_btnPing->setEnabled(false);
    m_btnPortScan->setEnabled(false);
    m_btnWol->setEnabled(false);
    m_btnCopy->setEnabled(false);
    appendLog("Target matrix purged.", "SYS");
}

void MainWindow::onFilterTextChanged(const QString &text)
{
    m_proxyModel->setFilterQuery(text);
}

void MainWindow::onShowOnlyAliveToggled(bool checked)
{
    m_proxyModel->setOnlyAlive(checked);
}

void MainWindow::onScanStarted(int totalHosts)
{
    m_progressBar->setRange(0, totalHosts);
    m_progressBar->setValue(0);
    m_statusLabel->setText(QString("> PROBING %1 TARGET NODES...").arg(totalHosts));
}

void MainWindow::updateMissingArpEntries()
{
    ArpReader::instance().refreshArpTable();
    for (int r = 0; r < m_model->rowCount(); ++r) {
        HostItem h = m_model->hostAt(r);
        if (h.isAlive && (h.macAddress.isEmpty() || h.vendor.isEmpty())) {
            QString mac = ArpReader::instance().getMacForIp(h.ip);
            if (!mac.isEmpty()) {
                h.macAddress = mac;
                h.vendor = MacVendorLookup::instance().lookup(mac);
                m_model->addOrUpdateHost(h);
            }
        }
    }
}

void MainWindow::onHostDiscovered(const HostItem &host)
{
    HostItem h = host;
    if (h.isAlive && h.macAddress.isEmpty()) {
        QString mac = ArpReader::instance().getMacForIp(h.ip);
        if (!mac.isEmpty()) {
            h.macAddress = mac;
            h.vendor = MacVendorLookup::instance().lookup(mac);
        }
    }

    if (h.isAlive) {
        appendLog(QString("NODE_UP: %1 (%2) [RTT: %3 ms] MAC: %4 [%5]")
                  .arg(h.ip)
                  .arg(h.hostname.isEmpty() ? "<UNRESOLVED>" : h.hostname)
                  .arg(h.responseTimeMs, 0, 'f', 1)
                  .arg(h.macAddress.isEmpty() ? "--:--:--:--:--:--" : h.macAddress)
                  .arg(h.vendor.isEmpty() ? "<UNKNOWN_OUI>" : h.vendor), "+");

        if (!h.openPorts.isEmpty()) {
            appendLog(QString("SERVICES: %1 -> %2").arg(h.ip, h.openPortsSummary()), "*");
        }
    }

    m_model->addOrUpdateHost(h);
}

void MainWindow::onScanProgress(int completed, int total, const QString &currentIp)
{
    m_progressBar->setValue(completed);
    int percent = (total > 0) ? (completed * 100 / total) : 0;
    m_statusLabel->setText(QString("> PROBING %1 // [%2% // %3 / %4 TARGETS]").arg(currentIp).arg(percent).arg(completed).arg(total));

    if (completed % 8 == 0) {
        updateMissingArpEntries();
    }
}

void MainWindow::onScanFinished()
{
    m_clockTimer->stop();
    m_scanBtn->setText("▶ EXEC SCAN");
    m_scanBtn->setObjectName("primaryBtn");
    m_scanBtn->setStyle(m_scanBtn->style());
    m_pauseBtn->setEnabled(false);
    m_pauseBtn->setText("⏸ HALT");

    updateMissingArpEntries();

    int alive = m_model->aliveCount();
    int total = m_model->totalCount();
    qint64 elapsedMs = m_scanTimer.elapsed();
    double elapsedSec = elapsedMs / 1000.0;

    m_progressBar->setValue(m_progressBar->maximum());
    m_statusLabel->setText(QString("> RECON FINISHED IN %1s // %2 NODES ACTIVE.").arg(elapsedSec, 0, 'f', 1).arg(alive));
    statusBar()->showMessage(QString("> RECON FINISHED: %1 ONLINE, %2 TOTAL IN %3s.").arg(alive).arg(total).arg(elapsedSec, 0, 'f', 1));

    appendLog(QString("RECON_COMPLETE: %1 online nodes identified across %2 targets in %3s.")
              .arg(alive).arg(total).arg(elapsedSec, 0, 'f', 1), "SYS");

    if (m_tableView->selectionModel()->selectedRows().isEmpty() && m_proxyModel->rowCount() > 0) {
        m_tableView->selectRow(0);
    }
}

void MainWindow::onTimerTick()
{
    if (m_scanTimer.isValid()) {
        qint64 ms = m_scanTimer.elapsed();
        int mins = static_cast<int>(ms / 60000);
        int secs = static_cast<int>((ms % 60000) / 1000);
        int tenths = static_cast<int>((ms % 1000) / 100);
        m_timeBadge->setText(QString("[⏱ %1:%2.%3]")
                             .arg(mins, 2, 10, QChar('0'))
                             .arg(secs, 2, 10, QChar('0'))
                             .arg(tenths));
    }
}

HostItem MainWindow::getSelectedHost() const
{
    QModelIndexList sel = m_tableView->selectionModel()->selectedRows();
    if (sel.isEmpty()) return HostItem();

    QModelIndex srcIdx = m_proxyModel->mapToSource(sel.first());
    return m_model->hostAt(srcIdx.row());
}

void MainWindow::onTableRowSelected(const QItemSelection &selected, const QItemSelection &deselected)
{
    Q_UNUSED(selected);
    Q_UNUSED(deselected);

    HostItem host = getSelectedHost();
    updateHostDetailsCard(host);
}

void MainWindow::updateHostDetailsCard(const HostItem &host)
{
    if (host.ip.isEmpty()) {
        m_detailTitle->setText("[ NO NODE SELECTED ]");
        m_detailInfo->setText("> Click any active target in the matrix above to extract full intel.");
        m_btnHttp->setEnabled(false);
        m_btnHttps->setEnabled(false);
        m_btnSsh->setEnabled(false);
        m_btnPing->setEnabled(false);
        m_btnPortScan->setEnabled(false);
        m_btnWol->setEnabled(false);
        m_btnCopy->setEnabled(false);
        return;
    }

    QString title = host.hostname.isEmpty() ? QString("[ TARGET: %1 ]").arg(host.ip)
                                           : QString("[ TARGET: %1 // IDENT: %2 ]").arg(host.ip, host.hostname);
    m_detailTitle->setText(title);

    QStringList details;
    details << QString("STATE: [ %1 ]").arg(host.statusText());
    if (!host.macAddress.isEmpty()) {
        QString vendorStr = host.vendor.isEmpty() ? "<UNKNOWN_OUI>" : host.vendor;
        details << QString("MAC: [ %1 ] (%2)").arg(host.macAddress, vendorStr);
    }
    if (!host.openPorts.isEmpty()) {
        details << QString("SERVICES: [ %1 ]").arg(host.openPortsSummary());
    }

    m_detailInfo->setText(details.join("  |  "));

    m_btnHttp->setEnabled(true);
    m_btnHttps->setEnabled(true);
    m_btnSsh->setEnabled(true);
    m_btnPing->setEnabled(true);
    m_btnPortScan->setEnabled(true);
    m_btnWol->setEnabled(!host.macAddress.isEmpty());
    m_btnCopy->setEnabled(true);
}

void MainWindow::onTableDoubleClicked(const QModelIndex &index)
{
    Q_UNUSED(index);
    HostItem host = getSelectedHost();
    if (host.ip.isEmpty()) return;

    if (host.openPorts.contains(80)) {
        onOpenHttp();
    } else if (host.openPorts.contains(443)) {
        onOpenHttps();
    } else if (host.openPorts.contains(22)) {
        onOpenSsh();
    } else {
        onDeepPortScan();
    }
}

void MainWindow::onContextMenuRequested(const QPoint &pos)
{
    QModelIndex index = m_tableView->indexAt(pos);
    if (!index.isValid()) return;

    HostItem host = getSelectedHost();
    if (host.ip.isEmpty()) return;

    QMenu menu(this);
    menu.addAction("🌐 [HTTP] Launch Web Browser", this, &MainWindow::onOpenHttp);
    menu.addAction("🔒 [HTTPS] Launch TLS Browser", this, &MainWindow::onOpenHttps);
    menu.addAction("💻 [SSH] Spawn Terminal Session", this, &MainWindow::onOpenSsh);
    menu.addSeparator();
    menu.addAction("🏓 [PING] Send ICMP Probe Stream", this, &MainWindow::onPingHost);
    menu.addAction("🔍 [PORT_RECON] Deep Port Scanner...", this, &MainWindow::onDeepPortScan);
    if (!host.macAddress.isEmpty()) {
        menu.addAction("⚡ [WOL] Transmit Magic Packet Frame...", this, &MainWindow::onWakeOnLan);
    }
    menu.addSeparator();
    menu.addAction("📋 Copy Target IP", this, &MainWindow::onCopyIp);
    if (!host.macAddress.isEmpty()) {
        menu.addAction("📋 Copy Target MAC", this, &MainWindow::onCopyMac);
    }
    if (!host.hostname.isEmpty()) {
        menu.addAction("📋 Copy Target Hostname", this, &MainWindow::onCopyHostname);
    }
    menu.addAction("📋 Copy Complete Target Intel", this, &MainWindow::onCopyAllInfo);

    menu.exec(m_tableView->viewport()->mapToGlobal(pos));
}

void MainWindow::onOpenHttp()
{
    HostItem host = getSelectedHost();
    if (!host.ip.isEmpty()) {
        QDesktopServices::openUrl(QUrl(QString("http://%1").arg(host.ip)));
        appendLog(QString("Triggered HTTP session for http://%1").arg(host.ip), "BROWSER");
    }
}

void MainWindow::onOpenHttps()
{
    HostItem host = getSelectedHost();
    if (!host.ip.isEmpty()) {
        QDesktopServices::openUrl(QUrl(QString("https://%1").arg(host.ip)));
        appendLog(QString("Triggered HTTPS session for https://%1").arg(host.ip), "BROWSER");
    }
}

void MainWindow::onOpenSsh()
{
    HostItem host = getSelectedHost();
    if (host.ip.isEmpty()) return;

    QString cmd = QString("ssh %1").arg(host.ip);
    QApplication::clipboard()->setText(cmd);

    QStringList terminals = {"x-terminal-emulator", "konsole", "gnome-terminal", "alacritty", "kitty", "xfce4-terminal", "xterm"};
    bool launched = false;
    for (const QString &term : terminals) {
        if (QProcess::startDetached(term, {"-e", "ssh", host.ip})) {
            launched = true;
            appendLog(QString("SSH terminal spawned for user@%1 via %2").arg(host.ip, term), "SSH");
            break;
        }
    }

    if (!launched) {
        appendLog(QString("Copied '%1' to clipboard.").arg(cmd), "SSH");
        statusBar()->showMessage(QString("> COPIED '%1' TO CLIPBOARD.").arg(cmd), 4000);
    }
}

void MainWindow::onPingHost()
{
    HostItem host = getSelectedHost();
    if (host.ip.isEmpty()) return;

    QStringList terminals = {"x-terminal-emulator", "konsole", "gnome-terminal", "alacritty", "kitty", "xfce4-terminal", "xterm"};
    bool launched = false;
    for (const QString &term : terminals) {
        if (QProcess::startDetached(term, {"-e", "ping", host.ip})) {
            launched = true;
            break;
        }
    }

    if (!launched) {
        QProcess proc;
        proc.start("ping", {"-c", "4", host.ip});
        if (proc.waitForFinished(5000)) {
            QString out = QString::fromUtf8(proc.readAllStandardOutput());
            QMessageBox::information(this, "ICMP PING // " + host.ip, out);
        }
    }
}

void MainWindow::onDeepPortScan()
{
    HostItem host = getSelectedHost();
    PortScanDialog dlg(host.ip, this);
    dlg.exec();
}

void MainWindow::onWakeOnLan()
{
    HostItem host = getSelectedHost();
    QString bcast = "255.255.255.255";
    int idx = m_ifaceCombo->currentIndex();
    if (idx >= 0) {
        QVariantMap data = m_ifaceCombo->itemData(idx).toMap();
        if (data.contains("bcastIp")) {
            bcast = data["bcastIp"].toString();
        }
    }

    WolDialog dlg(host.macAddress, bcast, this);
    dlg.exec();
}

void MainWindow::onCopyIp()
{
    HostItem host = getSelectedHost();
    if (!host.ip.isEmpty()) {
        QApplication::clipboard()->setText(host.ip);
        statusBar()->showMessage(QString("> COPIED IP %1 TO CLIPBOARD.").arg(host.ip), 3000);
    }
}

void MainWindow::onCopyMac()
{
    HostItem host = getSelectedHost();
    if (!host.macAddress.isEmpty()) {
        QApplication::clipboard()->setText(host.macAddress);
        statusBar()->showMessage(QString("> COPIED MAC %1 TO CLIPBOARD.").arg(host.macAddress), 3000);
    }
}

void MainWindow::onCopyHostname()
{
    HostItem host = getSelectedHost();
    if (!host.hostname.isEmpty()) {
        QApplication::clipboard()->setText(host.hostname);
        statusBar()->showMessage(QString("> COPIED HOSTNAME %1 TO CLIPBOARD.").arg(host.hostname), 3000);
    }
}

void MainWindow::onCopyAllInfo()
{
    HostItem host = getSelectedHost();
    if (host.ip.isEmpty()) return;

    QString line = QString("%1\t%2\t%3\t%4\t%5\t%6\t%7")
                       .arg(host.statusText(), host.ip, host.hostname,
                            QString::number(host.responseTimeMs, 'f', 1) + " ms",
                            host.macAddress, host.vendor, host.openPortsSummary());

    QApplication::clipboard()->setText(line);
    statusBar()->showMessage("> TARGET INTEL COPIED TO CLIPBOARD.", 3000);
}

void MainWindow::onExportCsv()
{
    QString filePath = QFileDialog::getSaveFileName(this, "DUMP MATRIX TO CSV", "recon_dump.csv", "CSV Files (*.csv)");
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "ERROR", "Failed to open file for write.");
        return;
    }

    QTextStream out(&file);
    out << "Status,IP Address,Hostname,Ping (ms),MAC Address,Vendor,Open Ports,Comments\n";

    for (int r = 0; r < m_proxyModel->rowCount(); ++r) {
        QModelIndex srcIdx = m_proxyModel->mapToSource(m_proxyModel->index(r, 0));
        HostItem h = m_model->hostAt(srcIdx.row());
        out << QString("\"%1\",\"%2\",\"%3\",\"%4\",\"%5\",\"%6\",\"%7\",\"%8\"\n")
                   .arg(h.isAlive ? "Online" : "Offline",
                        h.ip,
                        h.hostname,
                        h.isAlive ? QString::number(h.responseTimeMs, 'f', 1) : "",
                        h.macAddress,
                        h.vendor.replace("\"", "\"\""),
                        h.openPortsSummary(),
                        h.comments.replace("\"", "\"\""));
    }

    appendLog(QString("Exported %1 records to CSV: %2").arg(m_proxyModel->rowCount()).arg(filePath), "DUMP");
    statusBar()->showMessage(QString("> EXPORTED %1 RECORDS TO CSV.").arg(m_proxyModel->rowCount()), 4000);
}

void MainWindow::onExportJson()
{
    QString filePath = QFileDialog::getSaveFileName(this, "DUMP MATRIX TO JSON", "recon_dump.json", "JSON Files (*.json)");
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "ERROR", "Failed to open file for write.");
        return;
    }

    QJsonArray array;
    for (int r = 0; r < m_proxyModel->rowCount(); ++r) {
        QModelIndex srcIdx = m_proxyModel->mapToSource(m_proxyModel->index(r, 0));
        HostItem h = m_model->hostAt(srcIdx.row());

        QJsonObject obj;
        obj["ip"] = h.ip;
        obj["hostname"] = h.hostname;
        obj["isAlive"] = h.isAlive;
        obj["responseTimeMs"] = h.responseTimeMs;
        obj["macAddress"] = h.macAddress;
        obj["vendor"] = h.vendor;
        QJsonArray portsArr;
        for (int p : h.openPorts) portsArr.append(p);
        obj["openPorts"] = portsArr;
        obj["services"] = QJsonArray::fromStringList(h.services);
        obj["comments"] = h.comments;
        array.append(obj);
    }

    QJsonDocument doc(array);
    file.write(doc.toJson(QJsonDocument::Indented));

    appendLog(QString("Exported %1 records to JSON: %2").arg(m_proxyModel->rowCount()).arg(filePath), "DUMP");
    statusBar()->showMessage(QString("> EXPORTED %1 RECORDS TO JSON.").arg(m_proxyModel->rowCount()), 4000);
}

void MainWindow::onExportTxt()
{
    QString filePath = QFileDialog::getSaveFileName(this, "DUMP MATRIX TO TXT", "recon_dump.txt", "Text Files (*.txt)");
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "ERROR", "Failed to open file for write.");
        return;
    }

    QTextStream out(&file);
    out << QString("%1 | %2 | %3 | %4 | %5 | %6\n")
               .arg("IP_ADDRESS", -16)
               .arg("HOSTNAME", -22)
               .arg("MAC_ADDRESS", -18)
               .arg("RTT", -10)
               .arg("VENDOR", -25)
               .arg("SERVICES");
    out << QString().fill('-', 110) << "\n";

    for (int r = 0; r < m_proxyModel->rowCount(); ++r) {
        QModelIndex srcIdx = m_proxyModel->mapToSource(m_proxyModel->index(r, 0));
        HostItem h = m_model->hostAt(srcIdx.row());
        out << QString("%1 | %2 | %3 | %4 | %5 | %6\n")
                   .arg(h.ip, -16)
                   .arg(h.hostname.isEmpty() ? "-" : h.hostname, -22)
                   .arg(h.macAddress.isEmpty() ? "-" : h.macAddress, -18)
                   .arg(h.isAlive ? QString("%1 ms").arg(h.responseTimeMs, 0, 'f', 1) : "-", -10)
                   .arg(h.vendor.isEmpty() ? "-" : h.vendor.left(24), -25)
                   .arg(h.openPortsSummary());
    }

    appendLog(QString("Exported %1 records to TXT: %2").arg(m_proxyModel->rowCount()).arg(filePath), "DUMP");
    statusBar()->showMessage(QString("> EXPORTED %1 RECORDS TO TXT.").arg(m_proxyModel->rowCount()), 4000);
}

void MainWindow::onThemeChanged(int index)
{
    applyTheme(static_cast<ThemeMode>(index));
}

void MainWindow::onToggleTheme()
{
    int next = (static_cast<int>(m_currentTheme) + 1) % 5;
    m_themeCombo->setCurrentIndex(next);
}

void MainWindow::applyTheme(ThemeMode mode)
{
    m_currentTheme = mode;
    QString qssPath;
    switch (mode) {
    case ThemeRetroGreen: qssPath = ":/styles/retro_green.qss"; break;
    case ThemeRetroAmber: qssPath = ":/styles/retro_amber.qss"; break;
    case ThemeRetroCyan:  qssPath = ":/styles/retro_cyan.qss"; break;
    case ThemeDark:       qssPath = ":/styles/dark.qss"; break;
    case ThemeLight:      qssPath = ":/styles/light.qss"; break;
    }

    QFile file(qssPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qApp->setStyleSheet(file.readAll());
    }
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, "ABOUT // CYBERDECK RADAR",
                       "<h3>[ SYS.NET_RADAR // RECON CONSOLE v2.4 ]</h3>"
                       "<p>Retro / Hacker Subnet Reconnaissance Terminal built in Qt5 / C++17.</p>"
                       "<ul>"
                       "<li>Multi-threaded parallel ICMP/TCP scanning core</li>"
                       "<li>Reverse DNS / mDNS / NetBIOS node status acquisition</li>"
                       "<li>Kernel ARP table hardware mapping</li>"
                       "<li>Offline IEEE OUI manufacturer recognition (39,850+ OUIs)</li>"
                       "<li>Integrated Deep Port Recon & Wake-on-LAN injector</li>"
                       "<li>Real-time terminal stream & telemetry stream</li>"
                       "<li>Matrix Green, Amber CRT, and Cyber Cyan color palettes</li>"
                       "</ul>");
}
