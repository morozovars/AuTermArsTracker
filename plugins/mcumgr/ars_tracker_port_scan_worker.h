#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QSerialPort>

struct ArsTrackerPortProbeResult
{
    QString portName;
    bool matched = false;
    QString serial;
    QString error;
    qint64 elapsedMs = 0;
};
Q_DECLARE_METATYPE(ArsTrackerPortProbeResult)
Q_DECLARE_METATYPE(QList<ArsTrackerPortProbeResult>)

struct ArsTrackerPortScanSettings
{
    qint32 baudRate = 115200;
    QSerialPort::DataBits dataBits = QSerialPort::Data8;
    QSerialPort::StopBits stopBits = QSerialPort::OneStop;
    QSerialPort::Parity parity = QSerialPort::NoParity;
    QSerialPort::FlowControl flowControl = QSerialPort::NoFlowControl;
    bool setRts = false;
    bool setDtr = false;
    bool applyRts = true;
    uint8_t smpVersion = 1;
    uint16_t mtu = 512;
    uint8_t retries = 0;
    uint32_t smpTimeoutMs = 2000;
    uint32_t hardProbeTimeoutMs = 3000;
    uint32_t interPortDelayMs = 10;
};

class ArsTrackerPortScanWorker : public QObject
{
    Q_OBJECT
public:
    explicit ArsTrackerPortScanWorker(const ArsTrackerPortScanSettings &settings,
                                      QObject *parent = nullptr);

public slots:
    void startScan(const QStringList &ports);
    void cancel();

signals:
    void probeStarted(const QString &portName, int index, int total);
    void probeFinished(const ArsTrackerPortProbeResult &result);
    void scanFinished(const QList<ArsTrackerPortProbeResult> &results);
    void scanCancelled();
    void scanLog(const QString &message);

private:
    ArsTrackerPortProbeResult probePort(const QString &portName);

    ArsTrackerPortScanSettings settings_;
    bool cancelled_ = false;
};

