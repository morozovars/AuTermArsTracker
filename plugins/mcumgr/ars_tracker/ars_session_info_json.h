#ifndef ARS_SESSION_INFO_JSON_H
#define ARS_SESSION_INFO_JSON_H

#include <QString>

#include "ars_session_info.h"

class ArsSessionInfoJson
{
public:
		static bool saveSessionInfoJson(const QString &sessionPath,
																		const ArsSessionInfo &info,
																		QString *errorMessage = nullptr);
		static bool loadSessionInfoJson(const QString &sessionPath,
																		ArsSessionInfo *outInfo,
																		bool *outFileExists = nullptr,
																		bool *outHasPlannedSessionPeriod = nullptr,
																		QString *errorMessage = nullptr);
		static bool updateSessionInfoActualTimeJson(const QString &sessionPath,
																								const ArsSessionInfo::ArsSessionActualTime &actualTime,
																								QString *errorMessage = nullptr);
};

#endif // ARS_SESSION_INFO_JSON_H
