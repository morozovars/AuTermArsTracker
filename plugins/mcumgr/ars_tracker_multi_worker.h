#ifndef ARS_TRACKER_MULTI_WORKER_H
#define ARS_TRACKER_MULTI_WORKER_H

#include <QObject>
#include <QString>
#include <QSerialPort>
#include <QTimer>

#include "smp_processor.h"
#include "smp_uart_auterm.h"
#include "smp_group_shell_mgmt.h"

struct ars_tracker_multi_worker_settings_t {
    int baud_rate = 115200;
    QSerialPort::DataBits data_bits = QSerialPort::Data8;
    QSerialPort::StopBits stop_bits = QSerialPort::OneStop;
    QSerialPort::Parity parity = QSerialPort::NoParity;
    QSerialPort::FlowControl flow_control = QSerialPort::NoFlowControl;
    bool request_to_send = false;
    bool data_terminal_ready = false;
    int mtu = 256;
    int protocol_version = 0;
    uint32_t timeout_ms = 2000;
    uint8_t retries = 0;
    int open_delay_ms = 400;
    int command_timeout_ms = 3000;
};

struct ars_tracker_multi_tracker_info {
    QString port_name;
    QString serial_number;
    QString side;
    QString connection_status;
    QString error_message;
    bool identified = false;
    bool active = false;
};

Q_DECLARE_METATYPE(ars_tracker_multi_tracker_info)

class ars_tracker_multi_worker : public QObject
{
    Q_OBJECT

public:
    explicit ars_tracker_multi_worker(const QString &port_name,
                                      const ars_tracker_multi_worker_settings_t &settings,
                                      QObject *parent = nullptr);
    ~ars_tracker_multi_worker() override;

signals:
    void tracker_updated(const ars_tracker_multi_tracker_info &info);
    void probe_finished(const QString &port_name, bool matched,
                        const ars_tracker_multi_tracker_info &info);
    void work_finished(const QString &port_name);

public slots:
    void start_probe();
    void start_connect_and_hold();
    void disconnect_tracker();

private slots:
    void handle_ready_read();
    void handle_serial_error(QSerialPort::SerialPortError error);
    void handle_shell_status(uint8_t user_data, group_status status, QString shell_output);
    void send_param_sn();
    void send_param_type();
    void handle_command_timeout();

private:
    enum phase_t : uint8_t {
        PHASE_IDLE = 0,
        PHASE_OPENING,
        PHASE_WAIT_PARAM_SN,
        PHASE_WAIT_PARAM_TYPE,
        PHASE_CONNECTED,
    };

    void ensure_backend();
    void configure_serial_port();
    void begin_open_sequence(bool hold_connection);
    void finalize_identification_success();
    void finalize_probe_failure(const QString &reason);
    void finalize_connection_failure(const QString &reason);
    void close_transport();
    QString infer_side_with_fallback() const;

    QString port_name;
    ars_tracker_multi_worker_settings_t settings;
    ars_tracker_multi_tracker_info info;
    QString identified_serial;
    bool hold_connection = false;
    bool disconnect_requested = false;
    phase_t phase = PHASE_IDLE;
    int32_t shell_rc = 0;

    QSerialPort *serial_port = nullptr;
    smp_uart_auterm *transport = nullptr;
    smp_processor *processor = nullptr;
    smp_group_shell_mgmt *shell = nullptr;
    QTimer *command_timer = nullptr;
};

#endif // ARS_TRACKER_MULTI_WORKER_H
