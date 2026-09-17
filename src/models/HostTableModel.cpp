#include "HostTableModel.h"

HostTableModel::HostTableModel(QObject *parent)
    : QAbstractTableModel(parent)
    , m_aliveCount(0)
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
