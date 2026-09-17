#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QPair>

struct PortInfo {
    int port;
    QString service;
    bool isOpen;
    double responseTimeMs;
};

Q_DECLARE_METATYPE(PortInfo)

class PortScanner : public QObject {
    Q_OBJECT
public:
    explicit PortScanner(QObject *parent = nullptr);
    ~PortScanner() override;

    static QString serviceForPort(int port);
    static QList<int> commonPorts();
    static bool probePort(const QString &ip, int port, int timeoutMs = 250, double *rttMs = nullptr);

    void startScan(const QString &ip, const QList<int> &ports, int concurrency = 20, int timeoutMs = 300);
    void stopScan();
    bool isScanning() const;

signals:
    void portFound(const QString &ip, const PortInfo &info);
    void progressUpdated(int scanned, int total);
    void scanFinished();

private:
    bool m_cancelRequested;
    bool m_scanning;
};
