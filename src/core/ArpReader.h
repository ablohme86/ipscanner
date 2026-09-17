#pragma once

#include <QString>
#include <QHash>
#include <QMutex>

class ArpReader {
public:
    static ArpReader& instance();

    void refreshArpTable();
    QString getMacForIp(const QString &ip);
    bool hasMacForIp(const QString &ip);

private:
    ArpReader();
    ~ArpReader() = default;

    void readProcNetArp();
    void readLocalInterfaces();

    QHash<QString, QString> m_ipToMac;
    QMutex m_mutex;
};
