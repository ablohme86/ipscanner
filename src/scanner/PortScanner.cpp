#include "PortScanner.h"
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QAtomicInt>

PortScanner::PortScanner(QObject *parent)
    : QObject(parent)
    , m_cancelRequested(false)
    , m_scanning(false)
{
    qRegisterMetaType<PortInfo>("PortInfo");
}

PortScanner::~PortScanner()
{
    stopScan();
}

QString PortScanner::serviceForPort(int port)
{
    switch (port) {
    case 20: return "FTP-Data";
    case 21: return "FTP";
    case 22: return "SSH";
    case 23: return "Telnet";
    case 25: return "SMTP";
    case 53: return "DNS";
    case 67: return "DHCP Server";
    case 68: return "DHCP Client";
    case 69: return "TFTP";
    case 80: return "HTTP";
    case 110: return "POP3";
    case 123: return "NTP";
    case 137: return "NetBIOS Name";
    case 138: return "NetBIOS Datagram";
    case 139: return "NetBIOS Session";
    case 143: return "IMAP";
    case 161: return "SNMP";
    case 389: return "LDAP";
    case 443: return "HTTPS";
    case 445: return "SMB / CIFS";
    case 465: return "SMTPS";
    case 514: return "Syslog";
    case 587: return "SMTP Submission";
    case 636: return "LDAPS";
    case 993: return "IMAPS";
    case 995: return "POP3S";
    case 1433: return "MS-SQL";
    case 1521: return "Oracle DB";
    case 1883: return "MQTT";
    case 2049: return "NFS";
    case 3306: return "MySQL";
    case 3389: return "RDP (Remote Desktop)";
    case 5000: return "UPnP / Synology / Flask";
    case 5353: return "mDNS";
    case 5432: return "PostgreSQL";
    case 5900: return "VNC";
    case 6379: return "Redis";
    case 8000: return "HTTP-Alt";
    case 8080: return "HTTP-Proxy";
    case 8443: return "HTTPS-Alt";
    case 8888: return "HTTP-Alt";
    case 9000: return "Portainer / SonarQube";
    case 9090: return "Cockpit / Web Admin";
    case 9100: return "RAW JetDirect Printer";
    default: return QString();
    }
}

QList<int> PortScanner::commonPorts()
{
    return {
        21, 22, 23, 25, 53, 80, 110, 139, 143, 
        443, 445, 1433, 3306, 3389, 5000, 5432, 
        5900, 6379, 8000, 8080, 8443, 9090
    };
}

bool PortScanner::probePort(const QString &ip, int port, int timeoutMs, double *rttMs)
{
    QTcpSocket socket;
    QElapsedTimer timer;
    timer.start();

    socket.connectToHost(ip, static_cast<quint16>(port));
    bool connected = socket.waitForConnected(timeoutMs);

    if (connected) {
        if (rttMs) {
            *rttMs = timer.nsecsElapsed() / 1000000.0;
        }
        socket.disconnectFromHost();
        return true;
    }

    return false;
}

void PortScanner::startScan(const QString &ip, const QList<int> &ports, int concurrency, int timeoutMs)
{
    if (m_scanning) {
        stopScan();
    }

    m_cancelRequested = false;
    m_scanning = true;

    QThreadPool *pool = new QThreadPool(this);
    pool->setMaxThreadCount(qBound(1, concurrency, 100));

    auto *completed = new QAtomicInt(0);
    int total = ports.size();

    for (int port : ports) {
        QtConcurrent::run(pool, [this, ip, port, timeoutMs, total, completed]() {
            if (m_cancelRequested) {
                return;
            }

            double rtt = 0;
            bool open = probePort(ip, port, timeoutMs, &rtt);

            if (open && !m_cancelRequested) {
                PortInfo info;
                info.port = port;
                info.service = serviceForPort(port);
                info.isOpen = true;
                info.responseTimeMs = rtt;

                QMetaObject::invokeMethod(this, "portFound", Qt::QueuedConnection,
                                          Q_ARG(QString, ip),
                                          Q_ARG(PortInfo, info));
            }

            int count = completed->fetchAndAddRelaxed(1) + 1;
            QMetaObject::invokeMethod(this, "progressUpdated", Qt::QueuedConnection,
                                      Q_ARG(int, count),
                                      Q_ARG(int, total));

            if (count >= total) {
                QMetaObject::invokeMethod(this, [this]() {
                    m_scanning = false;
                    emit scanFinished();
                }, Qt::QueuedConnection);
            }
        });
    }
}

void PortScanner::stopScan()
{
    m_cancelRequested = true;
    m_scanning = false;
}

bool PortScanner::isScanning() const
{
    return m_scanning;
}
