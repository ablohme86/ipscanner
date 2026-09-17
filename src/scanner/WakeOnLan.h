#pragma once

#include <QString>
#include <QHostAddress>

class WakeOnLan {
public:
    static bool sendMagicPacket(const QString &macAddress, 
                               const QString &broadcastIp = "255.255.255.255", 
                               quint16 port = 9, 
                               QString *errorMessage = nullptr);
};
