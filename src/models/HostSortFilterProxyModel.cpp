#include "HostSortFilterProxyModel.h"
#include "HostTableModel.h"

HostSortFilterProxyModel::HostSortFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
    , m_onlyAlive(false)
{
    setSortRole(HostTableModel::RawSortRole);
    setDynamicSortFilter(true);
}

void HostSortFilterProxyModel::setFilterQuery(const QString &query)
{
    m_filterQuery = query.trimmed();
    invalidateFilter();
}

void HostSortFilterProxyModel::setOnlyAlive(bool onlyAlive)
{
    if (m_onlyAlive != onlyAlive) {
        m_onlyAlive = onlyAlive;
        invalidateFilter();
    }
}

bool HostSortFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    QModelIndex statusIndex = sourceModel()->index(sourceRow, HostTableModel::ColStatus, sourceParent);
    bool isAlive = sourceModel()->data(statusIndex, HostTableModel::IsAliveRole).toBool();

    if (m_onlyAlive && !isAlive) {
        return false;
    }

    if (m_filterQuery.isEmpty()) {
        return true;
    }

    // Check all visible columns for filterQuery match
    for (int col = 0; col < HostTableModel::ColumnCount; ++col) {
        QModelIndex idx = sourceModel()->index(sourceRow, col, sourceParent);
        QString val = sourceModel()->data(idx, Qt::DisplayRole).toString();
        if (val.contains(m_filterQuery, Qt::CaseInsensitive)) {
            return true;
        }
    }

    return false;
}

bool HostSortFilterProxyModel::lessThan(const QModelIndex &sourceLeft, const QModelIndex &sourceRight) const
{
    QVariant valLeft = sourceModel()->data(sourceLeft, HostTableModel::RawSortRole);
    QVariant valRight = sourceModel()->data(sourceRight, HostTableModel::RawSortRole);

    if (valLeft.userType() == QMetaType::ULongLong || valLeft.userType() == QMetaType::UInt || valLeft.userType() == QMetaType::Int) {
        return valLeft.toULongLong() < valRight.toULongLong();
    }

    if (valLeft.userType() == QMetaType::Double || valLeft.userType() == QMetaType::Float) {
        return valLeft.toDouble() < valRight.toDouble();
    }

    return QString::compare(valLeft.toString(), valRight.toString(), Qt::CaseInsensitive) < 0;
}
