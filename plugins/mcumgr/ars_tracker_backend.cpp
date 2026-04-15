#include "ars_tracker_backend.h"

#include "ars_tracker_parser.h"
#include "debug_logger.h"

ars_tracker_backend::ars_tracker_backend(QObject *parent) :
    QObject(parent)
{
    loading = false;
}

ars_tracker_backend::~ars_tracker_backend()
{
}

bool ars_tracker_backend::begin_session_list_request(QString *error_message)
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

void ars_tracker_backend::handle_session_list_response(group_status status, const QString &shell_output, int32_t shell_ret)
{
    log_debug() << "[ArsTracker] Raw meas ls output:" << shell_output;

    loading = false;
    emit loading_changed(loading);

    if (status == STATUS_COMPLETE)
    {
        if (shell_ret != 0)
        {
            emit status_message(QString("Session list command failed, shell ret: %1").arg(QString::number(shell_ret)));
            return;
        }

        QString parse_error;
        QList<ars_tracker_session_t> parsed_sessions;

        if (ars_tracker_parser::parse_meas_ls_output(shell_output, &parsed_sessions, &parse_error) == false)
        {
            latest_sessions.clear();
            emit session_list_ready(latest_sessions);
            emit status_message(parse_error);
            return;
        }

        latest_sessions = parsed_sessions;
        log_debug() << "[ArsTracker] Parsed sessions:" << latest_sessions.length();

        emit session_list_ready(latest_sessions);
        emit status_message(QString("Loaded %1 sessions.").arg(QString::number(latest_sessions.length())));
        return;
    }

    if (status == STATUS_TIMEOUT)
    {
        emit status_message(QString("Session list request timed out."));
    }
    else if (status == STATUS_CANCELLED)
    {
        emit status_message(QString("Session list request cancelled."));
    }
    else if (status == STATUS_PROCESSOR_TRANSPORT_ERROR)
    {
        emit status_message(QString("Session list failed due to transport error."));
    }
    else if (status == STATUS_TRANSPORT_DISCONNECTED)
    {
        emit status_message(QString("Session list failed: transport disconnected."));
    }
    else
    {
        emit status_message(shell_output.isEmpty() ? QString("Session list request failed.") : shell_output);
    }
}

const QList<ars_tracker_session_t> &ars_tracker_backend::sessions() const
{
    return latest_sessions;
}

#ifndef SKIPPLUGIN_LOGGER
void ars_tracker_backend::set_logger(debug_logger* object)
{
    logger = object;
}
#endif

void ars_tracker_backend::queue_session_download(const QString &session_id, const QString &destination_path)
{
    Q_UNUSED(session_id);
    Q_UNUSED(destination_path);
void ars_tracker_backend::queue_session_download(const QString &session_id, const QString &destination_path)
{
    Q_UNUSED(session_id);
    Q_UNUSED(destination_path);

    // TODO(AuTerm-ArsTracker): Build download queue for sensors_<i>.bin, trace.csv,
    // processedStr.csv, and battery.csv using protocol-defined file APIs.
    // TODO(AuTerm-ArsTracker): Persist resume state per file/chunk and continue from
    // the last confirmed chunk after retry/reconnect.
    emit status_message("TODO: ArsTracker session download queue is not implemented yet.");
}

void ars_tracker_backend::cancel_all()
{
    // TODO(AuTerm-ArsTracker): Cancel in-flight tracker command/file transfers and
    // keep any persisted resume state for future retries.
    emit status_message("TODO: ArsTracker cancellation is not implemented yet.");
}

void ars_tracker_backend::cancel_all()
{
    // TODO(AuTerm-ArsTracker): Cancel in-flight tracker command/file transfers and
    // keep any persisted resume state for future retries.
    emit status_message("TODO: ArsTracker cancellation is not implemented yet.");
}
