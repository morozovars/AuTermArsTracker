#pragma once

#include <QString>

class ArsSessionRepository
{
public:
    explicit ArsSessionRepository(const QString &sessionsPath = QString());

    bool ensureInitialized();
    QString sessionsPath() const;

private:
    QString m_sessionsPath;
};
