#ifndef ARS_TEAM_EDIT_DIALOG_H
#define ARS_TEAM_EDIT_DIALOG_H

#include <QDialog>

#include "ars/workspace/ArsTeamRepository.h"

class QSpinBox;
class QDoubleSpinBox;
class QLineEdit;
class QPlainTextEdit;
class QLabel;

class ArsTeamEditDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ArsTeamEditDialog(QWidget *parent = nullptr);
    void setCreateMode();
    void setEditMode(const ArsTeam &team);
    ArsTeam teamFromUi() const;
    QString selectedLogoSourcePath() const;
    bool logoSelectionChanged() const;

private slots:
    void onBrowseLogo();
    void onAcceptClicked();

private:
    bool m_editMode = false;
    int m_existingTeamId = 0;
    QLabel *m_teamIdLabel = nullptr;
    QSpinBox *m_teamNum = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_ageCategory = nullptr;
    QPlainTextEdit *m_coaches = nullptr;
    QLineEdit *m_logoPath = nullptr;
    QSpinBox *m_accelerationDistanceM = nullptr;
    QDoubleSpinBox *m_distanceKm = nullptr;
    QSpinBox *m_dribbles = nullptr;
    QSpinBox *m_footloadPerLeg = nullptr;
    QDoubleSpinBox *m_loadIntensity = nullptr;
    QDoubleSpinBox *m_maxSpeed = nullptr;
    QSpinBox *m_shots = nullptr;
    QSpinBox *m_touches = nullptr;
    QString m_selectedLogoSourcePath;
    bool m_logoSelectionChanged = false;
};

#endif // ARS_TEAM_EDIT_DIALOG_H
