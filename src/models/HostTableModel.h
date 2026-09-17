#pragma once

#include <QAbstractTableModel>
#include <QList>
#include <QHash>
#include <QColor>
#include "../core/HostItem.h"

class HostTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColStatus = 0,
        ColIp,
        ColHostname,
        ColPing,
        ColMac,
        ColVendor,
        ColPorts,
        ColComments,
        ColumnCount
    };

    enum CustomRole {
        RawSortRole = Qt::UserRole + 1,
        IsAliveRole,
        IpNumRole,
        PingMsRole
    };

    enum PaletteMode {
        PaletteBtopTokyo = 0,
        PaletteBtopDracula,
        PaletteBtopGruvbox,
        PaletteRetroCyan,
        PaletteRetroGreen,
        PaletteRetroAmber,
        PaletteDark,
        PaletteLight
    };

    explicit HostTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;

    void addOrUpdateHost(const HostItem &host);
    void clear();
    HostItem hostAt(int row) const;
    int aliveCount() const;
    int totalCount() const;

    void setPaletteMode(PaletteMode mode);
    PaletteMode paletteMode() const { return m_paletteMode; }

    const QList<HostItem>& hosts() const { return m_hosts; }

signals:
    void statsChanged(int alive, int total);

private:
    QList<HostItem> m_hosts;
    QHash<QString, int> m_ipToIndex;
    int m_aliveCount;
    PaletteMode m_paletteMode;
};
