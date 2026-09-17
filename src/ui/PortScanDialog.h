#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QTableWidget>
#include <QLabel>
#include "../scanner/PortScanner.h"

class PortScanDialog : public QDialog {
    Q_OBJECT
public:
    explicit PortScanDialog(const QString &ip = QString(), QWidget *parent = nullptr);
    ~PortScanDialog() override;

    void setTargetIp(const QString &ip);

private slots:
    void onStartStopClicked();
    void onPortPresetChanged(int index);
    void onPortFound(const QString &ip, const PortInfo &info);
    void onProgressUpdated(int scanned, int total);
    void onScanFinished();
    void onCopyClicked();

private:
    void setupUi();
    QList<int> getPortsToScan() const;

    QLineEdit *m_ipEdit;
    QComboBox *m_presetCombo;
    QLineEdit *m_customPortsEdit;
    QPushButton *m_startStopBtn;
    QProgressBar *m_progressBar;
    QLabel *m_statusLabel;
    QTableWidget *m_table;
    QPushButton *m_copyBtn;
    QPushButton *m_closeBtn;

    PortScanner *m_scanner;
};
