#include "WolDialog.h"
#include "../scanner/WakeOnLan.h"
#include <QVBoxLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMessageBox>

WolDialog::WolDialog(const QString &macAddress, const QString &broadcastIp, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Wake on LAN (Magic Packet)");
    resize(420, 260);
    setMinimumSize(380, 220);

    setupUi();

    if (!macAddress.isEmpty()) {
        m_macEdit->setText(macAddress);
    }
    if (!broadcastIp.isEmpty()) {
        m_broadcastEdit->setText(broadcastIp);
    }
}

void WolDialog::setMacAddress(const QString &mac)
{
    m_macEdit->setText(mac);
}

void WolDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    QLabel *infoLbl = new QLabel("Send a Wake-on-LAN magic packet to power on a remote machine on your network.", this);
    infoLbl->setWordWrap(true);
    infoLbl->setStyleSheet("color: #94a3b8; margin-bottom: 6px;");
    mainLayout->addWidget(infoLbl);

    QGridLayout *grid = new QGridLayout();
    grid->setSpacing(10);

    QLabel *macLbl = new QLabel("MAC Address:", this);
    m_macEdit = new QLineEdit(this);
    m_macEdit->setPlaceholderText("e.g. 00:11:22:33:44:55");

    QLabel *bcastLbl = new QLabel("Broadcast IP:", this);
    m_broadcastEdit = new QLineEdit("255.255.255.255", this);

    QLabel *portLbl = new QLabel("Port (UDP):", this);
    m_portEdit = new QLineEdit("9", this);

    grid->addWidget(macLbl, 0, 0);
    grid->addWidget(m_macEdit, 0, 1);
    grid->addWidget(bcastLbl, 1, 0);
    grid->addWidget(m_broadcastEdit, 1, 1);
    grid->addWidget(portLbl, 2, 0);
    grid->addWidget(m_portEdit, 2, 1);

    mainLayout->addLayout(grid);

    m_statusLabel = new QLabel("", this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet("font-weight: bold; margin-top: 4px;");
    mainLayout->addWidget(m_statusLabel);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    m_sendBtn = new QPushButton("Send Wake-on-LAN", this);
    m_sendBtn->setObjectName("primaryBtn");
    m_closeBtn = new QPushButton("Close", this);

    btnLayout->addWidget(m_sendBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(m_closeBtn);

    mainLayout->addLayout(btnLayout);

    connect(m_sendBtn, &QPushButton::clicked, this, &WolDialog::onSendClicked);
    connect(m_closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void WolDialog::onSendClicked()
{
    QString mac = m_macEdit->text().trimmed();
    QString bcast = m_broadcastEdit->text().trimmed();
    quint16 port = static_cast<quint16>(m_portEdit->text().toInt());

    if (mac.isEmpty()) {
        m_statusLabel->setStyleSheet("color: #ef4444;");
        m_statusLabel->setText("Error: MAC address is required.");
        return;
    }

    if (bcast.isEmpty()) {
        bcast = "255.255.255.255";
    }

    if (port == 0) {
        port = 9;
    }

    QString errMsg;
    bool success = WakeOnLan::sendMagicPacket(mac, bcast, port, &errMsg);

    if (success) {
        m_statusLabel->setStyleSheet("color: #10b981;");
        m_statusLabel->setText(QString("Magic packet sent to %1 via %2:%3 successfully!").arg(mac, bcast).arg(port));
    } else {
        m_statusLabel->setStyleSheet("color: #ef4444;");
        m_statusLabel->setText(QString("Failed to send magic packet: %1").arg(errMsg));
    }
}
