#include "ars_tracker_multi_worker.h"

#include <QByteArray>
#include <QDebug>

#include "ars_tracker_parser.h"

ars_tracker_multi_worker::ars_tracker_multi_worker(
        const QString &port_name_in, const ars_tracker_multi_worker_settings_t &settings_in,
        QObject *parent)
    : QObject(parent), port_name(port_name_in.trimmed()), settings(settings_in)
{
    info.port_name = port_name;
    info.connection_status = QString("disconnected");
}

ars_tracker_multi_worker::~ars_tracker_multi_worker()
{
    close_transport();
}

void ars_tracker_multi_worker::ensure_backend()
{
    if (serial_port != nullptr)
    {
        return;
    }

    serial_port = new QSerialPort(this);
    transport = new smp_uart_auterm(this);
    processor = new smp_processor(this);
    shell = new smp_group_shell_mgmt(processor);
    command_timer = new QTimer(this);
    command_timer->setSingleShot(true);

    connect(serial_port, &QSerialPort::readyRead, this,
            &ars_tracker_multi_worker::handle_ready_read);
    connect(serial_port, &QSerialPort::errorOccurred, this,
            &ars_tracker_multi_worker::handle_serial_error);
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
            &ars_tracker_multi_worker::handle_shell_status);
    connect(command_timer, &QTimer::timeout, this,
            &ars_tracker_multi_worker::handle_command_timeout);
}

void ars_tracker_multi_worker::configure_serial_port()
{
    if (serial_port == nullptr)
    {
        return;
    }

    serial_port->close();
    serial_port->setPortName(port_name);
    serial_port->setBaudRate(settings.baud_rate);
    serial_port->setDataBits(settings.data_bits);
    serial_port->setStopBits(settings.stop_bits);
    serial_port->setParity(settings.parity);
    serial_port->setFlowControl(settings.flow_control);
}

void ars_tracker_multi_worker::start_probe()
{
    begin_open_sequence(false);
}

void ars_tracker_multi_worker::start_connect_and_hold()
{
    begin_open_sequence(true);
}

void ars_tracker_multi_worker::begin_open_sequence(bool hold_connection_requested)
{
    if (phase != PHASE_IDLE)
    {
        return;
    }

    ensure_backend();
    hold_connection = hold_connection_requested;
    disconnect_requested = false;
    identified_serial.clear();
    info = ars_tracker_multi_tracker_info();
    info.port_name = port_name;
    info.connection_status = QString("connecting");
    phase = PHASE_OPENING;

    if (hold_connection == true)
    {
        emit tracker_updated(info);
    }

    configure_serial_port();
    if (serial_port->open(QIODevice::ReadWrite) == false)
    {
        QString reason = QString("Busy/unavailable: %1").arg(serial_port->errorString());
        if (hold_connection == true)
        {
            finalize_connection_failure(reason);
        }
        else
        {
            finalize_probe_failure(reason);
        }
        return;
    }

    if (settings.flow_control != QSerialPort::HardwareControl)
    {
        serial_port->setRequestToSend(settings.request_to_send);
    }
    serial_port->setDataTerminalReady(settings.data_terminal_ready);

    QTimer::singleShot(settings.open_delay_ms, this,
                       &ars_tracker_multi_worker::send_param_sn);
}

void ars_tracker_multi_worker::send_param_sn()
{
    if (phase != PHASE_OPENING || shell == nullptr || processor == nullptr || transport == nullptr)
    {
        return;
    }

    processor->set_transport(transport);
    shell->set_parameters(settings.protocol_version, settings.mtu, settings.retries,
                          settings.timeout_ms, 0);
    QStringList command_arguments = QStringList() << "param" << "sn";
    shell_rc = 0;
    phase = PHASE_WAIT_PARAM_SN;

    if (shell->start_execute(&command_arguments, &shell_rc) == false)
    {
        QString reason = serial_port != nullptr && serial_port->isOpen() == false ?
                                 QString("Failed to start mcumgr shell command: port is closed") :
                                 QString("Failed to start mcumgr shell command");
        if (hold_connection == true)
        {
            finalize_connection_failure(reason);
        }
        else
        {
            finalize_probe_failure(reason);
        }
        return;
    }

    command_timer->start(settings.command_timeout_ms);
}

void ars_tracker_multi_worker::send_param_type()
{
    if (phase != PHASE_WAIT_PARAM_TYPE || shell == nullptr || processor == nullptr ||
        transport == nullptr)
    {
        return;
    }

    processor->set_transport(transport);
    shell->set_parameters(settings.protocol_version, settings.mtu, settings.retries,
                          settings.timeout_ms, 0);
    QStringList command_arguments = QStringList() << "param" << "type";
    shell_rc = 0;

    if (shell->start_execute(&command_arguments, &shell_rc) == false)
    {
        info.side = infer_side_with_fallback();
        finalize_identification_success();
        return;
    }

    command_timer->start(settings.command_timeout_ms);
}

void ars_tracker_multi_worker::handle_command_timeout()
{
    if (phase == PHASE_WAIT_PARAM_SN)
    {
        if (hold_connection == true)
        {
            finalize_connection_failure(QString("param sn timeout"));
        }
        else
        {
            finalize_probe_failure(QString("param sn timeout/non-ARS"));
        }
    }
    else if (phase == PHASE_WAIT_PARAM_TYPE)
    {
        info.side = infer_side_with_fallback();
        finalize_identification_success();
    }
}

void ars_tracker_multi_worker::handle_ready_read()
{
    if (serial_port == nullptr || serial_port->isOpen() == false || transport == nullptr)
    {
        return;
    }

    QByteArray data = serial_port->readAll();
    if (data.isEmpty() == false)
    {
        transport->serial_read(&data);
    }
}

void ars_tracker_multi_worker::handle_serial_error(QSerialPort::SerialPortError error)
{
    if (serial_port == nullptr || error == QSerialPort::NoError)
    {
        return;
    }

    if (disconnect_requested == true)
    {
        return;
    }

    QString reason = serial_port->errorString();
    if (reason.trimmed().isEmpty())
    {
        reason = QString("Serial error");
    }

    if (hold_connection == true || info.identified == true)
    {
        finalize_connection_failure(reason);
    }
    else
    {
        finalize_probe_failure(reason);
    }
}

void ars_tracker_multi_worker::handle_shell_status(uint8_t user_data, group_status status,
                                                   QString shell_output)
{
    Q_UNUSED(user_data);

    if (command_timer != nullptr)
    {
        command_timer->stop();
    }

    if (phase == PHASE_WAIT_PARAM_SN)
    {
        if (status != STATUS_COMPLETE)
        {
            if (hold_connection == true)
            {
                finalize_connection_failure(QString("mcumgr shell command failed: %1")
                                                    .arg(shell_output));
            }
            else
            {
                finalize_probe_failure(QString("mcumgr shell command failed: %1")
                                               .arg(shell_output));
            }
            return;
        }

        if (shell_rc != 0)
        {
            QString reason = QString("mcumgr shell returned ret=%1").arg(shell_rc);
            if (hold_connection == true)
            {
                finalize_connection_failure(reason);
            }
            else
            {
                finalize_probe_failure(reason);
            }
            return;
        }

        QString decoded_serial;
        QString parse_error;
        if (ars_tracker_parser::parse_param_sn_output(shell_output, &decoded_serial, &parse_error) ==
            false)
        {
            if (hold_connection == true)
            {
                finalize_connection_failure(parse_error);
            }
            else
            {
                finalize_probe_failure(parse_error);
            }
            return;
        }

        QString validation_error;
        if (ars_tracker_parser::is_supported_ars_serial(decoded_serial, &validation_error) == false)
        {
            finalize_probe_failure(validation_error);
            return;
        }

        identified_serial = decoded_serial.trimmed();
        info.serial_number = identified_serial;
        info.identified = true;
        phase = PHASE_WAIT_PARAM_TYPE;
        send_param_type();
        return;
    }

    if (phase == PHASE_WAIT_PARAM_TYPE)
    {
        if (status == STATUS_COMPLETE && shell_rc == 0)
        {
            QString tracker_type;
            QString parse_error;
            if (ars_tracker_parser::parse_param_type_output(shell_output, &tracker_type,
                                                            &parse_error) == true)
            {
                info.side = ars_tracker_parser::normalize_tracker_side_token(tracker_type);
            }
        }

        if (info.side.isEmpty())
        {
            info.side = infer_side_with_fallback();
        }

        finalize_identification_success();
    }
}

QString ars_tracker_multi_worker::infer_side_with_fallback() const
{
    bool used_serial_fallback = false;
    QString side = ars_tracker_parser::infer_tracker_side_from_serial(identified_serial,
                                                                      &used_serial_fallback);
    if (used_serial_fallback == true)
    {
        // TODO: replace serial-based side fallback with a dedicated tracker-side source if the
        // protocol exposes one that is more stable than the current serial/type inference.
        qWarning() << "ArsTracker multi worker used serial-based side fallback for" << port_name
                   << "serial=" << identified_serial;
    }

    return side;
}

void ars_tracker_multi_worker::finalize_identification_success()
{
    if (info.side.isEmpty())
    {
        info.side = QString("?");
    }

    if (hold_connection == true)
    {
        phase = PHASE_CONNECTED;
        info.connection_status = QString("connected");
        info.error_message.clear();
        emit tracker_updated(info);
        return;
    }

    info.connection_status = QString("disconnected");
    info.error_message.clear();
    phase = PHASE_IDLE;
    close_transport();
    emit probe_finished(port_name, true, info);
    emit work_finished(port_name);
}

void ars_tracker_multi_worker::finalize_probe_failure(const QString &reason)
{
    phase = PHASE_IDLE;
    close_transport();
    info.error_message = reason;
    info.connection_status = QString("error");
    emit probe_finished(port_name, false, info);
    emit work_finished(port_name);
}

void ars_tracker_multi_worker::finalize_connection_failure(const QString &reason)
{
    phase = PHASE_IDLE;
    close_transport();
    info.error_message = reason;
    info.connection_status = QString("error");
    emit tracker_updated(info);
    emit work_finished(port_name);
}

void ars_tracker_multi_worker::disconnect_tracker()
{
    if (phase == PHASE_IDLE)
    {
        emit work_finished(port_name);
        return;
    }

    disconnect_requested = true;
    hold_connection = false;
    if (command_timer != nullptr)
    {
        command_timer->stop();
    }

    phase = PHASE_IDLE;
    close_transport();
    info.connection_status = QString("disconnected");
    info.error_message.clear();
    emit tracker_updated(info);
    emit work_finished(port_name);
}

void ars_tracker_multi_worker::close_transport()
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
