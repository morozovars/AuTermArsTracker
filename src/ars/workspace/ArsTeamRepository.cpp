#include "ArsTeamRepository.h"

#include <QFile>
#include <QDebug>

namespace
{
const QByteArray kDefaultTeamJson = "{\n  \"schema_version\": 1,\n  \"team\": null\n}\n";
}

ArsTeamRepository::ArsTeamRepository(const QString &filePath)
    : m_filePath(filePath)
{
}

bool ArsTeamRepository::ensureInitialized()
{
    if (m_filePath.isEmpty())
    {
        qWarning() << "ArsTeamRepository initialization failed: file path is empty";
        return false;
    }

    QFile file(m_filePath);
    if (file.exists())
    {
        qDebug().noquote() << "ArsTeamRepository file path:" << m_filePath;
        return true;
    }

    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text) == false)
    {
        qWarning().noquote() << "ArsTeamRepository initialization failed: could not create file:" << m_filePath
                             << "reason:" << file.errorString();
        return false;
    }

    if (file.write(kDefaultTeamJson) != kDefaultTeamJson.size())
    {
        qWarning().noquote() << "ArsTeamRepository initialization failed: incomplete write for file:" << m_filePath
                             << "reason:" << file.errorString();
        return false;
    }

    qDebug().noquote() << "ArsTeamRepository file path:" << m_filePath;
    return true;
}

QString ArsTeamRepository::filePath() const
{
    return m_filePath;
}
