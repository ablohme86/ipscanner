#pragma once

#include <QString>
#include <QHash>
#include <QMutex>

class MacVendorLookup {
public:
    static MacVendorLookup& instance();

    void init();
    QString lookup(const QString &macAddress) const;

private:
    MacVendorLookup();
    ~MacVendorLookup() = default;

    void loadFromResource();
    void loadFromSystem();
    void addKnownVirtualPrefixes();

    QHash<QString, QString> m_vendors;
    bool m_initialized;
    mutable QMutex m_mutex;
};
