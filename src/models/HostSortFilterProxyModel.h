#pragma once

#include <QSortFilterProxyModel>

class HostSortFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit HostSortFilterProxyModel(QObject *parent = nullptr);

    void setFilterQuery(const QString &query);
    void setOnlyAlive(bool onlyAlive);

    bool onlyAlive() const { return m_onlyAlive; }
    QString filterQuery() const { return m_filterQuery; }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &sourceLeft, const QModelIndex &sourceRight) const override;

private:
    QString m_filterQuery;
    bool m_onlyAlive;
};
