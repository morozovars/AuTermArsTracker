#include "ars_trackers_session_download_coordinator.h"
#include "ars_tracker_backend.h"

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
    emit logMessage(QString("TRACKERS_PARALLEL_DOWNLOAD_START operationId=%1 contexts=%2 mode=single-session")
                        .arg(QString::number(routeGeneration + 1), QString::number(tracker_jobs_count)));
    emit parallelDownloadProgressChanged();
}

void ArsTrackersSessionDownloadCoordinator::finishSessionDownload()
{
    active = false;
    cancelling = false;
    currentSessionName.clear();
    currentTrackerJobsCount = 0;
    emit logMessage(QString("TRACKERS_PARALLEL_DOWNLOAD_FINISHED operationId=%1 finished=%2 failed=%3 cancelled=%4 disconnected=%5 sessionsCompleted=%6 sessionsTotal=%7")
                        .arg(QString::number(routeGeneration),
                             QString::number(contextsFinished),
                             QString::number(contextsFailed),
                             QString::number(contextsCancelled),
                             QString::number(contextsDisconnected),
                             QString::number(totalSessionsCompleted),
                             QString::number(totalSessionsPlanned)));
    for (auto it = backendsByPort.begin(); it != backendsByPort.end(); ++it)
    {
        if (!it.value().isNull())
        {
            it.value()->deleteLater();
        }
    }
    backendsByPort.clear();
    backendToPort.clear();
    routesByPort.clear();
    fsByPort.clear();
    legacyRoute = ArsTrackersDownloadRouteInfo();
    legacyFsOperation = ArsTrackersDownloadFsOperationState();
    legacyBackend = nullptr;
    emit parallelDownloadProgressChanged();
}

void ArsTrackersSessionDownloadCoordinator::beginBulkDownload(int total_sessions)
{
    bulkTotal = total_sessions;
    bulkSuccess = 0;
    bulkFailed = 0;
    emit logMessage(QString("TRACKERS_PARALLEL_DOWNLOAD_START operationId=%1 contexts=%2 mode=download-all")
                        .arg(QString::number(routeGeneration + 1), QString::number(contextsByPort.size())));
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

void ArsTrackersSessionDownloadCoordinator::resetRoutingDiagnostics()
{
    for (auto it = backendsByPort.begin(); it != backendsByPort.end(); ++it)
    {
        if (!it.value().isNull())
        {
            it.value()->deleteLater();
        }
    }
    legacyRoute = ArsTrackersDownloadRouteInfo();
    legacyFsOperation = ArsTrackersDownloadFsOperationState();
    legacyFsSequence = 0;
    routesByPort.clear();
    fsByPort.clear();
    backendsByPort.clear();
    backendToPort.clear();
    contextsByPort.clear();
    totalSessionsPlanned = 0;
    totalSessionsCompleted = 0;
    contextsFinished = 0;
    contextsFailed = 0;
    contextsCancelled = 0;
    contextsDisconnected = 0;
    routeGeneration = 0;
    emit logMessage("trackers_download_route_reset");
}

quint64 ArsTrackersSessionDownloadCoordinator::currentGeneration() const
{
    return routeGeneration;
}

void ArsTrackersSessionDownloadCoordinator::registerLegacyActiveRoute(const QString &port,
                                                                      const QString &serial,
                                                                      const QString &displayName)
{
    const QString trimmedPort = port.trimmed();
    if (trimmedPort.isEmpty())
    {
        emit logMessage("TRACKERS_PARALLEL_CONTEXT_CREATE_IGNORED reason=empty-port");
        return;
    }

    ++routeGeneration;

    ArsTrackersDownloadRouteInfo route;
    route.valid = true;
    route.hasContext = true;
    route.cancelRequested = false;
    route.contextId = QString("trackers-parallel-%1-%2").arg(QString::number(routeGeneration), trimmedPort);
    route.port = trimmedPort;
    route.serial = serial.trimmed();
    route.displayName = displayName.trimmed();
    route.generation = routeGeneration;
    route.reason = "registered";

    routesByPort.insert(trimmedPort, route);
    legacyRoute = route;

    ParallelContextState context;
    context.contextId = route.contextId;
    context.port = route.port;
    context.serial = route.serial;
    context.displayName = route.displayName;
    if (contextsByPort.contains(trimmedPort))
    {
        context = contextsByPort.value(trimmedPort);
        context.contextId = route.contextId;
        context.serial = route.serial;
        context.displayName = route.displayName;
    }
    contextsByPort.insert(trimmedPort, context);

    emit logMessage(QString("TRACKERS_PARALLEL_CONTEXT_CREATE contextId=%1 port=%2 display=%3 sessions=%4")
                        .arg(route.contextId,
                             route.port,
                             route.displayName,
                             QString::number(context.sessionQueue.size())));
    emit parallelDownloadProgressChanged();
}

void ArsTrackersSessionDownloadCoordinator::unregisterLegacyActiveRoute(const QString &reason)
{
    const QString unregisterReason = reason.trimmed().isEmpty() ? QString("unspecified") : reason.trimmed();
    if (!legacyRoute.valid)
    {
        emit logMessage(QString("trackers_download_route_unregister_ignored reason=no-active-route request=%1")
                            .arg(unregisterReason));
        return;
    }

    const QString port = legacyRoute.port;
    emit logMessage(QString("trackers_download_route_unregister contextId=%1 generation=%2 port=%3 reason=%4")
                        .arg(legacyRoute.contextId,
                             QString::number(legacyRoute.generation),
                             port,
                             unregisterReason));

    destroyLegacyBackendForActiveRoute(QString("route-unregister:%1").arg(unregisterReason));
    routesByPort.remove(port);
    fsByPort.remove(port);
    contextsByPort.remove(port);

    if (routesByPort.isEmpty())
    {
        legacyRoute = ArsTrackersDownloadRouteInfo();
        legacyFsOperation = ArsTrackersDownloadFsOperationState();
    }
    emit parallelDownloadProgressChanged();
}

void ArsTrackersSessionDownloadCoordinator::markLegacyActiveRouteCancelRequested(const QString &reason)
{
    const QString markReason = reason.trimmed().isEmpty() ? QString("unspecified") : reason.trimmed();
    if (routesByPort.isEmpty())
    {
        emit logMessage(QString("trackers_download_route_cancel_requested_no_active_route reason=%1")
                            .arg(markReason));
        return;
    }

    cancelling = true;
    for (auto it = routesByPort.begin(); it != routesByPort.end(); ++it)
    {
        it.value().cancelRequested = true;
        emit logMessage(QString("trackers_download_route_cancel_requested contextId=%1 generation=%2 port=%3 reason=%4")
                            .arg(it.value().contextId,
                                 QString::number(it.value().generation),
                                 it.value().port,
                                 markReason));
    }
}

ArsTrackersDownloadRouteInfo ArsTrackersSessionDownloadCoordinator::resolveRouteForPort(const QString &port) const
{
    const QString trimmedPort = port.trimmed();
    if (trimmedPort.isEmpty())
    {
        ArsTrackersDownloadRouteInfo route;
        route.reason = "empty-port";
        return route;
    }

    if (routesByPort.contains(trimmedPort))
    {
        ArsTrackersDownloadRouteInfo route = routesByPort.value(trimmedPort);
        route.reason = "matched-active-route";
        return route;
    }

    ArsTrackersDownloadRouteInfo route;
    route.reason = "no-active-route";
    return route;
}

ArsTrackersDownloadRouteInfo ArsTrackersSessionDownloadCoordinator::resolveRouteForBackend(const ars_tracker_backend *backend) const
{
    if (backend == nullptr || !backendToPort.contains(backend))
    {
        ArsTrackersDownloadRouteInfo route;
        route.reason = "backend-not-mapped";
        return route;
    }
    return resolveRouteForPort(backendToPort.value(backend));
}

bool ArsTrackersSessionDownloadCoordinator::hasLegacyActiveRoute() const
{
    return !routesByPort.isEmpty();
}

bool ArsTrackersSessionDownloadCoordinator::beginLegacyFsOperation(const QString &port,
                                                                   int phase,
                                                                   const QString &phaseName,
                                                                   const QString &remoteFile,
                                                                   const QString &localTempFile,
                                                                   QString *errorMessage)
{
    ArsTrackersDownloadRouteInfo route = resolveRouteForPort(port);
    if (!route.valid)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "No active route for port.";
        }
        return false;
    }

    ArsTrackersDownloadFsOperationState fs;
    fs.active = true;
    fs.phase = phase;
    fs.phaseName = phaseName;
    fs.sequence = ++legacyFsSequence;
    fs.remoteFile = remoteFile;
    fs.localTempFile = localTempFile;
    fs.hashChecksumResponse.clear();
    fs.sizeResponse = 0;
    fs.supportedHashChecksumList.clear();
    fs.operationName = QString("%1:%2").arg(phaseName, remoteFile);

    fsByPort.insert(route.port, fs);
    if (route.port.compare(legacyRoute.port, Qt::CaseInsensitive) == 0)
    {
        legacyFsOperation = fs;
    }
    return true;
}

bool ArsTrackersSessionDownloadCoordinator::hasLegacyFsOperationForPort(const QString &port) const
{
    const QString trimmedPort = port.trimmed();
    return fsByPort.contains(trimmedPort) && fsByPort.value(trimmedPort).active;
}

ArsTrackersDownloadFsOperationState ArsTrackersSessionDownloadCoordinator::legacyFsOperationForPort(const QString &port) const
{
    const QString trimmedPort = port.trimmed();
    if (!fsByPort.contains(trimmedPort))
    {
        return ArsTrackersDownloadFsOperationState();
    }
    return fsByPort.value(trimmedPort);
}

ArsTrackersDownloadFsOperationState *ArsTrackersSessionDownloadCoordinator::legacyFsOperationMutableForPort(const QString &port)
{
    const QString trimmedPort = port.trimmed();
    if (!fsByPort.contains(trimmedPort))
    {
        return nullptr;
    }
    return &fsByPort[trimmedPort];
}

void ArsTrackersSessionDownloadCoordinator::resetLegacyFsOperation(const QString &port, const QString &reason)
{
    const QString trimmedPort = port.trimmed();
    if (!fsByPort.contains(trimmedPort))
    {
        emit logMessage(QString("TRACKERS_PARALLEL_FS_STALE_CALLBACK port=%1 reason=%2")
                            .arg(trimmedPort, reason));
        return;
    }

    ArsTrackersDownloadRouteInfo route = resolveRouteForPort(trimmedPort);
    ArsTrackersDownloadFsOperationState fs = fsByPort.value(trimmedPort);
    emit logMessage(QString("TRACKERS_PARALLEL_FS_STATUS contextId=%1 port=%2 phase=%3 status=done reason=%4 remote=%5")
                        .arg(route.contextId,
                             trimmedPort,
                             fs.phaseName,
                             reason,
                             fs.remoteFile));
    fsByPort.remove(trimmedPort);
    if (trimmedPort.compare(legacyRoute.port, Qt::CaseInsensitive) == 0)
    {
        legacyFsOperation = ArsTrackersDownloadFsOperationState();
    }
}

quint64 ArsTrackersSessionDownloadCoordinator::nextLegacyFsSequence()
{
    return legacyFsSequence + 1;
}

QStringList ArsTrackersSessionDownloadCoordinator::activePorts() const
{
    return contextsByPort.keys();
}

void ArsTrackersSessionDownloadCoordinator::markContextSessionFinished(const QString &port,
                                                                       bool success,
                                                                       bool cancelled,
                                                                       const QString &errorMessage)
{
    const QString trimmedPort = port.trimmed();
    if (!contextsByPort.contains(trimmedPort))
    {
        return;
    }

    ParallelContextState &ctx = contextsByPort[trimmedPort];
    if (ctx.finished)
    {
        emit logMessage(QString("TRACKERS_PARALLEL_LATE_CALLBACK_IGNORED contextId=%1 port=%2")
                            .arg(ctx.contextId, trimmedPort));
        return;
    }
    const QString finishedSession = (ctx.currentSessionIndex >= 0 && ctx.currentSessionIndex < ctx.sessionQueue.size())
                                        ? ctx.sessionQueue.at(ctx.currentSessionIndex)
                                        : QString();

    if (success && !cancelled)
    {
        ++ctx.completedSessions;
        ++totalSessionsCompleted;
    }
    else
    {
        ++ctx.failedSessions;
        ctx.errorText = errorMessage;
    }

    emit logMessage(QString("TRACKERS_PARALLEL_CONTEXT_SESSION_FINISHED contextId=%1 session=%2 success=%3 cancelled=%4")
                        .arg(ctx.contextId,
                             finishedSession,
                             success ? QString("true") : QString("false"),
                             cancelled ? QString("true") : QString("false")));

    if (cancelled || cancelling)
    {
        ctx.cancelled = true;
        ctx.finished = true;
    }
    else if (!success)
    {
        ctx.failed = true;
        ctx.finished = true;
    }
    else if (ctx.currentSessionIndex + 1 >= ctx.sessionQueue.size())
    {
        ctx.finished = true;
    }
    else
    {
        ++ctx.currentSessionIndex;
    }

    if (ctx.finished)
    {
        ++contextsFinished;
        if (ctx.cancelled)
        {
            ++contextsCancelled;
        }
        else if (ctx.disconnected)
        {
            ++contextsDisconnected;
        }
        else if (ctx.failed)
        {
            ++contextsFailed;
        }

        QString state = "Finished";
        if (ctx.cancelled) state = "Cancelled";
        else if (ctx.disconnected) state = "Disconnected";
        else if (ctx.failed) state = "Failed";

        emit logMessage(QString("TRACKERS_PARALLEL_CONTEXT_FINISHED contextId=%1 state=%2")
                            .arg(ctx.contextId, state));
        ctx.stateText = state;
    }
    finishOperationIfTerminal("context-session-finished");
    emit parallelDownloadProgressChanged();
}

bool ArsTrackersSessionDownloadCoordinator::hasPendingSessionForPort(const QString &port) const
{
    const QString trimmedPort = port.trimmed();
    if (!contextsByPort.contains(trimmedPort))
    {
        return false;
    }
    const ParallelContextState &ctx = contextsByPort.value(trimmedPort);
    return !ctx.finished && (ctx.currentSessionIndex >= 0) && (ctx.currentSessionIndex < ctx.sessionQueue.size());
}

QString ArsTrackersSessionDownloadCoordinator::currentSessionForPort(const QString &port) const
{
    const QString trimmedPort = port.trimmed();
    if (!contextsByPort.contains(trimmedPort))
    {
        return QString();
    }
    const ParallelContextState &ctx = contextsByPort.value(trimmedPort);
    if (ctx.currentSessionIndex < 0 || ctx.currentSessionIndex >= ctx.sessionQueue.size())
    {
        return QString();
    }
    return ctx.sessionQueue.at(ctx.currentSessionIndex);
}

QString ArsTrackersSessionDownloadCoordinator::destinationDirForPort(const QString &port) const
{
    const QString trimmedPort = port.trimmed();
    if (!contextsByPort.contains(trimmedPort))
    {
        return QString();
    }
    return contextsByPort.value(trimmedPort).destinationDir;
}

int ArsTrackersSessionDownloadCoordinator::contextCount() const { return contextsByPort.size(); }
int ArsTrackersSessionDownloadCoordinator::finishedContextCount() const { return contextsFinished; }
int ArsTrackersSessionDownloadCoordinator::failedContextCount() const { return contextsFailed; }
int ArsTrackersSessionDownloadCoordinator::cancelledContextCount() const { return contextsCancelled; }
int ArsTrackersSessionDownloadCoordinator::disconnectedContextCount() const { return contextsDisconnected; }
int ArsTrackersSessionDownloadCoordinator::completedSessionCount() const { return totalSessionsCompleted; }
int ArsTrackersSessionDownloadCoordinator::totalSessionCount() const { return totalSessionsPlanned; }

void ArsTrackersSessionDownloadCoordinator::cancel()
{
    if (!active)
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
    for (auto it = routesByPort.begin(); it != routesByPort.end(); ++it)
    {
        it.value().cancelRequested = true;
    }
    for (auto it = contextsByPort.begin(); it != contextsByPort.end(); ++it)
    {
        if (!it.value().finished)
        {
            it.value().stateText = "Cancelling";
        }
    }
    emit logMessage("trackers_download_coordinator_cancel_requested");
    emit statusMessage("Cancelling download...");
    emit parallelDownloadProgressChanged();
}

void ArsTrackersSessionDownloadCoordinator::clearCancel()
{
    cancelling = false;
}

ars_tracker_backend *ArsTrackersSessionDownloadCoordinator::activeLegacyBackend() const
{
    if (legacyRoute.valid)
    {
        const QString port = legacyRoute.port;
        if (backendsByPort.contains(port))
        {
            return backendsByPort.value(port).data();
        }
    }
    return legacyBackend.data();
}

ars_tracker_backend *ArsTrackersSessionDownloadCoordinator::activeLegacyBackendForPort(const QString &port) const
{
    const QString trimmedPort = port.trimmed();
    if (!backendsByPort.contains(trimmedPort))
    {
        return nullptr;
    }
    return backendsByPort.value(trimmedPort).data();
}

bool ArsTrackersSessionDownloadCoordinator::createLegacyBackendForActiveRoute(QString *errorMessage)
{
    if (!legacyRoute.valid)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "No active route.";
        }
        return false;
    }

    const QString port = legacyRoute.port;
    if (backendsByPort.contains(port) && !backendsByPort.value(port).isNull())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QString("Backend already exists for contextId=%1").arg(legacyRoute.contextId);
        }
        return false;
    }

    ars_tracker_backend *backend = new ars_tracker_backend(this);
    backendsByPort.insert(port, backend);
    backendToPort.insert(backend, port);
    legacyBackend = backend;

    if (!contextsByPort.contains(port))
    {
        ParallelContextState context;
        context.contextId = legacyRoute.contextId;
        context.port = legacyRoute.port;
        context.serial = legacyRoute.serial;
        context.displayName = legacyRoute.displayName;
        contextsByPort.insert(port, context);
    }

    emit logMessage(QString("TRACKERS_PARALLEL_CONTEXT_CREATE contextId=%1 port=%2 display=%3 sessions=%4 backend=%5")
                        .arg(legacyRoute.contextId,
                             port,
                             legacyRoute.displayName,
                             QString::number(contextsByPort.value(port).sessionQueue.size()),
                             QString("0x%1").arg(reinterpret_cast<quintptr>(backend), 0, 16)));

    return true;
}

void ArsTrackersSessionDownloadCoordinator::destroyLegacyBackendForActiveRoute(const QString &reason)
{
    const QString destroyReason = reason.trimmed().isEmpty() ? QString("unspecified") : reason.trimmed();
    if (!legacyRoute.valid)
    {
        emit logMessage(QString("TRACKERS_DOWNLOAD_BACKEND_DESTROY_IGNORED reason=no-active-route request=%1")
                            .arg(destroyReason));
        return;
    }

    const QString port = legacyRoute.port;
    if (!backendsByPort.contains(port) || backendsByPort.value(port).isNull())
    {
        emit logMessage(QString("TRACKERS_DOWNLOAD_BACKEND_DESTROY_IGNORED reason=no-backend request=%1")
                            .arg(destroyReason));
        return;
    }

    ars_tracker_backend *backend = backendsByPort.value(port).data();
    backendsByPort.remove(port);
    backendToPort.remove(backend);
    if (legacyBackend == backend)
    {
        legacyBackend = nullptr;
    }

    emit logMessage(QString("TRACKERS_DOWNLOAD_BACKEND_DESTROY contextId=%1 generation=%2 port=%3 backend=%4 reason=%5")
                        .arg(legacyRoute.contextId,
                             QString::number(legacyRoute.generation),
                             port,
                             QString("0x%1").arg(reinterpret_cast<quintptr>(backend), 0, 16),
                             destroyReason));

    backend->deleteLater();
    emit parallelDownloadProgressChanged();
}

void ArsTrackersSessionDownloadCoordinator::updateContextProgress(const QString &port,
                                                                  const QString &currentSession,
                                                                  const QString &currentRemoteFile,
                                                                  quint64 bytesDone,
                                                                  quint64 bytesTotal,
                                                                  int percent,
                                                                  const QString &stateText)
{
    const QString trimmedPort = port.trimmed();
    if (!contextsByPort.contains(trimmedPort))
    {
        return;
    }
    ParallelContextState &ctx = contextsByPort[trimmedPort];
    if (!currentSession.trimmed().isEmpty())
    {
        ctx.currentSession = currentSession.trimmed();
    }
    ctx.currentRemoteFile = currentRemoteFile;
    ctx.bytesDone = bytesDone;
    ctx.bytesTotal = bytesTotal;
    ctx.percent = qBound(0, percent, 100);
    if (!stateText.trimmed().isEmpty())
    {
        ctx.stateText = stateText.trimmed();
    }
    emit parallelDownloadProgressChanged();
}

void ArsTrackersSessionDownloadCoordinator::setContextTerminalState(const QString &port,
                                                                    const QString &stateText,
                                                                    const QString &errorText)
{
    const QString trimmedPort = port.trimmed();
    if (!contextsByPort.contains(trimmedPort))
    {
        return;
    }
    ParallelContextState &ctx = contextsByPort[trimmedPort];
    ctx.stateText = stateText;
    ctx.errorText = errorText;
    emit parallelDownloadProgressChanged();
}

ArsTrackersParallelDownloadProgress ArsTrackersSessionDownloadCoordinator::currentParallelDownloadProgress() const
{
    ArsTrackersParallelDownloadProgress snapshot;
    snapshot.active = active;
    snapshot.cancelling = cancelling;
    snapshot.totalContexts = contextsByPort.size();
    snapshot.finishedContexts = contextsFinished;
    snapshot.failedContexts = contextsFailed;
    snapshot.cancelledContexts = contextsCancelled;
    snapshot.disconnectedContexts = contextsDisconnected;

    int percentSum = 0;
    for (auto it = contextsByPort.constBegin(); it != contextsByPort.constEnd(); ++it)
    {
        const ParallelContextState &ctx = it.value();
        ArsTrackersTrackerDownloadProgress row;
        row.contextId = ctx.contextId;
        row.port = ctx.port;
        row.serial = ctx.serial;
        row.displayName = ctx.displayName;
        row.currentSession = ctx.currentSession;
        row.currentRemoteFile = ctx.currentRemoteFile;
        row.bytesDone = ctx.bytesDone;
        row.bytesTotal = ctx.bytesTotal;
        row.percent = ctx.percent;
        row.stateText = ctx.stateText;
        row.errorText = ctx.errorText;
        row.terminal = ctx.finished;
        snapshot.trackers.push_back(row);
        if (!ctx.finished)
        {
            snapshot.runningContexts++;
        }
        percentSum += ctx.percent;
    }
    snapshot.aggregatePercent = snapshot.totalContexts > 0 ? (percentSum / snapshot.totalContexts) : 0;
    lastSnapshot = snapshot;
    return snapshot;
}

bool ArsTrackersSessionDownloadCoordinator::finishOperationIfTerminal(const QString &reason, QString *summaryOut)
{
    if (!active)
    {
        return false;
    }

    int terminalCount = 0;
    int runningCount = 0;
    int pendingCount = 0;
    int finishedCount = 0;
    int failedCount = 0;
    int cancelledCount = 0;
    int disconnectedCount = 0;
    for (auto it = contextsByPort.constBegin(); it != contextsByPort.constEnd(); ++it)
    {
        const ParallelContextState &ctx = it.value();
        if (ctx.finished)
        {
            ++terminalCount;
            if (ctx.cancelled) ++cancelledCount;
            else if (ctx.disconnected) ++disconnectedCount;
            else if (ctx.failed) ++failedCount;
            else ++finishedCount;
        }
        else if (ctx.stateText.compare("Pending", Qt::CaseInsensitive) == 0)
        {
            ++pendingCount;
        }
        else
        {
            ++runningCount;
        }
    }

    emit logMessage(QString("TRACKERS_PARALLEL_OPERATION_TERMINAL_CHECK total=%1 terminal=%2 running=%3 pending=%4 activeBefore=%5 reason=%6")
                        .arg(QString::number(contextsByPort.size()),
                             QString::number(terminalCount),
                             QString::number(runningCount),
                             QString::number(pendingCount),
                             active ? QString("true") : QString("false"),
                             reason));

    if (contextsByPort.isEmpty() || terminalCount < contextsByPort.size())
    {
        return false;
    }

    contextsFinished = finishedCount + failedCount + cancelledCount + disconnectedCount;
    contextsFailed = failedCount;
    contextsCancelled = cancelledCount;
    contextsDisconnected = disconnectedCount;

    const QString summary =
            QString("Download finished: %1 finished, %2 failed, %3 cancelled / %4 trackers")
                .arg(QString::number(finishedCount),
                     QString::number(failedCount + disconnectedCount),
                     QString::number(cancelledCount),
                     QString::number(contextsByPort.size()));
    if (summaryOut != nullptr)
    {
        *summaryOut = summary;
    }

    finishSessionDownload();
    emit logMessage(QString("TRACKERS_PARALLEL_OPERATION_FINISHED operationId=%1 finished=%2 failed=%3 cancelled=%4 disconnected=%5 activeAfter=false")
                        .arg(QString::number(routeGeneration),
                             QString::number(finishedCount),
                             QString::number(failedCount),
                             QString::number(cancelledCount),
                             QString::number(disconnectedCount)));
    emit finished(summary);
    return true;
}
