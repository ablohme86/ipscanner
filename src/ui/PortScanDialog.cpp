#include "PortScanDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QClipboard>
#include <QApplication>
#include <QMessageBox>

PortScanDialog::PortScanDialog(const QString &ip, QWidget *parent)
    : QDialog(parent)
    , m_scanner(new PortScanner(this))
{
    setWindowTitle("Deep Port Scanner - " + (ip.isEmpty() ? "Target Host" : ip));
    resize(560, 480);
    setMinimumSize(480, 360);

    setupUi();
    if (!ip.isEmpty()) {
        m_ipEdit->setText(ip);
    }

    connect(m_scanner, &PortScanner::portFound, this, &PortScanDialog::onPortFound);
    connect(m_scanner, &PortScanner::progressUpdated, this, &PortScanDialog::onProgressUpdated);
    connect(m_scanner, &PortScanner::scanFinished, this, &PortScanDialog::onScanFinished);
}

PortScanDialog::~PortScanDialog()
{
    m_scanner->stopScan();
}

void PortScanDialog::setTargetIp(const QString &ip)
{
    m_ipEdit->setText(ip);
    setWindowTitle("Deep Port Scanner - " + ip);
}

void PortScanDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    // Controls Grid
    QGridLayout *grid = new QGridLayout();
    grid->setSpacing(8);

    QLabel *ipLbl = new QLabel("Target IP:", this);
    m_ipEdit = new QLineEdit(this);
    m_ipEdit->setPlaceholderText("e.g. 192.168.1.1");

    QLabel *presetLbl = new QLabel("Port Range:", this);
    m_presetCombo = new QComboBox(this);
    m_presetCombo->addItem("Top 20 Common Services", 0);
    m_presetCombo->addItem("Standard Well-Known (1 - 1024)", 1);
    m_presetCombo->addItem("Web & Admin Services", 2);
    m_presetCombo->addItem("Database Ports", 3);
    m_presetCombo->addItem("Custom Range / List", 4);

    m_customPortsEdit = new QLineEdit(this);
    m_customPortsEdit->setPlaceholderText("e.g. 80, 443, 8000-8080");
    m_customPortsEdit->setVisible(false);

    m_startStopBtn = new QPushButton("Start Port Scan", this);
    m_startStopBtn->setObjectName("primaryBtn");

    grid->addWidget(ipLbl, 0, 0);
    grid->addWidget(m_ipEdit, 0, 1);
    grid->addWidget(m_startStopBtn, 0, 2);

    grid->addWidget(presetLbl, 1, 0);
    grid->addWidget(m_presetCombo, 1, 1);
    grid->addWidget(m_customPortsEdit, 2, 1);

    mainLayout->addLayout(grid);

    // Progress
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    mainLayout->addWidget(m_progressBar);

    m_statusLabel = new QLabel("Ready to scan.", this);
    m_statusLabel->setStyleSheet("color: #94a3b8; font-size: 11px;");
    mainLayout->addWidget(m_statusLabel);

    // Table
    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({"Port", "Service", "Protocol", "Latency"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    mainLayout->addWidget(m_table);

    // Bottom buttons
    QHBoxLayout *btnLayout = new QHBoxLayout();
    m_copyBtn = new QPushButton("Copy Open Ports", this);
    m_closeBtn = new QPushButton("Close", this);
    btnLayout->addWidget(m_copyBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_closeBtn);

    mainLayout->addLayout(btnLayout);

    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PortScanDialog::onPortPresetChanged);
    connect(m_startStopBtn, &QPushButton::clicked, this, &PortScanDialog::onStartStopClicked);
    connect(m_copyBtn, &QPushButton::clicked, this, &PortScanDialog::onCopyClicked);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void PortScanDialog::onPortPresetChanged(int index)
{
    m_customPortsEdit->setVisible(index == 4);
}

QList<int> PortScanDialog::getPortsToScan() const
{
    int index = m_presetCombo->currentIndex();
    QList<int> ports;

    if (index == 0) {
        ports = PortScanner::commonPorts();
    } else if (index == 1) {
        ports.reserve(1024);
        for (int i = 1; i <= 1024; ++i) ports.append(i);
    } else if (index == 2) {
        ports = {80, 443, 8000, 8008, 8080, 8443, 8888, 9000, 9090, 10000};
    } else if (index == 3) {
        ports = {1433, 1521, 3306, 5432, 6379, 27017};
    } else if (index == 4) {
        QString txt = m_customPortsEdit->text().trimmed();
        QStringList parts = txt.split(',', Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            QString clean = part.trimmed();
            if (clean.contains('-')) {
                QStringList range = clean.split('-');
                if (range.size() == 2) {
                    int start = range[0].toInt();
                    int end = range[1].toInt();
                    if (start > 0 && end >= start && end <= 65535) {
                        for (int p = start; p <= end; ++p) ports.append(p);
                    }
                }
            } else {
                int p = clean.toInt();
                if (p > 0 && p <= 65535) ports.append(p);
            }
        }
    }

    return ports;
}

void PortScanDialog::onStartStopClicked()
{
    if (m_scanner->isScanning()) {
        m_scanner->stopScan();
        m_startStopBtn->setText("Start Port Scan");
        m_statusLabel->setText("Port scan stopped.");
        return;
    }

    QString ip = m_ipEdit->text().trimmed();
    if (ip.isEmpty()) {
        QMessageBox::warning(this, "Target Required", "Please enter a valid IP address.");
        return;
    }

    QList<int> ports = getPortsToScan();
    if (ports.isEmpty()) {
        QMessageBox::warning(this, "Ports Required", "Please specify at least one port to scan.");
        return;
    }

    m_table->setRowCount(0);
    m_progressBar->setValue(0);
    m_startStopBtn->setText("Stop Scan");
    m_statusLabel->setText(QString("Scanning %1 ports on %2...").arg(ports.size()).arg(ip));

    m_scanner->startScan(ip, ports, 30, 250);
}

void PortScanDialog::onPortFound(const QString &ip, const PortInfo &info)
{
    Q_UNUSED(ip);
    int row = m_table->rowCount();
    m_table->insertRow(row);

    QTableWidgetItem *portItem = new QTableWidgetItem(QString::number(info.port));
    QTableWidgetItem *svcItem = new QTableWidgetItem(info.service.isEmpty() ? "Unknown" : info.service);
    QTableWidgetItem *protoItem = new QTableWidgetItem("TCP");
    QTableWidgetItem *rttItem = new QTableWidgetItem(QString("%1 ms").arg(info.responseTimeMs, 0, 'f', 1));

    portItem->setForeground(QColor("#10b981"));
    svcItem->setForeground(QColor("#38bdf8"));

    m_table->setItem(row, 0, portItem);
    m_table->setItem(row, 1, svcItem);
    m_table->setItem(row, 2, protoItem);
    m_table->setItem(row, 3, rttItem);
}

void PortScanDialog::onProgressUpdated(int scanned, int total)
{
    if (total > 0) {
        int percent = (scanned * 100) / total;
        m_progressBar->setValue(percent);
        m_statusLabel->setText(QString("Scanned %1 of %2 ports (Found: %3 open)").arg(scanned).arg(total).arg(m_table->rowCount()));
    }
}

void PortScanDialog::onScanFinished()
{
    m_startStopBtn->setText("Start Port Scan");
    m_statusLabel->setText(QString("Scan finished. Found %1 open port(s).").arg(m_table->rowCount()));
}

void PortScanDialog::onCopyClicked()
{
    QStringList lines;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QString port = m_table->item(r, 0) ? m_table->item(r, 0)->text() : "";
        QString svc = m_table->item(r, 1) ? m_table->item(r, 1)->text() : "";
        lines.append(QString("%1 (%2)").arg(port, svc));
    }
    QApplication::clipboard()->setText(lines.join(", "));
}
