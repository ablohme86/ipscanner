#include "TuiApp.h"
#include <QNetworkInterface>
#include <QHostAddress>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>

#include "../core/ArpReader.h"
#include "../core/MacVendorLookup.h"
#include "../scanner/WakeOnLan.h"

TuiApp::TuiApp(QCoreApplication *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_palette(TuiPalette::get(TuiPaletteId::TokyoNight))
    , m_scanner(new NetworkScanner(this))
    , m_portScanner(new PortScanner(this))
{
    // Connect scanner signals
    connect(m_scanner, &NetworkScanner::scanStarted, this, &TuiApp::onScanStarted);
    connect(m_scanner, &NetworkScanner::hostDiscovered, this, &TuiApp::onHostDiscovered);
    connect(m_scanner, &NetworkScanner::scanProgress, this, &TuiApp::onScanProgress);
    connect(m_scanner, &NetworkScanner::scanFinished, this, &TuiApp::onScanFinished);

    // Connect port scanner signals
    connect(m_portScanner, &PortScanner::portFound, this, &TuiApp::onPortFound);
    connect(m_portScanner, &PortScanner::progressUpdated, this, &TuiApp::onPortProgress);
    connect(m_portScanner, &PortScanner::scanFinished, this, &TuiApp::onPortScanFinished);

    populateInterfaces();
    MacVendorLookup::instance().init();
    ArpReader::instance().refreshArpTable();

    appendLog("SYS.NET_RADAR // Recon Terminal Core initialized.", "SYS");
    appendLog(QString("Bound to %1 (%2/%3)").arg(m_interfaces.value(m_selectedIfaceIndex).name,
                                                 m_interfaces.value(m_selectedIfaceIndex).ip)
                                            .arg(m_interfaces.value(m_selectedIfaceIndex).prefix), "IFACE");
}

TuiApp::~TuiApp()
{
    if (m_scanner && m_scanner->isScanning()) {
        m_scanner->stopScan();
    }
    if (m_portScanner && m_portScanner->isScanning()) {
        m_portScanner->stopScan();
    }
    if (m_pingProcess) {
        m_pingProcess->kill();
        m_pingProcess->waitForFinished(500);
    }
    m_screen.restore();
}

void TuiApp::setRange(const QString &startIp, const QString &endIp)
{
    if (!startIp.isEmpty()) m_startIp = startIp;
    if (!endIp.isEmpty()) m_endIp = endIp;
    m_editStartIp = m_startIp;
    m_editEndIp = m_endIp;
}

void TuiApp::setExitAfter(int seconds)
{
    m_exitAfterSeconds = seconds;
    if (seconds > 0) {
        QTimer::singleShot(seconds * 1000, m_app, &QCoreApplication::quit);
    }
}

bool TuiApp::start()
{
    if (!m_screen.init()) {
        return false;
    }

    m_inputNotifier = new QSocketNotifier(STDIN_FILENO, QSocketNotifier::Read, this);
    connect(m_inputNotifier, &QSocketNotifier::activated, this, &TuiApp::onInputReady);

    m_tickTimer = new QTimer(this);
    connect(m_tickTimer, &QTimer::timeout, this, &TuiApp::onTick);
    m_tickTimer->start(60);

    if (m_autoScanOnStart) {
        QTimer::singleShot(250, this, [this]() {
            startScan();
        });
    }

    render();
    return true;
}

void TuiApp::populateInterfaces()
{
    m_interfaces.clear();
    const QList<QNetworkInterface> all = QNetworkInterface::allInterfaces();
    int defaultIdx = -1;

    for (const QNetworkInterface &iface : all) {
        if (!iface.flags().testFlag(QNetworkInterface::IsUp) ||
            iface.flags().testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }

        const QList<QNetworkAddressEntry> entries = iface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                QString ipStr = entry.ip().toString();
                int prefix = entry.prefixLength();
                if (prefix <= 0) prefix = 24;

                quint32 ipVal = entry.ip().toIPv4Address();
                quint32 maskVal = entry.netmask().toIPv4Address();
                if (maskVal == 0) maskVal = 0xFFFFFF00;

                quint32 netVal = ipVal & maskVal;
                quint32 bcastVal = netVal | (~maskVal);

                quint32 firstHost = netVal + 1;
                quint32 lastHost = (bcastVal > 1) ? (bcastVal - 1) : firstHost;

                InterfaceItem item;
                item.name = iface.humanReadableName();
                item.ip = ipStr;
                item.prefix = prefix;
                item.startIp = QHostAddress(firstHost).toString();
                item.endIp = QHostAddress(lastHost).toString();
                item.bcastIp = QHostAddress(bcastVal).toString();

                m_interfaces.append(item);

                if (defaultIdx == -1 && !item.name.contains("virbr") && !item.name.contains("docker")) {
                    defaultIdx = m_interfaces.size() - 1;
                }
            }
        }
    }

    if (!m_interfaces.isEmpty()) {
        m_selectedIfaceIndex = (defaultIdx >= 0) ? defaultIdx : 0;
        applyInterface(m_selectedIfaceIndex);
    }
}

void TuiApp::applyInterface(int index)
{
    if (index < 0 || index >= m_interfaces.size()) return;
    m_selectedIfaceIndex = index;
    const InterfaceItem &item = m_interfaces.at(index);
    m_startIp = item.startIp;
    m_endIp = item.endIp;
    m_bcastIp = item.bcastIp;
    m_editStartIp = m_startIp;
    m_editEndIp = m_endIp;
}

void TuiApp::startScan()
{
    if (m_scanner->isScanning()) {
        stopScan();
        return;
    }

    m_hosts.clear();
    m_filteredHosts.clear();
    m_selectedHostIndex = 0;
    m_tableScrollOffset = 0;
    m_completedHosts = 0;
    m_aliveCount = 0;
    m_offlineCount = 0;
    m_servicesCount = 0;
    m_isScanning = true;
    m_isPaused = false;
    m_scanTimer.restart();
    m_scanElapsedMs = 0;

    ArpReader::instance().refreshArpTable();

    appendLog(QString("Initiating subnet scan on %1 .. %2 [%3 threads, %4ms timeout]")
                  .arg(m_startIp, m_endIp)
                  .arg(m_concurrency)
                  .arg(static_cast<int>(m_timeoutSec * 1000.0)), "SCAN");

    m_scanner->startScan(m_startIp, m_endIp, m_concurrency, m_timeoutSec, m_scanPorts);
}

void TuiApp::pauseScan()
{
    if (m_scanner->isPaused()) {
        m_scanner->resumeScan();
        m_isPaused = false;
        m_statusMessage = "> RECONNAISSANCE RESUMED.";
        appendLog("SCAN RESUMED.", "SYS");
    } else if (m_scanner->isScanning()) {
        m_scanner->pauseScan();
        m_isPaused = true;
        m_statusMessage = "> SCAN SUSPENDED [HALT MODE].";
        appendLog("SCAN SUSPENDED.", "SYS");
    }
}

void TuiApp::resumeScan()
{
    if (m_scanner->isPaused()) {
        m_scanner->resumeScan();
        m_isPaused = false;
    }
}

void TuiApp::stopScan()
{
    if (m_scanner->isScanning()) {
        m_scanner->stopScan();
        m_isScanning = false;
        m_isPaused = false;
        m_statusMessage = "> SCAN ABORTED BY OPERATOR.";
        appendLog("SCAN TERMINATED BY OPERATOR.", "SYS");
    }
}

void TuiApp::clearScan()
{
    if (m_scanner->isScanning()) {
        m_scanner->stopScan();
    }
    m_hosts.clear();
    m_filteredHosts.clear();
    m_selectedHostIndex = 0;
    m_tableScrollOffset = 0;
    m_completedHosts = 0;
    m_aliveCount = 0;
    m_offlineCount = 0;
    m_servicesCount = 0;
    m_isScanning = false;
    m_isPaused = false;
    m_scanElapsedMs = 0;
    m_currentProbingIp = "-";
    m_statusMessage = "> TARGET MATRIX PURGED.";
    appendLog("Target matrix flushed.", "SYS");
}

void TuiApp::onScanStarted(int totalHosts)
{
    m_totalHosts = totalHosts;
    m_completedHosts = 0;
    m_isScanning = true;
    m_statusMessage = QString("> PROBING %1 TARGET NODES...").arg(totalHosts);
}

void TuiApp::onHostDiscovered(const HostItem &host)
{
    HostItem h = host;
    if (h.isAlive && h.macAddress.isEmpty()) {
        QString mac = ArpReader::instance().getMacForIp(h.ip);
        if (!mac.isEmpty()) {
            h.macAddress = mac;
            h.vendor = MacVendorLookup::instance().lookup(mac);
        }
    }

    // Update or insert
    bool found = false;
    for (int i = 0; i < m_hosts.size(); ++i) {
        if (m_hosts[i].ip == h.ip) {
            m_hosts[i] = h;
            found = true;
            break;
        }
    }
    if (!found) {
        m_hosts.append(h);
    }

    // Update counts
    m_aliveCount = 0;
    m_offlineCount = 0;
    m_servicesCount = 0;
    for (const auto &item : m_hosts) {
        if (item.isAlive) {
            m_aliveCount++;
            m_servicesCount += item.openPorts.size();
        } else {
            m_offlineCount++;
        }
    }

    if (h.isAlive) {
        QString logLine = QString("%1 %2 | MAC: %3 [%4] | RTT: %5ms")
                              .arg(h.ip)
                              .arg(h.hostname.isEmpty() ? "" : QString("(%1)").arg(h.hostname))
                              .arg(h.macAddress.isEmpty() ? "--:--:--:--:--:--" : h.macAddress)
                              .arg(h.vendor.isEmpty() ? "OUI_UNK" : h.vendor.left(18))
                              .arg(QString::number(h.responseTimeMs, 'f', 1));
        appendLog(logLine, "+");

        if (!h.openPorts.isEmpty()) {
            appendLog(QString("%1 -> %2").arg(h.ip, h.openPortsSummary()), "PORTS");
        }
    }

    applyFilter();
}

void TuiApp::onScanProgress(int completed, int total, const QString &currentIp)
{
    m_completedHosts = completed;
    m_totalHosts = total;
    m_currentProbingIp = currentIp;
}

void TuiApp::onScanFinished()
{
    m_isScanning = false;
    m_isPaused = false;
    m_currentProbingIp = "-";
    m_statusMessage = QString("> SCAN COMPLETE // Discovered %1 active node(s) in %2s.")
                          .arg(m_aliveCount)
                          .arg(QString::number(m_scanElapsedMs / 1000.0, 'f', 1));
    appendLog(QString("Subnet reconnaissance finished. Found %1 active node(s).").arg(m_aliveCount), "SYS");
}

void TuiApp::applyFilter()
{
    m_filteredHosts.clear();
    QString query = m_searchQuery.trimmed().toLower();

    for (const auto &h : m_hosts) {
        if (m_onlyAlive && !h.isAlive) continue;

        if (!query.isEmpty()) {
            bool match = h.ip.contains(query, Qt::CaseInsensitive) ||
                         h.hostname.contains(query, Qt::CaseInsensitive) ||
                         h.macAddress.contains(query, Qt::CaseInsensitive) ||
                         h.vendor.contains(query, Qt::CaseInsensitive) ||
                         h.comments.contains(query, Qt::CaseInsensitive);

            if (!match) {
                for (const QString &svc : h.services) {
                    if (svc.contains(query, Qt::CaseInsensitive)) { match = true; break; }
                }
                for (int port : h.openPorts) {
                    if (QString::number(port).contains(query)) { match = true; break; }
                }
            }
            if (!match) continue;
        }

        m_filteredHosts.append(h);
    }

    // Sort naturally by IP address
    std::sort(m_filteredHosts.begin(), m_filteredHosts.end(), [](const HostItem &a, const HostItem &b) {
        return a.ipv4Num < b.ipv4Num;
    });

    if (m_selectedHostIndex >= m_filteredHosts.size()) {
        m_selectedHostIndex = std::max(0, m_filteredHosts.size() - 1);
    }
}

void TuiApp::appendLog(const QString &msg, const QString &tag)
{
    QString timeStr = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString entry = QString("[%1] [%2] %3").arg(timeStr, tag, msg);
    m_logs.append(entry);
    if (m_logs.size() > 80) {
        m_logs.removeFirst();
    }
}

HostItem TuiApp::selectedHost() const
{
    if (m_selectedHostIndex >= 0 && m_selectedHostIndex < m_filteredHosts.size()) {
        return m_filteredHosts.at(m_selectedHostIndex);
    }
    return HostItem();
}

void TuiApp::onInputReady(int socket)
{
    Q_UNUSED(socket);
    std::vector<TuiKeyEvent> keys = m_screen.readKeys();
    for (const auto &ev : keys) {
        handleKey(ev);
    }
    render();
}

void TuiApp::onTick()
{
    m_animFrame++;
    if (m_isScanning && !m_isPaused) {
        m_scanElapsedMs = m_scanTimer.elapsed();
    }

    bool resized = m_screen.checkResize();
    static QString s_lastSec;
    QString curSec = QDateTime::currentDateTime().toString("hh:mm:ss");
    bool secChanged = (curSec != s_lastSec);

    if (resized || m_isScanning || m_portScanActive || secChanged) {
        s_lastSec = curSec;
        render();
    }
}

void TuiApp::handleKey(const TuiKeyEvent &ev)
{
    if (ev.key == TuiKey::CtrlC) {
        m_app->quit();
        return;
    }

    switch (m_modal) {
    case ModalMode::Search:
        handleSearchKey(ev);
        break;
    case ModalMode::RangeConfig:
        handleRangeKey(ev);
        break;
    case ModalMode::PortScan:
        handlePortScanKey(ev);
        break;
    case ModalMode::Wol:
        handleWolKey(ev);
        break;
    case ModalMode::Ping:
        handlePingKey(ev);
        break;
    case ModalMode::Export:
        handleExportKey(ev);
        break;
    case ModalMode::Theme:
        handleThemeKey(ev);
        break;
    case ModalMode::Help:
        handleHelpKey(ev);
        break;
    case ModalMode::None:
    default:
        handleNormalKey(ev);
        break;
    }
}

void TuiApp::handleNormalKey(const TuiKeyEvent &ev)
{
    switch (ev.key) {
    case TuiKey::Up:
        if (m_selectedHostIndex > 0) {
            m_selectedHostIndex--;
        }
        break;
    case TuiKey::Down:
        if (m_selectedHostIndex + 1 < m_filteredHosts.size()) {
            m_selectedHostIndex++;
        }
        break;
    case TuiKey::PageUp:
        m_selectedHostIndex = std::max(0, m_selectedHostIndex - 10);
        break;
    case TuiKey::PageDown:
        m_selectedHostIndex = std::min(std::max(0, m_filteredHosts.size() - 1), m_selectedHostIndex + 10);
        break;
    case TuiKey::Home:
        m_selectedHostIndex = 0;
        break;
    case TuiKey::End:
        m_selectedHostIndex = std::max(0, m_filteredHosts.size() - 1);
        break;
    case TuiKey::Char:
        if (ev.ch == 'q' || ev.ch == 'Q') {
            m_app->quit();
        } else if (ev.ch == ' ') {
            if (m_isScanning) stopScan();
            else startScan();
        } else if (ev.ch == 's' || ev.ch == 'S') {
            if (m_isScanning) stopScan();
            else startScan();
        } else if (ev.ch == 'p' || ev.ch == 'P') {
            pauseScan();
        } else if (ev.ch == 'c' || ev.ch == 'C') {
            clearScan();
        } else if (ev.ch == 'j') {
            if (m_selectedHostIndex + 1 < m_filteredHosts.size()) m_selectedHostIndex++;
        } else if (ev.ch == 'k') {
            if (m_selectedHostIndex > 0) m_selectedHostIndex--;
        } else if (ev.ch == '/' || ev.ch == 'f' || ev.ch == 'F') {
            m_modal = ModalMode::Search;
        } else if (ev.ch == 'a' || ev.ch == 'A') {
            m_onlyAlive = !m_onlyAlive;
            m_statusMessage = m_onlyAlive ? "> FILTER: DISPLAYING ONLINE NODES ONLY." : "> FILTER: DISPLAYING ALL SUBNET TARGETS.";
            applyFilter();
        } else if (ev.ch == 'r' || ev.ch == 'R' || ev.ch == 'i' || ev.ch == 'I') {
            m_modal = ModalMode::RangeConfig;
            m_editStartIp = m_startIp;
            m_editEndIp = m_endIp;
            m_editThreads = m_concurrency;
            m_editTimeoutMs = static_cast<int>(m_timeoutSec * 1000.0);
            m_editScanPorts = m_scanPorts;
            m_rangeField = 0;
        } else if (ev.ch == 'd' || ev.ch == 'D') {
            HostItem h = selectedHost();
            if (!h.ip.isEmpty()) {
                m_modal = ModalMode::PortScan;
                m_discoveredPorts.clear();
                m_portScanTotal = 0;
                m_portScanCompleted = 0;
                m_portScanActive = false;
            } else {
                m_statusMessage = "> SELECT A VALID HOST BEFORE PORT RECONNAISSANCE.";
            }
        } else if (ev.ch == 'w' || ev.ch == 'W') {
            HostItem h = selectedHost();
            if (!h.ip.isEmpty()) {
                m_modal = ModalMode::Wol;
                m_wolStatus.clear();
            } else {
                m_statusMessage = "> SELECT A VALID HOST BEFORE WAKE-ON-LAN.";
            }
        } else if (ev.ch == 'g' || ev.ch == 'G') {
            HostItem h = selectedHost();
            if (!h.ip.isEmpty()) {
                m_modal = ModalMode::Ping;
                m_pingTargetIp = h.ip;
                m_pingOutput.clear();
                m_pingOutput.append(QString("Initiating ICMP Echo to %1...").arg(h.ip));

                if (m_pingProcess) {
                    m_pingProcess->kill();
                    delete m_pingProcess;
                }
                m_pingProcess = new QProcess(this);
                connect(m_pingProcess, &QProcess::readyReadStandardOutput, this, &TuiApp::onPingReadyRead);
                connect(m_pingProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, &TuiApp::onPingFinished);
                m_pingProcess->start("ping", QStringList{"-c", "4", "-W", "1", h.ip});
            }
        } else if (ev.ch == 'e' || ev.ch == 'E') {
            m_modal = ModalMode::Export;
            m_exportFormat = 0;
            m_exportPath = "recon_dump.csv";
        } else if (ev.ch == 't') {
            // Quick cycle theme
            int nextId = (static_cast<int>(m_palette.id) + 1) % static_cast<int>(TuiPaletteId::Count);
            m_palette = TuiPalette::get(static_cast<TuiPaletteId>(nextId));
            m_statusMessage = QString("> PALETTE THEME: %1").arg(m_palette.name);
        } else if (ev.ch == 'T') {
            m_modal = ModalMode::Theme;
        } else if (ev.ch == '?' || ev.ch == 'h' || ev.ch == 'H') {
            m_modal = ModalMode::Help;
        } else if (ev.ch == 'y' || ev.ch == 'Y') {
            copySelectedToClipboard();
        }
        break;
    default:
        break;
    }
}

void TuiApp::handleSearchKey(const TuiKeyEvent &ev)
{
    if (ev.key == TuiKey::Escape || ev.key == TuiKey::Enter) {
        m_modal = ModalMode::None;
        m_screen.hideCursor();
    } else if (ev.key == TuiKey::Backspace) {
        if (!m_searchQuery.isEmpty()) {
            m_searchQuery.chop(1);
            applyFilter();
        }
    } else if (ev.key == TuiKey::CtrlU) {
        m_searchQuery.clear();
        applyFilter();
    } else if (ev.key == TuiKey::Char) {
        if (ev.ch >= 32 && ev.ch < 127) {
            m_searchQuery.append(QChar(static_cast<ushort>(ev.ch)));
            applyFilter();
        }
    }
}

void TuiApp::handleRangeKey(const TuiKeyEvent &ev)
{
    if (ev.key == TuiKey::Escape) {
        m_modal = ModalMode::None;
        m_screen.hideCursor();
        return;
    }

    if (ev.key == TuiKey::Up) {
        m_rangeField = (m_rangeField + 5) % 6;
    } else if (ev.key == TuiKey::Down) {
        m_rangeField = (m_rangeField + 1) % 6;
    } else if (ev.key == TuiKey::Left || ev.key == TuiKey::Right) {
        if (m_rangeField == 0 && !m_interfaces.isEmpty()) {
            int delta = (ev.key == TuiKey::Right) ? 1 : -1;
            int nextIdx = (m_selectedIfaceIndex + delta + m_interfaces.size()) % m_interfaces.size();
            applyInterface(nextIdx);
            m_editStartIp = m_startIp;
            m_editEndIp = m_endIp;
        } else if (m_rangeField == 3) {
            int delta = (ev.key == TuiKey::Right) ? 5 : -5;
            m_editThreads = std::clamp(m_editThreads + delta, 1, 128);
        } else if (m_rangeField == 4) {
            int delta = (ev.key == TuiKey::Right) ? 50 : -50;
            m_editTimeoutMs = std::clamp(m_editTimeoutMs + delta, 50, 3000);
        } else if (m_rangeField == 5) {
            m_editScanPorts = !m_editScanPorts;
        }
    } else if (ev.key == TuiKey::Enter) {
        m_startIp = m_editStartIp.trimmed();
        m_endIp = m_editEndIp.trimmed();
        m_concurrency = m_editThreads;
        m_timeoutSec = m_editTimeoutMs / 1000.0;
        m_scanPorts = m_editScanPorts;
        m_modal = ModalMode::None;
        m_screen.hideCursor();
        m_statusMessage = QString("> SUBNET BOUND TO %1 .. %2 [%3 THREADS, %4ms]")
                              .arg(m_startIp, m_endIp)
                              .arg(m_concurrency)
                              .arg(m_editTimeoutMs);
        appendLog(QString("Target parameters updated: %1 .. %2").arg(m_startIp, m_endIp), "CONFIG");
    } else if (ev.key == TuiKey::Backspace) {
        if (m_rangeField == 1 && !m_editStartIp.isEmpty()) m_editStartIp.chop(1);
        else if (m_rangeField == 2 && !m_editEndIp.isEmpty()) m_editEndIp.chop(1);
    } else if (ev.key == TuiKey::Char) {
        if (m_rangeField == 1 && (ev.ch == '.' || (ev.ch >= '0' && ev.ch <= '9'))) {
            m_editStartIp.append(QChar(static_cast<ushort>(ev.ch)));
        } else if (m_rangeField == 2 && (ev.ch == '.' || (ev.ch >= '0' && ev.ch <= '9'))) {
            m_editEndIp.append(QChar(static_cast<ushort>(ev.ch)));
        }
    }
}

void TuiApp::handlePortScanKey(const TuiKeyEvent &ev)
{
    if (ev.key == TuiKey::Escape) {
        if (m_portScanner->isScanning()) {
            m_portScanner->stopScan();
        }
        m_portScanActive = false;
        m_modal = ModalMode::None;
        return;
    }

    if (ev.key == TuiKey::Enter || (ev.key == TuiKey::Char && ev.ch == ' ')) {
        HostItem h = selectedHost();
        if (h.ip.isEmpty()) return;

        if (m_portScanner->isScanning()) {
            m_portScanner->stopScan();
            m_portScanActive = false;
            appendLog(QString("Port scan halted for %1").arg(h.ip), "PORTS");
        } else {
            m_discoveredPorts.clear();
            QList<int> ports;
            if (m_portPreset == 0) {
                // Top 20 common ports
                ports = {21, 22, 23, 25, 53, 80, 110, 135, 139, 143, 443, 445, 993, 995, 1433, 1521, 3306, 3389, 5432, 8080};
            } else if (m_portPreset == 1) {
                // Standard 1-1024
                for (int p = 1; p <= 1024; ++p) ports.append(p);
            } else if (m_portPreset == 2) {
                // Web & Admin
                ports = {80, 443, 8080, 8443, 8000, 8008, 8888, 9000, 9090, 10000};
            } else if (m_portPreset == 3) {
                // Database
                ports = {1433, 1521, 3306, 5432, 6379, 27017};
            }

            m_portScanTotal = ports.size();
            m_portScanCompleted = 0;
            m_portScanActive = true;
            appendLog(QString("Initiating deep port scan on %1 [%2 ports]").arg(h.ip).arg(ports.size()), "PORTS");
            m_portScanner->startScan(h.ip, ports, 30, 250);
        }
    } else if (ev.key == TuiKey::Char) {
        if (ev.ch >= '1' && ev.ch <= '4') {
            if (!m_portScanner->isScanning()) {
                m_portPreset = ev.ch - '1';
            }
        }
    }
}

void TuiApp::onPortFound(const QString &ip, const PortInfo &info)
{
    Q_UNUSED(ip);
    m_discoveredPorts.append(info);
}

void TuiApp::onPortProgress(int scanned, int total)
{
    m_portScanCompleted = scanned;
    m_portScanTotal = total;
}

void TuiApp::onPortScanFinished()
{
    m_portScanActive = false;
    HostItem h = selectedHost();
    appendLog(QString("Port scan completed for %1. Found %2 open port(s).").arg(h.ip).arg(m_discoveredPorts.size()), "PORTS");
}

void TuiApp::handleWolKey(const TuiKeyEvent &ev)
{
    if (ev.key == TuiKey::Escape) {
        m_modal = ModalMode::None;
        return;
    }

    if (ev.key == TuiKey::Enter) {
        HostItem h = selectedHost();
        if (h.macAddress.isEmpty()) {
            m_wolStatus = "[ERROR] Target node has no resolved MAC address.";
            return;
        }

        QString err;
        bool ok = WakeOnLan::sendMagicPacket(h.macAddress, m_bcastIp, 9, &err);
        if (ok) {
            m_wolStatus = QString("[SUCCESS] Magic packet transmitted to %1 via %2:9").arg(h.macAddress, m_bcastIp);
            appendLog(QString("WOL magic packet sent to %1 (%2)").arg(h.macAddress, h.ip), "WOL");
        } else {
            m_wolStatus = QString("[FAILURE] %1").arg(err);
            appendLog(QString("WOL transmission failed: %1").arg(err), "WOL");
        }
    }
}

void TuiApp::handlePingKey(const TuiKeyEvent &ev)
{
    if (ev.key == TuiKey::Escape) {
        if (m_pingProcess) {
            m_pingProcess->kill();
        }
        m_modal = ModalMode::None;
    }
}

void TuiApp::onPingReadyRead()
{
    if (!m_pingProcess) return;
    while (m_pingProcess->canReadLine()) {
        QString line = QString::fromUtf8(m_pingProcess->readLine()).trimmed();
        if (!line.isEmpty()) {
            m_pingOutput.append(line);
        }
    }
    if (m_pingOutput.size() > 14) {
        m_pingOutput.removeFirst();
    }
    render();
}

void TuiApp::onPingFinished(int exitCode)
{
    Q_UNUSED(exitCode);
    m_pingOutput.append("Ping probe complete. Press [Esc] to return.");
    render();
}

void TuiApp::handleExportKey(const TuiKeyEvent &ev)
{
    if (ev.key == TuiKey::Escape) {
        m_modal = ModalMode::None;
        m_screen.hideCursor();
        return;
    }

    if (ev.key == TuiKey::Left || ev.key == TuiKey::Right) {
        int delta = (ev.key == TuiKey::Right) ? 1 : -1;
        m_exportFormat = (m_exportFormat + delta + 3) % 3;
        if (m_exportFormat == 0) m_exportPath = "recon_dump.csv";
        else if (m_exportFormat == 1) m_exportPath = "recon_dump.json";
        else if (m_exportFormat == 2) m_exportPath = "recon_dump.txt";
    } else if (ev.key == TuiKey::Enter) {
        exportCurrentData(m_exportFormat, m_exportPath);
        m_modal = ModalMode::None;
        m_screen.hideCursor();
    } else if (ev.key == TuiKey::Backspace) {
        if (!m_exportPath.isEmpty()) m_exportPath.chop(1);
    } else if (ev.key == TuiKey::Char) {
        if (ev.ch >= 32 && ev.ch < 127) {
            m_exportPath.append(QChar(static_cast<ushort>(ev.ch)));
        }
    }
}

void TuiApp::exportCurrentData(int format, const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_statusMessage = QString("> FAILED TO WRITE EXPORT TO %1.").arg(filePath);
        return;
    }

    if (format == 0) { // CSV
        QTextStream out(&file);
        out << "Status,IP Address,Hostname,Ping (ms),MAC Address,Vendor,Open Ports,Comments\n";
        for (const auto &h : m_filteredHosts) {
            out << QString("\"%1\",\"%2\",\"%3\",\"%4\",\"%5\",\"%6\",\"%7\",\"%8\"\n")
                       .arg(h.isAlive ? "Online" : "Offline",
                            h.ip,
                            h.hostname,
                            h.isAlive ? QString::number(h.responseTimeMs, 'f', 1) : "",
                            h.macAddress,
                            QString(h.vendor).replace("\"", "\"\""),
                            h.openPortsSummary(),
                            QString(h.comments).replace("\"", "\"\""));
        }
    } else if (format == 1) { // JSON
        QJsonArray array;
        for (const auto &h : m_filteredHosts) {
            QJsonObject obj;
            obj["ip"] = h.ip;
            obj["hostname"] = h.hostname;
            obj["isAlive"] = h.isAlive;
            obj["responseTimeMs"] = h.responseTimeMs;
            obj["macAddress"] = h.macAddress;
            obj["vendor"] = h.vendor;
            QJsonArray portsArr;
            for (int p : h.openPorts) portsArr.append(p);
            obj["openPorts"] = portsArr;
            obj["services"] = QJsonArray::fromStringList(h.services);
            obj["comments"] = h.comments;
            array.append(obj);
        }
        QJsonDocument doc(array);
        file.write(doc.toJson(QJsonDocument::Indented));
    } else { // TXT
        QTextStream out(&file);
        out << QString("%1 | %2 | %3 | %4 | %5 | %6\n")
                   .arg("IP_ADDRESS", -16)
                   .arg("HOSTNAME", -22)
                   .arg("MAC_ADDRESS", -18)
                   .arg("RTT", -10)
                   .arg("VENDOR", -25)
                   .arg("SERVICES");
        out << QString().fill('-', 110) << "\n";
        for (const auto &h : m_filteredHosts) {
            out << QString("%1 | %2 | %3 | %4 | %5 | %6\n")
                       .arg(h.ip, -16)
                       .arg(h.hostname.isEmpty() ? "-" : h.hostname, -22)
                       .arg(h.macAddress.isEmpty() ? "-" : h.macAddress, -18)
                       .arg(h.isAlive ? QString("%1 ms").arg(h.responseTimeMs, 0, 'f', 1) : "-", -10)
                       .arg(h.vendor.isEmpty() ? "-" : h.vendor.left(24), -25)
                       .arg(h.openPortsSummary());
        }
    }

    m_statusMessage = QString("> EXPORTED %1 RECORDS TO %2.").arg(m_filteredHosts.size()).arg(filePath);
    appendLog(QString("Exported %1 records to %2").arg(m_filteredHosts.size()).arg(filePath), "DUMP");
}

void TuiApp::handleThemeKey(const TuiKeyEvent &ev)
{
    if (ev.key == TuiKey::Escape) {
        m_modal = ModalMode::None;
        return;
    }

    if (ev.key == TuiKey::Up) {
        int id = (static_cast<int>(m_palette.id) + static_cast<int>(TuiPaletteId::Count) - 1) % static_cast<int>(TuiPaletteId::Count);
        m_palette = TuiPalette::get(static_cast<TuiPaletteId>(id));
    } else if (ev.key == TuiKey::Down) {
        int id = (static_cast<int>(m_palette.id) + 1) % static_cast<int>(TuiPaletteId::Count);
        m_palette = TuiPalette::get(static_cast<TuiPaletteId>(id));
    } else if (ev.key == TuiKey::Enter) {
        m_modal = ModalMode::None;
        m_statusMessage = QString("> PALETTE THEME: %1").arg(m_palette.name);
    }
}

void TuiApp::handleHelpKey(const TuiKeyEvent &ev)
{
    if (ev.key == TuiKey::Escape || ev.key == TuiKey::Enter ||
        (ev.key == TuiKey::Char && (ev.ch == '?' || ev.ch == 'h' || ev.ch == 'q'))) {
        m_modal = ModalMode::None;
    }
}

void TuiApp::copySelectedToClipboard()
{
    HostItem h = selectedHost();
    if (h.ip.isEmpty()) return;

    QString text = QString("%1\t%2\t%3\t%4\t%5\t%6")
                       .arg(h.ip, h.hostname, h.macAddress,
                            h.vendor, QString::number(h.responseTimeMs, 'f', 1) + "ms",
                            h.openPortsSummary());

    QProcess proc;
    proc.start("wl-copy", QStringList{});
    if (!proc.waitForStarted(100)) {
        proc.start("xclip", QStringList{"-selection", "clipboard"});
    }
    if (proc.waitForStarted(100)) {
        proc.write(text.toUtf8());
        proc.closeWriteChannel();
        proc.waitForFinished(300);
    }

    m_statusMessage = QString("> COPIED INTEL FOR %1 TO CLIPBOARD.").arg(h.ip);
}

// ----------------------------------------------------------------------------
// RENDERERS
// ----------------------------------------------------------------------------

void TuiApp::render()
{
    int W = m_screen.cols();
    int H = m_screen.rows();

    m_screen.clear(m_palette.bg);

    if (W < 60 || H < 14) {
        m_screen.drawString(2, 2, "TERMINAL SIZE TOO SMALL FOR RECON CONSOLE", m_palette.error, m_palette.bg, true);
        m_screen.drawString(2, 4, QString("Current: %1x%2  Minimum required: 60x14").arg(W).arg(H), m_palette.textDim, m_palette.bg);
        m_screen.drawString(2, 6, "Please expand your terminal window.", m_palette.text, m_palette.bg);
        m_screen.flush();
        return;
    }

    renderMainView();

    // Render modal overlays if active
    switch (m_modal) {
    case ModalMode::Search:     renderSearchModal(); break;
    case ModalMode::RangeConfig: renderRangeModal(); break;
    case ModalMode::PortScan:   renderPortScanModal(); break;
    case ModalMode::Wol:        renderWolModal(); break;
    case ModalMode::Ping:       renderPingModal(); break;
    case ModalMode::Export:     renderExportModal(); break;
    case ModalMode::Theme:      renderThemeModal(); break;
    case ModalMode::Help:       renderHelpModal(); break;
    default: break;
    }

    m_screen.flush();
}

void TuiApp::renderMainView()
{
    int W = m_screen.cols();
    int H = m_screen.rows();

    renderHeader(0, 0, W);
    renderMetrics(0, 2, W);

    int midY = 6;
    int footerH = 3;
    int midH = H - midY - footerH;
    if (midH < 4) midH = 4;

    if (W >= 105) {
        // Horizontal split: Left = Host Table, Right = Inspector & Log
        int leftW = std::max(60, W * 58 / 100);
        int rightW = W - leftW;

        renderHostTable(0, midY, leftW, midH);

        int inspH = std::min(midH, 11);
        int logH = midH - inspH;
        renderInspector(leftW, midY, rightW, inspH);
        if (logH > 2) {
            renderLogPane(leftW, midY + inspH, rightW, logH);
        }
    } else {
        // Vertical split: Top = Table, Bottom = Inspector & Log
        int tableH = std::max(4, midH * 60 / 100);
        int bottomH = midH - tableH;

        renderHostTable(0, midY, W, tableH);
        if (bottomH > 2) {
            renderInspector(0, midY + tableH, W, bottomH);
        }
    }

    renderFooter(0, H - footerH, W);
}

void TuiApp::renderHeader(int x, int y, int w)
{
    // Row 0: Title banner & clock
    m_screen.fill(x, y, w, 1, ' ', m_palette.text, m_palette.headerBg);

    QString title = " [●] SYS.NET_RADAR // TERMINAL RECON CONSOLE v2.4 ";
    m_screen.drawString(x + 1, y, title, m_palette.borderFocus, m_palette.headerBg, true);

    QString statusBadge;
    TuiColor badgeColor;
    if (m_isPaused) {
        statusBadge = "[⏸ HALTED]";
        badgeColor = m_palette.warning;
    } else if (m_isScanning) {
        const char *spinner = "/-\\|";
        char spinChar = spinner[m_animFrame % 4];
        statusBadge = QString("[%1 SCANNING]").arg(spinChar);
        badgeColor = m_palette.online;
    } else {
        statusBadge = "[● IDLE]";
        badgeColor = m_palette.textDim;
    }

    QString clockStr = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString rightHeader = QString("%1  ⏱ %2 ").arg(statusBadge, clockStr);
    int rightX = x + w - rightHeader.length() - 1;
    if (rightX > x + title.length()) {
        m_screen.drawString(rightX, y, statusBadge, badgeColor, m_palette.headerBg, true);
        m_screen.drawString(rightX + statusBadge.length() + 2, y, QString("⏱ %1 ").arg(clockStr), m_palette.text, m_palette.headerBg);
    }

    // Row 1: Target subnet & tuning metrics
    m_screen.fill(x, y + 1, w, 1, ' ', m_palette.text, m_palette.panelBg);

    QString ifaceName = m_interfaces.isEmpty() ? "none" : m_interfaces.at(m_selectedIfaceIndex).name;
    QString ifaceIp = m_interfaces.isEmpty() ? "-" : m_interfaces.at(m_selectedIfaceIndex).ip;
    int prefix = m_interfaces.isEmpty() ? 24 : m_interfaces.at(m_selectedIfaceIndex).prefix;

    QString p1 = QString(" IFACE: %1 (%2/%3) │ TARGETS: %4 .. %5 │ THREADS: %6 │ TIMEOUT: %7ms │ THEME: %8")
                     .arg(ifaceName, ifaceIp)
                     .arg(prefix)
                     .arg(m_startIp, m_endIp)
                     .arg(m_concurrency)
                     .arg(static_cast<int>(m_timeoutSec * 1000.0))
                     .arg(m_palette.name.left(14));

    m_screen.drawString(x + 1, y + 1, p1, m_palette.textDim, m_palette.panelBg);
}

void TuiApp::renderMetrics(int x, int y, int w)
{
    // Divider
    m_screen.drawHLine(x, y, w, m_palette.border, m_palette.bg);

    // Row y+1: Progress bar
    int progressW = std::clamp(w - 38, 12, 50);
    double fraction = (m_totalHosts > 0) ? (static_cast<double>(m_completedHosts) / m_totalHosts) : 0.0;
    int pct = static_cast<int>(fraction * 100.0);

    m_screen.drawString(x + 1, y + 1, "PROGRESS: [", m_palette.textDim, m_palette.bg);
    m_screen.drawProgressBar(x + 12, y + 1, progressW, fraction, m_palette.progressFill, m_palette.progressEmpty, m_palette.bg);

    QString pctStr = QString("] %1% (%2/%3)  TARGET: %4")
                         .arg(pct, 3)
                         .arg(m_completedHosts)
                         .arg(m_totalHosts)
                         .arg(m_currentProbingIp);
    m_screen.drawString(x + 12 + progressW, y + 1, pctStr, m_palette.text, m_palette.bg);

    // Row y+2: Metrics badges & elapsed time
    QString metrics = QString(" METRICS:  ● %1 Online   ○ %2 Offline   Total: %3 Nodes   ⚡ %4 Open Services")
                          .arg(m_aliveCount)
                          .arg(m_offlineCount)
                          .arg(m_totalHosts)
                          .arg(m_servicesCount);

    m_screen.drawString(x, y + 2, metrics, m_palette.text, m_palette.bg);

    // Colorize specific words in metrics
    int onX = x + metrics.indexOf("●");
    if (onX >= 0) m_screen.drawString(onX, y + 2, "●", m_palette.online, m_palette.bg, true);

    int offX = x + metrics.indexOf("○");
    if (offX >= 0) m_screen.drawString(offX, y + 2, "○", m_palette.offline, m_palette.bg);

    int svcX = x + metrics.indexOf("⚡");
    if (svcX >= 0) m_screen.drawString(svcX, y + 2, "⚡", m_palette.warning, m_palette.bg, true);

    // Elapsed timer
    int sec = static_cast<int>(m_scanElapsedMs / 1000);
    int ms = static_cast<int>((m_scanElapsedMs % 1000) / 100);
    QString timeStr = QString("[⏱ %1:%2.%3] ")
                          .arg(sec / 60, 2, 10, QChar('0'))
                          .arg(sec % 60, 2, 10, QChar('0'))
                          .arg(ms);
    int timeX = x + w - timeStr.length() - 1;
    if (timeX > x + metrics.length()) {
        m_screen.drawString(timeX, y + 2, timeStr, m_palette.highlight, m_palette.bg, true);
    }

    // Divider below metrics
    m_screen.drawHLine(x, y + 3, w, m_palette.border, m_palette.bg);
}

void TuiApp::renderHostTable(int x, int y, int w, int h)
{
    QString title = QString(" DISCOVERED NODES (%1/%2) ").arg(m_filteredHosts.size()).arg(m_hosts.size());
    if (!m_searchQuery.isEmpty()) {
        title += QString("[FILTER: %1] ").arg(m_searchQuery);
    }
    m_screen.drawBox(x, y, w, h, title, m_palette.border, m_palette.borderFocus, m_palette.panelBg);

    // Table Header
    int headerY = y + 1;
    m_screen.fill(x + 1, headerY, w - 2, 1, ' ', m_palette.text, m_palette.headerBg);

    int colStat = x + 2;
    int colIp = x + 9;
    int colRtt = x + 26;
    int colHost = x + 36;
    int colMac = x + 56;
    int colVendor = x + 76;

    m_screen.drawString(colStat, headerY, "STAT", m_palette.textDim, m_palette.headerBg, true);
    m_screen.drawString(colIp, headerY, "IP ADDRESS", m_palette.textDim, m_palette.headerBg, true);
    m_screen.drawString(colRtt, headerY, "RTT", m_palette.textDim, m_palette.headerBg, true);
    if (colHost < x + w - 2) m_screen.drawString(colHost, headerY, "HOSTNAME", m_palette.textDim, m_palette.headerBg, true);
    if (colMac < x + w - 2) m_screen.drawString(colMac, headerY, "MAC ADDRESS", m_palette.textDim, m_palette.headerBg, true);
    if (colVendor < x + w - 2) m_screen.drawString(colVendor, headerY, "VENDOR / SERVICES", m_palette.textDim, m_palette.headerBg, true);

    int visibleRows = h - 3;
    if (visibleRows <= 0) return;

    // Adjust scroll
    if (m_selectedHostIndex < m_tableScrollOffset) {
        m_tableScrollOffset = m_selectedHostIndex;
    } else if (m_selectedHostIndex >= m_tableScrollOffset + visibleRows) {
        m_tableScrollOffset = m_selectedHostIndex - visibleRows + 1;
    }

    int rowY = y + 2;
    for (int i = 0; i < visibleRows; ++i) {
        int hostIdx = m_tableScrollOffset + i;
        int curRowY = rowY + i;

        if (hostIdx >= m_filteredHosts.size()) {
            break;
        }

        const HostItem &host = m_filteredHosts.at(hostIdx);
        bool isSelected = (hostIdx == m_selectedHostIndex);

        TuiColor rowBg = isSelected ? m_palette.selectionBg : m_palette.panelBg;
        TuiColor rowFg = isSelected ? m_palette.selectionFg : m_palette.text;

        m_screen.fill(x + 1, curRowY, w - 2, 1, ' ', rowFg, rowBg);

        // Selection arrow
        if (isSelected) {
            m_screen.drawString(x + 1, curRowY, "▶", m_palette.borderFocus, rowBg, true);
        }

        // Status
        if (host.isAlive) {
            m_screen.drawString(colStat, curRowY, "● ON", m_palette.online, rowBg, true);
        } else {
            m_screen.drawString(colStat, curRowY, "○ OFF", m_palette.offline, rowBg);
        }

        // IP Address
        m_screen.drawString(colIp, curRowY, host.ip, host.isAlive ? m_palette.borderFocus : rowFg, rowBg, host.isAlive);

        // RTT
        if (host.isAlive) {
            QString rttStr = QString("%1ms").arg(host.responseTimeMs, 0, 'f', 1);
            TuiColor rttColor = (host.responseTimeMs < 15.0) ? m_palette.online :
                                ((host.responseTimeMs < 60.0) ? m_palette.warning : m_palette.error);
            m_screen.drawString(colRtt, curRowY, rttStr, rttColor, rowBg);
        } else {
            m_screen.drawString(colRtt, curRowY, "--", m_palette.textDim, rowBg);
        }

        // Hostname
        if (colHost < x + w - 2) {
            QString hostStr = host.hostname.isEmpty() ? "-" : host.hostname;
            int maxHostLen = (colMac < x + w - 2) ? (colMac - colHost - 1) : (x + w - 2 - colHost);
            m_screen.drawString(colHost, curRowY, hostStr, m_palette.warning, rowBg, false, maxHostLen);
        }

        // MAC
        if (colMac < x + w - 2) {
            QString macStr = host.macAddress.isEmpty() ? "--:--:--:--:--:--" : host.macAddress;
            int maxMacLen = (colVendor < x + w - 2) ? (colVendor - colMac - 1) : (x + w - 2 - colMac);
            m_screen.drawString(colMac, curRowY, macStr, m_palette.highlight, rowBg, false, maxMacLen);
        }

        // Vendor / Services
        if (colVendor < x + w - 2) {
            QString vendorStr;
            if (!host.openPorts.isEmpty()) {
                vendorStr = QString("[%1] %2").arg(host.openPortsSummary(), host.vendor);
            } else {
                vendorStr = host.vendor.isEmpty() ? "-" : host.vendor;
            }
            int maxVendLen = x + w - 2 - colVendor;
            m_screen.drawString(colVendor, curRowY, vendorStr, rowFg, rowBg, false, maxVendLen);
        }
    }
}

void TuiApp::renderInspector(int x, int y, int w, int h)
{
    m_screen.drawBox(x, y, w, h, " TARGET INSPECTOR ", m_palette.border, m_palette.borderFocus, m_palette.panelBg);

    HostItem hItem = selectedHost();
    if (hItem.ip.isEmpty()) {
        m_screen.drawString(x + 2, y + 2, "[ NO NODE SELECTED ]", m_palette.textDim, m_palette.panelBg, true);
        m_screen.drawString(x + 2, y + 4, "> Select an active node from the matrix", m_palette.textDim, m_palette.panelBg);
        m_screen.drawString(x + 2, y + 5, "  to inspect hardware intel and launch tools.", m_palette.textDim, m_palette.panelBg);
        return;
    }

    int row = y + 1;
    m_screen.drawString(x + 2, row++, QString("TARGET IP:   %1").arg(hItem.ip), m_palette.borderFocus, m_palette.panelBg, true);
    m_screen.drawString(x + 2, row++, QString("STATUS:      %1").arg(hItem.statusText()),
                        hItem.isAlive ? m_palette.online : m_palette.offline, m_palette.panelBg, true);
    m_screen.drawString(x + 2, row++, QString("HOSTNAME:    %1").arg(hItem.hostname.isEmpty() ? "<UNRESOLVED>" : hItem.hostname),
                        m_palette.warning, m_palette.panelBg);
    m_screen.drawString(x + 2, row++, QString("MAC ADDR:    %1").arg(hItem.macAddress.isEmpty() ? "--:--:--:--:--:--" : hItem.macAddress),
                        m_palette.highlight, m_palette.panelBg);
    m_screen.drawString(x + 2, row++, QString("OUI VENDOR:  %1").arg(hItem.vendor.isEmpty() ? "<UNKNOWN>" : hItem.vendor),
                        m_palette.text, m_palette.panelBg, false, w - 4);
    m_screen.drawString(x + 2, row++, QString("OPEN PORTS:  %1").arg(hItem.openPorts.isEmpty() ? "None detected" : hItem.openPortsSummary()),
                        m_palette.online, m_palette.panelBg, false, w - 4);

    if (row < y + h - 2) {
        m_screen.drawHLine(x + 1, row++, w - 2, m_palette.border, m_palette.panelBg);
        m_screen.drawString(x + 2, row, "[D] Port Recon  [W] WOL  [G] Ping  [Y] Copy", m_palette.accent, m_palette.panelBg, true);
    }
}

void TuiApp::renderLogPane(int x, int y, int w, int h)
{
    m_screen.drawBox(x, y, w, h, " RECON EVENT FEED ", m_palette.border, m_palette.borderFocus, m_palette.panelBg);

    int visibleLogs = h - 2;
    if (visibleLogs <= 0) return;

    int totalLogs = m_logs.size();
    int startIdx = std::max(0, totalLogs - visibleLogs);

    for (int i = 0; i < visibleLogs && (startIdx + i) < totalLogs; ++i) {
        const QString &line = m_logs.at(startIdx + i);
        TuiColor logColor = m_palette.text;
        if (line.contains("[+]")) logColor = m_palette.online;
        else if (line.contains("[PORTS]")) logColor = m_palette.accent;
        else if (line.contains("[SCAN]")) logColor = m_palette.warning;
        else if (line.contains("[WOL]")) logColor = m_palette.highlight;
        else if (line.contains("[DUMP]")) logColor = m_palette.borderFocus;

        m_screen.drawString(x + 2, y + 1 + i, line, logColor, m_palette.panelBg, false, w - 4);
    }
}

void TuiApp::renderFooter(int x, int y, int w)
{
    // Status line
    m_screen.drawHLine(x, y, w, m_palette.border, m_palette.bg);
    m_screen.drawString(x + 1, y, QString(" STATUS: %1 ").arg(m_statusMessage), m_palette.textBold, m_palette.bg, true, w - 2);

    // Keybindings bar
    m_screen.fill(x, y + 1, w, 2, ' ', m_palette.text, m_palette.headerBg);

    QString keys = " [SPACE] Scan  [P] Pause  [C] Clear  [R] Range  [/] Search  [A] Alive  "
                   "[D] Ports  [W] WOL  [G] Ping  [E] Export  [T] Theme  [?] Help  [Q] Quit";
    m_screen.drawString(x + 1, y + 1, keys, m_palette.accent, m_palette.headerBg, true, w - 2);
}

// ----------------------------------------------------------------------------
// MODAL OVERLAYS
// ----------------------------------------------------------------------------

void TuiApp::renderSearchModal()
{
    int W = m_screen.cols();
    int boxW = std::clamp(W - 20, 40, 70);
    int boxH = 5;
    int boxX = (W - boxW) / 2;
    int boxY = 8;

    m_screen.drawBox(boxX, boxY, boxW, boxH, " SEARCH / FILTER NODES ", m_palette.borderFocus, m_palette.accent, m_palette.panelBg);
    m_screen.drawString(boxX + 2, boxY + 1, "Filter by IP, hostname, vendor, port, or service:", m_palette.textDim, m_palette.panelBg);

    QString prompt = QString("> %1_").arg(m_searchQuery);
    m_screen.drawString(boxX + 2, boxY + 2, prompt, m_palette.borderFocus, m_palette.panelBg, true);
    m_screen.drawString(boxX + 2, boxY + 3, "[Enter/Esc] Done  [Ctrl+U] Clear", m_palette.textDim, m_palette.panelBg);

    m_screen.showCursor(boxX + 4 + m_searchQuery.length(), boxY + 2);
}

void TuiApp::renderRangeModal()
{
    int W = m_screen.cols();
    int boxW = std::clamp(W - 16, 50, 75);
    int boxH = 14;
    int boxX = (W - boxW) / 2;
    int boxY = 5;

    m_screen.drawBox(boxX, boxY, boxW, boxH, " NETWORK INTERFACE & SCAN RANGE CONFIG ",
                     m_palette.borderFocus, m_palette.accent, m_palette.panelBg);

    int row = boxY + 1;
    auto drawField = [&](int fieldIdx, const QString &label, const QString &val, const QString &hint) {
        bool selected = (m_rangeField == fieldIdx);
        TuiColor bg = selected ? m_palette.selectionBg : m_palette.panelBg;
        TuiColor fg = selected ? m_palette.selectionFg : m_palette.text;

        m_screen.fill(boxX + 1, row, boxW - 2, 1, ' ', fg, bg);
        m_screen.drawString(boxX + 2, row, QString("%1 %2:").arg(selected ? "▶" : " ", -14).arg(label), fg, bg, selected);
        m_screen.drawString(boxX + 20, row, val, selected ? m_palette.borderFocus : m_palette.textBold, bg, true);
        if (!hint.isEmpty()) {
            m_screen.drawString(boxX + 42, row, hint, m_palette.textDim, bg);
        }
        row++;
    };

    QString ifaceStr = m_interfaces.isEmpty() ? "None" :
                       QString("%1 (%2)").arg(m_interfaces.at(m_selectedIfaceIndex).name, m_interfaces.at(m_selectedIfaceIndex).ip);
    drawField(0, "INTERFACE", ifaceStr, "[Left/Right] switch NIC");
    drawField(1, "START IP", m_editStartIp, "Type to edit");
    drawField(2, "END IP", m_editEndIp, "Type to edit");
    drawField(3, "THREADS", QString("%1 WORKERS").arg(m_editThreads), "[Left/Right] +/- 5");
    drawField(4, "TIMEOUT", QString("%1 MS").arg(m_editTimeoutMs), "[Left/Right] +/- 50ms");
    drawField(5, "SERVICES", m_editScanPorts ? "[ENABLED]" : "[DISABLED]", "[Left/Right] toggle");

    row++;
    m_screen.drawHLine(boxX + 1, row++, boxW - 2, m_palette.border, m_palette.panelBg);
    m_screen.drawString(boxX + 2, row, "[Up/Down] Select Field   [Enter] Save & Apply   [Esc] Cancel",
                        m_palette.accent, m_palette.panelBg, true);
}

void TuiApp::renderPortScanModal()
{
    int W = m_screen.cols();
    HostItem h = selectedHost();

    int boxW = std::clamp(W - 14, 55, 78);
    int boxH = 16;
    int boxX = (W - boxW) / 2;
    int boxY = 4;

    QString title = QString(" TARGET PORT RECON: %1 ").arg(h.ip);
    m_screen.drawBox(boxX, boxY, boxW, boxH, title, m_palette.borderFocus, m_palette.accent, m_palette.panelBg);

    QString presets = QString("Presets: %1[1] Top 20  %2[2] 1-1024  %3[3] Web/Admin  %4[4] Database")
                          .arg(m_portPreset == 0 ? "▶" : " ")
                          .arg(m_portPreset == 1 ? "▶" : " ")
                          .arg(m_portPreset == 2 ? "▶" : " ")
                          .arg(m_portPreset == 3 ? "▶" : " ");
    m_screen.drawString(boxX + 2, boxY + 1, presets, m_palette.text, m_palette.panelBg);

    // Progress
    double pct = (m_portScanTotal > 0) ? (static_cast<double>(m_portScanCompleted) / m_portScanTotal) : 0.0;
    QString stat = m_portScanActive ? QString("PROBING... %1% (%2/%3)").arg(static_cast<int>(pct * 100)).arg(m_portScanCompleted).arg(m_portScanTotal)
                                    : (m_discoveredPorts.isEmpty() ? "READY. Press [Enter] to scan." : QString("FINISHED. Found %1 open port(s).").arg(m_discoveredPorts.size()));

    m_screen.drawString(boxX + 2, boxY + 2, stat, m_portScanActive ? m_palette.online : m_palette.textDim, m_palette.panelBg, true);
    m_screen.drawProgressBar(boxX + 2, boxY + 3, boxW - 4, pct, m_palette.progressFill, m_palette.progressEmpty, m_palette.panelBg);

    m_screen.drawHLine(boxX + 1, boxY + 4, boxW - 2, m_palette.border, m_palette.panelBg);

    // Discovered ports list
    m_screen.drawString(boxX + 2, boxY + 5, "PORT     STATE     SERVICE          LATENCY", m_palette.textDim, m_palette.panelBg, true);

    int portRow = boxY + 6;
    int maxPortsToShow = boxH - 9;
    for (int i = 0; i < maxPortsToShow && i < m_discoveredPorts.size(); ++i) {
        const PortInfo &p = m_discoveredPorts.at(i);
        QString line = QString(" %1      OPEN      %2        %3 ms")
                           .arg(p.port, -6)
                           .arg(p.service.isEmpty() ? "unknown" : p.service, -14)
                           .arg(p.responseTimeMs, 0, 'f', 1);
        m_screen.drawString(boxX + 2, portRow + i, line, m_palette.online, m_palette.panelBg, true);
    }

    if (m_discoveredPorts.isEmpty() && !m_portScanActive) {
        m_screen.drawString(boxX + 2, portRow + 1, "No open ports discovered yet.", m_palette.textDim, m_palette.panelBg);
    }

    m_screen.drawHLine(boxX + 1, boxY + boxH - 2, boxW - 2, m_palette.border, m_palette.panelBg);
    m_screen.drawString(boxX + 2, boxY + boxH - 2, "[Enter/Space] Start/Stop   [1-4] Presets   [Esc] Close",
                        m_palette.accent, m_palette.panelBg, true);
}

void TuiApp::renderWolModal()
{
    int W = m_screen.cols();
    HostItem h = selectedHost();

    int boxW = std::clamp(W - 20, 48, 68);
    int boxH = 10;
    int boxX = (W - boxW) / 2;
    int boxY = 6;

    m_screen.drawBox(boxX, boxY, boxW, boxH, " WAKE-ON-LAN INJECTOR ", m_palette.borderFocus, m_palette.accent, m_palette.panelBg);

    m_screen.drawString(boxX + 2, boxY + 1, QString("Target Node:    %1 (%2)").arg(h.ip, h.hostname.isEmpty() ? "Unresolved" : h.hostname),
                        m_palette.text, m_palette.panelBg);
    m_screen.drawString(boxX + 2, boxY + 2, QString("Hardware MAC:   %1").arg(h.macAddress.isEmpty() ? "<MISSING MAC>" : h.macAddress),
                        m_palette.highlight, m_palette.panelBg, true);
    m_screen.drawString(boxX + 2, boxY + 3, QString("Manufacturer:   %1").arg(h.vendor.isEmpty() ? "Unknown" : h.vendor),
                        m_palette.textDim, m_palette.panelBg);
    m_screen.drawString(boxX + 2, boxY + 4, QString("Broadcast IP:   %1:9 (UDP Magic Packet)").arg(m_bcastIp),
                        m_palette.text, m_palette.panelBg);

    if (!m_wolStatus.isEmpty()) {
        TuiColor sc = m_wolStatus.contains("SUCCESS") ? m_palette.online : m_palette.error;
        m_screen.drawString(boxX + 2, boxY + 6, m_wolStatus, sc, m_palette.panelBg, true, boxW - 4);
    }

    m_screen.drawHLine(boxX + 1, boxY + boxH - 2, boxW - 2, m_palette.border, m_palette.panelBg);
    m_screen.drawString(boxX + 2, boxY + boxH - 2, "[Enter] Transmit Magic Packet   [Esc] Close",
                        m_palette.accent, m_palette.panelBg, true);
}

void TuiApp::renderPingModal()
{
    int W = m_screen.cols();
    int boxW = std::clamp(W - 14, 55, 78);
    int boxH = 14;
    int boxX = (W - boxW) / 2;
    int boxY = 5;

    QString title = QString(" ICMP ECHO PROBE // %1 ").arg(m_pingTargetIp);
    m_screen.drawBox(boxX, boxY, boxW, boxH, title, m_palette.borderFocus, m_palette.accent, m_palette.panelBg);

    int row = boxY + 1;
    for (int i = 0; i < m_pingOutput.size() && row < boxY + boxH - 2; ++i) {
        const QString &line = m_pingOutput.at(i);
        TuiColor col = line.contains("bytes from") ? m_palette.online : m_palette.text;
        m_screen.drawString(boxX + 2, row++, line, col, m_palette.panelBg, false, boxW - 4);
    }

    m_screen.drawHLine(boxX + 1, boxY + boxH - 2, boxW - 2, m_palette.border, m_palette.panelBg);
    m_screen.drawString(boxX + 2, boxY + boxH - 2, "[Esc] Return to matrix", m_palette.accent, m_palette.panelBg, true);
}

void TuiApp::renderExportModal()
{
    int W = m_screen.cols();
    int boxW = std::clamp(W - 20, 48, 68);
    int boxH = 9;
    int boxX = (W - boxW) / 2;
    int boxY = 7;

    m_screen.drawBox(boxX, boxY, boxW, boxH, " DUMP MATRIX TO FILE ", m_palette.borderFocus, m_palette.accent, m_palette.panelBg);

    QString fmtStr = QString("Format: %1[CSV]  %2[JSON]  %3[TXT]")
                         .arg(m_exportFormat == 0 ? "▶ " : "  ")
                         .arg(m_exportFormat == 1 ? "▶ " : "  ")
                         .arg(m_exportFormat == 2 ? "▶ " : "  ");
    m_screen.drawString(boxX + 2, boxY + 1, fmtStr, m_palette.text, m_palette.panelBg);
    m_screen.drawString(boxX + 2, boxY + 3, "Destination path (type to edit):", m_palette.textDim, m_palette.panelBg);
    m_screen.drawString(boxX + 2, boxY + 4, QString("> %1_").arg(m_exportPath), m_palette.borderFocus, m_palette.panelBg, true);

    m_screen.drawHLine(boxX + 1, boxY + boxH - 2, boxW - 2, m_palette.border, m_palette.panelBg);
    m_screen.drawString(boxX + 2, boxY + boxH - 2, "[Left/Right] Format   [Enter] Save   [Esc] Cancel",
                        m_palette.accent, m_palette.panelBg, true);

    m_screen.showCursor(boxX + 4 + m_exportPath.length(), boxY + 4);
}

void TuiApp::renderThemeModal()
{
    int W = m_screen.cols();
    int boxW = 42;
    int boxH = 12;
    int boxX = (W - boxW) / 2;
    int boxY = 6;

    m_screen.drawBox(boxX, boxY, boxW, boxH, " PALETTE THEME SELECTOR ", m_palette.borderFocus, m_palette.accent, m_palette.panelBg);

    for (int i = 0; i < static_cast<int>(TuiPaletteId::Count); ++i) {
        TuiPalette pal = TuiPalette::get(static_cast<TuiPaletteId>(i));
        bool selected = (m_palette.id == pal.id);

        TuiColor bg = selected ? m_palette.selectionBg : m_palette.panelBg;
        TuiColor fg = selected ? m_palette.selectionFg : m_palette.text;

        int rowY = boxY + 1 + i;
        m_screen.fill(boxX + 1, rowY, boxW - 2, 1, ' ', fg, bg);
        m_screen.drawString(boxX + 2, rowY, QString("%1 %2").arg(selected ? "▶" : " ", pal.name), fg, bg, selected);
    }

    m_screen.drawHLine(boxX + 1, boxY + boxH - 2, boxW - 2, m_palette.border, m_palette.panelBg);
    m_screen.drawString(boxX + 2, boxY + boxH - 2, "[Up/Down] Select   [Enter] Apply", m_palette.accent, m_palette.panelBg, true);
}

void TuiApp::renderHelpModal()
{
    int W = m_screen.cols();
    int boxW = std::clamp(W - 14, 55, 78);
    int boxH = 18;
    int boxX = (W - boxW) / 2;
    int boxY = 3;

    m_screen.drawBox(boxX, boxY, boxW, boxH, " SYS.NET_RADAR // RECON CONSOLE HELP ",
                     m_palette.borderFocus, m_palette.accent, m_palette.panelBg);

    int row = boxY + 1;
    auto addHelp = [&](const QString &key, const QString &desc) {
        m_screen.drawString(boxX + 3, row, key, m_palette.borderFocus, m_palette.panelBg, true);
        m_screen.drawString(boxX + 18, row, desc, m_palette.text, m_palette.panelBg);
        row++;
    };

    addHelp("SPACE / S", "Start / Stop subnet reconnaissance");
    addHelp("P",         "Pause / Resume active scan");
    addHelp("C",         "Clear / Flush discovered targets matrix");
    addHelp("R / I",     "Configure network interface, IP range, threads, timeout");
    addHelp("/ or F",    "Search / filter nodes by IP, hostname, vendor, port");
    addHelp("A",         "Toggle alive-only / all nodes filter");
    addHelp("D",         "Deep Port Reconnaissance on selected node");
    addHelp("W",         "Wake-on-LAN magic packet transmission");
    addHelp("G",         "Interactive ICMP Ping probe on selected node");
    addHelp("Y",         "Copy selected node intel to system clipboard");
    addHelp("E",         "Export targets matrix to CSV, JSON, or TXT");
    addHelp("T / Shift+T","Cycle / Select retro cyber color theme");
    addHelp("Up / Down", "Navigate host list (also vim j/k, PgUp/PgDn, Home/End)");
    addHelp("Q / Ctrl+C", "Quit application cleanly");

    row++;
    m_screen.drawHLine(boxX + 1, boxY + boxH - 2, boxW - 2, m_palette.border, m_palette.panelBg);
    m_screen.drawString(boxX + 2, boxY + boxH - 2, "Press [Esc] or [Enter] to return to recon matrix",
                        m_palette.accent, m_palette.panelBg, true);
}
