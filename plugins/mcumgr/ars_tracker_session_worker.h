#ifndef ARS_TRACKER_SESSION_WORKER_H
#define ARS_TRACKER_SESSION_WORKER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QSerialPort>
#include <QTimer>

#include "ars_tracker_multi_worker.h"
#include "smp_group.h"

class smp_uart_auterm;
class smp_processor;
class smp_group_shell_mgmt;
class smp_group_fs_mgmt;
class smp_group_img_mgmt;
class smp_group_os_mgmt;
class debug_logger;

class ars_tracker_session_worker : public QObject
{
    Q_OBJECT

public:
    explicit ars_tracker_session_worker(const ars_tracker_multi_worker_settings_t &settings,
                                        QObject *parent = nullptr);
    ~ars_tracker_session_worker() override;

signals:
    void connected(const QString &port_name, const QString &serial);
    void disconnected(const QString &port_name, const QString &reason);
    void status_changed(const QString &port_name, const QString &status, const QString &error_text);
    void operation_failed(const QString &port_name, const QString &operation, const QString &error_text);
    void log_message(const QString &port_name, const QString &level, const QString &text);
    void thread_finished(const QString &serial);

public slots:
    void connectToTracker(const QString &port_name, const QString &expected_serial);
    void disconnectFromTracker();
    void requestInfo() {}
    void requestBattery() {}
    void requestFirmwareState() {}
    void listSessions() {}
    void downloadSession(const QString &) {}
    void cancelCurrentOperation();

private slots:
    void handle_ready_read();
    void handle_serial_error(QSerialPort::SerialPortError error);
    void handle_shell_status(uint8_t user_data, group_status status, QString shell_output);
    void handle_command_timeout();
    void send_param_sn();

private:
    void ensure_backend();
    void close_transport();
    void fail_connect(const QString &reason);
    void post_log(const QString &level, const QString &text);

    ars_tracker_multi_worker_settings_t settings;
    QString current_port;
    QString expected_serial;
    bool connecting = false;
    bool connected_state = false;
    int32_t shell_rc = 0;

    QSerialPort *serial_port = nullptr;
    smp_uart_auterm *transport = nullptr;
    smp_processor *processor = nullptr;
    smp_group_shell_mgmt *shell = nullptr;
    smp_group_fs_mgmt *fs_mgmt = nullptr;
    smp_group_img_mgmt *img_mgmt = nullptr;
    smp_group_os_mgmt *os_mgmt = nullptr;
    debug_logger *local_logger = nullptr;
    QTimer *command_timer = nullptr;
};

#endif
