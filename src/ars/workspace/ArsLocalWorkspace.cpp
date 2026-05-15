#include "ArsLocalWorkspace.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QDebug>

namespace
{
const QByteArray kTeamJson = "{\n  \"schema_version\": 1,\n  \"team\": null\n}\n";
const QByteArray kPlayersJson = "{\n  \"schema_version\": 1,\n  \"players\": []\n}\n";
const QByteArray kTrackerBindingsJson = "{\n  \"schema_version\": 1,\n  \"bindings\": []\n}\n";

bool ensureDirectoryExists(const QString &path, const QString &label)
{
    QDir dir(path);
    if (dir.exists())
    {
        return true;
    }

    if (QDir().mkpath(path) == false)
    {
        qWarning().noquote() << "Workspace initialization failed: could not create" << label << "directory:" << path;
        return false;
    }

    return true;
}

bool ensureJsonFileExists(const QString &filePath, const QByteArray &defaultContent)
{
    QFile file(filePath);
    if (file.exists())
    {
        return true;
    }

    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text) == false)
    {
        qWarning().noquote() << "Workspace initialization failed: could not create file:" << filePath
                             << "reason:" << file.errorString();
        return false;
    }

    const qint64 bytesWritten = file.write(defaultContent);
    if (bytesWritten != defaultContent.size())
    {
        qWarning().noquote() << "Workspace initialization failed: incomplete write for file:" << filePath
                             << "written:" << bytesWritten << "expected:" << defaultContent.size()
                             << "reason:" << file.errorString();
        return false;
    }

    return true;
}
}

ArsLocalWorkspace::ArsLocalWorkspace()
{
}

bool ArsLocalWorkspace::initialize()
{
    const QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty())
    {
        qWarning() << "Workspace initialization failed: AppDataLocation is empty";
        return false;
    }

    m_rootPath = QDir::cleanPath(appDataPath + QDir::separator() + "ars_workspace");
    m_sessionsPath = QDir::cleanPath(m_rootPath + QDir::separator() + "sessions");
    m_teamFilePath = QDir::cleanPath(m_rootPath + QDir::separator() + "team.json");
    m_playersFilePath = QDir::cleanPath(m_rootPath + QDir::separator() + "players.json");
    m_trackerBindingsFilePath = QDir::cleanPath(m_rootPath + QDir::separator() + "tracker_bindings.json");

    qDebug().noquote() << "ArsLocalWorkspace root path:" << m_rootPath;
    qDebug().noquote() << "ArsLocalWorkspace team file:" << m_teamFilePath;
    qDebug().noquote() << "ArsLocalWorkspace players file:" << m_playersFilePath;
    qDebug().noquote() << "ArsLocalWorkspace tracker bindings file:" << m_trackerBindingsFilePath;
    qDebug().noquote() << "ArsLocalWorkspace sessions path:" << m_sessionsPath;

    if (ensureDirectoryExists(m_rootPath, "workspace root") == false)
    {
        return false;
    }

    if (ensureDirectoryExists(m_sessionsPath, "sessions") == false)
    {
        return false;
    }

    if (ensureJsonFileExists(m_teamFilePath, kTeamJson) == false)
    {
        return false;
    }

    if (ensureJsonFileExists(m_playersFilePath, kPlayersJson) == false)
    {
        return false;
    }

    if (ensureJsonFileExists(m_trackerBindingsFilePath, kTrackerBindingsJson) == false)
    {
        return false;
    }

    return true;
}

QString ArsLocalWorkspace::rootPath() const
{
    return m_rootPath;
}

QString ArsLocalWorkspace::sessionsPath() const
{
    return m_sessionsPath;
}

QString ArsLocalWorkspace::teamFilePath() const
{
    return m_teamFilePath;
}

QString ArsLocalWorkspace::playersFilePath() const
{
    return m_playersFilePath;
}

QString ArsLocalWorkspace::trackerBindingsFilePath() const
{
    return m_trackerBindingsFilePath;
}
