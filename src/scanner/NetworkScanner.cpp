#include "NetworkScanner.h"
#include "ScanWorker.h"
#include "../core/ArpReader.h"
#include <QHostAddress>
#include <algorithm>

NetworkScanner::NetworkScanner(QObject *parent)
    : QObject(parent)
    , m_totalCount(0)
    , m_concurrency(40)
    , m_timeoutSec(0.4)
    , m_scanPorts(false)
    , m_isScanning(false)
    , m_isPaused(false)
    , m_stopRequested(false)
{
    qRegisterMetaType<HostItem>("HostItem");

    m_arpRefreshTimer = new QTimer(this);
    m_arpRefreshTimer->setInterval(1500);
    connect(m_arpRefreshTimer, &QTimer::timeout, this, &NetworkScanner::onPeriodicArpRefresh);

    m_dispatchTimer = new QTimer(this);
    m_dispatchTimer->setInterval(50);
    connect(m_dispatchTimer, &QTimer::timeout, this, &NetworkScanner::dispatchNextBatch);
}

NetworkScanner::~NetworkScanner()
{
    stopScan();
}

QList<QString> NetworkScanner::generateIpRange(const QString &startIp, const QString &endIp)
{
    QList<QString> list;
    QHostAddress sAddr(startIp);
    QHostAddress eAddr(endIp);

    if (sAddr.protocol() != QAbstractSocket::IPv4Protocol || eAddr.protocol() != QAbstractSocket::IPv4Protocol) {
        return list;
    }

    quint32 startVal = sAddr.toIPv4Address();
    quint32 endVal = eAddr.toIPv4Address();

    if (startVal > endVal) {
        std::swap(startVal, endVal);
    }

    // Protect against accidentally generating excessive range
    quint32 count = endVal - startVal + 1;
    if (count > 65536) {
        count = 65536;
        endVal = startVal + count - 1;
    }

    list.reserve(static_cast<int>(count));
    for (quint32 ip = startVal; ip <= endVal; ++ip) {
        list.append(QHostAddress(ip).toString());
    }

    return list;
}

void NetworkScanner::startScan(const QString &startIp, 
                             const QString &endIp, 
                             int concurrency, 
                             double timeoutSec, 
                             bool scanPorts)
{
    stopScan();

    m_pendingIps = generateIpRange(startIp, endIp);
    m_totalCount = m_pendingIps.size();
    m_completedCount.storeRelaxed(0);
    m_concurrency = qBound(1, concurrency, 128);
    m_timeoutSec = qMax(0.1, timeoutSec);
    m_scanPorts = scanPorts;

    if (m_totalCount == 0) {
        emit scanFinished();
        return;
    }

    m_threadPool.setMaxThreadCount(m_concurrency);
    m_isScanning = true;
    m_isPaused = false;
    m_stopRequested = false;

    emit scanStarted(m_totalCount);

    m_arpRefreshTimer->start();
    m_dispatchTimer->start();
    dispatchNextBatch();
}

void NetworkScanner::pauseScan()
{
    if (m_isScanning) {
        m_isPaused = true;
    }
}

void NetworkScanner::resumeScan()
{
    if (m_isScanning && m_isPaused) {
        m_isPaused = false;
        dispatchNextBatch();
    }
}

void NetworkScanner::stopScan()
{
    if (!m_isScanning && !m_isPaused) {
        return;
    }

    m_stopRequested = true;
    m_isScanning = false;
    m_isPaused = false;

    m_dispatchTimer->stop();
    m_arpRefreshTimer->stop();

    m_pendingIps.clear();
    m_threadPool.clear();
    m_threadPool.waitForDone(500);

    emit scanFinished();
}

void NetworkScanner::finishScan()
{
    if (!m_isScanning) {
        return;
    }
    m_isScanning = false;
    m_isPaused = false;
    m_dispatchTimer->stop();
    m_arpRefreshTimer->stop();
    emit scanFinished();
}

void NetworkScanner::dispatchNextBatch()
{
    if (!m_isScanning || m_isPaused || m_stopRequested) {
        return;
    }

    // Keep the queue populated up to concurrency * 2
    while (!m_pendingIps.isEmpty() && m_threadPool.activeThreadCount() < m_concurrency * 2) {
        QString ip = m_pendingIps.takeFirst();
        ScanWorker *worker = new ScanWorker(ip, m_timeoutSec, m_scanPorts, this);
        m_threadPool.start(worker);
    }

    if (m_pendingIps.isEmpty() && m_threadPool.activeThreadCount() == 0) {
        finishScan();
    }
}

void NetworkScanner::onHostScanned(const HostItem &host)
{
    if (!m_stopRequested) {
        emit hostDiscovered(host);
    }
}

void NetworkScanner::onWorkerFinished(const QString &ip)
{
    int done = m_completedCount.fetchAndAddRelaxed(1) + 1;
    emit scanProgress(done, m_totalCount, ip);

    if (done >= m_totalCount) {
        finishScan();
    } else {
        dispatchNextBatch();
    }
}

void NetworkScanner::onPeriodicArpRefresh()
{
    ArpReader::instance().refreshArpTable();
}
