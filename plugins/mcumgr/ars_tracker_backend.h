#ifndef ARS_TRACKER_BACKEND_H
#define ARS_TRACKER_BACKEND_H

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QList>
#include <QStringList>
#include <cstdint>

#include "smp_group.h"
#include "smp_group_fs_mgmt.h"

struct ars_tracker_session_t {
    QString id;
    QString display_name;
    QString remote_path_or_name;
    QString raw_source_line;
};

Q_DECLARE_METATYPE(ars_tracker_session_t)

enum ars_tracker_info_field_status_t : uint8_t {
    ARS_TRACKER_INFO_FIELD_IDLE = 0,
    ARS_TRACKER_INFO_FIELD_LOADING,
    ARS_TRACKER_INFO_FIELD_READY,
    ARS_TRACKER_INFO_FIELD_ERROR,
};

struct ars_tracker_info_field_t {
    QString value;
    QString error;
    ars_tracker_info_field_status_t status;
};

struct ars_tracker_info_t {
    ars_tracker_info_field_t serial_number;
    ars_tracker_info_field_t board_id;
    ars_tracker_info_field_t tracker_type;
    ars_tracker_info_field_t tracker_status;
};

Q_DECLARE_METATYPE(ars_tracker_info_t)

enum ars_tracker_download_category_t : uint8_t {
    ARS_TRACKER_FILE_FIXED = 0,
    ARS_TRACKER_FILE_SENSOR,
};

enum ars_tracker_download_status_t : uint8_t {
    ARS_TRACKER_STATUS_PENDING = 0,
    ARS_TRACKER_STATUS_CHECKING_EXISTING,
    ARS_TRACKER_STATUS_ALREADY_PRESENT,
    ARS_TRACKER_STATUS_DOWNLOADING,
    ARS_TRACKER_STATUS_DOWNLOADED,
    ARS_TRACKER_STATUS_MISSING,
    ARS_TRACKER_STATUS_SENSORS_END,
    ARS_TRACKER_STATUS_FAILED,
    ARS_TRACKER_STATUS_CANCELLED,
};

struct ars_tracker_download_item_t {
    QString remote_file;
    QString local_temp_file;
    QString local_file;
    qint64 bytes_completed;
    qint64 total_bytes;
    uint8_t retry_count;
    ars_tracker_download_category_t category;
    ars_tracker_download_status_t status;
    uint32_t remote_file_size;
    QByteArray remote_file_hash;
    QString error_text;
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
    const ars_tracker_info_t &tracker_info() const;

    bool begin_tracker_info_refresh(QString *error_message);
    void handle_tracker_info_response(group_status status, const QString &shell_output,
                                      int32_t shell_ret);
    bool begin_session_delete(const QString &session_id, QString *session_name,
                              QString *error_message);
    void handle_session_delete_response(group_status status, const QString &shell_output,
                                        int32_t shell_ret);
    bool begin_session_export(const QString &session_id, const QString &destination_path,
                              QString *error_message);
    void handle_export_hash_support_result(group_status status, const QString &error_message,
                                           const QList<hash_checksum_t> &supported_hashes);
    void handle_file_metadata_result(group_status status, const QString &error_message,
                                     const QByteArray &remote_hash, uint32_t remote_size);
    void handle_file_download_progress(uint8_t percent);
    void handle_file_download_result(group_status status, const QString &error_message);
    void cancel_all();
    bool export_in_progress() const;
#ifndef SKIPPLUGIN_LOGGER
    void set_logger(debug_logger* object);
#endif

signals:
    void session_list_ready(const QList<ars_tracker_session_t> &sessions);
    void tracker_info_changed(const ars_tracker_info_t &info);
    void tracker_info_loading_changed(bool loading);
    void loading_changed(bool loading);
    void status_message(const QString& message);

    void delete_loading_changed(bool loading);
    void export_loading_changed(bool loading);
    void export_progress_changed(const QString &progress_text);
    void export_file_list_changed(const QStringList &rows);
    void export_finished(bool success, bool cancelled, const QString &message);

    void request_session_list_refresh_after_delete();
    void request_tracker_info_shell_command(const QStringList &arguments);
    void request_cancel_tracker_info_shell_command();
    void request_file_hash_support();
    void request_file_metadata(const QString &remote_file, const QString &hash_name);
    void request_file_download(const QString &remote_file, const QString &local_temp_file);
    void request_cancel_file_download();

private:
    enum tracker_info_step_t : uint8_t {
        TRACKER_INFO_STEP_NONE = 0,
        TRACKER_INFO_STEP_SN,
        TRACKER_INFO_STEP_BID,
        TRACKER_INFO_STEP_TYPE,
        TRACKER_INFO_STEP_STATUS,
        TRACKER_INFO_STEP_SESSION_LIST,
    };

    bool loading;
    QList<ars_tracker_session_t> latest_sessions;
    bool tracker_info_loading;
    tracker_info_step_t active_tracker_info_step;
    ars_tracker_info_t latest_tracker_info;
    bool tracker_info_session_list_failed;

    bool delete_loading;
    QString active_delete_session_id;
    QString active_delete_session_name;

    bool export_loading;
    bool export_cancel_requested;
    bool export_failed;
    bool sensors_enumeration_done;
    bool export_hash_ready;
    QString active_session_id;
    QString active_session_remote_root;
    QString active_destination_path;
    QString export_hash_name;
    int current_download_index;
    uint32_t next_sensor_index;
    uint32_t export_transition_sequence;
    QList<ars_tracker_download_item_t> download_queue;

    bool resolve_session(const QString &session_id, ars_tracker_session_t *session) const;
    QString resolve_destination_path(const QString &raw_destination_path) const;
    QString build_tracker_export_name(QString *error_message) const;
    QString build_session_export_root_path(const QString &destination,
                                           const ars_tracker_session_t &session,
                                           QString *error_message) const;
    QString build_remote_file_path(const QString &remote_root, const QString &filename) const;
    QString build_local_final_file_path(const QString &destination, const QString &filename) const;
    QString build_local_temp_file_path(const QString &destination, const QString &filename) const;
    bool ensure_download_temp_file(ars_tracker_download_item_t *item, QString *error_message) const;
    QString choose_export_hash_type(const QList<hash_checksum_t> &supported_hashes,
                                    QString *error_message) const;
    bool compute_local_file_hash(const QString &file_path, const QString &hash_name,
                                 QByteArray *result, QString *error_message) const;

    void reset_tracker_info_state();
    ars_tracker_info_field_t *tracker_info_field_for_step(tracker_info_step_t step);
    tracker_info_step_t next_tracker_info_step(tracker_info_step_t step) const;
    QStringList tracker_info_command_arguments(tracker_info_step_t step) const;
    QString tracker_info_step_name(tracker_info_step_t step) const;
    void begin_tracker_info_step(tracker_info_step_t step);
    void finish_tracker_info_refresh(const QString &message);
    void set_tracker_info_field_error(tracker_info_step_t step, const QString &message);
    int tracker_info_error_count() const;

    void reset_export_state();
    void enqueue_fixed_files();
    void enqueue_next_sensor_candidate();
    void schedule_next_download_or_finish(const QString &reason);
    void schedule_current_file_download(const QString &reason);
    void request_next_download_or_finish();
    void start_file_download(ars_tracker_download_item_t *item);
    void finish_export(bool success, bool cancelled, const QString &message);
    void publish_export_file_rows();
    void publish_progress_text(const QString &current_file = QString());

    bool is_not_found_error(group_status status, const QString &error_message) const;
    bool is_empty_file_error(group_status status, const QString &error_message) const;
    bool finalize_downloaded_file(ars_tracker_download_item_t *item, QString *error_message);
    QString to_status_text(ars_tracker_download_status_t status) const;
#ifndef SKIPPLUGIN_LOGGER
    debug_logger* logger;
#endif
};

#endif // ARS_TRACKER_BACKEND_H
