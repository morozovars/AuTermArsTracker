#include "ArsPlayerRepository.h"

#include <QFile>
#include <QDebug>

namespace
{
const QByteArray kDefaultPlayersJson = "{\n  \"schema_version\": 1,\n  \"players\": []\n}\n";
}

ArsPlayerRepository::ArsPlayerRepository(const QString &filePath)
    : m_filePath(filePath)
{
}

bool ArsPlayerRepository::ensureInitialized()
{
    if (m_filePath.isEmpty())
    {
        qWarning() << "ArsPlayerRepository initialization failed: file path is empty";
        return false;
    }

    QFile file(m_filePath);
    if (file.exists())
    {
        qDebug().noquote() << "ArsPlayerRepository file path:" << m_filePath;
        return true;
    }

    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text) == false)
    {
        qWarning().noquote() << "ArsPlayerRepository initialization failed: could not create file:" << m_filePath
                             << "reason:" << file.errorString();
        return false;
    }

    if (file.write(kDefaultPlayersJson) != kDefaultPlayersJson.size())
    {
        qWarning().noquote() << "ArsPlayerRepository initialization failed: incomplete write for file:" << m_filePath
                             << "reason:" << file.errorString();
        return false;
    }

    qDebug().noquote() << "ArsPlayerRepository file path:" << m_filePath;
    return true;
}

QString ArsPlayerRepository::filePath() const
{
    return m_filePath;
}
