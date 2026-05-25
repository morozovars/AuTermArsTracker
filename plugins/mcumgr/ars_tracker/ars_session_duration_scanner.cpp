#include "ars_session_duration_scanner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <limits>

namespace
{
bool parse_uint_field(const QString &field, uint32_t *out)
{
		bool ok = false;
		const qulonglong value = field.trimmed().toULongLong(&ok);
		if (!ok || value > std::numeric_limits<uint32_t>::max())
		{
				return false;
		}
		*out = static_cast<uint32_t>(value);
		return true;
}

void append_warning(QStringList *warnings, const QString &message)
{
		if (warnings != nullptr)
		{
				warnings->append(message);
		}
}
}

bool ArsSessionDurationScanner::scanSessionDuration(const QString &sessionPath,
																										uint32_t *outMaxTimestamp100ms,
																										bool *outHasTimestamp,
																										QStringList *warnings)
{
		if (outMaxTimestamp100ms == nullptr || outHasTimestamp == nullptr)
		{
				append_warning(warnings, "Duration scanner output pointers are null");
				return false;
		}
		*outMaxTimestamp100ms = 0;
		*outHasTimestamp = false;

		QStringList inputWarnings;
		const QList<ArsDurationScanFileInput> inputs = scanDurationInputs(sessionPath, &inputWarnings);
		for (const QString &w : inputWarnings)
		{
				append_warning(warnings, w);
		}
		for (const ArsDurationScanFileInput &input : inputs)
		{
				const ArsDurationScanFileResult result = scanDurationFile(input);
				for (const QString &w : result.warnings)
				{
						append_warning(warnings, w);
				}
				if (!result.ok || !result.hasTimestamp)
				{
						continue;
				}
				if (!*outHasTimestamp || result.maxTimestamp100ms > *outMaxTimestamp100ms)
				{
						*outMaxTimestamp100ms = result.maxTimestamp100ms;
				}
				*outHasTimestamp = true;
		}

		return true;
}

QList<ArsDurationScanFileInput> ArsSessionDurationScanner::scanDurationInputs(const QString &sessionPath,
																																			 QStringList *warnings)
{
		QList<ArsDurationScanFileInput> out;
		const QDir sessionDir(sessionPath);
		if (!sessionDir.exists())
		{
				append_warning(warnings, QString("Session path is not found: %1").arg(sessionPath));
				return out;
		}

		const QRegularExpression trackerRx("^.+[LlRr]$");
		const QFileInfoList trackerDirs = sessionDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
		for (const QFileInfo &trackerDir : trackerDirs)
		{
				const QString folderName = trackerDir.fileName().trimmed();
				if (!trackerRx.match(folderName).hasMatch())
				{
						continue;
				}
				const QString processedPath = QDir(trackerDir.absoluteFilePath()).filePath("processedStr.csv");
				if (!QFileInfo::exists(processedPath))
				{
						append_warning(warnings, QString("processedStr.csv missing for %1").arg(folderName));
						continue;
				}
				ArsDurationScanFileInput input;
				input.trackerFolderName = folderName;
				input.processedStrPath = processedPath;
				out.append(input);
		}
		return out;
}

ArsDurationScanFileResult ArsSessionDurationScanner::scanDurationFile(const ArsDurationScanFileInput &input)
{
		ArsDurationScanFileResult out;
		out.trackerFolderName = input.trackerFolderName;
		out.processedStrPath = input.processedStrPath;

		QFile file(input.processedStrPath);
		if (!file.exists())
		{
				out.warnings.append(QString("processedStr.csv missing for %1").arg(input.trackerFolderName));
				return out;
		}
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		{
				out.warnings.append(QString("failed to open processedStr.csv for %1").arg(input.trackerFolderName));
				return out;
		}

		uint32_t maxTs = 0;
		bool hasTs = false;
		QTextStream stream(&file);
		int lineNumber = 0;
		while (!stream.atEnd())
		{
				lineNumber++;
				const QString line = stream.readLine().trimmed();
				if (line.isEmpty() || !line.startsWith("i:"))
				{
						continue;
				}
				const QString payload = line.mid(2);
				const QStringList parts = payload.split(',', Qt::KeepEmptyParts);
				if (parts.isEmpty())
				{
						out.warnings.append(QString("malformed i: line in %1 at line %2").arg(input.trackerFolderName).arg(lineNumber));
						continue;
				}
				uint32_t ts = 0;
				if (!parse_uint_field(parts.first(), &ts))
				{
						out.warnings.append(QString("invalid i: timestamp in %1 at line %2").arg(input.trackerFolderName).arg(lineNumber));
						continue;
				}
				if (!hasTs || ts > maxTs)
				{
						maxTs = ts;
				}
				hasTs = true;
		}
		file.close();

		out.ok = true;
		out.hasTimestamp = hasTs;
		out.maxTimestamp100ms = maxTs;
		if (!hasTs)
		{
				out.warnings.append(QString("no IntegralState timestamps found in %1").arg(input.trackerFolderName));
		}
		return out;
}
