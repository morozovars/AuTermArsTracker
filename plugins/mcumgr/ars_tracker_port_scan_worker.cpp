#include "ars_tracker_port_scan_worker.h"

#include <QDateTime>
#include <QEventLoop>
#include <QTimer>
#include <QElapsedTimer>
#include <QThread>
#include <QApplication>

#include "ars_tracker_parser.h"
#include "ars_tracker_utils.h"
#include "smp_uart_auterm.h"
#include "smp_processor.h"
#include "smp_group_shell_mgmt.h"

ArsTrackerPortScanWorker::ArsTrackerPortScanWorker(const ArsTrackerPortScanSettings &settings,
                                                   QObject *parent)
    : QObject(parent), settings_(settings)
{
}

void ArsTrackerPortScanWorker::startScan(const QStringList &ports)
{
    cancelled_ = false;
    emit scanLog(QString("scan worker started thread=0x%1 ports=%2")
                     .arg(QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16))
                     .arg(ports.size()));
    if (qEnvironmentVariableIntValue("ARS_TRACKER_SCAN_PERF") == 1 &&
        QThread::currentThread() == qApp->thread())
    {
        emit scanLog("perf worker warning: running on GUI thread");
    }
    QList<ArsTrackerPortProbeResult> results;
    results.reserve(ports.size());

    for (int i = 0; i < ports.size(); ++i)
    {
        if (cancelled_)
        {
            emit scanCancelled();
            return;
        }

        const QString port = ports.at(i).trimmed();
        emit probeStarted(port, i + 1, ports.size());
        ArsTrackerPortProbeResult result = probePort(port);
        results.push_back(result);
        emit probeFinished(result);

        if (settings_.interPortDelayMs > 0)
        {
            QThread::msleep(settings_.interPortDelayMs);
        }
    }

    if (cancelled_)
    {
        emit scanCancelled();
        return;
    }
    emit scanFinished(results);
}

void ArsTrackerPortScanWorker::cancel()
{
    cancelled_ = true;
}

ArsTrackerPortProbeResult ArsTrackerPortScanWorker::probePort(const QString &portName)
{
    ArsTrackerPortProbeResult result;
    result.portName = portName;
    emit scanLog(QString("probe begin thread=0x%1 port=%2")
                     .arg(QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16))
                     .arg(portName));
    QElapsedTimer timer;
    timer.start();

    QSerialPort serial;
    serial.setPortName(portName);
    serial.setBaudRate(settings_.baudRate);
    serial.setDataBits(settings_.dataBits);
    serial.setStopBits(settings_.stopBits);
    serial.setParity(settings_.parity);
    serial.setFlowControl(settings_.flowControl);

    if (!serial.open(QIODevice::ReadWrite))
    {
        result.error = QString("Busy/unavailable: %1").arg(serial.errorString());
        result.elapsedMs = timer.elapsed();
        return result;
    }

    if (settings_.applyRts)
    {
        serial.setRequestToSend(settings_.setRts);
    }
    serial.setDataTerminalReady(settings_.setDtr);

    smp_uart_auterm transport;
    smp_processor        processor(this);
    smp_group_shell_mgmt shell(&processor);
    processor.set_transport(&transport);
    shell.set_parameters(settings_.smpVersion, settings_.mtu, settings_.retries,
                         settings_.smpTimeoutMs, 0xA5);

    QObject::connect(&transport, &smp_uart_auterm::serial_write, &serial,
                     [&serial](QByteArray *data) {
                         if (data == nullptr || !serial.isOpen())
                         {
                             return;
                         }
                         serial.write(*data);
                     },
                     Qt::DirectConnection);
    QObject::connect(&serial, &QSerialPort::readyRead, &transport, [&serial, &transport]() {
        QByteArray data = serial.readAll();
        if (!data.isEmpty())
        {
            transport.serial_read(&data);
        }
    });

    QEventLoop loop;
    QTimer timeout_timer;
    timeout_timer.setSingleShot(true);
    int32_t shell_rc = 0;
    bool done = false;

    QObject::connect(&shell, &smp_group_shell_mgmt::status, &loop,
                     [&result, &done, &loop, &shell_rc](uint8_t user_data, group_status status,
                                                         QString error_string) {
                         Q_UNUSED(user_data);
                         if (done)
                         {
                             return;
                         }
                         done = true;
                         if (status != STATUS_COMPLETE)
                         {
                             result.error =
                                 QString("mcumgr shell command failed: %1").arg(error_string);
                             loop.quit();
                             return;
                         }
                         if (shell_rc != 0)
                         {
                             result.error =
                                 QString("mcumgr shell returned ret=%1").arg(shell_rc);
                             loop.quit();
                             return;
                         }
                         QString serial_value;
                         QString parse_error;
                         if (!ars_tracker_parser::parse_param_sn_output(error_string, &serial_value,
                                                                        &parse_error))
                         {
                             result.error = parse_error;
                             loop.quit();
                             return;
                         }
                         QString validation_error;
                         const bool valid =
                             ars_tracker_utils::serial_is_valid(serial_value, &validation_error);
                         if (!valid)
                         {
                             result.error = validation_error;
                             loop.quit();
                             return;
                         }
                         result.matched = true;
                         result.serial = serial_value.trimmed();
                         loop.quit();
                     },
                     Qt::DirectConnection);

    QObject::connect(&timeout_timer, &QTimer::timeout, &loop, [&result, &done, &loop]() {
        if (!done)
        {
            result.error = "param sn timeout/non-ARS";
            loop.quit();
        }
    });

    QStringList args;
    args << "param" << "sn";
    if (!shell.start_execute(&args, &shell_rc))
    {
        result.error = "Failed to start mcumgr shell command";
    }
    else
    {
        timeout_timer.start(int(settings_.hardProbeTimeoutMs));
        loop.exec();
    }

    serial.close();
    result.elapsedMs = timer.elapsed();
    return result;
}
