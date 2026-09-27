#ifndef ARS_TRACKER_BULK_FW_UPDATE_DIALOG_H
#define ARS_TRACKER_BULK_FW_UPDATE_DIALOG_H

#include <QDialog>
#include <QHash>
#include <QHeaderView>
#include <QSet>
#include <QStringList>

#include "ars_tracker_bulk_fw_update_models.h"

class QLineEdit;
class QTableWidget;
class QPushButton;
class QProgressBar;
class QLabel;
class QTimer;
class QMouseEvent;
class QPainter;
class plugin_mcumgr;
class ArsTrackerBulkFwUpdateWorker;

class ArsTrackerCheckBoxHeader : public QHeaderView
{
    Q_OBJECT
public:
    explicit ArsTrackerCheckBoxHeader(QWidget *parent = nullptr);
    void setCheckState(Qt::CheckState state);
    void setCheckBoxEnabled(bool enabled);

signals:
    void checkStateChanged(Qt::CheckState state);

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override;
    QSize sectionSizeFromContents(int logicalIndex) const override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRect checkBoxRect(const QRect &sectionRect) const;
    QRect checkBoxRect(int logicalIndex) const;
    void updateCheckBox();

    Qt::CheckState check_state = Qt::Unchecked;
    bool check_box_enabled = true;
    bool check_box_hovered = false;
    bool check_box_pressed = false;
};

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
    void setAllTrackersChecked(bool checked);
    void updateHeaderCheckState();
    int rowForPort(const QString &portName) const;

    plugin_mcumgr *plugin_mcumgr_instance = nullptr;
    ArsTrackerBulkFwUpdateWorker *worker = nullptr;
    QTableWidget *table_trackers = nullptr;
    ArsTrackerCheckBoxHeader *check_box_header = nullptr;
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
    bool tracker_checkboxes_updating = false;
};

#endif // ARS_TRACKER_BULK_FW_UPDATE_DIALOG_H
