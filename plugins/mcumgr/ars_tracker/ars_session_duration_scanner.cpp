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

		const QDir sessionDir(sessionPath);
		if (!sessionDir.exists())
		{
				append_warning(warnings, QString("Session path is not found: %1").arg(sessionPath));
				return false;
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
				QFile file(processedPath);
				if (!file.exists())
				{
						append_warning(warnings, QString("processedStr.csv is missing: %1").arg(processedPath));
						continue;
				}
				if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
				{
						append_warning(warnings, QString("Failed to open processedStr.csv: %1").arg(processedPath));
						continue;
				}

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
								append_warning(warnings, QString("Malformed integral line (empty payload) file=%1 line=%2")
																								 .arg(processedPath)
																								 .arg(lineNumber));
								continue;
						}
						uint32_t ts = 0;
						if (!parse_uint_field(parts.first(), &ts))
						{
								append_warning(warnings, QString("Malformed integral timestamp file=%1 line=%2")
																								 .arg(processedPath)
																								 .arg(lineNumber));
								continue;
						}
						if (!*outHasTimestamp || ts > *outMaxTimestamp100ms)
						{
								*outMaxTimestamp100ms = ts;
						}
						*outHasTimestamp = true;
				}
				file.close();
		}

		return true;
}
