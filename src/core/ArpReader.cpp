#include "ArpReader.h"
#include <QFile>
#include <QTextStream>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QDebug>

ArpReader& ArpReader::instance()
{
    static ArpReader inst;
    return inst;
}

ArpReader::ArpReader()
{
    refreshArpTable();
}

void ArpReader::refreshArpTable()
{
    QMutexLocker locker(&m_mutex);
    readProcNetArp();
    readLocalInterfaces();
}

void ArpReader::readProcNetArp()
{
    QFile file("/proc/net/arp");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QByteArray content = file.readAll();
    file.close();

    QList<QByteArray> lines = content.split('\n');
    // Skip header
    for (int i = 1; i < lines.size(); ++i) {
        QByteArray line = lines[i].trimmed();
        if (line.isEmpty()) {
            continue;
        }

        QList<QByteArray> rawTokens = line.split(' ');
        QStringList tokens;
        for (const QByteArray &t : rawTokens) {
            if (!t.isEmpty()) {
                tokens.append(QString::fromLatin1(t));
            }
        }

        if (tokens.size() >= 4) {
            QString ip = tokens[0];
            QString flags = tokens[2];
            QString mac = tokens[3].toUpper();

            if (flags != "0x0" && mac != "00:00:00:00:00:00" && mac.contains(':')) {
                m_ipToMac.insert(ip, mac);
            }
        }
    }
}

void ArpReader::readLocalInterfaces()
{
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        if (!iface.isValid() || !(iface.flags() & QNetworkInterface::IsUp)) {
            continue;
        }

        QString mac = iface.hardwareAddress().toUpper();
        if (mac.isEmpty() || mac == "00:00:00:00:00:00") {
            continue;
        }

        const QList<QNetworkAddressEntry> entries = iface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                m_ipToMac.insert(entry.ip().toString(), mac);
            }
        }
    }
}

QString ArpReader::getMacForIp(const QString &ip)
{
    QMutexLocker locker(&m_mutex);
    if (m_ipToMac.contains(ip)) {
        return m_ipToMac.value(ip);
    }

    // Refresh and check again
    readProcNetArp();
    readLocalInterfaces();
    return m_ipToMac.value(ip, QString());
}

bool ArpReader::hasMacForIp(const QString &ip)
{
    QMutexLocker locker(&m_mutex);
    return m_ipToMac.contains(ip);
}
