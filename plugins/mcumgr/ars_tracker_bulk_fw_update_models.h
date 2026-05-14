#ifndef ARS_TRACKER_BULK_FW_UPDATE_MODELS_H
#define ARS_TRACKER_BULK_FW_UPDATE_MODELS_H

#include <QString>

struct ArsTrackerBulkFwTarget
{
    QString displayName;
    QString serialNumber;
    QString portName;
};

enum ArsTrackerBulkFwStatus
{
    ARS_TRACKER_BULK_FW_PENDING = 0,
    ARS_TRACKER_BULK_FW_UPLOADING,
    ARS_TRACKER_BULK_FW_SUCCESS,
    ARS_TRACKER_BULK_FW_ERROR,
    ARS_TRACKER_BULK_FW_CANCELLED,
    ARS_TRACKER_BULK_FW_SKIPPED
};

inline QString ars_tracker_bulk_fw_status_text(ArsTrackerBulkFwStatus status,
                                               const QString &errorText = QString())
{
    switch (status)
    {
    case ARS_TRACKER_BULK_FW_PENDING:
        return "Pending";
    case ARS_TRACKER_BULK_FW_UPLOADING:
        return "Uploading";
    case ARS_TRACKER_BULK_FW_SUCCESS:
        return "Firmware successfully loaded";
    case ARS_TRACKER_BULK_FW_ERROR:
        return errorText.isEmpty() ? QString("Error") : QString("Error: %1").arg(errorText);
    case ARS_TRACKER_BULK_FW_CANCELLED:
        return "Cancelled";
    case ARS_TRACKER_BULK_FW_SKIPPED:
        return "Skipped";
    default:
        return "Pending";
    }
}

#endif // ARS_TRACKER_BULK_FW_UPDATE_MODELS_H

