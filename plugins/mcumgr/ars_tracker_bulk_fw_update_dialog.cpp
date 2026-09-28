#include "ars_tracker_bulk_fw_update_dialog.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QStyleOptionButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "ars_tracker_bulk_fw_update_worker.h"
#include "plugin_mcumgr.h"

ArsTrackerCheckBoxHeader::ArsTrackerCheckBoxHeader(QWidget *parent)
    : QHeaderView(Qt::Horizontal, parent)
{
    setHighlightSections(false);
    setMouseTracking(true);
}

void ArsTrackerCheckBoxHeader::setCheckState(Qt::CheckState state)
{
    if (check_state != state)
    {
        check_state = state;
        updateCheckBox();
    }
}

void ArsTrackerCheckBoxHeader::setCheckBoxEnabled(bool enabled)
{
    if (check_box_enabled != enabled)
    {
        check_box_enabled = enabled;
        check_box_pressed = false;
        updateCheckBox();
    }
}

QRect ArsTrackerCheckBoxHeader::checkBoxRect(const QRect &sectionRect) const
{
    QStyleOptionButton option;
    option.initFrom(viewport());
    option.styleObject = nullptr;
    option.rect = sectionRect;

    QSize indicator_size = style()->subElementRect(QStyle::SE_CheckBoxIndicator,
                                                   &option,
                                                   viewport()).size();
    if (!indicator_size.isValid() || indicator_size.isEmpty())
    {
        indicator_size = QSize(style()->pixelMetric(QStyle::PM_IndicatorWidth,
                                                    &option,
                                                    viewport()),
                               style()->pixelMetric(QStyle::PM_IndicatorHeight,
                                                    &option,
                                                    viewport()));
    }

    return QStyle::alignedRect(layoutDirection(), Qt::AlignCenter,
                               indicator_size, sectionRect);
}

QRect ArsTrackerCheckBoxHeader::checkBoxRect(int logicalIndex) const
{
    return checkBoxRect(QRect(sectionViewportPosition(logicalIndex), 0,
                              sectionSize(logicalIndex), viewport()->height()));
}

void ArsTrackerCheckBoxHeader::updateCheckBox()
{
    updateSection(0);
    viewport()->update(checkBoxRect(0));
}

void ArsTrackerCheckBoxHeader::paintSection(QPainter *painter, const QRect &rect,
                                            int logicalIndex) const
{
    painter->save();
    QHeaderView::paintSection(painter, rect, logicalIndex);
    painter->restore();
    if (logicalIndex != 0)
    {
        return;
    }

    QStyleOptionButton option;
    option.initFrom(viewport());
    option.styleObject = nullptr;
    option.rect = checkBoxRect(rect);
    option.state &= ~(QStyle::State_On | QStyle::State_Off | QStyle::State_NoChange |
                      QStyle::State_MouseOver | QStyle::State_Sunken);
    option.state |= check_state == Qt::Checked ? QStyle::State_On :
                    check_state == Qt::PartiallyChecked ? QStyle::State_NoChange :
                                                          QStyle::State_Off;
    if (check_box_enabled && isEnabled())
    {
        option.state |= QStyle::State_Enabled;
    }
    else
    {
        option.state &= ~QStyle::State_Enabled;
    }
    if (check_box_hovered)
    {
        option.state |= QStyle::State_MouseOver;
    }
    if (check_box_pressed)
    {
        option.state |= QStyle::State_Sunken;
    }
    painter->save();
    painter->setClipRect(rect, Qt::IntersectClip);
    style()->drawPrimitive(QStyle::PE_IndicatorCheckBox, &option, painter, this);
    painter->restore();
}

QSize ArsTrackerCheckBoxHeader::sectionSizeFromContents(int logicalIndex) const
{
    QSize size = QHeaderView::sectionSizeFromContents(logicalIndex);
    if (logicalIndex == 0)
    {
        QStyleOptionButton option;
        option.initFrom(viewport());
        option.styleObject = nullptr;
        const int indicator_width = style()->pixelMetric(QStyle::PM_IndicatorWidth,
                                                         &option,
                                                         viewport());
        const int indicator_height = style()->pixelMetric(QStyle::PM_IndicatorHeight,
                                                          &option,
                                                          viewport());
        const int margin = style()->pixelMetric(QStyle::PM_HeaderMargin, nullptr, viewport());
        size.setWidth(qMax(size.width(), indicator_width + 2 * margin));
        size.setHeight(qMax(size.height(), indicator_height + 2 * margin));
    }
    return size;
}

void ArsTrackerCheckBoxHeader::mouseMoveEvent(QMouseEvent *event)
{
    const bool hovered = checkBoxRect(0).contains(event->position().toPoint());
    if (check_box_hovered != hovered)
    {
        check_box_hovered = hovered;
        updateCheckBox();
    }
    QHeaderView::mouseMoveEvent(event);
}

void ArsTrackerCheckBoxHeader::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && check_box_enabled && isEnabled() &&
        checkBoxRect(0).contains(event->position().toPoint()))
    {
        check_box_pressed = true;
        updateCheckBox();
        event->accept();
        return;
    }
    QHeaderView::mousePressEvent(event);
}

void ArsTrackerCheckBoxHeader::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && check_box_pressed)
    {
        const bool activate = check_box_enabled && isEnabled() &&
                              checkBoxRect(0).contains(event->position().toPoint());
        check_box_pressed = false;
        updateCheckBox();
        if (activate)
        {
            check_state = check_state == Qt::Checked ? Qt::Unchecked : Qt::Checked;
            emit checkStateChanged(check_state);
        }
        event->accept();
        return;
    }
    QHeaderView::mouseReleaseEvent(event);
}

void ArsTrackerCheckBoxHeader::leaveEvent(QEvent *event)
{
    if (check_box_hovered || check_box_pressed)
    {
        check_box_hovered = false;
        check_box_pressed = false;
        updateCheckBox();
    }
    QHeaderView::leaveEvent(event);
}

ArsTrackerBulkFwUpdateDialog::ArsTrackerBulkFwUpdateDialog(plugin_mcumgr *plugin, QWidget *parent)
    : QDialog(parent), plugin_mcumgr_instance(plugin)
{
    setWindowTitle("Trackers firmware update");
    resize(860, 520);

    QVBoxLayout *main_layout = new QVBoxLayout(this);

    table_trackers = new QTableWidget(this);
    check_box_header = new ArsTrackerCheckBoxHeader(table_trackers);
    table_trackers->setHorizontalHeader(check_box_header);
    table_trackers->setColumnCount(6);
    table_trackers->setHorizontalHeaderLabels(
        QStringList() << QString() << "Tracker" << "Serial" << "Port" << "Current firmware" << "Status");
    table_trackers->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_trackers->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_trackers->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_trackers->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_trackers->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table_trackers->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    table_trackers->verticalHeader()->setVisible(false);
    table_trackers->setSelectionMode(QAbstractItemView::NoSelection);
    table_trackers->setEditTriggers(QAbstractItemView::NoEditTriggers);
    main_layout->addWidget(table_trackers, 1);

    QHBoxLayout *file_row = new QHBoxLayout();
    file_row->addWidget(new QLabel("Firmware file (.bin):", this));
    edit_firmware_file = new QLineEdit(this);
    file_row->addWidget(edit_firmware_file, 1);
    btn_browse = new QPushButton("Browse", this);
    file_row->addWidget(btn_browse);
    main_layout->addLayout(file_row);

    lbl_current = new QLabel("Current tracker: -", this);
    main_layout->addWidget(lbl_current);

    progress_current = new QProgressBar(this);
    progress_current->setRange(0, 100);
    progress_current->setValue(0);
    main_layout->addWidget(progress_current);

    lbl_summary = new QLabel("Ready", this);
    main_layout->addWidget(lbl_summary);

    QHBoxLayout *buttons_row = new QHBoxLayout();
    buttons_row->addStretch(1);
    btn_update = new QPushButton("Update", this);
    btn_cancel = new QPushButton("Cancel", this);
    buttons_row->addWidget(btn_update);
    buttons_row->addWidget(btn_cancel);
    main_layout->addLayout(buttons_row);

    worker = new ArsTrackerBulkFwUpdateWorker(plugin_mcumgr_instance, this);
    connect(btn_browse, &QPushButton::clicked, this, &ArsTrackerBulkFwUpdateDialog::onBrowseClicked);
    connect(btn_update, &QPushButton::clicked, this, &ArsTrackerBulkFwUpdateDialog::onUpdateClicked);
    connect(btn_cancel, &QPushButton::clicked, this, &ArsTrackerBulkFwUpdateDialog::onCancelClicked);
    connect(check_box_header, &ArsTrackerCheckBoxHeader::checkStateChanged, this,
            [this](Qt::CheckState state) { setAllTrackersChecked(state == Qt::Checked); });
    connect(worker, &ArsTrackerBulkFwUpdateWorker::trackerStatusChanged, this,
            &ArsTrackerBulkFwUpdateDialog::onTrackerStatusChanged);
    connect(worker, &ArsTrackerBulkFwUpdateWorker::trackerProgressChanged, this,
            &ArsTrackerBulkFwUpdateDialog::onTrackerProgressChanged);
    connect(worker, &ArsTrackerBulkFwUpdateWorker::currentTrackerChanged, this,
            &ArsTrackerBulkFwUpdateDialog::onCurrentTrackerChanged);
    connect(worker, &ArsTrackerBulkFwUpdateWorker::finishedSummary, this,
            &ArsTrackerBulkFwUpdateDialog::onFinishedSummary);
    connect(plugin_mcumgr_instance, &plugin_mcumgr::trackersFirmwareVersionResolved, this,
            &ArsTrackerBulkFwUpdateDialog::onFirmwareVersionResolved);
    install_reconnect_check_timer = new QTimer(this);
    install_reconnect_check_timer->setInterval(1000);
    connect(install_reconnect_check_timer, &QTimer::timeout, this,
            &ArsTrackerBulkFwUpdateDialog::onInstallReconnectCheckTimer);

    populateTrackers();
    requestNextFirmwareVersion();
}

void ArsTrackerBulkFwUpdateDialog::populateTrackers()
{
    row_by_port.clear();
    firmware_version_request_queue.clear();
    table_trackers->setRowCount(0);
    if (plugin_mcumgr_instance == nullptr)
    {
        updateHeaderCheckState();
        return;
    }

    const QVector<ArsTrackerBulkFwTarget> targets =
        plugin_mcumgr_instance->connectedTrackersForBulkFirmwareUpdate();
    table_trackers->setRowCount(targets.size());
    for (int i = 0; i < targets.size(); ++i)
    {
        const ArsTrackerBulkFwTarget &target = targets[i];
        row_by_port.insert(target.portName, i);
        QCheckBox *check = new QCheckBox(table_trackers);
        check->setChecked(true);
        check->setProperty("tracker_port", target.portName);
        connect(check, &QCheckBox::toggled, this,
                [this](bool) { updateHeaderCheckState(); });
        table_trackers->setCellWidget(i, 0, check);
        table_trackers->setItem(i, 1, new QTableWidgetItem(target.displayName));
        table_trackers->setItem(i, 2, new QTableWidgetItem(target.serialNumber));
        table_trackers->setItem(i, 3, new QTableWidgetItem(target.portName));
        table_trackers->setItem(i, 4, new QTableWidgetItem("Loading..."));
        table_trackers->setItem(
            i, 5, new QTableWidgetItem(ars_tracker_bulk_fw_status_text(ARS_TRACKER_BULK_FW_PENDING)));
        firmware_version_request_queue.append(target.portName);
    }
    table_trackers->resizeRowsToContents();
    updateHeaderCheckState();
}

void ArsTrackerBulkFwUpdateDialog::requestNextFirmwareVersion()
{
    if (firmware_version_request_active || plugin_mcumgr_instance == nullptr)
    {
        return;
    }
    if (firmware_version_request_queue.isEmpty())
    {
        return;
    }
    firmware_version_request_active = true;
    const QString port = firmware_version_request_queue.takeFirst();
    QString error;
    if (!plugin_mcumgr_instance->requestTrackerFirmwareVersionForPort(port, &error))
    {
        onFirmwareVersionResolved(port, false, QString(), error.isEmpty() ? QString("Unknown") : error);
    }
}

QVector<ArsTrackerBulkFwTarget> ArsTrackerBulkFwUpdateDialog::selectedTargets() const
{
    QVector<ArsTrackerBulkFwTarget> selected;
    for (int row = 0; row < table_trackers->rowCount(); ++row)
    {
        QCheckBox *check = qobject_cast<QCheckBox *>(table_trackers->cellWidget(row, 0));
        if (check == nullptr || !check->isChecked())
        {
            continue;
        }
        ArsTrackerBulkFwTarget target;
        target.displayName = table_trackers->item(row, 1) != nullptr ? table_trackers->item(row, 1)->text() : QString();
        target.serialNumber = table_trackers->item(row, 2) != nullptr ? table_trackers->item(row, 2)->text() : QString();
        target.portName = table_trackers->item(row, 3) != nullptr ? table_trackers->item(row, 3)->text() : QString();
        if (!target.portName.trimmed().isEmpty())
        {
            selected.append(target);
        }
    }
    return selected;
}

void ArsTrackerBulkFwUpdateDialog::setControlsEnabled(bool enabled)
{
    table_trackers->setEnabled(enabled);
    if (check_box_header != nullptr)
    {
        check_box_header->setCheckBoxEnabled(enabled);
    }
    edit_firmware_file->setEnabled(enabled);
    btn_browse->setEnabled(enabled);
    btn_update->setEnabled(enabled);
}

void ArsTrackerBulkFwUpdateDialog::setAllTrackersChecked(bool checked)
{
    tracker_checkboxes_updating = true;
    for (int row = 0; row < table_trackers->rowCount(); ++row)
    {
        QCheckBox *check = qobject_cast<QCheckBox *>(table_trackers->cellWidget(row, 0));
        if (check != nullptr)
        {
            check->setChecked(checked);
        }
    }
    tracker_checkboxes_updating = false;
    updateHeaderCheckState();
}

void ArsTrackerBulkFwUpdateDialog::updateHeaderCheckState()
{
    if (tracker_checkboxes_updating || check_box_header == nullptr)
    {
        return;
    }
    int checked_count = 0;
    for (int row = 0; row < table_trackers->rowCount(); ++row)
    {
        const QCheckBox *check = qobject_cast<QCheckBox *>(table_trackers->cellWidget(row, 0));
        if (check != nullptr && check->isChecked())
        {
            ++checked_count;
        }
    }
    Qt::CheckState state = Qt::Unchecked;
    if (table_trackers->rowCount() > 0 && checked_count == table_trackers->rowCount())
    {
        state = Qt::Checked;
    }
    else if (checked_count > 0)
    {
        state = Qt::PartiallyChecked;
    }
    check_box_header->setCheckState(state);
}

int ArsTrackerBulkFwUpdateDialog::rowForPort(const QString &portName) const
{
    return row_by_port.value(portName, -1);
}

void ArsTrackerBulkFwUpdateDialog::reject()
{
    if (worker != nullptr && worker->isRunning())
    {
        onCancelClicked();
        return;
    }
    QDialog::reject();
}

void ArsTrackerBulkFwUpdateDialog::onBrowseClicked()
{
    const QString selected_file = QFileDialog::getOpenFileName(
        this, tr("Open firmware file"), edit_firmware_file->text(),
        tr("Binary Files (*.bin);;All Files (*)"));
    if (!selected_file.isEmpty())
    {
        edit_firmware_file->setText(selected_file);
    }
}

void ArsTrackerBulkFwUpdateDialog::onUpdateClicked()
{
    const QVector<ArsTrackerBulkFwTarget> targets = selectedTargets();
    if (targets.isEmpty())
    {
        QMessageBox::warning(this, "Firmware update", "Select at least one tracker.");
        return;
    }
    const QString firmware_file = edit_firmware_file->text().trimmed();
    if (firmware_file.isEmpty() || !QFileInfo::exists(firmware_file))
    {
        QMessageBox::warning(this, "Firmware update", "Select a valid .bin firmware file.");
        return;
    }
    if (plugin_mcumgr_instance != nullptr && !plugin_mcumgr_instance->canStartTrackersBulkFirmwareUpdate())
    {
        QMessageBox::warning(this, "Firmware update",
                             "Another conflicting operation is in progress.");
        return;
    }

    for (int row = 0; row < table_trackers->rowCount(); ++row)
    {
        if (table_trackers->item(row, 5) != nullptr)
        {
            table_trackers->item(row, 5)->setText(
                ars_tracker_bulk_fw_status_text(ARS_TRACKER_BULK_FW_PENDING));
        }
    }
    progress_current->setValue(0);
    lbl_summary->setText("Updating firmware...");

    setControlsEnabled(false);
    btn_cancel->setText("Cancel update");
    worker->configure(targets, firmware_file);
    worker->start();
}

void ArsTrackerBulkFwUpdateDialog::onCancelClicked()
{
    if (worker->isRunning())
    {
        worker->cancel();
        btn_cancel->setEnabled(false);
        lbl_summary->setText("Cancelling remaining trackers...");
        return;
    }
    reject();
}

void ArsTrackerBulkFwUpdateDialog::onTrackerStatusChanged(const QString &portName,
                                                          ArsTrackerBulkFwStatus status,
                                                          const QString &message)
{
    const int row = rowForPort(portName);
    if (row >= 0 && table_trackers->item(row, 5) != nullptr)
    {
        table_trackers->item(row, 5)->setText(message);
    }
    if (status == ARS_TRACKER_BULK_FW_SUCCESS)
    {
        ports_installing.insert(portName);
        ports_reconnected_after_install.remove(portName);
        ports_waiting_delayed_version_query.remove(portName);
        if (install_reconnect_check_timer != nullptr && !install_reconnect_check_timer->isActive())
        {
            install_reconnect_check_timer->start();
        }
    }
}

void ArsTrackerBulkFwUpdateDialog::onTrackerProgressChanged(const QString &portName, int percent,
                                                            const QString &message)
{
    Q_UNUSED(portName);
    progress_current->setValue(percent);
    if (!message.trimmed().isEmpty())
    {
        lbl_summary->setText(message);
    }
}

void ArsTrackerBulkFwUpdateDialog::onCurrentTrackerChanged(const QString &displayName,
                                                           const QString &serial, const QString &port)
{
    lbl_current->setText(
        QString("Current tracker: %1  [%2 | %3]").arg(displayName, serial, port));
}

void ArsTrackerBulkFwUpdateDialog::onFinishedSummary(int successCount, int failedCount,
                                                     int cancelledCount)
{
    setControlsEnabled(true);
    btn_cancel->setText("Close");
    btn_cancel->setEnabled(true);
    lbl_current->setText("Current tracker: -");
    lbl_summary->setText(
        QString("Completed: success=%1, failed=%2, cancelled=%3")
            .arg(successCount)
            .arg(failedCount)
            .arg(cancelledCount));
}

void ArsTrackerBulkFwUpdateDialog::onFirmwareVersionResolved(const QString &portName, bool success,
                                                             const QString &version,
                                                             const QString &message)
{
    const int row = rowForPort(portName);
    if (row >= 0 && table_trackers->item(row, 4) != nullptr)
    {
        if (success)
        {
            table_trackers->item(row, 4)->setText(version.trimmed().isEmpty() ? "Unknown" : version);
        }
        else
        {
            const bool expected_install_reconnect =
                ports_installing.contains(portName) && !ports_reconnected_after_install.contains(portName);
            if (!expected_install_reconnect)
            {
                table_trackers->item(row, 4)->setText(
                    message.trimmed().isEmpty() ? "Error: Unknown" : QString("Error: %1").arg(message));
            }
        }
    }
    if (success && ports_installing.contains(portName))
    {
        if (row >= 0 && table_trackers->item(row, 5) != nullptr)
        {
            table_trackers->item(row, 5)->setText("Firmware successfully loaded");
        }
        ports_installing.remove(portName);
        ports_waiting_delayed_version_query.remove(portName);
        ports_reconnected_after_install.remove(portName);
    }
    firmware_version_request_active = false;
    requestNextFirmwareVersion();
}

void ArsTrackerBulkFwUpdateDialog::onInstallReconnectCheckTimer()
{
    if (ports_installing.isEmpty())
    {
        if (install_reconnect_check_timer != nullptr)
        {
            install_reconnect_check_timer->stop();
        }
        return;
    }
    if (plugin_mcumgr_instance == nullptr)
    {
        return;
    }

    const QStringList ports = ports_installing.values();
    for (const QString &port : ports)
    {
        if (!plugin_mcumgr_instance->isTrackerConnectedForBulkFirmwareUpdate(port))
        {
            continue;
        }
        ports_reconnected_after_install.insert(port);
        if (ports_waiting_delayed_version_query.contains(port))
        {
            continue;
        }
        ports_waiting_delayed_version_query.insert(port);
        QTimer::singleShot(3000, this, [this, port]() {
            if (!ports_installing.contains(port) || plugin_mcumgr_instance == nullptr)
            {
                ports_waiting_delayed_version_query.remove(port);
                return;
            }
            QString error;
            if (!plugin_mcumgr_instance->requestTrackerFirmwareVersionForPort(port, &error))
            {
                onFirmwareVersionResolved(port, false, QString(),
                                          error.isEmpty() ? QString("Failed to load firmware version")
                                                          : error);
            }
            ports_waiting_delayed_version_query.remove(port);
        });
    }
}
