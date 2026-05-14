#ifndef ARS_TRACKER_BULK_FW_UPDATE_DIALOG_H
#define ARS_TRACKER_BULK_FW_UPDATE_DIALOG_H

#include <QDialog>
#include <QHash>
#include <QSet>
#include <QStringList>

#include "ars_tracker_bulk_fw_update_models.h"

class QLineEdit;
class QTableWidget;
class QPushButton;
class QProgressBar;
class QLabel;
class QTimer;
class plugin_mcumgr;
class ArsTrackerBulkFwUpdateWorker;

class ArsTrackerBulkFwUpdateDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ArsTrackerBulkFwUpdateDialog(plugin_mcumgr *plugin, QWidget *parent = nullptr);

private slots:
    void onBrowseClicked();
    void onUpdateClicked();
    void onCancelClicked();
    void onTrackerStatusChanged(const QString &portName, ArsTrackerBulkFwStatus status,
                                const QString &message);
    void onTrackerProgressChanged(const QString &portName, int percent, const QString &message);
    void onCurrentTrackerChanged(const QString &displayName, const QString &serial,
                                 const QString &port);
    void onFinishedSummary(int successCount, int failedCount, int cancelledCount);
    void onFirmwareVersionResolved(const QString &portName, bool success, const QString &version,
                                   const QString &message);
    void onInstallReconnectCheckTimer();

private:
    void reject() override;
    void populateTrackers();
    void requestNextFirmwareVersion();
    QVector<ArsTrackerBulkFwTarget> selectedTargets() const;
    void setControlsEnabled(bool enabled);
    int rowForPort(const QString &portName) const;

    plugin_mcumgr *plugin_mcumgr_instance = nullptr;
    ArsTrackerBulkFwUpdateWorker *worker = nullptr;
    QTableWidget *table_trackers = nullptr;
    QLineEdit *edit_firmware_file = nullptr;
    QPushButton *btn_update = nullptr;
    QPushButton *btn_cancel = nullptr;
    QPushButton *btn_browse = nullptr;
    QProgressBar *progress_current = nullptr;
    QLabel *lbl_current = nullptr;
    QLabel *lbl_summary = nullptr;
    QHash<QString, int> row_by_port;
    QStringList firmware_version_request_queue;
    bool firmware_version_request_active = false;
    QSet<QString> ports_installing;
    QSet<QString> ports_waiting_delayed_version_query;
    QSet<QString> ports_reconnected_after_install;
    QTimer *install_reconnect_check_timer = nullptr;
};

#endif // ARS_TRACKER_BULK_FW_UPDATE_DIALOG_H
