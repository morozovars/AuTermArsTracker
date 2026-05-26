#include "ArsAppSettings.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

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

    QFile file(settingsPath());
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

    const QJsonValue value = doc.object().value("default_team_id");
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
    QFile readFile(settingsPath());
    if (readFile.exists())
    {
        if (!readFile.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = QString("Failed to read settings: %1").arg(readFile.errorString());
            }
            return false;
        }

        QJsonParseError parseError{};
        const QJsonDocument existing = QJsonDocument::fromJson(readFile.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !existing.isObject())
        {
            if (errorMessage != nullptr)
            {
                *errorMessage = "settings.json is invalid.";
            }
            return false;
        }
        root = existing.object();
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
