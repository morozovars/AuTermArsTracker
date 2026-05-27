#pragma once

#include <QList>
#include <QString>
#include <QStringList>

struct ArsSessionTrackerPair
{
    QString pairId;
    QString leftTrackerSerial;
    QString rightTrackerSerial;
};

struct ArsSessionPairAssignment
{
    QString pairId;
    QString leftTrackerSerial;
    QString rightTrackerSerial;
    QString playerId;
    QString playerName;
    int teamId = -1;
    QString source;
    bool isOverride = false;
};

class ArsSessionPlayerBindingResolver
{
public:
    explicit ArsSessionPlayerBindingResolver(const QString &workspaceRootPath);

    QList<ArsSessionPairAssignment> resolveAssignments(
        const QString &sessionPath,
        int sessionTeamId,
        const QList<ArsSessionTrackerPair> &pairs,
        const QList<ArsSessionPairAssignment> &existingAssignments,
        QStringList *warnings = nullptr) const;

private:
    QString m_workspaceRootPath;
};
