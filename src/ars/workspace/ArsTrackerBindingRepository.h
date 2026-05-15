#pragma once

#include <QString>

class ArsTrackerBindingRepository
{
public:
    explicit ArsTrackerBindingRepository(const QString &filePath = QString());

    bool ensureInitialized();
    QString filePath() const;

private:
    QString m_filePath;
};
