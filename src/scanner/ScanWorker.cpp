#include "ScanWorker.h"
#include "NetworkScanner.h"
#include "../core/ArpReader.h"
#include "../core/MacVendorLookup.h"
#include "PortScanner.h"

#include <QProcess>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QHostInfo>
#include <QDateTime>
#include <QThread>

ScanWorker::ScanWorker(const QString &ip, 
                       double timeoutSec, 
                       bool scanPorts, 
                       NetworkScanner *scanner)
    : m_ip(ip)
    , m_timeoutSec(timeoutSec)
    , m_scanPorts(scanPorts)
{
    setAutoDelete(true);
    if (scanner) {
        connect(this, &ScanWorker::hostScanned, scanner, &NetworkScanner::onHostScanned, Qt::QueuedConnection);
        connect(this, &ScanWorker::workerFinished, scanner, &NetworkScanner::onWorkerFinished, Qt::QueuedConnection);
    }
}

void ScanWorker::run()
{
    HostItem host(m_ip);
    double rttMs = -1.0;

    // 1. Try ICMP Ping
    bool alive = pingHost(m_ip, m_timeoutSec, rttMs);

    // 2. If ICMP ping failed, try TCP ping and check ARP
    if (!alive) {
        alive = tcpPing(m_ip, static_cast<int>(m_timeoutSec * 500));
    }

    // 3. Check if ARP table already caught this host
    QString mac = ArpReader::instance().getMacForIp(m_ip);
    if (!alive && !mac.isEmpty()) {
        alive = true;
    }

    host.isAlive = alive;
    host.responseTimeMs = rttMs;

    if (alive) {
        // Resolve MAC
        mac = ArpReader::instance().getMacForIp(m_ip);
        if (mac.isEmpty()) {
            QThread::msleep(30);
            mac = ArpReader::instance().getMacForIp(m_ip);
        }
        host.macAddress = mac;

        // Resolve Vendor
        if (!mac.isEmpty()) {
            host.vendor = MacVendorLookup::instance().lookup(mac);
        }

        // Resolve Hostname
        host.hostname = resolveHostname(m_ip);

        // Optional Port Scan
        if (m_scanPorts) {
            const QList<int> portsToProbe = { 21, 22, 53, 80, 139, 443, 445, 3389, 8080 };
            for (int p : portsToProbe) {
                double portRtt = 0;
                if (PortScanner::probePort(m_ip, p, 150, &portRtt)) {
                    host.openPorts.append(p);
                    host.services.append(PortScanner::serviceForPort(p));
                }
            }
        }
    }

    host.lastSeen = QDateTime::currentDateTime();

    emit hostScanned(host);
    emit workerFinished(m_ip);
}

bool ScanWorker::pingHost(const QString &ip, double timeoutSec, double &rttMs)
{
    QProcess proc;
    QStringList args;
    // Format ping args for Linux: -c 1 (1 packet), -W timeout (in seconds)
    args << "-c" << "1" << "-W" << QString::number(qMax(0.2, timeoutSec), 'f', 2) << ip;

    QElapsedTimer timer;
    timer.start();

    proc.start("ping", args);
    int waitMs = static_cast<int>(timeoutSec * 1000) + 300;
    if (proc.waitForFinished(waitMs)) {
        if (proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0) {
            QString output = QString::fromUtf8(proc.readAllStandardOutput());
            static const QRegularExpression rx("time[=<]([0-9.]+)\\s*ms");
            QRegularExpressionMatch match = rx.match(output);
            if (match.hasMatch()) {
                rttMs = match.captured(1).toDouble();
            } else {
                rttMs = timer.nsecsElapsed() / 1000000.0;
            }
            return true;
        }
    }
    return false;
}

bool ScanWorker::tcpPing(const QString &ip, int timeoutMs)
{
    const int ports[] = { 80, 443, 22, 445, 139 };
    for (int port : ports) {
        QTcpSocket socket;
        socket.connectToHost(ip, static_cast<quint16>(port));
        if (socket.waitForConnected(timeoutMs)) {
            socket.disconnectFromHost();
            return true;
        }
    }
    return false;
}

QString ScanWorker::resolveHostname(const QString &ip)
{
    // 1. Try standard system reverse DNS lookup (handles /etc/hosts, DNS, mDNS)
    QHostInfo info = QHostInfo::fromName(ip);
    if (info.error() == QHostInfo::NoError) {
        QString name = info.hostName().trimmed();
        if (!name.isEmpty() && name != ip) {
            return name;
        }
    }

    // 2. Try NetBIOS Name Query on UDP 137 (great for Windows & Samba hosts)
    QString nbName = queryNetBiosName(ip, 200);
    if (!nbName.isEmpty()) {
        return nbName;
    }

    return QString();
}

QString ScanWorker::queryNetBiosName(const QString &ip, int timeoutMs)
{
    QUdpSocket sock;
    if (!sock.bind(QHostAddress(QHostAddress::AnyIPv4), quint16(0))) {
        return QString();
    }

    // Construct NetBIOS Status Request query packet for wildcard '*'
    QByteArray pkt;
    // Transaction ID: 0xAB 0xCD
    pkt.append("\xAB\xCD", 2);
    // Flags: 0x0000 (Query)
    pkt.append("\x00\x00", 2);
    // Questions: 1, Answers: 0, Authority: 0, Additional: 0
    pkt.append("\x00\x01\x00\x00\x00\x00\x00\x00", 8);
    // Question Name: 32 bytes encoded '*' + null
    pkt.append('\x20');
    pkt.append("CKAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    pkt.append('\0');
    // Type: NBSTAT (0x0021), Class: IN (0x0001)
    pkt.append("\x00\x21\x00\x01", 4);

    sock.writeDatagram(pkt, QHostAddress(ip), 137);

    if (sock.waitForReadyRead(timeoutMs)) {
        QByteArray resp;
        resp.resize(static_cast<int>(sock.pendingDatagramSize()));
        QHostAddress sender;
        quint16 senderPort;
        sock.readDatagram(resp.data(), resp.size(), &sender, &senderPort);

        // NetBIOS response parsing
        if (resp.size() >= 57) {
            quint8 numNames = static_cast<quint8>(resp.at(56));
            for (int i = 0; i < numNames; ++i) {
                int offset = 57 + i * 18;
                if (offset + 18 <= resp.size()) {
                    QString name = QString::fromLatin1(resp.mid(offset, 15)).trimmed();
                    quint8 nameType = static_cast<quint8>(resp.at(offset + 15));
                    // 0x00 or 0x20 usually denotes workstation or server name
                    if ((nameType == 0x00 || nameType == 0x20) && !name.isEmpty()) {
                        return name;
                    }
                }
            }
        }
    }

    return QString();
}
