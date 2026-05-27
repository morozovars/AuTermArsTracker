#pragma once

#include "ArsPlayer.h"

#include <QList>
#include <QString>
#include <QStringList>

class ArsPlayerRepository
{
public:
    explicit ArsPlayerRepository(const QString &workspaceRootPath);

    QString playersPath() const;
    QString playerFilePath(const QString &playerId) const;
    QString playerAssetsPath() const;

    QList<ArsPlayer> loadPlayers(QStringList *warnings = nullptr) const;
    QList<ArsPlayer> loadPlayersForTeam(int teamId, QStringList *warnings = nullptr) const;

    bool loadPlayer(const QString &playerId, ArsPlayer *outPlayer, QString *errorMessage = nullptr) const;
    bool savePlayer(const ArsPlayer &player, QString *errorMessage = nullptr) const;
    bool deletePlayer(const QString &playerId, QString *errorMessage = nullptr) const;

    QString nextPlayerId(QStringList *warnings = nullptr) const;

    bool copyPlayerPhotoToWorkspace(const QString &playerId,
                                    const QString &sourcePhotoPath,
                                    QString *outRelativePhotoPath,
                                    QString *errorMessage = nullptr) const;

    QString resolvePhotoAbsolutePath(const QString &photoPath) const;

private:
    bool ensurePlayersDir(QString *errorMessage = nullptr) const;
    bool ensurePlayerAssetsDir(QString *errorMessage = nullptr) const;
    QString playerAssetPrefix(const QString &playerId) const;
    bool removePlayerAssetsByPrefix(const QString &playerId, QString *errorMessage = nullptr) const;
    QString m_workspaceRootPath;
};
