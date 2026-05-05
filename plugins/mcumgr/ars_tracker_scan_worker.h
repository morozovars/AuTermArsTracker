#ifndef ARS_TRACKER_SCAN_WORKER_H
#define ARS_TRACKER_SCAN_WORKER_H

#include <QAtomicInt>
#include <QObject>
#include <QString>
#include <QStringList>

#include "ars_tracker_multi_worker.h"

class ars_tracker_scan_worker : public QObject
{
    Q_OBJECT

public:
    explicit ars_tracker_scan_worker(const QStringList &ports, const QString &source,
                                     const ars_tracker_multi_worker_settings_t &settings,
                                     int open_timeout_ms, int command_timeout_ms,
                                     QObject *parent = nullptr);
    ~ars_tracker_scan_worker() override;

signals:
    void scan_started(int total);
    void port_started(const QString &port_name, int index, int total);
    void port_opened(const QString &port_name);
    void port_found(const QString &port_name, const QString &serial_number,
                    const QString &response_text);
    void port_failed(const QString &port_name, const QString &reason);
    void scan_finished();
    void scan_cancelled();
    void log_debug_message(const QString &message);
    void log_warning_message(const QString &message);

public slots:
    void start();
    void cancel();

private:
    bool probe_single_port(const QString &port_name, int index, int total);
    void post_debug(const QString &message);
    void post_warning(const QString &message);

    QStringList ports;
    QString source;
    ars_tracker_multi_worker_settings_t settings;
    int open_timeout_ms = 0;
    int command_timeout_ms = 0;
    QAtomicInt cancel_requested = 0;
    bool running = false;
};

#endif // ARS_TRACKER_SCAN_WORKER_H
