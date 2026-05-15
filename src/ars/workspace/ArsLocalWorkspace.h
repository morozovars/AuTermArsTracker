#pragma once

#include <QString>

class ArsLocalWorkspace
{
public:
    ArsLocalWorkspace();

    bool initialize();

    QString rootPath() const;
    QString sessionsPath() const;
    QString teamFilePath() const;
    QString playersFilePath() const;
    QString trackerBindingsFilePath() const;

private:
    QString m_rootPath;
    QString m_sessionsPath;
    QString m_teamFilePath;
    QString m_playersFilePath;
    QString m_trackerBindingsFilePath;
};
