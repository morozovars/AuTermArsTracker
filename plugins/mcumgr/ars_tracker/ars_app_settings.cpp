#include "ars_app_settings.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

ArsAppSettings::ArsAppSettings(const QString &workspacePath)
    : m_workspacePath(workspacePath)
{
}

QString ArsAppSettings::settingsPath() const
{
    return QDir(m_workspacePath).filePath("config/settings.json");
}

bool ArsAppSettings::ensureConfigDir(QString *errorMessage) const
{
    const QString path = QDir(m_workspacePath).filePath("config");
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
    const QString path = settingsPath();
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
    QJsonParseError pe{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QString("Failed to parse settings.json: %1").arg(pe.errorString());
        }
        return false;
    }
    *outTeamId = doc.object().value("default_team_id").toInt(-1);
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
    const QString path = settingsPath();
    QJsonObject root;
    QFile readFile(path);
    if (readFile.exists() && readFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        QJsonParseError pe{};
        const QJsonDocument existing = QJsonDocument::fromJson(readFile.readAll(), &pe);
        if (pe.error == QJsonParseError::NoError && existing.isObject())
        {
            root = existing.object();
        }
    }
    root["default_team_id"] = teamId;
    QFile writeFile(path);
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
