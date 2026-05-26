#pragma once

#include <QString>

class ArsAppSettings
{
public:
    explicit ArsAppSettings(const QString &workspaceRootPath);

    bool loadDefaultTeamId(int *outTeamId, QString *errorMessage = nullptr) const;
    bool saveDefaultTeamId(int teamId, QString *errorMessage = nullptr) const;
    QString settingsPath() const;

private:
    bool ensureConfigDir(QString *errorMessage = nullptr) const;
    QString m_workspaceRootPath;
};
