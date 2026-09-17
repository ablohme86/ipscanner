#include <QCoreApplication>
#include <QDebug>
#include <cassert>
#include <iostream>

#include "core/HostItem.h"
#include "core/MacVendorLookup.h"
#include "core/ArpReader.h"
#include "scanner/NetworkScanner.h"
#include "scanner/PortScanner.h"
#include "scanner/WakeOnLan.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    std::cout << "[TEST] Starting unit tests..." << std::endl;

    // 1. Test HostItem
    std::cout << "[TEST] Testing HostItem..." << std::endl;
    HostItem item("192.168.1.100");
    assert(item.ip == "192.168.1.100");
    assert(item.ipv4Num == 0xC0A80164);
    assert(item.statusText() == "Offline");
    item.isAlive = true;
    item.responseTimeMs = 2.5;
    assert(item.statusText() == "Online (2.5 ms)");
    item.openPorts = {80, 443};
    item.services = QStringList{"HTTP", "HTTPS"};
    assert(item.openPortsSummary() == "80 (HTTP), 443 (HTTPS)");
    std::cout << "  -> HostItem passed!" << std::endl;

    // 2. Test MacVendorLookup
    std::cout << "[TEST] Testing MacVendorLookup..." << std::endl;
    MacVendorLookup::instance().init();
    QString rpi = MacVendorLookup::instance().lookup("D8:3A:DD:34:F1:9E");
    std::cout << "  D8:3A:DD:34:F1:9E -> " << rpi.toStdString() << std::endl;
    assert(rpi.contains("Raspberry Pi", Qt::CaseInsensitive));

    QString apple = MacVendorLookup::instance().lookup("F0:B3:EC:01:57:7B");
    std::cout << "  F0:B3:EC:01:57:7B -> " << apple.toStdString() << std::endl;
    assert(apple.contains("Apple", Qt::CaseInsensitive));

    QString tplink = MacVendorLookup::instance().lookup("50:91:E3:5E:EC:C0");
    std::cout << "  50:91:E3:5E:EC:C0 -> " << tplink.toStdString() << std::endl;
    assert(tplink.contains("TP-Link", Qt::CaseInsensitive));
    std::cout << "  -> MacVendorLookup passed!" << std::endl;

    // 3. Test ArpReader
    std::cout << "[TEST] Testing ArpReader..." << std::endl;
    ArpReader::instance().refreshArpTable();
    QString myMac = ArpReader::instance().getMacForIp("10.10.10.195");
    std::cout << "  10.10.10.195 (Local Host) MAC: " << myMac.toStdString() << std::endl;
    assert(!myMac.isEmpty());
    QString piMac = ArpReader::instance().getMacForIp("10.10.10.20");
    std::cout << "  10.10.10.20 MAC: " << piMac.toStdString() << std::endl;
    std::cout << "  -> ArpReader passed!" << std::endl;

    // 4. Test IP range generation
    std::cout << "[TEST] Testing NetworkScanner IP range generator..." << std::endl;
    QList<QString> range = NetworkScanner::generateIpRange("10.10.10.1", "10.10.10.5");
    assert(range.size() == 5);
    assert(range.first() == "10.10.10.1");
    assert(range.last() == "10.10.10.5");
    std::cout << "  -> IP range generator passed!" << std::endl;

    // 5. Test WOL packet generation
    std::cout << "[TEST] Testing WakeOnLan packet craft..." << std::endl;
    QString wolErr;
    bool wolOk = WakeOnLan::sendMagicPacket("D8:3A:DD:34:F1:9E", "127.0.0.1", 9999, &wolErr);
    assert(wolOk);
    std::cout << "  -> WakeOnLan passed!" << std::endl;

    // 6. Test Live Scan on 10.10.10.194 - 10.10.10.196
    std::cout << "[TEST] Testing NetworkScanner live scan on 10.10.10.194 - 10.10.10.196..." << std::endl;
    NetworkScanner scanner;
    int foundCount = 0;
    QObject::connect(&scanner, &NetworkScanner::hostDiscovered, [&](const HostItem &h) {
        if (h.isAlive) {
            foundCount++;
            std::cout << "  Discovered alive: " << h.ip.toStdString() 
                      << " (" << h.hostname.toStdString() << ") " 
                      << " MAC: " << h.macAddress.toStdString() 
                      << " Vendor: " << h.vendor.toStdString() 
                      << " Ping: " << h.responseTimeMs << "ms" << std::endl;
        }
    });

    QObject::connect(&scanner, &NetworkScanner::scanFinished, [&]() {
        std::cout << "  Scan finished! Found " << foundCount << " alive host(s)." << std::endl;
        assert(foundCount >= 1);
        app.quit();
    });

    scanner.startScan("10.10.10.194", "10.10.10.196", 10, 0.4, true);

    app.exec();

    std::cout << "ALL TESTS PASSED SUCCESSFULLY!" << std::endl;
    return 0;
}
