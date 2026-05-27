#include "ArsAppSettings.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
bool load_root_object(const QString &path, QJsonObject *outRoot, QString *errorMessage)
{
    if (outRoot == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Output root object is null";
        }
        return false;
    }
    *outRoot = QJsonObject();
    QFile file(path);
    if (!file.exists())
    {
        return true;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QString("Failed to read settings: %1").arg(file.errorString());
        }
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QString("settings.json is invalid: %1").arg(parseError.errorString());
        }
        return false;
    }
    *outRoot = doc.object();
    return true;
}
}

ArsAppSettings::ArsAppSettings(const QString &workspaceRootPath)
    : m_workspaceRootPath(workspaceRootPath)
{
}

QString ArsAppSettings::settingsPath() const
{
    return QDir(m_workspaceRootPath).filePath("config/settings.json");
}

bool ArsAppSettings::ensureConfigDir(QString *errorMessage) const
{
    const QString path = QDir(m_workspaceRootPath).filePath("config");
    if (QDir(path).exists() || QDir().mkpath(path))
    {
        return true;
    }
    if (errorMessage != nullptr)
    {
        *errorMessage = QString("Failed to create config directory: %1").arg(path);
    }
    return false;
}

bool ArsAppSettings::loadDefaultTeamId(int *outTeamId, QString *errorMessage) const
{
    if (outTeamId == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Output pointer is null";
        }
        return false;
    }
    *outTeamId = -1;

    QJsonObject root;
    if (!load_root_object(settingsPath(), &root, errorMessage))
    {
        return false;
    }

    const QJsonValue value = root.value("default_team_id");
    if (value.isDouble())
    {
        *outTeamId = value.toInt(-1);
    }
    else
    {
        *outTeamId = -1;
    }
    return true;
}

bool ArsAppSettings::saveDefaultTeamId(int teamId, QString *errorMessage) const
{
    QString dirError;
    if (!ensureConfigDir(&dirError))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = dirError;
        }
        return false;
    }

    QJsonObject root;
    QString loadError;
    if (!load_root_object(settingsPath(), &root, &loadError))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "settings.json is invalid.";
        }
        return false;
    }

    root["default_team_id"] = teamId;
    QFile writeFile(settingsPath());
    if (!writeFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QString("Failed to write settings: %1").arg(writeFile.errorString());
        }
        return false;
    }

    writeFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool ArsAppSettings::clearDefaultTeamId(QString *errorMessage) const
{
    return saveDefaultTeamId(-1, errorMessage);
}

bool ArsAppSettings::loadLastTeamId(int *outTeamId, QString *errorMessage) const
{
    if (outTeamId == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Output pointer is null";
        }
        return false;
    }
    *outTeamId = -1;
    QJsonObject root;
    if (!load_root_object(settingsPath(), &root, errorMessage))
    {
        return false;
    }
    const QJsonValue value = root.value("last_team_id");
    if (value.isDouble())
    {
        *outTeamId = value.toInt(-1);
    }
    return true;
}

bool ArsAppSettings::saveLastTeamId(int teamId, QString *errorMessage) const
{
    QString dirError;
    if (!ensureConfigDir(&dirError))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = dirError;
        }
        return false;
    }
    QJsonObject root;
    QString loadError;
    if (!load_root_object(settingsPath(), &root, &loadError))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "settings.json is invalid.";
        }
        return false;
    }
    root["last_team_id"] = teamId;
    QFile writeFile(settingsPath());
    if (!writeFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QString("Failed to write settings: %1").arg(writeFile.errorString());
        }
        return false;
    }
    writeFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool ArsAppSettings::loadLastPlayerId(int *outPlayerId, QString *errorMessage) const
{
    if (outPlayerId == nullptr)
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "Output pointer is null";
        }
        return false;
    }
    *outPlayerId = -1;
    QJsonObject root;
    if (!load_root_object(settingsPath(), &root, errorMessage))
    {
        return false;
    }
    const QJsonValue value = root.value("last_player_id");
    if (value.isDouble())
    {
        *outPlayerId = value.toInt(-1);
    }
    return true;
}

bool ArsAppSettings::saveLastPlayerId(int playerId, QString *errorMessage) const
{
    QString dirError;
    if (!ensureConfigDir(&dirError))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = dirError;
        }
        return false;
    }
    QJsonObject root;
    QString loadError;
    if (!load_root_object(settingsPath(), &root, &loadError))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = "settings.json is invalid.";
        }
        return false;
    }
    root["last_player_id"] = playerId;
    QFile writeFile(settingsPath());
    if (!writeFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QString("Failed to write settings: %1").arg(writeFile.errorString());
        }
        return false;
    }
    writeFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}
