#ifndef ARS_TRACKER_BACKEND_H
#define ARS_TRACKER_BACKEND_H

#include <QObject>
#include <QString>
#include <QList>
#include <cstdint>

#include "smp_group.h"

struct ars_tracker_session_t {
    QString id;
    QString display_name;
    QString remote_path_or_name;
    QString raw_source_line;
};

Q_DECLARE_METATYPE(ars_tracker_session_t)

struct ars_tracker_download_item_t {
    QString remote_file;
    QString local_file;
    qint64 bytes_completed;
    qint64 total_bytes;
    uint8_t retry_count;
};

class ars_tracker_backend : public QObject
{
    Q_OBJECT

public:
    explicit ars_tracker_backend(QObject *parent = nullptr);
    ~ars_tracker_backend() override;

    bool begin_session_list_request(QString *error_message);
    void handle_session_list_response(group_status status, const QString &shell_output, int32_t shell_ret);

    const QList<ars_tracker_session_t> &sessions() const;

    void queue_session_download(const QString &session_id, const QString &destination_path);
    void cancel_all();

signals:
    void session_list_ready(const QList<ars_tracker_session_t> &sessions);
    void loading_changed(bool loading);
    void status_message(const QString &message);

private:
    bool loading;
    QList<ars_tracker_session_t> latest_sessions;
};

#endif // ARS_TRACKER_BACKEND_H
