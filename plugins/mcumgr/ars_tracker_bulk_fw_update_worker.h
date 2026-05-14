#ifndef ARS_TRACKER_BULK_FW_UPDATE_WORKER_H
#define ARS_TRACKER_BULK_FW_UPDATE_WORKER_H

#include <QObject>
#include <QVector>

#include "ars_tracker_bulk_fw_update_models.h"

class plugin_mcumgr;

class ArsTrackerBulkFwUpdateWorker : public QObject
{
    Q_OBJECT
public:
    explicit ArsTrackerBulkFwUpdateWorker(plugin_mcumgr *plugin, QObject *parent = nullptr);

    void configure(const QVector<ArsTrackerBulkFwTarget> &targets, const QString &firmwareFile);
    bool isRunning() const;
    void cancel();

public slots:
    void start();

signals:
    void trackerStatusChanged(const QString &portName, ArsTrackerBulkFwStatus status,
                              const QString &message);
    void trackerProgressChanged(const QString &portName, int percent, const QString &message);
    void currentTrackerChanged(const QString &displayName, const QString &serial, const QString &port);
    void finishedSummary(int successCount, int failedCount, int cancelledCount);

private slots:
    void onPluginFirmwareProgress(const QString &portName, int percent, const QString &message);
    void onPluginFirmwareFinished(const QString &portName, bool success, const QString &message);

private:
    void startNext();
    void finishQueue();

    plugin_mcumgr *plugin_mcumgr_instance = nullptr;
    QVector<ArsTrackerBulkFwTarget> selected_targets;
    QString firmware_file;
    int current_index = -1;
    bool running = false;
    bool cancel_requested = false;
    int success_count = 0;
    int failed_count = 0;
    int cancelled_count = 0;
};

#endif // ARS_TRACKER_BULK_FW_UPDATE_WORKER_H

