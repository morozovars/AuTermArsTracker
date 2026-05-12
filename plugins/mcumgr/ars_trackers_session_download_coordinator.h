#ifndef ARS_TRACKERS_SESSION_DOWNLOAD_COORDINATOR_H
#define ARS_TRACKERS_SESSION_DOWNLOAD_COORDINATOR_H

#include <QObject>
#include <QString>
#include <QPointer>
#include <QHash>
#include <QStringList>
#include <QVector>
#include "smp_group_fs_mgmt.h"

class ars_tracker_backend;

struct ArsTrackersDownloadRouteInfo
{
    bool valid = false;
    bool hasContext = false;
    bool cancelRequested = false;
    QString contextId;
    QString port;
    QString serial;
    QString displayName;
    quint64 generation = 0;
    QString reason;
};

struct ArsTrackersDownloadFsOperationState
{
    bool active = false;
    int phase = 0;
    QString phaseName;
    quint64 sequence = 0;
    QString remoteFile;
    QString localTempFile;
    QByteArray hashChecksumResponse;
    uint32_t sizeResponse = 0;
    QList<hash_checksum_t> supportedHashChecksumList;
    QString operationName;
};

struct ArsTrackersTrackerDownloadProgress
{
    QString contextId;
    QString port;
    QString serial;
    QString displayName;
    QString currentSession;
    QString currentRemoteFile;
    quint64 bytesDone = 0;
    quint64 bytesTotal = 0;
    int percent = 0;
    QString stateText;
    QString errorText;
    bool terminal = false;
};

struct ArsTrackersParallelDownloadProgress
{
    bool active = false;
    bool cancelling = false;
    int totalContexts = 0;
    int runningContexts = 0;
    int finishedContexts = 0;
    int failedContexts = 0;
    int cancelledContexts = 0;
    int disconnectedContexts = 0;
    int aggregatePercent = 0;
    QVector<ArsTrackersTrackerDownloadProgress> trackers;
};

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
    void resetRoutingDiagnostics();
    quint64 currentGeneration() const;
    void registerLegacyActiveRoute(const QString &port,
                                   const QString &serial,
                                   const QString &displayName);
    void unregisterLegacyActiveRoute(const QString &reason);
    void markLegacyActiveRouteCancelRequested(const QString &reason);
    ArsTrackersDownloadRouteInfo resolveRouteForPort(const QString &port) const;
    ArsTrackersDownloadRouteInfo resolveRouteForBackend(const ars_tracker_backend *backend) const;
    bool hasLegacyActiveRoute() const;
    bool beginLegacyFsOperation(const QString &port,
                                int phase,
                                const QString &phaseName,
                                const QString &remoteFile,
                                const QString &localTempFile,
                                QString *errorMessage);
    bool hasLegacyFsOperationForPort(const QString &port) const;
    ArsTrackersDownloadFsOperationState legacyFsOperationForPort(const QString &port) const;
    ArsTrackersDownloadFsOperationState *legacyFsOperationMutableForPort(const QString &port);
    void resetLegacyFsOperation(const QString &port, const QString &reason);
    quint64 nextLegacyFsSequence();
    QStringList activePorts() const;
    void markContextSessionFinished(const QString &port,
                                    bool success,
                                    bool cancelled,
                                    const QString &errorMessage);
    bool hasPendingSessionForPort(const QString &port) const;
    QString currentSessionForPort(const QString &port) const;
    QString destinationDirForPort(const QString &port) const;
    int contextCount() const;
    int finishedContextCount() const;
    int failedContextCount() const;
    int cancelledContextCount() const;
    int disconnectedContextCount() const;
    int completedSessionCount() const;
    int totalSessionCount() const;
    void updateContextProgress(const QString &port,
                               const QString &currentSession,
                               const QString &currentRemoteFile,
                               quint64 bytesDone,
                               quint64 bytesTotal,
                               int percent,
                               const QString &stateText);
    void setContextTerminalState(const QString &port,
                                 const QString &stateText,
                                 const QString &errorText);
    ArsTrackersParallelDownloadProgress currentParallelDownloadProgress() const;

signals:
    void logMessage(const QString &message);
    void statusMessage(const QString &message);
    void finished(const QString &summary);
    void parallelDownloadProgressChanged();

public slots:
    void cancel();
    void clearCancel();

private:
    struct ParallelContextState
    {
        QString contextId;
        QString port;
        QString serial;
        QString displayName;
        QString destinationDir;
        QStringList sessionQueue;
        int currentSessionIndex = -1;
        bool finished = false;
        bool failed = false;
        bool cancelled = false;
        bool disconnected = false;
        QString errorText;
        int completedSessions = 0;
        int failedSessions = 0;
        QString currentSession;
        QString currentRemoteFile;
        quint64 bytesDone = 0;
        quint64 bytesTotal = 0;
        int percent = 0;
        QString stateText = "Pending";
    };

    bool active = false;
    bool cancelling = false;
    QString currentSessionName;
    int currentTrackerJobsCount = 0;
    int bulkTotal = 0;
    int bulkSuccess = 0;
    int bulkFailed = 0;
    ArsTrackersDownloadRouteInfo legacyRoute;
    quint64 routeGeneration = 0;
    ArsTrackersDownloadFsOperationState legacyFsOperation;
    quint64 legacyFsSequence = 0;
    QPointer<ars_tracker_backend> legacyBackend;
    QHash<QString, ArsTrackersDownloadRouteInfo> routesByPort;
    QHash<QString, ArsTrackersDownloadFsOperationState> fsByPort;
    QHash<QString, QPointer<ars_tracker_backend>> backendsByPort;
    QHash<const ars_tracker_backend*, QString> backendToPort;
    QHash<QString, ParallelContextState> contextsByPort;
    int totalSessionsPlanned = 0;
    int totalSessionsCompleted = 0;
    int contextsFinished = 0;
    int contextsFailed = 0;
    int contextsCancelled = 0;
    int contextsDisconnected = 0;

public:
    ars_tracker_backend *activeLegacyBackend() const;
    ars_tracker_backend *activeLegacyBackendForPort(const QString &port) const;
    bool createLegacyBackendForActiveRoute(QString *errorMessage);
    void destroyLegacyBackendForActiveRoute(const QString &reason);
};

#endif // ARS_TRACKERS_SESSION_DOWNLOAD_COORDINATOR_H
