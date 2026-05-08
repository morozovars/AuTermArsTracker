#ifndef ARS_TRACKERS_SESSION_DOWNLOAD_COORDINATOR_H
#define ARS_TRACKERS_SESSION_DOWNLOAD_COORDINATOR_H

#include <QObject>
#include <QString>

class ArsTrackersSessionDownloadCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit ArsTrackersSessionDownloadCoordinator(QObject *parent = nullptr);

    bool isActive() const;
    bool isCancelling() const;

    void beginSessionDownload(const QString &session_name, int tracker_jobs_count);
    void finishSessionDownload();

    void beginBulkDownload(int total_sessions);
    void updateBulkCounters(int success, int failed);
    int bulkTotalSessions() const;
    int bulkCompletedSessions() const;

signals:
    void logMessage(const QString &message);
    void statusMessage(const QString &message);
    void finished(const QString &summary);

public slots:
    void cancel();
    void clearCancel();

private:
    bool active = false;
    bool cancelling = false;
    QString currentSessionName;
    int currentTrackerJobsCount = 0;
    int bulkTotal = 0;
    int bulkSuccess = 0;
    int bulkFailed = 0;
};

#endif // ARS_TRACKERS_SESSION_DOWNLOAD_COORDINATOR_H
