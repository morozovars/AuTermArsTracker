#include "ars_tracker_bulk_fw_update_worker.h"

#include <QFileInfo>
#include <QTimer>

#include "plugin_mcumgr.h"

ArsTrackerBulkFwUpdateWorker::ArsTrackerBulkFwUpdateWorker(plugin_mcumgr *plugin, QObject *parent)
    : QObject(parent), plugin_mcumgr_instance(plugin)
{
    if (plugin_mcumgr_instance != nullptr)
    {
        connect(plugin_mcumgr_instance, &plugin_mcumgr::trackersFirmwareUpdateProgress, this,
                &ArsTrackerBulkFwUpdateWorker::onPluginFirmwareProgress);
        connect(plugin_mcumgr_instance, &plugin_mcumgr::trackersFirmwareUpdateFinished, this,
                &ArsTrackerBulkFwUpdateWorker::onPluginFirmwareFinished);
    }
}

void ArsTrackerBulkFwUpdateWorker::configure(const QVector<ArsTrackerBulkFwTarget> &targets,
                                             const QString &firmwareFile)
{
    selected_targets = targets;
    firmware_file = firmwareFile;
}

bool ArsTrackerBulkFwUpdateWorker::isRunning() const
{
    return running;
}

void ArsTrackerBulkFwUpdateWorker::cancel()
{
    cancel_requested = true;
}

void ArsTrackerBulkFwUpdateWorker::start()
{
    if (running || plugin_mcumgr_instance == nullptr)
    {
        return;
    }
    running = true;
    cancel_requested = false;
    success_count = 0;
    failed_count = 0;
    cancelled_count = 0;
    current_index = -1;
    plugin_mcumgr_instance->setTrackersBulkFirmwareUpdateActive(true);
    startNext();
}

void ArsTrackerBulkFwUpdateWorker::startNext()
{
    if (!running)
    {
        return;
    }
    current_index++;
    if (current_index >= selected_targets.size())
    {
        finishQueue();
        return;
    }

    const ArsTrackerBulkFwTarget target = selected_targets[current_index];
    if (cancel_requested)
    {
        cancelled_count++;
        emit trackerStatusChanged(target.portName, ARS_TRACKER_BULK_FW_CANCELLED,
                                  ars_tracker_bulk_fw_status_text(ARS_TRACKER_BULK_FW_CANCELLED));
        QTimer::singleShot(0, this, &ArsTrackerBulkFwUpdateWorker::startNext);
        return;
    }

    emit currentTrackerChanged(target.displayName, target.serialNumber, target.portName);
    emit trackerStatusChanged(target.portName, ARS_TRACKER_BULK_FW_UPLOADING,
                              ars_tracker_bulk_fw_status_text(ARS_TRACKER_BULK_FW_UPLOADING));
    emit trackerProgressChanged(target.portName, 0, "Uploading");

    QString start_error;
    if (!plugin_mcumgr_instance->startFirmwareUpdateForTracker(target.portName, firmware_file,
                                                               &start_error))
    {
        failed_count++;
        emit trackerStatusChanged(target.portName, ARS_TRACKER_BULK_FW_ERROR,
                                  ars_tracker_bulk_fw_status_text(ARS_TRACKER_BULK_FW_ERROR,
                                                                  start_error));
        QTimer::singleShot(0, this, &ArsTrackerBulkFwUpdateWorker::startNext);
    }
}

void ArsTrackerBulkFwUpdateWorker::finishQueue()
{
    if (!running)
    {
        return;
    }
    running = false;
    if (plugin_mcumgr_instance != nullptr)
    {
        plugin_mcumgr_instance->setTrackersBulkFirmwareUpdateActive(false);
    }
    emit finishedSummary(success_count, failed_count, cancelled_count);
}

void ArsTrackerBulkFwUpdateWorker::onPluginFirmwareProgress(const QString &portName, int percent,
                                                            const QString &message)
{
    if (!running || current_index < 0 || current_index >= selected_targets.size())
    {
        return;
    }
    if (selected_targets[current_index].portName.compare(portName, Qt::CaseInsensitive) != 0)
    {
        return;
    }
    emit trackerProgressChanged(portName, percent, message);
}

void ArsTrackerBulkFwUpdateWorker::onPluginFirmwareFinished(const QString &portName, bool success,
                                                            const QString &message)
{
    if (!running || current_index < 0 || current_index >= selected_targets.size())
    {
        return;
    }
    if (selected_targets[current_index].portName.compare(portName, Qt::CaseInsensitive) != 0)
    {
        return;
    }

    if (success)
    {
        success_count++;
        emit trackerStatusChanged(portName, ARS_TRACKER_BULK_FW_SUCCESS,
                                  ars_tracker_bulk_fw_status_text(ARS_TRACKER_BULK_FW_SUCCESS));
        emit trackerProgressChanged(portName, 100, "Firmware successfully loaded");
    }
    else
    {
        failed_count++;
        emit trackerStatusChanged(portName, ARS_TRACKER_BULK_FW_ERROR,
                                  ars_tracker_bulk_fw_status_text(ARS_TRACKER_BULK_FW_ERROR,
                                                                  message));
    }

    QTimer::singleShot(0, this, &ArsTrackerBulkFwUpdateWorker::startNext);
}

