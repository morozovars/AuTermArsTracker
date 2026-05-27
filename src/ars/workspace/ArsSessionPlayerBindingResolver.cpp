#include "ArsSessionPlayerBindingResolver.h"

#include "ArsPlayerRepository.h"
#include "ArsTrackerBindingRepository.h"

#include <QDebug>

ArsSessionPlayerBindingResolver::ArsSessionPlayerBindingResolver(const QString &workspaceRootPath)
    : m_workspaceRootPath(workspaceRootPath)
{
}

QList<ArsSessionPairAssignment> ArsSessionPlayerBindingResolver::resolveAssignments(
    const QString &sessionPath,
    int sessionTeamId,
    const QList<ArsSessionTrackerPair> &pairs,
    const QList<ArsSessionPairAssignment> &existingAssignments,
    QStringList *warnings) const
{
    Q_UNUSED(sessionPath);
    qDebug() << "Session assignments resolve begin session=" << sessionPath << "teamId=" << sessionTeamId << "pairCount=" << pairs.size();
    qDebug() << "Session assignments existing count=" << existingAssignments.size();

    ArsPlayerRepository playerRepository(m_workspaceRootPath);
    ArsTrackerBindingRepository bindingRepository(m_workspaceRootPath);

    QStringList playerWarnings;
    const QList<ArsPlayer> players = playerRepository.loadPlayers(&playerWarnings);
    for (const QString &warning : playerWarnings)
    {
        if (warnings != nullptr) warnings->append(warning);
        qWarning().noquote() << warning;
    }

    QStringList bindingWarnings;
    const QList<ArsTrackerPlayerBinding> teamBindings = bindingRepository.bindingsForTeam(sessionTeamId, &bindingWarnings);
    for (const QString &warning : bindingWarnings)
    {
        if (warnings != nullptr) warnings->append(warning);
        qWarning().noquote() << warning;
    }

    auto findPlayerById = [&players](const QString &playerId) -> const ArsPlayer * {
        for (const ArsPlayer &player : players)
        {
            if (player.playerId == playerId) return &player;
        }
        return nullptr;
    };

    auto findManualExisting = [&existingAssignments](const QString &pairId) -> const ArsSessionPairAssignment * {
        for (const ArsSessionPairAssignment &assignment : existingAssignments)
        {
            if (assignment.pairId.compare(pairId, Qt::CaseInsensitive) == 0 &&
                (assignment.isOverride || assignment.source.compare("manual", Qt::CaseInsensitive) == 0))
            {
                return &assignment;
            }
        }
        return nullptr;
    };

    auto findGlobalBinding = [&teamBindings](const QString &pairId) -> const ArsTrackerPlayerBinding * {
        for (const ArsTrackerPlayerBinding &binding : teamBindings)
        {
            if (binding.pairId.compare(pairId, Qt::CaseInsensitive) == 0) return &binding;
        }
        return nullptr;
    };

    QList<ArsSessionPairAssignment> result;
    for (const ArsSessionTrackerPair &pair : pairs)
    {
        ArsSessionPairAssignment assignment;
        assignment.pairId = pair.pairId;
        assignment.leftTrackerSerial = pair.leftTrackerSerial;
        assignment.rightTrackerSerial = pair.rightTrackerSerial;
        assignment.teamId = sessionTeamId;

        const ArsSessionPairAssignment *manual = findManualExisting(pair.pairId);
        if (manual != nullptr)
        {
            assignment.playerId = manual->playerId;
            assignment.playerName = manual->playerName;
            assignment.source = "manual";
            assignment.isOverride = true;
            const ArsPlayer *player = findPlayerById(assignment.playerId);
            if (player == nullptr || player->teamId != sessionTeamId)
            {
                const QString warning = QString("Session assignment manual player mismatch pairId=%1 playerId=%2 teamId=%3")
                                            .arg(assignment.pairId, assignment.playerId)
                                            .arg(sessionTeamId);
                if (warnings != nullptr) warnings->append(warning);
            }
            qDebug() << "Session assignment manual kept pairId=" << assignment.pairId << "playerId=" << assignment.playerId;
            result.append(assignment);
            continue;
        }

        const ArsTrackerPlayerBinding *binding = findGlobalBinding(pair.pairId);
        if (binding != nullptr)
        {
            assignment.playerId = binding->playerId;
            const ArsPlayer *player = findPlayerById(binding->playerId);
            if (player != nullptr && player->teamId == sessionTeamId)
            {
                assignment.playerName = QString("%1 %2").arg(player->surname, player->name).trimmed();
                assignment.source = "global";
                assignment.isOverride = false;
                qDebug() << "Session assignment global resolved pairId=" << assignment.pairId
                         << "playerId=" << assignment.playerId << "playerName=" << assignment.playerName;
            }
            else
            {
                assignment.playerName.clear();
                assignment.source = "missing-player";
                assignment.isOverride = false;
                qWarning() << "Session assignment missing player pairId=" << assignment.pairId << "playerId=" << assignment.playerId;
            }
        }
        else
        {
            assignment.playerId.clear();
            assignment.playerName.clear();
            assignment.source = "unresolved";
            assignment.isOverride = false;
            qDebug() << "Session assignment unresolved pairId=" << assignment.pairId;
        }
        result.append(assignment);
    }
    return result;
}
