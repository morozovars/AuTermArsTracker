#ifndef ARS_TRACKER_TEAM_TAB_H
#define ARS_TRACKER_TEAM_TAB_H

#include <QWidget>

#include "ars/workspace/ArsTeamRepository.h"

class QLabel;
class QPushButton;
class QTableWidget;

class ArsTrackerTeamTab : public QWidget
{
    Q_OBJECT

public:
    explicit ArsTrackerTeamTab(QWidget *parent = nullptr);

public slots:
    void reloadTeams(const QString &reason = QString());

private slots:
    void onCreateTeam();
    void onEditTeam();
    void onDeleteTeam();
    void onSetDefaultTeam();

private:
    QString workspacePath() const;
    void buildUi();
    void updateDefaultTeamLabel();
    void rebuildTeamsTable();
    QString coachesDisplay(const ArsTeam &team) const;
    bool saveTeamWithLog(const ArsTeam &team);
    bool saveTeamWithLogoSelection(const ArsTeam &team,
                                   const QString &logoSourcePath,
                                   bool logoSelectionChanged);
    bool saveDefaultTeamIdWithLog(int teamId);
    ArsTeam *findTeamById(int teamId);
    const ArsTeam *findTeamById(int teamId) const;
    bool hasDuplicateTeamNum(int teamNum, int excludeTeamId) const;

    QLabel *m_defaultLabel = nullptr;
    QPushButton *m_reloadButton = nullptr;
    QPushButton *m_createButton = nullptr;
    QTableWidget *m_table = nullptr;
    QList<ArsTeam> m_teams;
    int m_defaultTeamId = -1;
};

#endif // ARS_TRACKER_TEAM_TAB_H
