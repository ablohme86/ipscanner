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

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_scanner(new NetworkScanner(this))
    , m_clockTimer(new QTimer(this))
    , m_isDarkTheme(true)
{
    setWindowTitle("Network IP Scanner");
    resize(1150, 720);
    setMinimumSize(850, 500);

    // Initialize vendor lookup and arp reader
    MacVendorLookup::instance().init();
    ArpReader::instance().refreshArpTable();

    m_model = new HostTableModel(this);
    m_proxyModel = new HostSortFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_model);

    setupMenuBar();
    setupUi();
    populateInterfaces();
    applyTheme(true);

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
        m_aliveBadge->setText(QString("● Online: %1").arg(alive));
        m_offlineBadge->setText(QString("○ Offline: %1").arg(total - alive));
        m_totalBadge->setText(QString("Total: %1").arg(total));
    });
}

MainWindow::~MainWindow()
{
    m_scanner->stopScan();
}

void MainWindow::setupMenuBar()
{
    QMenuBar *menuBar = this->menuBar();

    // File Menu
    QMenu *fileMenu = menuBar->addMenu("&File");
    QAction *exportCsvAct = fileMenu->addAction("Export to CSV...", this, &MainWindow::onExportCsv);
    exportCsvAct->setShortcut(QKeySequence("Ctrl+E"));
    QAction *exportJsonAct = fileMenu->addAction("Export to JSON...", this, &MainWindow::onExportJson);
    QAction *exportTxtAct = fileMenu->addAction("Export to Text File...", this, &MainWindow::onExportTxt);
    fileMenu->addSeparator();
    fileMenu->addAction("Exit", this, &QWidget::close, QKeySequence::Quit);

    // Tools Menu
    QMenu *toolsMenu = menuBar->addMenu("&Tools");
    toolsMenu->addAction("Deep Port Scanner...", this, &MainWindow::onDeepPortScan, QKeySequence("Ctrl+P"));
    toolsMenu->addAction("Wake-on-LAN (WOL)...", this, &MainWindow::onWakeOnLan, QKeySequence("Ctrl+W"));
    toolsMenu->addSeparator();
    toolsMenu->addAction("Refresh Network Interfaces", this, &MainWindow::onRefreshInterfaces, QKeySequence("F5"));

    // View Menu
    QMenu *viewMenu = menuBar->addMenu("&View");
    QAction *themeAct = viewMenu->addAction("Toggle Dark / Light Theme", this, &MainWindow::onToggleTheme, QKeySequence("Ctrl+T"));
    Q_UNUSED(themeAct);

    // Help Menu
    QMenu *helpMenu = menuBar->addMenu("&Help");
    helpMenu->addAction("About IP Scanner", this, &MainWindow::onAbout);
}

void MainWindow::setupUi()
{
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(14, 14, 14, 14);
    mainLayout->setSpacing(10);

    // ==========================================
    // 1. Control Card (Top Bar)
    // ==========================================
    QFrame *controlCard = new QFrame(this);
    controlCard->setObjectName("controlCard");
    QVBoxLayout *cardLayout = new QVBoxLayout(controlCard);
    cardLayout->setContentsMargins(14, 12, 14, 12);
    cardLayout->setSpacing(10);

    // Row 1: Interface & IP Range & Primary Action
    QHBoxLayout *row1 = new QHBoxLayout();
    row1->setSpacing(8);

    QLabel *ifaceLbl = new QLabel("Interface:", this);
    ifaceLbl->setStyleSheet("font-weight: 600;");
    m_ifaceCombo = new QComboBox(this);
    m_ifaceCombo->setMinimumWidth(230);

    m_refreshIfaceBtn = new QPushButton("Refresh", this);
    m_refreshIfaceBtn->setToolTip("Refresh network interfaces");
    m_refreshIfaceBtn->setMinimumWidth(75);
    m_refreshIfaceBtn->setFixedHeight(32);

    QLabel *rangeLbl = new QLabel("Range:", this);
    rangeLbl->setStyleSheet("font-weight: 600; margin-left: 4px;");

    m_startIpEdit = new QLineEdit(this);
    m_startIpEdit->setPlaceholderText("192.168.1.1");
    m_startIpEdit->setFixedWidth(120);

    QLabel *toLbl = new QLabel("to", this);

    m_endIpEdit = new QLineEdit(this);
    m_endIpEdit->setPlaceholderText("192.168.1.254");
    m_endIpEdit->setFixedWidth(120);

    m_cidrCombo = new QComboBox(this);
    m_cidrCombo->addItem("Preset: /24 (254 hosts)", 24);
    m_cidrCombo->addItem("Preset: /23 (510 hosts)", 23);
    m_cidrCombo->addItem("Preset: /22 (1022 hosts)", 22);
    m_cidrCombo->addItem("Preset: /16 (65534 hosts)", 16);
    m_cidrCombo->addItem("Custom Range", 0);
    m_cidrCombo->setMinimumWidth(185);

    m_scanBtn = new QPushButton("Start Scan", this);
    m_scanBtn->setObjectName("primaryBtn");
    m_scanBtn->setMinimumWidth(105);
    m_scanBtn->setFixedHeight(32);

    m_pauseBtn = new QPushButton("Pause", this);
    m_pauseBtn->setFixedWidth(70);
    m_pauseBtn->setFixedHeight(32);
    m_pauseBtn->setEnabled(false);

    m_clearBtn = new QPushButton("Clear", this);
    m_clearBtn->setFixedWidth(65);
    m_clearBtn->setFixedHeight(32);

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

    // Row 2: Advanced Options & Quick Tools
    QHBoxLayout *row2 = new QHBoxLayout();
    row2->setSpacing(10);

    QLabel *threadsLbl = new QLabel("Threads:", this);
    m_threadsSpin = new QSpinBox(this);
    m_threadsSpin->setRange(1, 128);
    m_threadsSpin->setValue(45);
    m_threadsSpin->setSuffix(" workers");

    QLabel *timeoutLbl = new QLabel("Timeout:", this);
    m_timeoutSpin = new QSpinBox(this);
    m_timeoutSpin->setRange(100, 3000);
    m_timeoutSpin->setSingleStep(50);
    m_timeoutSpin->setValue(400);
    m_timeoutSpin->setSuffix(" ms");

    m_scanPortsCheck = new QCheckBox("Probe Ports", this);
    m_scanPortsCheck->setChecked(true);
    m_scanPortsCheck->setToolTip("Quickly check common ports (Web, SSH, SMB, DNS)");

    m_onlyAliveCheck = new QCheckBox("Online Only", this);
    m_onlyAliveCheck->setChecked(true);
    m_onlyAliveCheck->setToolTip("Show only alive / responding devices in the table");

    m_portScanToolBtn = new QPushButton("Port Scanner", this);
    m_wolToolBtn = new QPushButton("Wake-on-LAN", this);
    m_exportBtn = new QPushButton("Export...", this);
    m_themeBtn = new QPushButton("Toggle Theme", this);

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
    row2->addWidget(m_themeBtn);

    cardLayout->addLayout(row2);
    mainLayout->addWidget(controlCard);

    // ==========================================
    // 2. Filter & Metrics Bar
    // ==========================================
    QFrame *filterBar = new QFrame(this);
    filterBar->setObjectName("filterBar");
    QHBoxLayout *filterLayout = new QHBoxLayout(filterBar);
    filterLayout->setContentsMargins(12, 8, 12, 8);
    filterLayout->setSpacing(10);

    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText("🔍 Quick filter by IP, Hostname, MAC, Vendor, Port...");
    m_filterEdit->setClearButtonEnabled(true);
    filterLayout->addWidget(m_filterEdit, 2);

    m_aliveBadge = new QLabel("● Online: 0", this);
    m_aliveBadge->setObjectName("aliveBadge");
    m_aliveBadge->setProperty("class", "badge");

    m_offlineBadge = new QLabel("○ Offline: 0", this);
    m_offlineBadge->setObjectName("offlineBadge");
    m_offlineBadge->setProperty("class", "badge");

    m_totalBadge = new QLabel("Total: 0", this);
    m_totalBadge->setObjectName("totalBadge");
    m_totalBadge->setProperty("class", "badge");

    m_timeBadge = new QLabel("⏱ 00:00.0", this);
    m_timeBadge->setObjectName("timeBadge");
    m_timeBadge->setProperty("class", "badge");

    filterLayout->addWidget(m_aliveBadge);
    filterLayout->addWidget(m_offlineBadge);
    filterLayout->addWidget(m_totalBadge);
    filterLayout->addWidget(m_timeBadge);

    mainLayout->addWidget(filterBar);

    // Progress Bar & Status
    QHBoxLayout *progLayout = new QHBoxLayout();
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);

    m_statusLabel = new QLabel("Ready to scan.", this);
    m_statusLabel->setStyleSheet("color: #94a3b8; font-size: 12px;");

    progLayout->addWidget(m_progressBar, 2);
    progLayout->addWidget(m_statusLabel, 1);
    mainLayout->addLayout(progLayout);

    // ==========================================
    // 3. Results Table View
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

    // Column widths
    m_tableView->setColumnWidth(HostTableModel::ColStatus, 95);
    m_tableView->setColumnWidth(HostTableModel::ColIp, 130);
    m_tableView->setColumnWidth(HostTableModel::ColHostname, 170);
    m_tableView->setColumnWidth(HostTableModel::ColPing, 90);
    m_tableView->setColumnWidth(HostTableModel::ColMac, 150);
    m_tableView->setColumnWidth(HostTableModel::ColVendor, 200);
    m_tableView->setColumnWidth(HostTableModel::ColPorts, 160);

    mainLayout->addWidget(m_tableView, 1);

    // ==========================================
    // 4. Host Details Bottom Card
    // ==========================================
    m_detailsFrame = new QFrame(this);
    m_detailsFrame->setObjectName("detailsCard");
    QHBoxLayout *detailsLayout = new QHBoxLayout(m_detailsFrame);
    detailsLayout->setContentsMargins(14, 10, 14, 10);
    detailsLayout->setSpacing(14);

    QVBoxLayout *infoLayout = new QVBoxLayout();
    m_detailTitle = new QLabel("No host selected", this);
    m_detailTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #38bdf8;");
    m_detailInfo = new QLabel("Select a host in the table above to view device details and quick actions.", this);
    m_detailInfo->setStyleSheet("color: #94a3b8; font-size: 12px;");
    infoLayout->addWidget(m_detailTitle);
    infoLayout->addWidget(m_detailInfo);

    detailsLayout->addLayout(infoLayout, 1);

    QHBoxLayout *actionsLayout = new QHBoxLayout();
    actionsLayout->setSpacing(8);

    m_btnHttp = new QPushButton("🌐 HTTP", this);
    m_btnHttps = new QPushButton("🔒 HTTPS", this);
    m_btnSsh = new QPushButton("💻 SSH", this);
    m_btnPing = new QPushButton("🏓 Ping", this);
    m_btnPortScan = new QPushButton("🔍 Ports", this);
    m_btnWol = new QPushButton("⚡ WOL", this);
    m_btnCopy = new QPushButton("📋 Copy", this);

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

    detailsLayout->addLayout(actionsLayout);
    mainLayout->addWidget(m_detailsFrame);

    // Connect signals
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

    connect(m_portScanToolBtn, &QPushButton::clicked, this, &MainWindow::onDeepPortScan);
    connect(m_wolToolBtn, &QPushButton::clicked, this, &MainWindow::onWakeOnLan);
    connect(m_exportBtn, &QPushButton::clicked, this, &MainWindow::onExportCsv);
    connect(m_themeBtn, &QPushButton::clicked, this, &MainWindow::onToggleTheme);

    connect(m_btnHttp, &QPushButton::clicked, this, &MainWindow::onOpenHttp);
    connect(m_btnHttps, &QPushButton::clicked, this, &MainWindow::onOpenHttps);
    connect(m_btnSsh, &QPushButton::clicked, this, &MainWindow::onOpenSsh);
    connect(m_btnPing, &QPushButton::clicked, this, &MainWindow::onPingHost);
    connect(m_btnPortScan, &QPushButton::clicked, this, &MainWindow::onDeepPortScan);
    connect(m_btnWol, &QPushButton::clicked, this, &MainWindow::onWakeOnLan);
    connect(m_btnCopy, &QPushButton::clicked, this, &MainWindow::onCopyAllInfo);

    statusBar()->showMessage("IP Scanner ready.");
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
                QString maskStr = entry.netmask().toString();
                int prefixLen = entry.prefixLength();
                if (prefixLen <= 0) {
                    prefixLen = 24;
                }

                quint32 ipVal = entry.ip().toIPv4Address();
                quint32 maskVal = entry.netmask().toIPv4Address();
                if (maskVal == 0) {
                    maskVal = 0xFFFFFF00;
                }

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

                // Prefer non-virtual active ethernet/wifi
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
    statusBar()->showMessage("Network interfaces refreshed.", 3000);
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
        m_scanBtn->setText("Start Scan");
        m_scanBtn->setObjectName("primaryBtn");
        m_scanBtn->setStyle(m_scanBtn->style());
        m_pauseBtn->setEnabled(false);
        m_pauseBtn->setText("Pause");
        m_statusLabel->setText("Scan stopped by user.");
        statusBar()->showMessage("Scan cancelled.");
        return;
    }

    QString startIp = m_startIpEdit->text().trimmed();
    QString endIp = m_endIpEdit->text().trimmed();

    QHostAddress sAddr(startIp);
    QHostAddress eAddr(endIp);

    if (sAddr.protocol() != QAbstractSocket::IPv4Protocol || eAddr.protocol() != QAbstractSocket::IPv4Protocol) {
        QMessageBox::warning(this, "Invalid IP Range", "Please enter valid IPv4 addresses for Start IP and End IP.");
        return;
    }

    m_model->clear();
    m_progressBar->setValue(0);

    int concurrency = m_threadsSpin->value();
    double timeoutSec = m_timeoutSpin->value() / 1000.0;
    bool scanPorts = m_scanPortsCheck->isChecked();

    m_scanBtn->setText("Stop Scan");
    m_scanBtn->setObjectName("scanBtnActive");
    m_scanBtn->setStyle(m_scanBtn->style());
    m_pauseBtn->setEnabled(true);
    m_pauseBtn->setText("Pause");

    m_scanTimer.start();
    m_clockTimer->start();

    statusBar()->showMessage(QString("Scanning subnet %1 to %2...").arg(startIp, endIp));
    m_scanner->startScan(startIp, endIp, concurrency, timeoutSec, scanPorts);
}

void MainWindow::onPauseResumeScan()
{
    if (m_scanner->isPaused()) {
        m_scanner->resumeScan();
        m_pauseBtn->setText("Pause");
        m_statusLabel->setText("Scanning resumed...");
    } else if (m_scanner->isScanning()) {
        m_scanner->pauseScan();
        m_pauseBtn->setText("Resume");
        m_statusLabel->setText("Scanning paused.");
    }
}

void MainWindow::onClearResults()
{
    if (m_scanner->isScanning()) {
        m_scanner->stopScan();
    }
    m_model->clear();
    m_progressBar->setValue(0);
    m_timeBadge->setText("⏱ 00:00.0");
    m_statusLabel->setText("Results cleared.");
    m_detailTitle->setText("No host selected");
    m_detailInfo->setText("Select a host in the table above to view device details and quick actions.");
    m_btnHttp->setEnabled(false);
    m_btnHttps->setEnabled(false);
    m_btnSsh->setEnabled(false);
    m_btnPing->setEnabled(false);
    m_btnPortScan->setEnabled(false);
    m_btnWol->setEnabled(false);
    m_btnCopy->setEnabled(false);
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
    m_statusLabel->setText(QString("Starting scan across %1 hosts...").arg(totalHosts));
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
    m_model->addOrUpdateHost(h);
}

void MainWindow::onScanProgress(int completed, int total, const QString &currentIp)
{
    m_progressBar->setValue(completed);
    int percent = (total > 0) ? (completed * 100 / total) : 0;
    m_statusLabel->setText(QString("Probing %1 (%2% - %3 / %4 hosts)").arg(currentIp).arg(percent).arg(completed).arg(total));

    if (completed % 8 == 0) {
        updateMissingArpEntries();
    }
}

void MainWindow::onScanFinished()
{
    m_clockTimer->stop();
    m_scanBtn->setText("Start Scan");
    m_scanBtn->setObjectName("primaryBtn");
    m_scanBtn->setStyle(m_scanBtn->style());
    m_pauseBtn->setEnabled(false);
    m_pauseBtn->setText("Pause");

    updateMissingArpEntries();

    int alive = m_model->aliveCount();
    int total = m_model->totalCount();
    qint64 elapsedMs = m_scanTimer.elapsed();
    double elapsedSec = elapsedMs / 1000.0;

    m_progressBar->setValue(m_progressBar->maximum());
    m_statusLabel->setText(QString("Scan completed in %1s! Found %2 online hosts.").arg(elapsedSec, 0, 'f', 1).arg(alive));
    statusBar()->showMessage(QString("Scan completed: %1 online, %2 total hosts scanned in %3s.").arg(alive).arg(total).arg(elapsedSec, 0, 'f', 1));

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
        m_timeBadge->setText(QString("⏱ %1:%2.%3")
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
        m_detailTitle->setText("No host selected");
        m_detailInfo->setText("Select a host in the table above to view device details and quick actions.");
        m_btnHttp->setEnabled(false);
        m_btnHttps->setEnabled(false);
        m_btnSsh->setEnabled(false);
        m_btnPing->setEnabled(false);
        m_btnPortScan->setEnabled(false);
        m_btnWol->setEnabled(false);
        m_btnCopy->setEnabled(false);
        return;
    }

    QString title = host.hostname.isEmpty() ? host.ip : QString("%1  (%2)").arg(host.ip, host.hostname);
    m_detailTitle->setText(title);

    QStringList details;
    details << QString("Status: <b>%1</b>").arg(host.statusText());
    if (!host.macAddress.isEmpty()) {
        QString vendorStr = host.vendor.isEmpty() ? "Unknown Vendor" : host.vendor;
        details << QString("MAC: <b>%1</b> (%2)").arg(host.macAddress, vendorStr);
    }
    if (!host.openPorts.isEmpty()) {
        details << QString("Open Services: <b>%1</b>").arg(host.openPortsSummary());
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

    // If HTTP/HTTPS open, open in browser, otherwise ping
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
    menu.addAction("🌐 Open in Web Browser (HTTP)", this, &MainWindow::onOpenHttp);
    menu.addAction("🔒 Open in Web Browser (HTTPS)", this, &MainWindow::onOpenHttps);
    menu.addAction("💻 Connect via SSH", this, &MainWindow::onOpenSsh);
    menu.addSeparator();
    menu.addAction("🏓 Ping Host", this, &MainWindow::onPingHost);
    menu.addAction("🔍 Deep Port Scanner...", this, &MainWindow::onDeepPortScan);
    if (!host.macAddress.isEmpty()) {
        menu.addAction("⚡ Send Wake-on-LAN Magic Packet...", this, &MainWindow::onWakeOnLan);
    }
    menu.addSeparator();
    menu.addAction("📋 Copy IP Address", this, &MainWindow::onCopyIp);
    if (!host.macAddress.isEmpty()) {
        menu.addAction("📋 Copy MAC Address", this, &MainWindow::onCopyMac);
    }
    if (!host.hostname.isEmpty()) {
        menu.addAction("📋 Copy Hostname", this, &MainWindow::onCopyHostname);
    }
    menu.addAction("📋 Copy All Host Details", this, &MainWindow::onCopyAllInfo);

    menu.exec(m_tableView->viewport()->mapToGlobal(pos));
}

void MainWindow::onOpenHttp()
{
    HostItem host = getSelectedHost();
    if (!host.ip.isEmpty()) {
        QDesktopServices::openUrl(QUrl(QString("http://%1").arg(host.ip)));
    }
}

void MainWindow::onOpenHttps()
{
    HostItem host = getSelectedHost();
    if (!host.ip.isEmpty()) {
        QDesktopServices::openUrl(QUrl(QString("https://%1").arg(host.ip)));
    }
}

void MainWindow::onOpenSsh()
{
    HostItem host = getSelectedHost();
    if (host.ip.isEmpty()) return;

    // Launch default terminal with ssh or copy command
    QString cmd = QString("ssh %1").arg(host.ip);
    QApplication::clipboard()->setText(cmd);

    // Try common terminal emulators
    QStringList terminals = {"x-terminal-emulator", "konsole", "gnome-terminal", "alacritty", "kitty", "xfce4-terminal", "xterm"};
    bool launched = false;
    for (const QString &term : terminals) {
        if (QProcess::startDetached(term, {"-e", "ssh", host.ip})) {
            launched = true;
            break;
        }
    }

    if (!launched) {
        statusBar()->showMessage(QString("Copied '%1' to clipboard.").arg(cmd), 4000);
    }
}

void MainWindow::onPingHost()
{
    HostItem host = getSelectedHost();
    if (host.ip.isEmpty()) return;

    // Run ping in a detached terminal or report ping
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
            QMessageBox::information(this, "Ping " + host.ip, out);
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
        statusBar()->showMessage(QString("Copied IP %1 to clipboard.").arg(host.ip), 3000);
    }
}

void MainWindow::onCopyMac()
{
    HostItem host = getSelectedHost();
    if (!host.macAddress.isEmpty()) {
        QApplication::clipboard()->setText(host.macAddress);
        statusBar()->showMessage(QString("Copied MAC %1 to clipboard.").arg(host.macAddress), 3000);
    }
}

void MainWindow::onCopyHostname()
{
    HostItem host = getSelectedHost();
    if (!host.hostname.isEmpty()) {
        QApplication::clipboard()->setText(host.hostname);
        statusBar()->showMessage(QString("Copied Hostname %1 to clipboard.").arg(host.hostname), 3000);
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
    statusBar()->showMessage("Host details copied to clipboard.", 3000);
}

void MainWindow::onExportCsv()
{
    QString filePath = QFileDialog::getSaveFileName(this, "Export Results to CSV", "network_scan.csv", "CSV Files (*.csv)");
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Export Error", "Could not create file for writing.");
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

    statusBar()->showMessage(QString("Exported %1 rows to CSV.").arg(m_proxyModel->rowCount()), 4000);
}

void MainWindow::onExportJson()
{
    QString filePath = QFileDialog::getSaveFileName(this, "Export Results to JSON", "network_scan.json", "JSON Files (*.json)");
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Export Error", "Could not create file for writing.");
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

    statusBar()->showMessage(QString("Exported %1 rows to JSON.").arg(m_proxyModel->rowCount()), 4000);
}

void MainWindow::onExportTxt()
{
    QString filePath = QFileDialog::getSaveFileName(this, "Export Results to Text", "network_scan.txt", "Text Files (*.txt)");
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Export Error", "Could not create file for writing.");
        return;
    }

    QTextStream out(&file);
    out << QString("%1 | %2 | %3 | %4 | %5 | %6\n")
               .arg("IP Address", -16)
               .arg("Hostname", -22)
               .arg("MAC Address", -18)
               .arg("Ping", -10)
               .arg("Vendor", -25)
               .arg("Open Ports");
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

    statusBar()->showMessage(QString("Exported %1 rows to Text.").arg(m_proxyModel->rowCount()), 4000);
}

void MainWindow::onToggleTheme()
{
    applyTheme(!m_isDarkTheme);
}

void MainWindow::applyTheme(bool dark)
{
    m_isDarkTheme = dark;
    QString qssPath = dark ? ":/styles/dark.qss" : ":/styles/light.qss";
    QFile file(qssPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qApp->setStyleSheet(file.readAll());
    }
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, "About Network IP Scanner",
                       "<h3>Network IP Scanner</h3>"
                       "<p>A fast, multi-threaded Qt5 / C++ local network IP scanner.</p>"
                       "<ul>"
                       "<li>Discovers active hosts on your local subnet</li>"
                       "<li>Resolves Hostnames via Reverse DNS, mDNS, and NetBIOS</li>"
                       "<li>Retrieves MAC addresses from the kernel ARP cache</li>"
                       "<li>Identifies Hardware Vendors using built-in IEEE OUI database</li>"
                       "<li>Probes common services (HTTP, HTTPS, SSH, SMB, DNS, etc.)</li>"
                       "<li>Includes integrated Deep Port Scanner & Wake-on-LAN tools</li>"
                       "<li>Supports exporting to CSV, JSON, and Text formats</li>"
                       "</ul>");
}
