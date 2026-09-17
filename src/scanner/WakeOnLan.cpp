#include "WakeOnLan.h"
#include <QUdpSocket>
#include <QByteArray>

bool WakeOnLan::sendMagicPacket(const QString &macAddress, 
                               const QString &broadcastIp, 
                               quint16 port, 
                               QString *errorMessage)
{
    QString cleanMac = macAddress;
    cleanMac.remove(':').remove('-').remove('.').remove(' ');

    if (cleanMac.length() != 12) {
        if (errorMessage) {
            *errorMessage = "Invalid MAC address length. Expected 12 hexadecimal characters.";
        }
        return false;
    }

    bool ok = false;
    quint8 macBytes[6];
    for (int i = 0; i < 6; ++i) {
        QString byteStr = cleanMac.mid(i * 2, 2);
        macBytes[i] = static_cast<quint8>(byteStr.toUShort(&ok, 16));
        if (!ok) {
            if (errorMessage) {
                *errorMessage = QString("Invalid hexadecimal character in MAC address: '%1'").arg(byteStr);
            }
            return false;
        }
    }

    // Magic packet format: 6 bytes 0xFF, followed by 16 repetitions of the 6-byte target MAC
    QByteArray packet;
    packet.resize(102);

    for (int i = 0; i < 6; ++i) {
        packet[i] = static_cast<char>(0xFF);
    }

    for (int rep = 0; rep < 16; ++rep) {
        for (int i = 0; i < 6; ++i) {
            packet[6 + rep * 6 + i] = static_cast<char>(macBytes[i]);
        }
    }

    QUdpSocket socket;
    QHostAddress bcastAddr(broadcastIp);
    qint64 bytesSent = socket.writeDatagram(packet, bcastAddr, port);

    if (bytesSent != packet.size()) {
        if (errorMessage) {
            *errorMessage = socket.errorString();
        }
        return false;
    }

    return true;
}
