#include "ars_tracker_backend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "ars_tracker_parser.h"
#include "debug_logger.h"

static const QStringList ars_tracker_fixed_files =
    QStringList() << "trace.csv" << "processedStr.csv" << "battery.csv";

ars_tracker_backend::ars_tracker_backend(QObject* parent) : QObject(parent)
{
    loading = false;
    reset_export_state();
#ifndef SKIPPLUGIN_LOGGER
    logger = nullptr;
#endif
}

ars_tracker_backend::~ars_tracker_backend()
{
}

void ars_tracker_backend::reset_export_state()
{
    export_loading           = false;
    export_cancel_requested  = false;
    export_failed            = false;
    sensors_enumeration_done = false;
    active_session_id.clear();
    active_session_remote_root.clear();
    active_destination_path.clear();
    current_download_index = -1;
    next_sensor_index      = 0;
    download_queue.clear();
}

bool ars_tracker_backend::begin_session_list_request(QString* error_message)
{
    if (loading == true)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Session list request is already in progress.");
        }

        return false;
    }

    loading = true;
    emit loading_changed(loading);
    emit status_message(QString("Loading sessions..."));

    return true;
}

void ars_tracker_backend::handle_session_list_response(group_status   status,
                                                       const QString& shell_output,
                                                       int32_t        shell_ret)
{
    loading = false;
    emit loading_changed(loading);

    if (status == STATUS_COMPLETE)
    {
        if (shell_ret != 0)
        {
            emit status_message(QString("Session list command failed, shell ret: %1")
                                    .arg(QString::number(shell_ret)));
            return;
        }

        QString                      parse_error;
        QList<ars_tracker_session_t> parsed_sessions;

        if (ars_tracker_parser::parse_meas_ls_output(shell_output, &parsed_sessions,
                                                     &parse_error) == false)
        {
            latest_sessions.clear();
            emit session_list_ready(latest_sessions);
            emit status_message(parse_error);
            return;
        }

        latest_sessions = parsed_sessions;
        emit session_list_ready(latest_sessions);
        emit status_message(
            QString("Loaded %1 sessions.").arg(QString::number(latest_sessions.length())));
        return;
    }

    if (status == STATUS_TIMEOUT)
    {
        emit status_message(QString("Session list request timed out."));
    } else if (status == STATUS_CANCELLED)
    {
        emit status_message(QString("Session list request cancelled."));
    } else if (status == STATUS_PROCESSOR_TRANSPORT_ERROR)
    {
        emit status_message(QString("Session list failed due to transport error."));
    } else if (status == STATUS_TRANSPORT_DISCONNECTED)
    {
        emit status_message(QString("Session list failed: transport disconnected."));
    } else
    {
        emit status_message(shell_output.isEmpty() ? QString("Session list request failed.") :
                                                     shell_output);
    }
}

const QList<ars_tracker_session_t>& ars_tracker_backend::sessions() const
{
    return latest_sessions;
}

bool ars_tracker_backend::resolve_session(const QString&         session_id,
                                          ars_tracker_session_t* session) const
{
    for (const ars_tracker_session_t& existing_session : latest_sessions)
    {
        if (existing_session.id == session_id)
        {
            if (session != nullptr)
            {
                *session = existing_session;
            }

            return true;
        }
    }

    return false;
}

QString ars_tracker_backend::build_remote_file_path(const QString& remote_root,
                                                    const QString& filename) const
{
    if (remote_root.isEmpty())
    {
        return filename;
    }

    QString fixed_root = remote_root;

    while (fixed_root.endsWith('/'))
    {
        fixed_root.chop(1);
    }

    return "/NAND:/" + fixed_root + "/" + filename;
}

QString ars_tracker_backend::build_local_final_file_path(const QString& destination,
                                                         const QString& filename) const
{
    return QDir(destination).filePath(filename);
}

QString ars_tracker_backend::build_local_temp_file_path(const QString& destination,
                                                        const QString& filename) const
{
    return QDir(destination).filePath(QString(".%1.part").arg(filename));
}

void ars_tracker_backend::enqueue_fixed_files()
{
    for (const QString& file_name : ars_tracker_fixed_files)
    {
        ars_tracker_download_item_t item;
        item.remote_file     = build_remote_file_path(active_session_remote_root, file_name);
        item.local_file      = build_local_final_file_path(active_destination_path, file_name);
        item.local_temp_file = build_local_temp_file_path(active_destination_path, file_name);
        item.bytes_completed = 0;
        item.total_bytes     = 0;
        item.retry_count     = 0;
        item.category        = ARS_TRACKER_FILE_FIXED;
        item.status          = ARS_TRACKER_STATUS_PENDING;

        download_queue.append(item);
    }
}

void ars_tracker_backend::enqueue_next_sensor_candidate()
{
    if (sensors_enumeration_done == true)
    {
        return;
    }

    QString sensor_filename = QString("sensors_%1.bin").arg(QString::number(next_sensor_index));

    ars_tracker_download_item_t item;
    item.remote_file     = build_remote_file_path(active_session_remote_root, sensor_filename);
    item.local_file      = build_local_final_file_path(active_destination_path, sensor_filename);
    item.local_temp_file = build_local_temp_file_path(active_destination_path, sensor_filename);
    item.bytes_completed = 0;
    item.total_bytes     = 0;
    item.retry_count     = 0;
    item.category        = ARS_TRACKER_FILE_SENSOR;
    item.status          = ARS_TRACKER_STATUS_PENDING;

    ++next_sensor_index;
    download_queue.append(item);
}

bool ars_tracker_backend::begin_session_export(const QString& session_id,
                                               const QString& destination_path,
                                               QString*       error_message)
{
    if (export_loading == true)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("A session export is already in progress.");
        }

        return false;
    }

    if (session_id.isEmpty())
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Select a session first.");
        }

        return false;
    }

    if (destination_path.isEmpty())
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Choose a destination folder first.");
        }

        return false;
    }

    QFileInfo destination_info(destination_path);

    if (!destination_info.exists() || !destination_info.isDir())
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Destination folder does not exist.");
        }

        return false;
    }

    ars_tracker_session_t session;

    if (resolve_session(session_id, &session) == false)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Selected session is no longer available.");
        }

        return false;
    }

    reset_export_state();

    export_loading             = true;
    active_session_id          = session.id;
    active_session_remote_root = session.remote_path_or_name;
    active_destination_path    = destination_path;

    enqueue_fixed_files();
    enqueue_next_sensor_candidate();

    emit export_loading_changed(true);
    publish_export_file_rows();
    publish_progress_text();
    emit status_message(QString("Starting session export for '%1'.").arg(session.display_name));

    request_next_download_or_finish();
    return true;
}

bool ars_tracker_backend::is_not_found_error(group_status   status,
                                             const QString& error_message) const
{
    if (status == STATUS_COMPLETE)
    {
        return false;
    }

    QString lowered = error_message.toLower();

    return lowered.contains("does not exist") || lowered.contains("not found") ||
           lowered.contains("no such file") || lowered.contains("no such file/entry") ||
           lowered.contains("enoent");
}

bool ars_tracker_backend::finalize_downloaded_file(ars_tracker_download_item_t* item,
                                                   QString*                     error_message)
{
    QFile temp_file(item->local_temp_file);

    if (!temp_file.exists())
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Downloaded temp file is missing.");
        }

        return false;
    }

    QFile::remove(item->local_file);

    if (QFile::rename(item->local_temp_file, item->local_file))
    {
        return true;
    }

    if (QFile::copy(item->local_temp_file, item->local_file) == false)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Could not finalise %1 in destination folder.")
                                 .arg(QFileInfo(item->local_file).fileName());
        }

        return false;
    }

    QFile::remove(item->local_temp_file);
    return true;
}

QString ars_tracker_backend::to_status_text(ars_tracker_download_status_t status) const
{
    switch (status)
    {
    case ARS_TRACKER_STATUS_PENDING:
        return "Pending";
    case ARS_TRACKER_STATUS_DOWNLOADING:
        return "Downloading";
    case ARS_TRACKER_STATUS_DOWNLOADED:
        return "Downloaded";
    case ARS_TRACKER_STATUS_MISSING:
        return "Missing (skipped)";
    case ARS_TRACKER_STATUS_SENSORS_END:
        return "Sensors sequence complete";
    case ARS_TRACKER_STATUS_FAILED:
        return "Failed";
    case ARS_TRACKER_STATUS_CANCELLED:
        return "Cancelled";
    }

    return "Unknown";
}

void ars_tracker_backend::publish_export_file_rows()
{
    QStringList rows;

    for (const ars_tracker_download_item_t& item : download_queue)
    {
        QString row = QString("%1 — %2").arg(QFileInfo(item.remote_file).fileName(),
                                             to_status_text(item.status));

        if (item.error_text.isEmpty() == false)
        {
            row.append(QString(" (%1)").arg(item.error_text));
        }

        rows.append(row);
    }

    emit export_file_list_changed(rows);
}

void ars_tracker_backend::publish_progress_text(const QString& current_file)
{
    uint32_t finished_count = 0;

    for (const ars_tracker_download_item_t& item : download_queue)
    {
        if (item.status == ARS_TRACKER_STATUS_DOWNLOADED ||
            item.status == ARS_TRACKER_STATUS_MISSING ||
            item.status == ARS_TRACKER_STATUS_SENSORS_END ||
            item.status == ARS_TRACKER_STATUS_FAILED || item.status == ARS_TRACKER_STATUS_CANCELLED)
        {
            ++finished_count;
        }
    }

    QString progress =
        QString("Files finished: %1/%2")
            .arg(QString::number(finished_count), QString::number(download_queue.length()));

    if (current_file.isEmpty() == false)
    {
        progress.append(QString(" | Current: %1").arg(QFileInfo(current_file).fileName()));
    }

    emit export_progress_changed(progress);
}

void ars_tracker_backend::request_next_download_or_finish()
{
    if (export_loading == false)
    {
        return;
    }

    if (export_cancel_requested == true)
    {
        finish_export(false, true, "Session export cancelled.");
        return;
    }

    for (int i = 0; i < download_queue.length(); ++i)
    {
        if (download_queue.at(i).status == ARS_TRACKER_STATUS_PENDING)
        {
            current_download_index            = i;
            ars_tracker_download_item_t& item = download_queue[i];

            item.status = ARS_TRACKER_STATUS_DOWNLOADING;
            item.error_text.clear();
            item.bytes_completed = 0;
            item.total_bytes     = 0;

            QFile::remove(item.local_temp_file);
            publish_export_file_rows();
            publish_progress_text(item.remote_file);
            emit status_message(
                QString("Downloading %1...").arg(QFileInfo(item.remote_file).fileName()));
            emit request_file_download(item.remote_file, item.local_temp_file);
            return;
        }
    }

    if (export_failed == true)
    {
        finish_export(false, false, "Session export failed.");
    } else
    {
        finish_export(true, false, "Session export completed.");
    }
}

void ars_tracker_backend::finish_export(bool success, bool cancelled, const QString& message)
{
    if (export_loading == false)
    {
        return;
    }

    export_loading = false;
    emit export_loading_changed(false);
    publish_export_file_rows();
    publish_progress_text();
    emit status_message(message);
    emit export_finished(success, cancelled, message);
}

void ars_tracker_backend::handle_file_download_progress(uint8_t percent)
{
    if (export_loading == false || current_download_index < 0 ||
        current_download_index >= download_queue.length())
    {
        return;
    }

    ars_tracker_download_item_t& item = download_queue[current_download_index];
    item.bytes_completed              = percent;
    item.total_bytes                  = 100;

    publish_progress_text(item.remote_file + QString(" (%1%)").arg(QString::number(percent)));
}

void ars_tracker_backend::handle_file_download_result(group_status   status,
                                                      const QString& error_message)
{
    if (export_loading == false || current_download_index < 0 ||
        current_download_index >= download_queue.length())
    {
        return;
    }

    ars_tracker_download_item_t& item = download_queue[current_download_index];

    if (status == STATUS_COMPLETE)
    {
        QString finalise_error;

        if (finalize_downloaded_file(&item, &finalise_error) == false)
        {
            item.status     = ARS_TRACKER_STATUS_FAILED;
            item.error_text = finalise_error;
            export_failed   = true;
            publish_export_file_rows();
            finish_export(false, false, finalise_error);
            return;
        }

        item.status = ARS_TRACKER_STATUS_DOWNLOADED;
        item.error_text.clear();

        if (item.category == ARS_TRACKER_FILE_SENSOR)
        {
            enqueue_next_sensor_candidate();
        }

        publish_export_file_rows();
        request_next_download_or_finish();
        return;
    }

    if (status == STATUS_CANCELLED || export_cancel_requested == true)
    {
        item.status     = ARS_TRACKER_STATUS_CANCELLED;
        item.error_text = "Cancelled";
        publish_export_file_rows();
        finish_export(false, true, "Session export cancelled.");
        return;
    }

    if (is_not_found_error(status, error_message))
    {
        if (item.category == ARS_TRACKER_FILE_SENSOR)
        {
            item.status              = ARS_TRACKER_STATUS_SENSORS_END;
            item.error_text          = "No more sensor files";
            sensors_enumeration_done = true;
            publish_export_file_rows();
            request_next_download_or_finish();
            return;
        }

        item.status     = ARS_TRACKER_STATUS_MISSING;
        item.error_text = "Remote file missing";
        publish_export_file_rows();
        request_next_download_or_finish();
        return;
    }

    if (status == STATUS_TRANSPORT_DISCONNECTED)
    {
        item.status     = ARS_TRACKER_STATUS_FAILED;
        item.error_text = "Transport disconnected";
    } else if (status == STATUS_PROCESSOR_TRANSPORT_ERROR)
    {
        item.status     = ARS_TRACKER_STATUS_FAILED;
        item.error_text = "Transport send failed";
    } else if (status == STATUS_TIMEOUT)
    {
        item.status     = ARS_TRACKER_STATUS_FAILED;
        item.error_text = "Transfer timed out";
    } else
    {
        item.status     = ARS_TRACKER_STATUS_FAILED;
        item.error_text = error_message.isEmpty() ? "Transfer failed" : error_message;
    }

    export_failed = true;
    publish_export_file_rows();
    finish_export(false, false,
                  QString("Session export failed while downloading %1.")
                      .arg(QFileInfo(item.remote_file).fileName()));
}

bool ars_tracker_backend::export_in_progress() const
{
    return export_loading;
}

#ifndef SKIPPLUGIN_LOGGER
void ars_tracker_backend::set_logger(debug_logger* object)
{
    logger = object;
}
#endif

void ars_tracker_backend::cancel_all()
{
    if (export_loading == false)
    {
        emit status_message("No session export is currently running.");
        return;
    }

    export_cancel_requested = true;
    emit status_message("Cancelling session export...");
    emit request_cancel_file_download();
}
