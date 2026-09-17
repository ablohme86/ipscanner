#pragma once

#include <QRunnable>
#include <QObject>
#include <QString>
#include "../core/HostItem.h"

class NetworkScanner;

class ScanWorker : public QObject, public QRunnable {
    Q_OBJECT
public:
    ScanWorker(const QString &ip, 
               double timeoutSec, 
               bool scanPorts, 
               NetworkScanner *scanner);
    ~ScanWorker() override = default;

    void run() override;

signals:
    void hostScanned(const HostItem &host);
    void workerFinished(const QString &ip);

private:
    bool pingHost(const QString &ip, double timeoutSec, double &rttMs);
    bool tcpPing(const QString &ip, int timeoutMs);
    QString resolveHostname(const QString &ip);
    QString queryNetBiosName(const QString &ip, int timeoutMs = 250);

    QString m_ip;
    double m_timeoutSec;
    bool m_scanPorts;
};
