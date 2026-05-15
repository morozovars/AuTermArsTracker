#pragma once

#include <QString>

class ArsPlayerRepository
{
public:
    explicit ArsPlayerRepository(const QString &filePath = QString());

    bool ensureInitialized();
    QString filePath() const;

private:
    QString m_filePath;
};
