#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

class WolDialog : public QDialog {
    Q_OBJECT
public:
    explicit WolDialog(const QString &macAddress = QString(), 
                      const QString &broadcastIp = "255.255.255.255", 
                      QWidget *parent = nullptr);

    void setMacAddress(const QString &mac);

private slots:
    void onSendClicked();

private:
    void setupUi();

    QLineEdit *m_macEdit;
    QLineEdit *m_broadcastEdit;
    QLineEdit *m_portEdit;
    QLabel *m_statusLabel;
    QPushButton *m_sendBtn;
    QPushButton *m_closeBtn;
};
