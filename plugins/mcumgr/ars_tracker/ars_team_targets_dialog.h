#ifndef ARS_TEAM_TARGETS_DIALOG_H
#define ARS_TEAM_TARGETS_DIALOG_H

#include <QDialog>
#include <QMap>

#include "ars/workspace/ArsTeamRepository.h"

class QDoubleSpinBox;
class QSpinBox;

class ArsTeamTargetsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ArsTeamTargetsDialog(QWidget *parent = nullptr);
    void setTargetsByPosition(const QMap<QString, ArsPlannedMetrics> &targets);
    QMap<QString, ArsPlannedMetrics> targetsByPosition() const;

private:
    struct RowWidgets
    {
        QDoubleSpinBox *distanceKm = nullptr;
        QDoubleSpinBox *accelDistanceM = nullptr;
        QDoubleSpinBox *footload = nullptr;
        QSpinBox *touches = nullptr;
        QDoubleSpinBox *intensity = nullptr;
        QDoubleSpinBox *maxSpeed = nullptr;
        QSpinBox *shots = nullptr;
        QSpinBox *possessions = nullptr;
    };

    QMap<QString, RowWidgets> m_rows;
};

#endif // ARS_TEAM_TARGETS_DIALOG_H
