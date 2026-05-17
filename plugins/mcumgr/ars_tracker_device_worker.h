/******************************************************************************
** Copyright (C) 2026
**
** Project: AuTermArsTracker
**
** Module:  ars_tracker_device_worker.h
*******************************************************************************/
#ifndef ARS_TRACKER_DEVICE_WORKER_H
#define ARS_TRACKER_DEVICE_WORKER_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QElapsedTimer>
#include <QTimer>

#include "smp_group_shell_mgmt.h"
#include "smp_processor.h"
#include "smp_uart_auterm.h"

class QSerialPort;

class ArsTrackerDeviceWorker : public QObject
{
    Q_OBJECT
public:
    explicit ArsTrackerDeviceWorker(const QString &portName, const QString &serial,
                                    QObject *parent = nullptr);
    ~ArsTrackerDeviceWorker() override;

public slots:
    void start();
    void stop();

    void setSerialConfig(int baudRate, int dataBits, int stopBits, int parity, int flowControl,
                         bool applyRts, bool setRts, bool setDtr);
    void setSmpConfig(int version, int mtu, int retries, int timeoutMs);

    void requestStatus();
    void requestBattery();
    void requestMemory();
    void requestFirmwareInfo();
    void requestShellCommand(const QString &command);

signals:
    void started(const QString &portName);
    void stopped(const QString &portName);
    void error(const QString &portName, const QString &message);

    void statusReceived(const QString &portName, const QString &statusText);
    void batteryReceived(const QString &portName, const QString &batteryText);
    void memoryReceived(const QString &portName, const QString &memoryText);
    void firmwareInfoReceived(const QString &portName, const QString &versionText,
                              const QString &secondSlotText);

    void telemetryCommandFinished(const QString &portName, const QString &command, bool ok,
                                  const QString &responseOrError);
    void nonSmpBytesReceived(const QString &portName, const QByteArray &bytes);
    void logMessage(const QString &portName, const QString &message);

private slots:
    void onSerialReadyRead();
    void onShellStatus(uint8_t user_data, group_status status, QString error_string);
    void flushNonSmpBuffer();
    void flushPerfWindow();

private:
    enum PendingCommand
    {
        PendingNone = 0,
        PendingStatus,
        PendingBattery,
        PendingMemory,
        PendingFirmwareInfo,
        PendingShellCommand
    };

    bool ensureThreadAffinity() const;
    bool startShellCommand(const QStringList &arguments, PendingCommand commandType,
                           const QString &commandName);
    void finishPending(bool ok, const QString &responseOrError);
    QString pendingCommandName(PendingCommand cmd) const;
    void handleNonSmpBytes(const QByteArray &bytes);

    QString portName_;
    QString serial_;

    int baudRate_ = 115200;
    int dataBits_ = 8;
    int stopBits_ = 1;
    int parity_ = 0;
    int flowControl_ = 0;
    bool applyRts_ = false;
    bool setRts_ = false;
    bool setDtr_ = true;

    int smpVersion_ = 1;
    int smpMtu_ = 512;
    int smpRetries_ = 3;
    int smpTimeoutMs_ = 2000;

    QSerialPort *serialPort_ = nullptr;
    smp_uart_auterm *transport_ = nullptr;
    smp_processor *processor_ = nullptr;
    smp_group_shell_mgmt *shell_ = nullptr;
    int shellRc_ = 0;
    bool started_ = false;

    PendingCommand pendingCommand_ = PendingNone;
    QString pendingCommandName_;
    QElapsedTimer pendingCommandTimer_;

    QTimer *nonSmpFlushTimer_ = nullptr;
    QByteArray nonSmpBuffer_;
    quint64 nonSmpDroppedBytes_ = 0;
    quint64 nonSmpDroppedBytesWindow_ = 0;

    QTimer *perfWindowTimer_ = nullptr;
    quint64 readyReadCallsWindow_ = 0;
    quint64 readyReadBytesWindow_ = 0;
    qint64 readyReadTotalHandlerMsWindow_ = 0;
    qint64 readyReadMaxHandlerMsWindow_ = 0;
    quint64 telemetryCommandsWindow_ = 0;
    qint64 telemetryLatencyTotalMsWindow_ = 0;
    qint64 telemetryLatencyMaxMsWindow_ = 0;
    quint64 guiSignalsWindow_ = 0;
};

#endif // ARS_TRACKER_DEVICE_WORKER_H

