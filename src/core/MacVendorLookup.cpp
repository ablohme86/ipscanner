#include "MacVendorLookup.h"
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDebug>

MacVendorLookup& MacVendorLookup::instance()
{
    static MacVendorLookup inst;
    return inst;
}

MacVendorLookup::MacVendorLookup()
    : m_initialized(false)
{
}

void MacVendorLookup::init()
{
    QMutexLocker locker(&m_mutex);
    if (m_initialized) {
        return;
    }

    // Attempt to load from bundled resource first
    loadFromResource();

    // If resource wasn't found or was empty, check system locations
    if (m_vendors.isEmpty()) {
        loadFromSystem();
    }

    addKnownVirtualPrefixes();
    m_initialized = true;
}

void MacVendorLookup::loadFromResource()
{
    QFile file(":/data/oui_fallback.txt");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }

        int tabIdx = line.indexOf('\t');
        if (tabIdx > 0) {
            QString prefix = line.left(tabIdx).trimmed().toUpper();
            QString vendor = line.mid(tabIdx + 1).trimmed();
            if (prefix.length() == 6 && !vendor.isEmpty()) {
                m_vendors.insert(prefix, vendor);
            }
        }
    }
}

void MacVendorLookup::loadFromSystem()
{
    const QStringList candidates = {
        "/usr/share/hwdata/oui.txt",
        "/var/lib/ieee-data/oui.txt",
        "/usr/share/misc/oui.txt"
    };

    for (const QString &path : candidates) {
        if (QFileInfo::exists(path)) {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream in(&file);
                while (!in.atEnd()) {
                    QString line = in.readLine();
                    int hexIdx = line.indexOf("(hex)");
                    if (hexIdx != -1) {
                        QString prefix = line.left(hexIdx).trimmed().remove('-').remove(':').toUpper();
                        QString vendor = line.mid(hexIdx + 5).trimmed();
                        if (prefix.length() == 6 && !vendor.isEmpty()) {
                            m_vendors.insert(prefix, vendor);
                        }
                    }
                }
                if (!m_vendors.isEmpty()) {
                    break;
                }
            }
        }
    }
}

void MacVendorLookup::addKnownVirtualPrefixes()
{
    // Virtualization and container prefixes
    if (!m_vendors.contains("000569")) m_vendors.insert("000569", "VMware, Inc.");
    if (!m_vendors.contains("000C29")) m_vendors.insert("000C29", "VMware, Inc.");
    if (!m_vendors.contains("005056")) m_vendors.insert("005056", "VMware, Inc.");
    if (!m_vendors.contains("00155D")) m_vendors.insert("00155D", "Microsoft Hyper-V");
    if (!m_vendors.contains("080027")) m_vendors.insert("080027", "Oracle VirtualBox");
    if (!m_vendors.contains("525400")) m_vendors.insert("525400", "QEMU/KVM Virtual NIC");
    if (!m_vendors.contains("0242AC")) m_vendors.insert("0242AC", "Docker Container");
}

QString MacVendorLookup::lookup(const QString &macAddress) const
{
    if (macAddress.isEmpty()) {
        return QString();
    }

    // Normalize: remove delimiters and uppercase
    QString clean = macAddress;
    clean.remove(':').remove('-').remove('.').remove(' ');
    clean = clean.toUpper();

    if (clean.length() < 6) {
        return QString();
    }

    QString prefix = clean.left(6);

    QMutexLocker locker(&m_mutex);
    return m_vendors.value(prefix, QString());
}
