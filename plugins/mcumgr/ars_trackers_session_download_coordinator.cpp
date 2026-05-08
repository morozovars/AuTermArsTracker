#include "ars_trackers_session_download_coordinator.h"

ArsTrackersSessionDownloadCoordinator::ArsTrackersSessionDownloadCoordinator(QObject *parent)
    : QObject(parent)
{
}

bool ArsTrackersSessionDownloadCoordinator::isActive() const
{
    return active;
}

bool ArsTrackersSessionDownloadCoordinator::isCancelling() const
{
    return cancelling;
}

void ArsTrackersSessionDownloadCoordinator::beginSessionDownload(const QString &session_name,
                                                                 int tracker_jobs_count)
{
    active = true;
    cancelling = false;
    currentSessionName = session_name;
    currentTrackerJobsCount = tracker_jobs_count;
    emit logMessage(QString("trackers_download_coordinator_begin session=%1 jobs=%2")
                        .arg(session_name)
                        .arg(tracker_jobs_count));
}

void ArsTrackersSessionDownloadCoordinator::finishSessionDownload()
{
    active = false;
    cancelling = false;
    currentSessionName.clear();
    currentTrackerJobsCount = 0;
    emit logMessage("trackers_download_coordinator_finish");
}

void ArsTrackersSessionDownloadCoordinator::beginBulkDownload(int total_sessions)
{
    bulkTotal = total_sessions;
    bulkSuccess = 0;
    bulkFailed = 0;
    emit logMessage(QString("trackers_download_coordinator_bulk_begin total=%1")
                        .arg(total_sessions));
}

void ArsTrackersSessionDownloadCoordinator::updateBulkCounters(int success, int failed)
{
    bulkSuccess = success;
    bulkFailed = failed;
}

int ArsTrackersSessionDownloadCoordinator::bulkTotalSessions() const
{
    return bulkTotal;
}

int ArsTrackersSessionDownloadCoordinator::bulkCompletedSessions() const
{
    return bulkSuccess + bulkFailed;
}

void ArsTrackersSessionDownloadCoordinator::cancel()
{
    if (active == false)
    {
        emit logMessage("trackers_download_coordinator_cancel_ignored reason=inactive");
        return;
    }
    if (cancelling)
    {
        emit logMessage("trackers_download_coordinator_cancel_ignored reason=already-cancelling");
        return;
    }

    cancelling = true;
    emit logMessage("trackers_download_coordinator_cancel_requested");
    emit statusMessage("Cancelling download...");
}

void ArsTrackersSessionDownloadCoordinator::clearCancel()
{
    cancelling = false;
}
