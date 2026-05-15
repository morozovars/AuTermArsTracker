#include "ArsSessionRepository.h"

#include <QDir>
#include <QDebug>

ArsSessionRepository::ArsSessionRepository(const QString &sessionsPath)
    : m_sessionsPath(QDir::cleanPath(sessionsPath))
{
}

bool ArsSessionRepository::ensureInitialized()
{
    if (m_sessionsPath.isEmpty())
    {
        qWarning() << "ArsSessionRepository initialization failed: sessions path is empty";
        return false;
    }

    QDir dir(m_sessionsPath);
    if (dir.exists())
    {
        qDebug().noquote() << "ArsSessionRepository sessions path:" << m_sessionsPath;
        return true;
    }

    if (QDir().mkpath(m_sessionsPath) == false)
    {
        qWarning().noquote() << "ArsSessionRepository initialization failed: could not create sessions directory:"
                             << m_sessionsPath;
        return false;
    }

    qDebug().noquote() << "ArsSessionRepository sessions path:" << m_sessionsPath;
    return true;
}

QString ArsSessionRepository::sessionsPath() const
{
    return m_sessionsPath;
}
