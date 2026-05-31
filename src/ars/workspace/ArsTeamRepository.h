#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

struct ArsPlannedMetrics
{
    int accelerationDistanceM = 0;
    double distanceKm = 0.0;
    int dribbles = 0;
    int footloadPerLeg = 0;
    double loadIntensityGPerMin = 0.0;
    double maxSpeedMps = 0.0;
    int shots = 0;
    int touches = 0;
};

struct ArsTeam
{
    int teamNum = 0;
    int teamId = 0;
    QString name;
    QString ageCategory;
    QStringList defaultCoaches;
    ArsPlannedMetrics defaultPlannedMetrics;
    QMap<QString, ArsPlannedMetrics> targetsByPosition;
    QString teamLogoPath;
};

class ArsTeamRepository
{
public:
    explicit ArsTeamRepository(const QString &workspacePath = QString());

    bool ensureInitialized();
    QString filePath() const;
    QString teamsPath() const;
    bool ensureTeamsDir(QString *errorMessage = nullptr) const;
    QList<ArsTeam> loadTeams(QStringList *warnings = nullptr) const;
    bool loadTeam(int teamId, ArsTeam *outTeam, QString *errorMessage = nullptr) const;
    bool saveTeam(const ArsTeam &team, QString *errorMessage = nullptr) const;
    bool deleteTeam(int teamId, QString *errorMessage = nullptr) const;
    int nextTeamId(QStringList *warnings = nullptr) const;
    bool copyTeamLogoToWorkspace(int teamId,
                                 const QString &sourceLogoPath,
                                 QString *outRelativeLogoPath,
                                 QString *errorMessage = nullptr) const;
    QString workspaceRootPath() const;
    QString resolveLogoAbsolutePath(const QString &logoPath) const;

private:
    QString teamFilePath(int teamId) const;
    QString teamAssetsPath(int teamId) const;
    QString m_workspacePath;
};
