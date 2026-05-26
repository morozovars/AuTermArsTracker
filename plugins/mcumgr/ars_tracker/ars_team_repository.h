#ifndef ARS_TEAM_REPOSITORY_H
#define ARS_TEAM_REPOSITORY_H

#include <QList>
#include <QString>
#include <QStringList>

#include "ars_team.h"

class ArsTeamRepository
{
public:
    explicit ArsTeamRepository(const QString &workspacePath);

    QString teamsPath() const;
    bool ensureTeamsDir(QString *errorMessage = nullptr) const;
    QList<ArsTeam> loadTeams(QStringList *warnings = nullptr) const;
    bool loadTeam(int teamId, ArsTeam *outTeam, QString *errorMessage = nullptr) const;
    bool saveTeam(const ArsTeam &team, QString *errorMessage = nullptr) const;
    int nextTeamId(QStringList *warnings = nullptr) const;

private:
    QString teamFilePath(int teamId) const;
    QString m_workspacePath;
};

#endif // ARS_TEAM_REPOSITORY_H
