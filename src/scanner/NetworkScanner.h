#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QThreadPool>
#include <QAtomicInt>
#include <QTimer>
#include "../core/HostItem.h"

class NetworkScanner : public QObject {
    Q_OBJECT
public:
    explicit NetworkScanner(QObject *parent = nullptr);
    ~NetworkScanner() override;

    void startScan(const QString &startIp, 
                   const QString &endIp, 
                   int concurrency = 40, 
                   double timeoutSec = 0.4, 
                   bool scanPorts = false);
    void pauseScan();
    void resumeScan();
    void stopScan();

    bool isScanning() const { return m_isScanning; }
    bool isPaused() const { return m_isPaused; }
    int totalCount() const { return m_totalCount; }
    int completedCount() const { return m_completedCount.loadRelaxed(); }

    static QList<QString> generateIpRange(const QString &startIp, const QString &endIp);

signals:
    void scanStarted(int totalHosts);
    void hostDiscovered(const HostItem &host);
    void scanProgress(int completed, int total, const QString &currentIp);
    void scanFinished();

public slots:
    void onHostScanned(const HostItem &host);
    void onWorkerFinished(const QString &ip);

private slots:
    void dispatchNextBatch();
    void onPeriodicArpRefresh();

private:
    void finishScan();

private:
    QThreadPool m_threadPool;
    QList<QString> m_pendingIps;
    QAtomicInt m_completedCount;
    int m_totalCount;
    int m_concurrency;
    double m_timeoutSec;
    bool m_scanPorts;
    bool m_isScanning;
    bool m_isPaused;
    bool m_stopRequested;

    QTimer *m_arpRefreshTimer;
    QTimer *m_dispatchTimer;
};
