#pragma once

#include <QString>

class ArsTeamRepository
{
public:
    explicit ArsTeamRepository(const QString &filePath = QString());

    bool ensureInitialized();
    QString filePath() const;

private:
    QString m_filePath;
};
