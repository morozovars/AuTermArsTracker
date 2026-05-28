#ifndef ARS_REPORT_SESSION_DATA_BUILDER_H
#define ARS_REPORT_SESSION_DATA_BUILDER_H

#include <QString>
#include <QStringList>

struct ArsReportSessionDataBuildResult
{
    bool ok = false;
    QString sessionDataPath;
    QString error;
    QStringList warnings;
};

ArsReportSessionDataBuildResult buildArsReportSessionDataJson(const QString sessionPath);

#endif // ARS_REPORT_SESSION_DATA_BUILDER_H
