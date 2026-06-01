#include "ArsPlayerPosition.h"

#include <QHash>

QList<ArsPlayerPositionInfo> arsPlayerPositions()
{
    return {
        {QStringLiteral("goalkeeper"), QStringLiteral("Вратарь")},
        {QStringLiteral("wide_defender"), QStringLiteral("Фланговый защитник")},
        {QStringLiteral("central_defender"), QStringLiteral("Центральный защитник")},
        {QStringLiteral("wide_midfielder"), QStringLiteral("Фланговый полузащитник")},
        {QStringLiteral("central_midfielder"), QStringLiteral("Центральный полузащитник")},
        {QStringLiteral("forward"), QStringLiteral("Нападающий")}
    };
}

QStringList arsPlayerPositionKeys()
{
    QStringList out;
    const QList<ArsPlayerPositionInfo> positions = arsPlayerPositions();
    for (const ArsPlayerPositionInfo &position : positions)
    {
        out.append(position.key);
    }
    return out;
}

QString arsPlayerPositionDisplayName(const QString &key)
{
    const QString trimmed = key.trimmed();
    const QList<ArsPlayerPositionInfo> positions = arsPlayerPositions();
    for (const ArsPlayerPositionInfo &position : positions)
    {
        if (QString::compare(position.key, trimmed, Qt::CaseInsensitive) == 0)
        {
            return position.displayName;
        }
    }
    return QStringLiteral("Центральный полузащитник");
}

QString arsPlayerPositionKeyFromDisplayName(const QString &displayName)
{
    const QString trimmed = displayName.trimmed();
    const QList<ArsPlayerPositionInfo> positions = arsPlayerPositions();
    for (const ArsPlayerPositionInfo &position : positions)
    {
        if (QString::compare(position.displayName, trimmed, Qt::CaseInsensitive) == 0)
        {
            return position.key;
        }
    }
    return QStringLiteral("central_midfielder");
}

QString arsNormalizePlayerPositionKey(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized.isEmpty())
    {
        return QStringLiteral("central_midfielder");
    }

    static const QHash<QString, QString> map = {
        {QStringLiteral("goalkeeper"), QStringLiteral("goalkeeper")},
        {QStringLiteral("вратарь"), QStringLiteral("goalkeeper")},
        {QStringLiteral("wide_defender"), QStringLiteral("wide_defender")},
        {QStringLiteral("фланговый защитник"), QStringLiteral("wide_defender")},
        {QStringLiteral("central_defender"), QStringLiteral("central_defender")},
        {QStringLiteral("центральный защитник"), QStringLiteral("central_defender")},
        {QStringLiteral("defender"), QStringLiteral("central_defender")},
        {QStringLiteral("защитник"), QStringLiteral("central_defender")},
        {QStringLiteral("wide_midfielder"), QStringLiteral("wide_midfielder")},
        {QStringLiteral("фланговый полузащитник"), QStringLiteral("wide_midfielder")},
        {QStringLiteral("central_midfielder"), QStringLiteral("central_midfielder")},
        {QStringLiteral("центральный полузащитник"), QStringLiteral("central_midfielder")},
        {QStringLiteral("midfielder"), QStringLiteral("central_midfielder")},
        {QStringLiteral("полузащитник"), QStringLiteral("central_midfielder")},
        {QStringLiteral("forward"), QStringLiteral("forward")},
        {QStringLiteral("нападающий"), QStringLiteral("forward")},
        {QStringLiteral("нападющий"), QStringLiteral("forward")},
        {QStringLiteral("форвард"), QStringLiteral("forward")}
    };

    const auto it = map.constFind(normalized);
    if (it != map.cend())
    {
        return it.value();
    }

    return QStringLiteral("central_midfielder");
}

QString arsPlayerPositionDisplayNameFromAny(const QString &value)
{
    return arsPlayerPositionDisplayName(arsNormalizePlayerPositionKey(value));
}
