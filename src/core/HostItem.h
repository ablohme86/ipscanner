#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <QDateTime>
#include <QHostAddress>

class HostItem {
public:
    HostItem();
    explicit HostItem(const QString &ip);

    QString ip;
    quint32 ipv4Num;
    QString hostname;
    QString macAddress;
    QString vendor;
    double responseTimeMs;
    bool isAlive;
    QList<int> openPorts;
    QStringList services;
    QString comments;
    QDateTime lastSeen;

    static quint32 ipToNumber(const QString &ipStr);
    QString openPortsSummary() const;
    QString statusText() const;
};

Q_DECLARE_METATYPE(HostItem)
