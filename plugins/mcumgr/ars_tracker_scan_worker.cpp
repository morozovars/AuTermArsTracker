#include "ars_tracker_scan_worker.h"

#include <QEventLoop>
#include <QSerialPort>
#include <QTimer>
#include <exception>
#include <new>

#include "ars_tracker_parser.h"
#include "debug_logger.h"
#include "smp_group_shell_mgmt.h"
#include "smp_processor.h"
#include "smp_uart_auterm.h"

static QString ars_tracker_scan_worker_status_to_string(group_status status)
{
    switch (status)
    {
    case STATUS_COMPLETE:
        return "complete";
    case STATUS_ERROR:
        return "error";
    case STATUS_TIMEOUT:
        return "timeout";
    case STATUS_CANCELLED:
        return "cancelled";
    case STATUS_PROCESSOR_TRANSPORT_ERROR:
        return "processor_transport_error";
    case STATUS_TRANSPORT_DISCONNECTED:
        return "transport_disconnected";
    default:
        return QString("unknown(%1)").arg(int(status));
    }
}

ars_tracker_scan_worker::ars_tracker_scan_worker(const QStringList &ports_in,
                                                 const QString &source_in,
                                                 const ars_tracker_multi_worker_settings_t &settings_in,
                                                 int open_timeout_ms_in,
                                                 int command_timeout_ms_in,
                                                 QObject *parent)
    : QObject(parent),
      ports(ports_in),
      source(source_in),
      settings(settings_in),
      open_timeout_ms(open_timeout_ms_in),
      command_timeout_ms(command_timeout_ms_in)
{
}

ars_tracker_scan_worker::~ars_tracker_scan_worker() = default;

void ars_tracker_scan_worker::start()
{
    if (running)
    {
        return;
    }

    running = true;
    cancel_requested.storeRelease(0);
    post_debug(QString("ArsTracker scan worker start source=%1 ports=%2")
                   .arg(source)
                   .arg(ports.count()));
    emit scan_started(ports.count());

    int total = ports.count();
    for (int i = 0; i < total; ++i)
    {
        if (cancel_requested.loadAcquire() != 0)
        {
            running = false;
            emit scan_cancelled();
            return;
        }

        QString port_name = ports.at(i).trimmed();
        if (port_name.isEmpty())
        {
            continue;
        }

        try
        {
            probe_single_port(port_name, i + 1, total);
        }
        catch (const std::bad_alloc &ex)
        {
            emit port_failed(port_name, QString("bad_alloc: %1").arg(ex.what()));
        }
        catch (const std::exception &ex)
        {
            emit port_failed(port_name, QString("exception: %1").arg(ex.what()));
        }
        catch (...)
        {
            emit port_failed(port_name, QString("unknown exception"));
        }
    }

    running = false;
    if (cancel_requested.loadAcquire() != 0)
    {
        emit scan_cancelled();
        return;
    }
    emit scan_finished();
}

void ars_tracker_scan_worker::cancel()
{
    cancel_requested.storeRelease(1);
}

bool ars_tracker_scan_worker::probe_single_port(const QString &port_name, int index, int total)
{
    Q_UNUSED(total);

    try
    {
        emit port_started(port_name, index, ports.count());
        post_debug(QString("ArsTracker scan worker using legacy SMP stack port=%1").arg(port_name));

        QSerialPort serial;
        serial.setPortName(port_name);
        serial.setBaudRate(settings.baud_rate);
        serial.setDataBits(settings.data_bits);
        serial.setStopBits(settings.stop_bits);
        serial.setParity(settings.parity);
        serial.setFlowControl(settings.flow_control);

        if (serial.open(QIODevice::ReadWrite) == false)
        {
            emit port_failed(port_name, QString("Busy/unavailable: %1").arg(serial.errorString()));
            return false;
        }

        if (settings.flow_control != QSerialPort::HardwareControl)
        {
            serial.setRequestToSend(settings.request_to_send);
        }
        serial.setDataTerminalReady(settings.data_terminal_ready);
        serial.clear(QSerialPort::AllDirections);
        emit port_opened(port_name);

        QObject context;
        debug_logger local_logger(&context);
        smp_uart_auterm transport(&context);
        smp_processor processor(&context);
        smp_group_shell_mgmt shell(&processor);
        transport.set_logger(&local_logger);
        processor.set_logger(&local_logger);
        shell.set_logger(&local_logger);
        processor.set_transport(&transport);
        shell.set_parameters(settings.protocol_version, settings.mtu, settings.retries,
                             settings.timeout_ms, 0);

        QEventLoop loop;
        QTimer timeout_timer;
        timeout_timer.setSingleShot(true);
        timeout_timer.setInterval(command_timeout_ms);

        bool done = false;
        bool success = false;
        int32_t shell_rc = 0;
        QString shell_output;
        QString fail_reason;

        auto finish_fail = [&](const QString &reason) {
            if (done)
            {
                return;
            }
            done = true;
            fail_reason = reason;
            loop.quit();
        };
        auto finish_ok = [&](const QString &output) {
            if (done)
            {
                return;
            }
            done = true;
            success = true;
            shell_output = output;
            loop.quit();
        };

        QObject::connect(&serial, &QSerialPort::readyRead, &loop, [&]() {
            QByteArray data = serial.readAll();
            if (data.isEmpty())
            {
                return;
            }
            transport.serial_read(&data);
        });
        QObject::connect(&serial, &QSerialPort::errorOccurred, &loop,
                         [&](QSerialPort::SerialPortError error) {
                             if (error == QSerialPort::NoError || done)
                             {
                                 return;
                             }
                             finish_fail(QString("Serial error: %1").arg(serial.errorString()));
                         });
        QObject::connect(&transport, &smp_uart_auterm::serial_write, &loop, [&](QByteArray *data) {
            if (data == nullptr || serial.isOpen() == false || done)
            {
                return;
            }
            qint64 written = serial.write(*data);
            if (written < 0)
            {
                finish_fail(QString("Serial write failed: %1").arg(serial.errorString()));
            }
            else
            {
                serial.waitForBytesWritten(open_timeout_ms);
            }
        });
        QObject::connect(&transport, &smp_uart_auterm::non_smp_uart_data_received, &loop,
                         [&](const QByteArray &) {});
        QObject::connect(&transport, &smp_uart_auterm::receive_waiting, &processor,
                         &smp_processor::message_received);
        QObject::connect(&shell, &smp_group_shell_mgmt::status, &loop,
                         [&](uint8_t user_data, group_status status, QString output) {
                             Q_UNUSED(user_data);
                             timeout_timer.stop();
                             post_debug(QString("ArsTracker scan worker shell status port=%1 status=%2")
                                            .arg(port_name)
                                            .arg(ars_tracker_scan_worker_status_to_string(status)));

                             if (status != STATUS_COMPLETE)
                             {
                                 finish_fail(QString("mcumgr shell command failed: %1").arg(output));
                                 return;
                             }
                             if (shell_rc != 0)
                             {
                                 finish_fail(QString("mcumgr shell returned ret=%1").arg(shell_rc));
                                 return;
                             }
                             finish_ok(output);
                         });
        QObject::connect(&timeout_timer, &QTimer::timeout, &loop, [&]() {
            finish_fail(QString("param sn timeout/non-ARS"));
        });

        post_debug(QString("ArsTracker scan worker sending param sn port=%1").arg(port_name));
        QStringList args;
        args << "param"
             << "sn";
        if (shell.start_execute(&args, &shell_rc) == false)
        {
            serial.close();
            emit port_failed(port_name, QString("Failed to start mcumgr shell command"));
            return false;
        }

        timeout_timer.start();
        while (done == false)
        {
            if (cancel_requested.loadAcquire() != 0)
            {
                shell.cancel();
                processor.cancel();
                transport.reset_state();
                serial.close();
                emit port_failed(port_name, QString("scan cancelled"));
                return false;
            }
            loop.processEvents(QEventLoop::AllEvents, 20);
        }

        shell.cancel();
        processor.cancel();
        transport.reset_state();
        serial.close();

        if (success == false)
        {
            post_warning(QString("ArsTracker scan worker failed port=%1 reason=%2")
                             .arg(port_name)
                             .arg(fail_reason));
            emit port_failed(port_name, fail_reason);
            return false;
        }

        post_debug(QString("ArsTracker scan worker shell output port=%1 output=%2")
                       .arg(port_name)
                       .arg(shell_output));

        QString decoded_serial;
        QString parse_error;
        if (ars_tracker_parser::parse_param_sn_output(shell_output, &decoded_serial, &parse_error) ==
            false)
        {
            emit port_failed(port_name, parse_error);
            return false;
        }

        QString validation_error;
        if (ars_tracker_parser::is_supported_ars_serial(decoded_serial, &validation_error) == false)
        {
            emit port_failed(port_name, QString("non-ARS serial: %1").arg(validation_error));
            return false;
        }

        post_debug(QString("ArsTracker scan worker found tracker port=%1 serial=%2")
                       .arg(port_name)
                       .arg(decoded_serial.trimmed()));
        emit port_found(port_name, decoded_serial.trimmed(), shell_output);
        return true;
    }
    catch (const std::bad_alloc &ex)
    {
        emit port_failed(port_name, QString("bad_alloc: %1").arg(ex.what()));
        return false;
    }
    catch (const std::exception &ex)
    {
        emit port_failed(port_name, QString("exception: %1").arg(ex.what()));
        return false;
    }
    catch (...)
    {
        emit port_failed(port_name, QString("unknown exception"));
        return false;
    }
}

void ars_tracker_scan_worker::post_debug(const QString &message)
{
    emit log_debug_message(message);
}

void ars_tracker_scan_worker::post_warning(const QString &message)
{
    emit log_warning_message(message);
}
