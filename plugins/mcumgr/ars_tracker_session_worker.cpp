#include "ars_tracker_session_worker.h"

#include "ars_tracker_parser.h"
#include "debug_logger.h"
#include "smp_group_fs_mgmt.h"
#include "smp_group_img_mgmt.h"
#include "smp_group_os_mgmt.h"
#include "smp_group_shell_mgmt.h"
#include "smp_processor.h"
#include "smp_uart_auterm.h"

class ars_tracker_session_quiet_logger : public debug_logger
{
public:
    explicit ars_tracker_session_quiet_logger(QObject *parent = nullptr) : debug_logger(parent) {}
protected:
    qint64 writeData(const char *data, qint64 len) override
    {
        Q_UNUSED(data);
        return len;
    }
};

ars_tracker_session_worker::ars_tracker_session_worker(
    const ars_tracker_multi_worker_settings_t &settings_in, QObject *parent)
    : QObject(parent), settings(settings_in)
{
}

ars_tracker_session_worker::~ars_tracker_session_worker()
{
    close_transport();
}

void ars_tracker_session_worker::ensure_backend()
{
    if (serial_port != nullptr)
    {
        return;
    }

    serial_port = new QSerialPort(this);
    transport = new smp_uart_auterm(this);
    processor = new smp_processor(this);
    shell = new smp_group_shell_mgmt(processor);
    fs_mgmt = new smp_group_fs_mgmt(processor);
    img_mgmt = new smp_group_img_mgmt(processor);
    os_mgmt = new smp_group_os_mgmt(processor);
    local_logger = new ars_tracker_session_quiet_logger(this);
    command_timer = new QTimer(this);
    command_timer->setSingleShot(true);

    transport->set_logger(local_logger);
    processor->set_logger(local_logger);
    shell->set_logger(local_logger);
    fs_mgmt->set_logger(local_logger);
    img_mgmt->set_logger(local_logger);
    os_mgmt->set_logger(local_logger);

    connect(serial_port, &QSerialPort::readyRead, this,
            &ars_tracker_session_worker::handle_ready_read);
    connect(serial_port, &QSerialPort::errorOccurred, this,
            &ars_tracker_session_worker::handle_serial_error);
    connect(transport, &smp_uart_auterm::receive_waiting, processor,
            &smp_processor::message_received);
    connect(transport, &smp_uart_auterm::serial_write, this, [this](QByteArray *data) {
        if (serial_port == nullptr || data == nullptr || serial_port->isOpen() == false)
        {
            return;
        }
        serial_port->write(*data);
    });
    connect(shell, &smp_group_shell_mgmt::status, this,
            &ars_tracker_session_worker::handle_shell_status);
    connect(command_timer, &QTimer::timeout, this,
            &ars_tracker_session_worker::handle_command_timeout);
}

void ars_tracker_session_worker::connectToTracker(const QString &port_name,
                                                  const QString &expected_serial_in)
{
    if (connecting || connected_state)
    {
        return;
    }

    ensure_backend();
    current_port = port_name.trimmed();
    expected_serial = expected_serial_in.trimmed();
    connecting = true;
    shell_rc = 0;

    post_log("info", QString("SessionWorker connecting serial=%1 port=%2")
                         .arg(expected_serial)
                         .arg(current_port));
    emit status_changed(current_port, "connecting", QString());

    serial_port->close();
    serial_port->setPortName(current_port);
    serial_port->setBaudRate(settings.baud_rate);
    serial_port->setDataBits(settings.data_bits);
    serial_port->setStopBits(settings.stop_bits);
    serial_port->setParity(settings.parity);
    serial_port->setFlowControl(settings.flow_control);

    if (serial_port->open(QIODevice::ReadWrite) == false)
    {
        fail_connect(QString("open failed: %1").arg(serial_port->errorString()));
        return;
    }
    if (settings.flow_control != QSerialPort::HardwareControl)
    {
        serial_port->setRequestToSend(settings.request_to_send);
    }
    serial_port->setDataTerminalReady(settings.data_terminal_ready);

    QTimer::singleShot(settings.open_delay_ms, this, &ars_tracker_session_worker::send_param_sn);
}

void ars_tracker_session_worker::send_param_sn()
{
    if (connecting == false || shell == nullptr)
    {
        return;
    }
    processor->set_transport(transport);
    shell->set_parameters(settings.protocol_version, settings.mtu, settings.retries,
                          settings.timeout_ms, 0);
    QStringList args;
    args << "param"
         << "sn";
    shell_rc = 0;
    if (shell->start_execute(&args, &shell_rc) == false)
    {
        fail_connect("Failed to start mcumgr shell command");
        return;
    }
    command_timer->start(settings.command_timeout_ms);
}

void ars_tracker_session_worker::handle_ready_read()
{
    if (serial_port == nullptr || transport == nullptr || serial_port->isOpen() == false)
    {
        return;
    }
    QByteArray data = serial_port->readAll();
    if (data.isEmpty() == false)
    {
        transport->serial_read(&data);
    }
}

void ars_tracker_session_worker::handle_serial_error(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError || serial_port == nullptr)
    {
        return;
    }
    QString reason = serial_port->errorString().trimmed();
    if (reason.isEmpty())
    {
        reason = "Serial error";
    }
    if (connecting)
    {
        fail_connect(reason);
        return;
    }
    if (connected_state)
    {
        connected_state = false;
        close_transport();
        emit status_changed(current_port, "error", reason);
        emit disconnected(current_port, reason);
        emit thread_finished(expected_serial);
    }
}

void ars_tracker_session_worker::handle_shell_status(uint8_t user_data, group_status status,
                                                     QString shell_output)
{
    Q_UNUSED(user_data);
    if (command_timer != nullptr)
    {
        command_timer->stop();
    }

    if (connecting == false)
    {
        return;
    }
    if (status != STATUS_COMPLETE)
    {
        fail_connect(QString("shell status failed: %1").arg(int(status)));
        return;
    }
    if (shell_rc != 0)
    {
        fail_connect(QString("shell rc=%1").arg(shell_rc));
        return;
    }

    QString decoded_serial;
    QString parse_error;
    if (ars_tracker_parser::parse_param_sn_output(shell_output, &decoded_serial, &parse_error) == false)
    {
        fail_connect(parse_error);
        return;
    }
    decoded_serial = decoded_serial.trimmed();
    if (expected_serial.isEmpty() == false &&
        decoded_serial.compare(expected_serial, Qt::CaseInsensitive) != 0)
    {
        QString reason = QString("serial mismatch expected=%1 actual=%2")
                             .arg(expected_serial)
                             .arg(decoded_serial);
        post_log("warning", QString("SessionWorker serial mismatch expected=%1 actual=%2")
                                 .arg(expected_serial)
                                 .arg(decoded_serial));
        fail_connect(reason);
        return;
    }

    connecting = false;
    connected_state = true;
    emit status_changed(current_port, "connected", QString());
    emit connected(current_port, decoded_serial);
    post_log("info", QString("SessionWorker connected serial=%1 port=%2")
                         .arg(decoded_serial)
                         .arg(current_port));
}

void ars_tracker_session_worker::handle_command_timeout()
{
    if (connecting)
    {
        fail_connect("param sn timeout/non-ARS");
    }
}

void ars_tracker_session_worker::disconnectFromTracker()
{
    QString serial_snapshot = expected_serial;
    QString port_snapshot = current_port;
    connecting = false;
    connected_state = false;
    close_transport();
    emit status_changed(port_snapshot, "disconnected", QString());
    emit disconnected(port_snapshot, QString());
    post_log("info", QString("SessionWorker disconnected serial=%1 port=%2")
                         .arg(serial_snapshot)
                         .arg(port_snapshot));
    emit thread_finished(serial_snapshot);
}

void ars_tracker_session_worker::cancelCurrentOperation()
{
    if (shell != nullptr)
    {
        shell->cancel();
    }
}

void ars_tracker_session_worker::close_transport()
{
    if (command_timer != nullptr)
    {
        command_timer->stop();
    }
    if (shell != nullptr)
    {
        shell->cancel();
    }
    if (processor != nullptr)
    {
        processor->cancel();
    }
    if (transport != nullptr)
    {
        transport->reset_state();
    }
    if (serial_port != nullptr && serial_port->isOpen())
    {
        serial_port->close();
    }
}

void ars_tracker_session_worker::fail_connect(const QString &reason)
{
    connecting = false;
    connected_state = false;
    close_transport();
    emit status_changed(current_port, "error", reason);
    emit operation_failed(current_port, "connect", reason);
    post_log("warning", QString("SessionWorker failed serial=%1 port=%2 reason=%3")
                            .arg(expected_serial)
                            .arg(current_port)
                            .arg(reason));
    emit thread_finished(expected_serial);
}

void ars_tracker_session_worker::post_log(const QString &level, const QString &text)
{
    emit log_message(current_port, level, text);
}
