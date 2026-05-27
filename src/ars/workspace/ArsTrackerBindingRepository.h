#pragma once

#include "ArsTrackerBinding.h"

#include <QList>
#include <QString>
#include <QStringList>

class ArsTrackerBindingRepository
{
public:
    explicit ArsTrackerBindingRepository(const QString workspaceRootPath);

    QString bindingsPath() const;

    QList<ArsTrackerPlayerBinding> loadBindings(QStringList *warnings = nullptr) const;
    bool saveBindings(const QList<ArsTrackerPlayerBinding> &bindings, QString *errorMessage = nullptr) const;

    bool findBinding(int teamId,
                     const QString &pairId,
                     ArsTrackerPlayerBinding *outBinding,
                     QString *errorMessage = nullptr) const;

    bool upsertBinding(const ArsTrackerPlayerBinding &binding, QString *errorMessage = nullptr);

    bool removeBinding(int teamId,
                       const QString &pairId,
                       QString *errorMessage = nullptr);

    QList<ArsTrackerPlayerBinding> bindingsForTeam(int teamId,
                                                   QStringList *warnings = nullptr) const;

    QList<ArsTrackerPlayerBinding> bindingsForPlayer(const QString &playerId,
                                                     QStringList *warnings = nullptr) const;

private:
    QString m_workspaceRootPath;
};
