#include "ArsTrackerBindingRepository.h"

#include <QFile>
#include <QDebug>

namespace
{
const QByteArray kDefaultTrackerBindingsJson = "{\n  \"schema_version\": 1,\n  \"bindings\": []\n}\n";
}

ArsTrackerBindingRepository::ArsTrackerBindingRepository(const QString &filePath)
    : m_filePath(filePath)
{
}

bool ArsTrackerBindingRepository::ensureInitialized()
{
    if (m_filePath.isEmpty())
    {
        qWarning() << "ArsTrackerBindingRepository initialization failed: file path is empty";
        return false;
    }

    QFile file(m_filePath);
    if (file.exists())
    {
        qDebug().noquote() << "ArsTrackerBindingRepository file path:" << m_filePath;
        return true;
    }

    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text) == false)
    {
        qWarning().noquote() << "ArsTrackerBindingRepository initialization failed: could not create file:" << m_filePath
                             << "reason:" << file.errorString();
        return false;
    }

    if (file.write(kDefaultTrackerBindingsJson) != kDefaultTrackerBindingsJson.size())
    {
        qWarning().noquote() << "ArsTrackerBindingRepository initialization failed: incomplete write for file:" << m_filePath
                             << "reason:" << file.errorString();
        return false;
    }

    qDebug().noquote() << "ArsTrackerBindingRepository file path:" << m_filePath;
    return true;
}

QString ArsTrackerBindingRepository::filePath() const
{
    return m_filePath;
}
