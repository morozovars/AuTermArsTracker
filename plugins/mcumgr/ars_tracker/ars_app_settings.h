#ifndef ARS_APP_SETTINGS_H
#define ARS_APP_SETTINGS_H

#include <QString>

class ArsAppSettings
{
public:
    explicit ArsAppSettings(const QString &workspacePath);

    bool loadDefaultTeamId(int *outTeamId, QString *errorMessage = nullptr) const;
    bool saveDefaultTeamId(int teamId, QString *errorMessage = nullptr) const;
    QString settingsPath() const;

private:
    bool ensureConfigDir(QString *errorMessage = nullptr) const;
    QString m_workspacePath;
};

#endif // ARS_APP_SETTINGS_H
