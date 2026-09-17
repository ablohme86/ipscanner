#include "HostTableModel.h"

HostTableModel::HostTableModel(QObject *parent)
    : QAbstractTableModel(parent)
    , m_aliveCount(0)
    , m_paletteMode(PaletteBtopTokyo)
{
}

int HostTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_hosts.size();
}

int HostTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return ColumnCount;
}

QVariant HostTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_hosts.size()) {
        return QVariant();
    }

    const HostItem &host = m_hosts.at(index.row());
    int col = index.column();

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (col) {
        case ColStatus:
            return host.isAlive ? "● [UP]" : "○ [DOWN]";
        case ColIp:
            return host.ip;
        case ColHostname:
            return host.hostname.isEmpty() ? "<UNRESOLVED>" : host.hostname;
        case ColPing:
            if (!host.isAlive) return "[ ---- ]";
            if (host.responseTimeMs >= 0) {
                return QString("[ %1 ms ]").arg(host.responseTimeMs, 0, 'f', 1);
            }
            return "[ <1ms ]";
        case ColMac:
            return host.macAddress.isEmpty() ? "--:--:--:--:--:--" : host.macAddress;
        case ColVendor:
            return host.vendor.isEmpty() ? "<UNKNOWN>" : host.vendor;
        case ColPorts: {
            QString summary = host.openPortsSummary();
            return summary.isEmpty() ? "[ NONE ]" : summary;
        }
        case ColComments:
            return host.comments;
        default:
            return QVariant();
        }
    }

    if (role == RawSortRole) {
        switch (col) {
        case ColStatus:
            return host.isAlive ? 1 : 0;
        case ColIp:
            return static_cast<qulonglong>(host.ipv4Num);
        case ColHostname:
            return host.hostname.toLower();
        case ColPing:
            return host.isAlive ? (host.responseTimeMs >= 0 ? host.responseTimeMs : 0.1) : 999999.0;
        case ColMac:
            return host.macAddress;
        case ColVendor:
            return host.vendor.toLower();
        case ColPorts:
            return host.openPorts.size();
        case ColComments:
            return host.comments.toLower();
        default:
            return QVariant();
        }
    }

    if (role == IsAliveRole) {
        return host.isAlive;
    }
    if (role == IpNumRole) {
        return static_cast<qulonglong>(host.ipv4Num);
    }
    if (role == PingMsRole) {
        return host.responseTimeMs;
    }

    if (role == Qt::TextAlignmentRole) {
        if (col == ColStatus) {
            return static_cast<int>(Qt::AlignCenter);
        }
        if (col == ColPing) {
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        }
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    }

    if (role == Qt::ForegroundRole) {
        bool alive = host.isAlive;
        switch (m_paletteMode) {
        case PaletteBtopTokyo: {
            switch (col) {
            case ColStatus:   return alive ? QColor("#9ece6a") : QColor("#f7768e");
            case ColIp:       return QColor("#7dcfff");
            case ColHostname: return host.hostname.isEmpty() ? QColor("#565f89") : QColor("#e0af68");
            case ColPing:
                if (!alive) return QColor("#565f89");
                return (host.responseTimeMs < 5.0) ? QColor("#9ece6a") :
                       (host.responseTimeMs < 50.0) ? QColor("#e0af68") : QColor("#f7768e");
            case ColMac:      return host.macAddress.isEmpty() ? QColor("#565f89") : QColor("#bb9af7");
            case ColVendor:   return host.vendor.isEmpty() ? QColor("#565f89") : QColor("#73daca");
            case ColPorts:    return host.openPorts.isEmpty() ? QColor("#565f89") : QColor("#ff9e64");
            case ColComments: return QColor("#c0caf5");
            }
            break;
        }
        case PaletteBtopDracula: {
            switch (col) {
            case ColStatus:   return alive ? QColor("#50fa7b") : QColor("#ff5555");
            case ColIp:       return QColor("#8be9fd");
            case ColHostname: return host.hostname.isEmpty() ? QColor("#6272a4") : QColor("#f1fa8c");
            case ColPing:
                if (!alive) return QColor("#6272a4");
                return (host.responseTimeMs < 5.0) ? QColor("#50fa7b") :
                       (host.responseTimeMs < 50.0) ? QColor("#ffb86c") : QColor("#ff5555");
            case ColMac:      return host.macAddress.isEmpty() ? QColor("#6272a4") : QColor("#bd93f9");
            case ColVendor:   return host.vendor.isEmpty() ? QColor("#6272a4") : QColor("#50fa7b");
            case ColPorts:    return host.openPorts.isEmpty() ? QColor("#6272a4") : QColor("#ffb86c");
            case ColComments: return QColor("#f8f8f2");
            }
            break;
        }
        case PaletteBtopGruvbox: {
            switch (col) {
            case ColStatus:   return alive ? QColor("#b8bb26") : QColor("#fb4934");
            case ColIp:       return QColor("#8ec07c");
            case ColHostname: return host.hostname.isEmpty() ? QColor("#928374") : QColor("#fabd2f");
            case ColPing:
                if (!alive) return QColor("#928374");
                return (host.responseTimeMs < 5.0) ? QColor("#b8bb26") :
                       (host.responseTimeMs < 50.0) ? QColor("#fabd2f") : QColor("#fb4934");
            case ColMac:      return host.macAddress.isEmpty() ? QColor("#928374") : QColor("#d3869b");
            case ColVendor:   return host.vendor.isEmpty() ? QColor("#928374") : QColor("#8ec07c");
            case ColPorts:    return host.openPorts.isEmpty() ? QColor("#928374") : QColor("#fe8019");
            case ColComments: return QColor("#ebdbb2");
            }
            break;
        }
        case PaletteRetroCyan: {
            switch (col) {
            case ColStatus:   return alive ? QColor("#00f0ff") : QColor("#ff007f");
            case ColIp:       return QColor("#00f0ff");
            case ColHostname: return host.hostname.isEmpty() ? QColor("#507080") : QColor("#ffffff");
            case ColPing:
                if (!alive) return QColor("#507080");
                return (host.responseTimeMs < 5.0) ? QColor("#00f0ff") :
                       (host.responseTimeMs < 50.0) ? QColor("#ffb86c") : QColor("#ff007f");
            case ColMac:      return host.macAddress.isEmpty() ? QColor("#507080") : QColor("#d600ff");
            case ColVendor:   return host.vendor.isEmpty() ? QColor("#507080") : QColor("#00e5ff");
            case ColPorts:    return host.openPorts.isEmpty() ? QColor("#507080") : QColor("#ffaa00");
            case ColComments: return QColor("#e0e0e0");
            }
            break;
        }
        case PaletteRetroGreen: {
            switch (col) {
            case ColStatus:   return alive ? QColor("#00ff66") : QColor("#006622");
            case ColIp:       return QColor("#33ff88");
            case ColHostname: return host.hostname.isEmpty() ? QColor("#00882b") : QColor("#00ff66");
            case ColPing:     return alive ? QColor("#00ff66") : QColor("#004414");
            case ColMac:      return host.macAddress.isEmpty() ? QColor("#00882b") : QColor("#00ee55");
            case ColVendor:   return host.vendor.isEmpty() ? QColor("#00882b") : QColor("#33ff77");
            case ColPorts:    return host.openPorts.isEmpty() ? QColor("#00882b") : QColor("#66ff99");
            case ColComments: return QColor("#00ff66");
            }
            break;
        }
        case PaletteRetroAmber: {
            switch (col) {
            case ColStatus:   return alive ? QColor("#ffaa00") : QColor("#884400");
            case ColIp:       return QColor("#ffcc33");
            case ColHostname: return host.hostname.isEmpty() ? QColor("#885500") : QColor("#ffaa00");
            case ColPing:     return alive ? QColor("#ffaa00") : QColor("#553300");
            case ColMac:      return host.macAddress.isEmpty() ? QColor("#885500") : QColor("#ee9900");
            case ColVendor:   return host.vendor.isEmpty() ? QColor("#885500") : QColor("#ffb020");
            case ColPorts:    return host.openPorts.isEmpty() ? QColor("#885500") : QColor("#ffbb33");
            case ColComments: return QColor("#ffaa00");
            }
            break;
        }
        case PaletteDark: {
            switch (col) {
            case ColStatus:   return alive ? QColor("#10b981") : QColor("#ef4444");
            case ColIp:       return QColor("#38bdf8");
            case ColHostname: return host.hostname.isEmpty() ? QColor("#64748b") : QColor("#f8fafc");
            case ColPing:
                if (!alive) return QColor("#64748b");
                return (host.responseTimeMs < 5.0) ? QColor("#10b981") :
                       (host.responseTimeMs < 50.0) ? QColor("#f59e0b") : QColor("#ef4444");
            case ColMac:      return host.macAddress.isEmpty() ? QColor("#64748b") : QColor("#a78bfa");
            case ColVendor:   return host.vendor.isEmpty() ? QColor("#64748b") : QColor("#2dd4bf");
            case ColPorts:    return host.openPorts.isEmpty() ? QColor("#64748b") : QColor("#fb923c");
            case ColComments: return QColor("#cbd5e1");
            }
            break;
        }
        case PaletteLight: {
            switch (col) {
            case ColStatus:   return alive ? QColor("#059669") : QColor("#dc2626");
            case ColIp:       return QColor("#0284c7");
            case ColHostname: return host.hostname.isEmpty() ? QColor("#94a3b8") : QColor("#0f172a");
            case ColPing:
                if (!alive) return QColor("#94a3b8");
                return (host.responseTimeMs < 5.0) ? QColor("#059669") :
                       (host.responseTimeMs < 50.0) ? QColor("#d97706") : QColor("#dc2626");
            case ColMac:      return host.macAddress.isEmpty() ? QColor("#94a3b8") : QColor("#7c3aed");
            case ColVendor:   return host.vendor.isEmpty() ? QColor("#94a3b8") : QColor("#0d9488");
            case ColPorts:    return host.openPorts.isEmpty() ? QColor("#94a3b8") : QColor("#ea580c");
            case ColComments: return QColor("#334155");
            }
            break;
        }
        }
        return QVariant();
    }

    return QVariant();
}

QVariant HostTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case ColStatus: return "[ STATUS ]";
        case ColIp: return "[ IP_ADDRESS ]";
        case ColHostname: return "[ HOST_IDENT ]";
        case ColPing: return "[ LATENCY ]";
        case ColMac: return "[ MAC_ADDR ]";
        case ColVendor: return "[ HARDWARE_VENDOR ]";
        case ColPorts: return "[ OPEN_PORTS ]";
        case ColComments: return "[ NOTES ]";
        default: return QVariant();
        }
    }

    if (orientation == Qt::Horizontal && role == Qt::TextAlignmentRole) {
        if (section == ColStatus) {
            return static_cast<int>(Qt::AlignCenter);
        }
        if (section == ColPing) {
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        }
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    }

    return QAbstractTableModel::headerData(section, orientation, role);
}

Qt::ItemFlags HostTableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;

    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == ColComments) {
        f |= Qt::ItemIsEditable;
    }
    return f;
}

bool HostTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_hosts.size()) {
        return false;
    }

    if (index.column() == ColComments && role == Qt::EditRole) {
        m_hosts[index.row()].comments = value.toString();
        emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
        return true;
    }

    return false;
}

void HostTableModel::addOrUpdateHost(const HostItem &host)
{
    if (m_ipToIndex.contains(host.ip)) {
        int row = m_ipToIndex.value(host.ip);
        bool wasAlive = m_hosts[row].isAlive;

        // Merge updated fields
        HostItem &existing = m_hosts[row];
        existing.isAlive = host.isAlive;
        if (host.responseTimeMs >= 0) existing.responseTimeMs = host.responseTimeMs;
        if (!host.hostname.isEmpty()) existing.hostname = host.hostname;
        if (!host.macAddress.isEmpty()) existing.macAddress = host.macAddress;
        if (!host.vendor.isEmpty()) existing.vendor = host.vendor;
        if (!host.openPorts.isEmpty()) {
            existing.openPorts = host.openPorts;
            existing.services = host.services;
        }
        existing.lastSeen = host.lastSeen;

        if (!wasAlive && host.isAlive) {
            m_aliveCount++;
            emit statsChanged(m_aliveCount, m_hosts.size());
        }

        emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
    } else {
        int newRow = m_hosts.size();
        beginInsertRows(QModelIndex(), newRow, newRow);
        m_hosts.append(host);
        m_ipToIndex.insert(host.ip, newRow);
        if (host.isAlive) {
            m_aliveCount++;
        }
        endInsertRows();

        emit statsChanged(m_aliveCount, m_hosts.size());
    }
}

void HostTableModel::clear()
{
    beginResetModel();
    m_hosts.clear();
    m_ipToIndex.clear();
    m_aliveCount = 0;
    endResetModel();

    emit statsChanged(0, 0);
}

HostItem HostTableModel::hostAt(int row) const
{
    if (row >= 0 && row < m_hosts.size()) {
        return m_hosts.at(row);
    }
    return HostItem();
}

int HostTableModel::aliveCount() const
{
    return m_aliveCount;
}

int HostTableModel::totalCount() const
{
    return m_hosts.size();
}

void HostTableModel::setPaletteMode(PaletteMode mode)
{
    if (m_paletteMode != mode) {
        m_paletteMode = mode;
        if (!m_hosts.isEmpty()) {
            emit dataChanged(index(0, 0), index(m_hosts.size() - 1, ColumnCount - 1), {Qt::ForegroundRole});
        }
    }
}
