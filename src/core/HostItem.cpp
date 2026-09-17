#include "HostItem.h"

HostItem::HostItem()
    : ipv4Num(0)
    , responseTimeMs(-1.0)
    , isAlive(false)
{
    lastSeen = QDateTime::currentDateTime();
}

HostItem::HostItem(const QString &ipStr)
    : ip(ipStr)
    , ipv4Num(ipToNumber(ipStr))
    , responseTimeMs(-1.0)
    , isAlive(false)
{
    lastSeen = QDateTime::currentDateTime();
}

quint32 HostItem::ipToNumber(const QString &ipStr)
{
    QHostAddress addr(ipStr);
    if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
        return addr.toIPv4Address();
    }
    return 0;
}

QString HostItem::openPortsSummary() const
{
    if (openPorts.isEmpty()) {
        return QString();
    }

    QStringList portStrings;
    for (int i = 0; i < openPorts.size(); ++i) {
        int port = openPorts[i];
        QString svc = (i < services.size() && !services[i].isEmpty()) ? services[i] : QString();
        if (!svc.isEmpty()) {
            portStrings.append(QString("%1 (%2)").arg(port).arg(svc));
        } else {
            portStrings.append(QString::number(port));
        }
    }
    return portStrings.join(", ");
}

QString HostItem::statusText() const
{
    if (!isAlive) {
        return "Offline";
    }
    if (responseTimeMs >= 0) {
        return QString("Online (%1 ms)").arg(responseTimeMs, 0, 'f', 1);
    }
    return "Online";
}
