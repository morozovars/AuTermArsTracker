#include "ars_tracker_device_worker.h"

#include <QApplication>
#include <QMetaObject>
#include <QSerialPort>
#include <QThread>

namespace
{
static constexpr int kTelemetryActionUserData = 0x7A;
static constexpr int kNonSmpFlushMs = 500;
static constexpr int kNonSmpMaxFlushBytes = 4096;
}

ArsTrackerDeviceWorker::ArsTrackerDeviceWorker(const QString &portName, const QString &serial,
                                               QObject *parent)
    : QObject(parent), portName_(portName.trimmed()), serial_(serial.trimmed())
{
}

ArsTrackerDeviceWorker::~ArsTrackerDeviceWorker()
{
    stop();
}

bool ArsTrackerDeviceWorker::ensureThreadAffinity() const
{
    return QThread::currentThread() == thread();
}

void ArsTrackerDeviceWorker::setSerialConfig(int baudRate, int dataBits, int stopBits, int parity,
                                             int flowControl, bool applyRts, bool setRts, bool setDtr)
{
    baudRate_ = baudRate;
    dataBits_ = dataBits;
    stopBits_ = stopBits;
    parity_ = parity;
    flowControl_ = flowControl;
    applyRts_ = applyRts;
    setRts_ = setRts;
    setDtr_ = setDtr;
}

void ArsTrackerDeviceWorker::setSmpConfig(int version, int mtu, int retries, int timeoutMs)
{
    smpVersion_ = version;
    smpMtu_ = mtu;
    smpRetries_ = retries;
    smpTimeoutMs_ = timeoutMs;
}

void ArsTrackerDeviceWorker::start()
{
    if (!ensureThreadAffinity())
    {
        emit error(portName_, "Worker start called on wrong thread.");
        return;
    }
    if (started_)
    {
        return;
    }
    if (QThread::currentThread() == qApp->thread())
    {
        emit logMessage(portName_, "perf worker thread affinity mismatch: worker started on GUI thread");
    }

    serialPort_ = new QSerialPort();
    transport_ = new smp_uart_auterm();
    processor_ = new smp_processor(this);
    shell_ = new smp_group_shell_mgmt(processor_);

    serialPort_->setPortName(portName_);
    serialPort_->setBaudRate(baudRate_);
    serialPort_->setDataBits(static_cast<QSerialPort::DataBits>(dataBits_));
    serialPort_->setStopBits(static_cast<QSerialPort::StopBits>(stopBits_));
    serialPort_->setParity(static_cast<QSerialPort::Parity>(parity_));
    serialPort_->setFlowControl(static_cast<QSerialPort::FlowControl>(flowControl_));

    if (!serialPort_->open(QIODevice::ReadWrite))
    {
        const QString err = serialPort_->errorString();
        delete shell_;
        delete processor_;
        delete transport_;
        delete serialPort_;
        shell_ = nullptr;
        processor_ = nullptr;
        transport_ = nullptr;
        serialPort_ = nullptr;
        emit error(portName_, QString("Failed to open %1: %2").arg(portName_, err));
        return;
    }
    if (applyRts_)
    {
        serialPort_->setRequestToSend(setRts_);
    }
    serialPort_->setDataTerminalReady(setDtr_);

    processor_->set_transport(transport_);

    connect(serialPort_, &QSerialPort::readyRead, this, &ArsTrackerDeviceWorker::onSerialReadyRead);
    connect(serialPort_, &QSerialPort::errorOccurred, this, [this](QSerialPort::SerialPortError serial_error) {
        if (serial_error == QSerialPort::NoError || serialPort_ == nullptr)
        {
            return;
        }
        emit error(portName_, QString("Serial error %1: %2")
                                 .arg(int(serial_error))
                                 .arg(serialPort_->errorString()));
    });
    connect(transport_, &smp_uart_auterm::serial_write, this, [this](QByteArray *data) {
        if (serialPort_ == nullptr || data == nullptr || !serialPort_->isOpen())
        {
            return;
        }
        serialPort_->write(*data);
    });
    connect(transport_, &smp_uart_auterm::non_smp_uart_data_received, this,
            &ArsTrackerDeviceWorker::handleNonSmpBytes);
    connect(shell_, &smp_group_shell_mgmt::status, this, &ArsTrackerDeviceWorker::onShellStatus);

    nonSmpFlushTimer_ = new QTimer(this);
    nonSmpFlushTimer_->setInterval(kNonSmpFlushMs);
    connect(nonSmpFlushTimer_, &QTimer::timeout, this, &ArsTrackerDeviceWorker::flushNonSmpBuffer);
    nonSmpFlushTimer_->start();

    perfWindowTimer_ = new QTimer(this);
    perfWindowTimer_->setInterval(5000);
    connect(perfWindowTimer_, &QTimer::timeout, this, &ArsTrackerDeviceWorker::flushPerfWindow);
    perfWindowTimer_->start();

    started_ = true;
    emit started(portName_);
}

void ArsTrackerDeviceWorker::stop()
{
    if (!ensureThreadAffinity())
    {
        if (thread() != nullptr)
        {
            QMetaObject::invokeMethod(this, "stop", Qt::QueuedConnection);
        }
        return;
    }

    if (nonSmpFlushTimer_ != nullptr)
    {
        nonSmpFlushTimer_->stop();
    }
    flushNonSmpBuffer();
    if (perfWindowTimer_ != nullptr)
    {
        perfWindowTimer_->stop();
    }

    if (shell_ != nullptr)
    {
        shell_->cancel();
        delete shell_;
        shell_ = nullptr;
    }
    if (processor_ != nullptr)
    {
        processor_->cancel();
        delete processor_;
        processor_ = nullptr;
    }
    if (transport_ != nullptr)
    {
        transport_->reset_state();
        delete transport_;
        transport_ = nullptr;
    }
    if (serialPort_ != nullptr)
    {
        if (serialPort_->isOpen())
        {
            serialPort_->close();
        }
        delete serialPort_;
        serialPort_ = nullptr;
    }
    started_ = false;
    pendingCommand_ = PendingNone;
    pendingCommandName_.clear();
    emit stopped(portName_);
}

bool ArsTrackerDeviceWorker::startShellCommand(const QStringList &arguments, PendingCommand commandType,
                                               const QString &commandName)
{
    if (!started_ || shell_ == nullptr || processor_ == nullptr || transport_ == nullptr)
    {
        emit telemetryCommandFinished(portName_, commandName, false, "Worker not started");
        return false;
    }
    if (pendingCommand_ != PendingNone)
    {
        emit telemetryCommandFinished(portName_, commandName, false, "Worker command already in progress");
        return false;
    }

    processor_->set_transport(transport_);
    shell_->set_parameters(static_cast<uint8_t>(smpVersion_), static_cast<uint16_t>(smpMtu_),
                           static_cast<uint8_t>(smpRetries_), static_cast<uint32_t>(smpTimeoutMs_),
                           kTelemetryActionUserData);
    shellRc_ = 0;
    pendingCommand_ = commandType;
    pendingCommandName_ = commandName;
    pendingCommandTimer_.start();
    const bool started = shell_->start_execute(const_cast<QStringList *>(&arguments), &shellRc_);
    if (!started)
    {
        pendingCommand_ = PendingNone;
        pendingCommandName_.clear();
        emit telemetryCommandFinished(portName_, commandName, false, "Failed to start shell command");
        return false;
    }
    ++telemetryCommandsWindow_;
    return true;
}

void ArsTrackerDeviceWorker::requestStatus()
{
    if (QThread::currentThread() == qApp->thread())
    {
        emit logMessage(portName_, "BUG worker requestStatus called on GUI thread");
    }
    QStringList args;
    args << "status";
    startShellCommand(args, PendingStatus, "status");
}

void ArsTrackerDeviceWorker::requestBattery()
{
    if (QThread::currentThread() == qApp->thread())
    {
        emit logMessage(portName_, "BUG worker requestBattery called on GUI thread");
    }
    QStringList args;
    args << "bat"
         << "i";
    startShellCommand(args, PendingBattery, "bat i");
}

void ArsTrackerDeviceWorker::requestMemory()
{
    if (QThread::currentThread() == qApp->thread())
    {
        emit logMessage(portName_, "BUG worker requestMemory called on GUI thread");
    }
    QStringList args;
    args << "mem"
         << "i";
    startShellCommand(args, PendingMemory, "mem i");
}

void ArsTrackerDeviceWorker::requestFirmwareInfo()
{
    QStringList args;
    args << "img"
         << "state";
    startShellCommand(args, PendingFirmwareInfo, "img state");
}

void ArsTrackerDeviceWorker::requestShellCommand(const QString &command)
{
    const QString trimmed = command.trimmed();
    if (trimmed.isEmpty())
    {
        emit telemetryCommandFinished(portName_, "shell", false, "Empty command");
        return;
    }
    QStringList args = trimmed.split(' ', Qt::SkipEmptyParts);
    startShellCommand(args, PendingShellCommand, trimmed);
}

void ArsTrackerDeviceWorker::onSerialReadyRead()
{
    if (serialPort_ == nullptr || transport_ == nullptr)
    {
        return;
    }
    QElapsedTimer t;
    t.start();
    QByteArray data = serialPort_->readAll();
    if (!data.isEmpty())
    {
        ++readyReadCallsWindow_;
        readyReadBytesWindow_ += quint64(data.size());
        transport_->serial_read(&data);
    }
    const qint64 elapsed = t.elapsed();
    readyReadTotalHandlerMsWindow_ += elapsed;
    if (elapsed > readyReadMaxHandlerMsWindow_)
    {
        readyReadMaxHandlerMsWindow_ = elapsed;
    }
}

void ArsTrackerDeviceWorker::onShellStatus(uint8_t user_data, group_status status, QString error_string)
{
    if (user_data != kTelemetryActionUserData || pendingCommand_ == PendingNone)
    {
        return;
    }
    const bool ok = (status == STATUS_COMPLETE && shellRc_ == 0);
    if (pendingCommand_ == PendingStatus)
    {
        emit statusReceived(portName_, error_string);
        ++guiSignalsWindow_;
    }
    else if (pendingCommand_ == PendingBattery)
    {
        emit batteryReceived(portName_, error_string);
        ++guiSignalsWindow_;
    }
    else if (pendingCommand_ == PendingMemory)
    {
        emit memoryReceived(portName_, error_string);
        ++guiSignalsWindow_;
    }
    else if (pendingCommand_ == PendingFirmwareInfo)
    {
        emit firmwareInfoReceived(portName_, error_string, QString());
        ++guiSignalsWindow_;
    }
    finishPending(ok, error_string);
}

void ArsTrackerDeviceWorker::finishPending(bool ok, const QString &responseOrError)
{
    const PendingCommand finished = pendingCommand_;
    const QString command = pendingCommandName_.isEmpty() ? pendingCommandName(finished) : pendingCommandName_;
    const qint64 latency = pendingCommandTimer_.isValid() ? pendingCommandTimer_.elapsed() : 0;
    telemetryLatencyTotalMsWindow_ += latency;
    if (latency > telemetryLatencyMaxMsWindow_)
    {
        telemetryLatencyMaxMsWindow_ = latency;
    }
    pendingCommand_ = PendingNone;
    pendingCommandName_.clear();
    emit telemetryCommandFinished(portName_, command, ok, responseOrError);
    ++guiSignalsWindow_;
}

QString ArsTrackerDeviceWorker::pendingCommandName(PendingCommand cmd) const
{
    switch (cmd)
    {
    case PendingStatus:
        return "status";
    case PendingBattery:
        return "bat i";
    case PendingMemory:
        return "mem i";
    case PendingFirmwareInfo:
        return "img state";
    case PendingShellCommand:
        return "shell";
    default:
        return QString();
    }
}

void ArsTrackerDeviceWorker::handleNonSmpBytes(const QByteArray &bytes)
{
    if (bytes.isEmpty())
    {
        return;
    }
    const bool forwardRaw = (qEnvironmentVariableIntValue("ARS_TRACKER_FORWARD_RAW_DEVICE_LOGS") == 1);
    if (forwardRaw)
    {
        emit nonSmpBytesReceived(portName_, bytes);
        ++guiSignalsWindow_;
        return;
    }

    nonSmpBuffer_.append(bytes);
    if (nonSmpBuffer_.size() > kNonSmpMaxFlushBytes)
    {
        const int drop = nonSmpBuffer_.size() - kNonSmpMaxFlushBytes;
        nonSmpBuffer_.remove(0, drop);
        nonSmpDroppedBytes_ += quint64(drop);
        nonSmpDroppedBytesWindow_ += quint64(drop);
    }
}

void ArsTrackerDeviceWorker::flushNonSmpBuffer()
{
    if (nonSmpBuffer_.isEmpty())
    {
        return;
    }
    QByteArray chunk = nonSmpBuffer_.left(kNonSmpMaxFlushBytes);
    nonSmpBuffer_.remove(0, chunk.size());
    emit nonSmpBytesReceived(portName_, chunk);
    ++guiSignalsWindow_;
}

void ArsTrackerDeviceWorker::flushPerfWindow()
{
    if (qEnvironmentVariableIntValue("ARS_TRACKER_DEVICE_WORKER_PERF") != 1)
    {
        readyReadCallsWindow_ = 0;
        readyReadBytesWindow_ = 0;
        readyReadTotalHandlerMsWindow_ = 0;
        readyReadMaxHandlerMsWindow_ = 0;
        telemetryCommandsWindow_ = 0;
        telemetryLatencyTotalMsWindow_ = 0;
        telemetryLatencyMaxMsWindow_ = 0;
        guiSignalsWindow_ = 0;
        nonSmpDroppedBytesWindow_ = 0;
        return;
    }
    const double telemetryAvg = telemetryCommandsWindow_ > 0 ?
                (double(telemetryLatencyTotalMsWindow_) / double(telemetryCommandsWindow_)) :
                0.0;
    emit logMessage(
        portName_,
        QString("perf worker port=%1 thread=%2 readyRead=%3/s bytes=%4/s maxHandler=%5ms totalHandler=%6ms/s telemetry=%7/s avgTelemetry=%8ms maxTelemetry=%9ms guiSignals=%10/s rawDropped=%11B/s")
            .arg(portName_)
            .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()))
            .arg(double(readyReadCallsWindow_) / 5.0, 0, 'f', 1)
            .arg(double(readyReadBytesWindow_) / 5.0, 0, 'f', 1)
            .arg(readyReadMaxHandlerMsWindow_)
            .arg(double(readyReadTotalHandlerMsWindow_) / 5.0, 0, 'f', 1)
            .arg(double(telemetryCommandsWindow_) / 5.0, 0, 'f', 1)
            .arg(telemetryAvg, 0, 'f', 2)
            .arg(telemetryLatencyMaxMsWindow_)
            .arg(double(guiSignalsWindow_) / 5.0, 0, 'f', 1)
            .arg(double(nonSmpDroppedBytesWindow_) / 5.0, 0, 'f', 1));

    readyReadCallsWindow_ = 0;
    readyReadBytesWindow_ = 0;
    readyReadTotalHandlerMsWindow_ = 0;
    readyReadMaxHandlerMsWindow_ = 0;
    telemetryCommandsWindow_ = 0;
    telemetryLatencyTotalMsWindow_ = 0;
    telemetryLatencyMaxMsWindow_ = 0;
    guiSignalsWindow_ = 0;
    nonSmpDroppedBytesWindow_ = 0;
}
