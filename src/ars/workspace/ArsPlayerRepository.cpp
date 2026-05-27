#include "ArsPlayerRepository.h"

#include "ArsAppSettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

namespace
{
QString normalize_foot_value(const QString &raw)
{
    const QString value = raw.trimmed();
    if (value.compare("L", Qt::CaseInsensitive) == 0 || value.compare("Left", Qt::CaseInsensitive) == 0 ||
        value.compare(QString::fromUtf8("Левая"), Qt::CaseInsensitive) == 0)
    {
        return "L";
    }
    return "R";
}

QJsonObject player_to_json(const ArsPlayer &player)
{
    QJsonObject o;
    o["playerId"] = player.playerId;
    o["surname"] = player.surname;
    o["name"] = player.name;
    o["photoPath"] = player.photoPath;
    o["number"] = player.number;
    o["birthDate"] = player.birthDate.isValid() ? player.birthDate.toString(Qt::ISODate) : QString();
    o["position"] = player.position;
    o["dominantFoot"] = normalize_foot_value(player.dominantFoot);
    o["heightCm"] = player.heightCm;
    o["weightKg"] = player.weightKg;
    o["teamId"] = player.teamId;
    QJsonObject norms;
    norms["maxSpeedMps"] = player.personalNorms.maxSpeedMps;
    norms["maxShotLoadG"] = player.personalNorms.maxShotLoadG;
    o["personalNorms"] = norms;
    return o;
}

ArsPlayer player_from_json(const QJsonObject &o)
{
    ArsPlayer player;
    player.playerId = o.value("playerId").toString().trimmed();
    player.surname = o.value("surname").toString().trimmed();
    player.name = o.value("name").toString().trimmed();
    player.photoPath = o.value("photoPath").toString().trimmed();
    player.number = o.value("number").toInt(0);
    player.birthDate = QDate::fromString(o.value("birthDate").toString().trimmed(), Qt::ISODate);
    player.position = o.value("position").toString().trimmed();
    player.dominantFoot = normalize_foot_value(o.value("dominantFoot").toString());
    player.heightCm = o.value("heightCm").toInt(0);
    player.weightKg = o.value("weightKg").toInt(0);
    player.teamId = o.value("teamId").toInt(-1);
    const QJsonObject norms = o.value("personalNorms").toObject();
    player.personalNorms.maxSpeedMps = norms.value("maxSpeedMps").toDouble(0.0);
    player.personalNorms.maxShotLoadG = norms.value("maxShotLoadG").toDouble(0.0);
    return player;
}
}

ArsPlayerRepository::ArsPlayerRepository(const QString &workspaceRootPath)
    : m_workspaceRootPath(workspaceRootPath)
{
}

QString ArsPlayerRepository::playersPath() const
{
    return QDir(m_workspaceRootPath).filePath("players");
}

QString ArsPlayerRepository::playerAssetsPath() const
{
    return QDir(m_workspaceRootPath).filePath("assets/players");
}

QString ArsPlayerRepository::playerFilePath(const QString &playerId) const
{
    return QDir(playersPath()).filePath(QString("%1.json").arg(playerId));
}

bool ArsPlayerRepository::ensurePlayersDir(QString *errorMessage) const
{
    const QString path = playersPath();
    if (QDir(path).exists() || QDir().mkpath(path))
    {
        return true;
    }
    if (errorMessage != nullptr)
    {
        *errorMessage = QString("Failed to create players directory: %1").arg(path);
    }
    return false;
}

bool ArsPlayerRepository::ensurePlayerAssetsDir(QString *errorMessage) const
{
    const QString path = playerAssetsPath();
    if (QDir(path).exists() || QDir().mkpath(path))
    {
        return true;
    }
    if (errorMessage != nullptr)
    {
        *errorMessage = QString("Failed to create player assets directory: %1").arg(path);
    }
    return false;
}

QList<ArsPlayer> ArsPlayerRepository::loadPlayers(QStringList *warnings) const
{
    QList<ArsPlayer> players;
    QString dirError;
    if (!ensurePlayersDir(&dirError))
    {
        if (warnings != nullptr)
        {
            warnings->append(dirError);
        }
        return players;
    }

    const QDir dir(playersPath());
    const QFileInfoList files = dir.entryInfoList(QStringList() << "*.json", QDir::Files, QDir::Name);
    for (const QFileInfo &fi : files)
    {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            if (warnings != nullptr)
            {
                warnings->append(QString("Ars Player load warning file=%1 error=%2").arg(fi.fileName(), f.errorString()));
            }
            continue;
        }
        QJsonParseError pe{};
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
        if (pe.error != QJsonParseError::NoError || !doc.isObject())
        {
            if (warnings != nullptr)
            {
                warnings->append(QString("Ars Player load warning file=%1 error=%2").arg(fi.fileName(), pe.errorString()));
            }
            continue;
        }
        ArsPlayer player = player_from_json(doc.object());
        if (player.playerId.trimmed().isEmpty())
        {
            player.playerId = fi.completeBaseName();
        }
        if (player.playerId.trimmed().isEmpty())
        {
            if (warnings != nullptr)
            {
                warnings->append(QString("Ars Player load warning file=%1 error=invalid playerId").arg(fi.fileName()));
            }
            continue;
        }
        players.append(player);
    }

    return players;
}

QList<ArsPlayer> ArsPlayerRepository::loadPlayersForTeam(int teamId, QStringList *warnings) const
{
    QList<ArsPlayer> players = loadPlayers(warnings);
    players.erase(std::remove_if(players.begin(), players.end(), [teamId](const ArsPlayer &p) { return p.teamId != teamId; }),
                  players.end());
    return players;
}

bool ArsPlayerRepository::loadPlayer(const QString &playerId, ArsPlayer *outPlayer, QString *errorMessage) const
{
    if (outPlayer == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Output pointer is null";
        }
        return false;
    }
    const QString id = playerId.trimmed();
    if (id.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "playerId is empty";
        }
        return false;
    }
    QFile file(playerFilePath(id));
    if (!file.exists())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QString("Player file not found for id=%1").arg(id);
        }
        return false;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QString("Failed to read player file: %1").arg(file.errorString());
        }
        return false;
    }
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QString("Invalid player json: %1").arg(pe.errorString());
        }
        return false;
    }
    ArsPlayer player = player_from_json(doc.object());
    if (player.playerId.trimmed().isEmpty())
    {
        player.playerId = id;
    }
    *outPlayer = player;
    return true;
}

bool ArsPlayerRepository::savePlayer(const ArsPlayer &player, QString *errorMessage) const
{
    const QString id = player.playerId.trimmed();
    if (id.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Invalid playerId";
        }
        return false;
    }
    QString dirError;
    if (!ensurePlayersDir(&dirError))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = dirError;
        }
        return false;
    }
    QFile file(playerFilePath(id));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QString("Failed to write player file: %1").arg(file.errorString());
        }
        return false;
    }
    file.write(QJsonDocument(player_to_json(player)).toJson(QJsonDocument::Indented));
    return true;
}

QString ArsPlayerRepository::playerAssetPrefix(const QString &playerId) const
{
    return QString("player-%1.").arg(playerId.trimmed());
}

bool ArsPlayerRepository::removePlayerAssetsByPrefix(const QString &playerId, QString *errorMessage) const
{
    const QString id = playerId.trimmed();
    if (id.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "playerId is empty";
        }
        return false;
    }
    QDir assetsDir(playerAssetsPath());
    if (!assetsDir.exists())
    {
        return true;
    }
    const QString prefix = playerAssetPrefix(id);
    const QFileInfoList files = assetsDir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &fileInfo : files)
    {
        if (!fileInfo.fileName().startsWith(prefix, Qt::CaseInsensitive))
        {
            continue;
        }
        if (!QFile::remove(fileInfo.absoluteFilePath()))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QString("Failed to delete player asset: %1").arg(fileInfo.absoluteFilePath());
            }
            return false;
        }
    }
    return true;
}

bool ArsPlayerRepository::deletePlayer(const QString &playerId, QString *errorMessage) const
{
    const QString id = playerId.trimmed();
    if (id.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Invalid playerId";
        }
        return false;
    }

    const QString playersDirPath = QDir::cleanPath(playersPath());
    const QString jsonPath = QDir::cleanPath(playerFilePath(id));
    if (!jsonPath.startsWith(playersDirPath + QDir::separator(), Qt::CaseInsensitive))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Safety check failed: player json path is outside players directory";
        }
        return false;
    }

    if (QFileInfo::exists(jsonPath) && !QFile::remove(jsonPath))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QString("Failed to delete player JSON: %1").arg(jsonPath);
        }
        return false;
    }

    QString assetError;
    if (!removePlayerAssetsByPrefix(id, &assetError))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = assetError;
        }
        return false;
    }
    return true;
}

QString ArsPlayerRepository::nextPlayerId(QStringList *warnings) const
{
    int maxId = 0;
    const QList<ArsPlayer> players = loadPlayers(warnings);
    for (const ArsPlayer &player : players)
    {
        bool ok = false;
        const int id = player.playerId.toInt(&ok);
        if (ok)
        {
            maxId = std::max(maxId, id);
        }
    }

    int lastPlayerId = -1;
    QString settingsError;
    if (!ArsAppSettings(m_workspaceRootPath).loadLastPlayerId(&lastPlayerId, &settingsError))
    {
        if (warnings != nullptr && !settingsError.trimmed().isEmpty())
        {
            warnings->append(QString("Ars Player load warning settings error=%1").arg(settingsError));
        }
    }
    maxId = std::max(maxId, lastPlayerId);
    return QString::number(maxId + 1);
}

bool ArsPlayerRepository::copyPlayerPhotoToWorkspace(const QString &playerId,
                                                     const QString &sourcePhotoPath,
                                                     QString *outRelativePhotoPath,
                                                     QString *errorMessage) const
{
    if (outRelativePhotoPath != nullptr)
    {
        outRelativePhotoPath->clear();
    }
    const QString id = playerId.trimmed();
    if (id.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Invalid playerId for photo copy";
        }
        return false;
    }
    const QString source = sourcePhotoPath.trimmed();
    if (source.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Photo source path is empty";
        }
        return false;
    }
    const QFileInfo sourceInfo(source);
    if (!sourceInfo.exists() || !sourceInfo.isFile())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QString("Photo source does not exist: %1").arg(source);
        }
        return false;
    }
    const QString extension = sourceInfo.suffix().toLower();
    const QSet<QString> allowed = {"png", "jpg", "jpeg", "bmp", "webp"};
    if (!allowed.contains(extension))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QString("Unsupported photo extension: .%1").arg(extension);
        }
        return false;
    }

    QString assetsError;
    if (!ensurePlayerAssetsDir(&assetsError))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = assetsError;
        }
        return false;
    }

    QString cleanupError;
    if (!removePlayerAssetsByPrefix(id, &cleanupError))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = cleanupError;
        }
        return false;
    }

    const QString relativePath = QString("assets/players/player-%1.%2").arg(id, extension);
    const QString targetPath = QDir::cleanPath(QDir(m_workspaceRootPath).filePath(relativePath));
    if (!QFile::copy(sourceInfo.absoluteFilePath(), targetPath))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QString("Failed to copy photo to workspace: %1").arg(targetPath);
        }
        return false;
    }
    if (outRelativePhotoPath != nullptr)
    {
        *outRelativePhotoPath = QDir::cleanPath(relativePath);
    }
    return true;
}

QString ArsPlayerRepository::resolvePhotoAbsolutePath(const QString &photoPath) const
{
    const QString trimmed = photoPath.trimmed();
    if (trimmed.isEmpty())
    {
        return QString();
    }
    const QFileInfo info(trimmed);
    if (info.isAbsolute())
    {
        return QDir::cleanPath(info.absoluteFilePath());
    }
    return QDir::cleanPath(QDir(m_workspaceRootPath).filePath(trimmed));
}
