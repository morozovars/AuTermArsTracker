#ifndef ARS_TRACKER_PLAYER_EDIT_DIALOG_H
#define ARS_TRACKER_PLAYER_EDIT_DIALOG_H

#include <QDialog>
#include <QList>

#include "ars/workspace/ArsPlayer.h"
#include "ars/workspace/ArsTeamRepository.h"

class QComboBox;
class QDateEdit;
class QDoubleSpinBox;
class QLineEdit;
class QLabel;
class QSpinBox;

class ArsTrackerPlayerEditDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ArsTrackerPlayerEditDialog(QWidget *parent = nullptr);

    void setWorkspaceRootPath(const QString &workspaceRootPath);
    void setTeams(const QList<ArsTeam> &teams);
    void setCreateMode(int defaultTeamId);
    void setEditMode(const ArsPlayer &player);

    ArsPlayer playerFromUi() const;
    QString selectedPhotoSourcePath() const;
    bool photoSelectionChanged() const;

private slots:
    void onBrowsePhoto();
    void onAcceptClicked();

private:
    void updatePhotoPreview(const QString &path);
    void selectTeamIdOrAddMissing(int teamId);
    bool validateForm(QString *errorMessage) const;

    bool m_editMode = false;
    ArsPlayer m_existingPlayer;
    QList<ArsTeam> m_teams;

    QLineEdit *m_surname = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_photoPath = nullptr;
    QLabel *m_photoPreview = nullptr;
    QSpinBox *m_number = nullptr;
    QDateEdit *m_birthDate = nullptr;
    QComboBox *m_position = nullptr;
    QComboBox *m_dominantFoot = nullptr;
    QSpinBox *m_heightCm = nullptr;
    QSpinBox *m_weightKg = nullptr;
    QComboBox *m_teamId = nullptr;
    QDoubleSpinBox *m_maxSpeedMps = nullptr;
    QDoubleSpinBox *m_maxShotLoadG = nullptr;

    QString m_selectedPhotoSourcePath;
    bool m_photoSelectionChanged = false;
    QString m_workspaceRootPath;
};

#endif // ARS_TRACKER_PLAYER_EDIT_DIALOG_H
