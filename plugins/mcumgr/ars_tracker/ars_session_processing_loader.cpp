#include "ars_session_processing_loader.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <algorithm>

namespace
{
QString normalize_pair_serial_for_ui(const QString &serial)
{
		static const QRegularExpression hexLike("^[0-9A-Fa-f]+$");
		if (serial.size() < 8 && hexLike.match(serial).hasMatch())
		{
				return serial.rightJustified(8, '0').toUpper();
		}
		return serial.toUpper();
}
}

QList<ArsPairProcessedData> ArsSessionProcessingLoader::loadSession(const QString &sessionPath,
																																QStringList *warnings)
{
		QList<ArsPairProcessedData> out;
		const QDir sessionDir(sessionPath);
		if (!sessionDir.exists())
		{
				if (warnings != nullptr)
				{
						warnings->append(QString("Session path not found: %1").arg(sessionPath));
				}
				return out;
		}

		const QFileInfoList trackerDirs =
				sessionDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
																 QDir::Name | QDir::IgnoreCase);

		QMap<QString, ArsPairProcessedData> bySerial;
		for (const QFileInfo &trackerInfo : trackerDirs)
		{
				const QString trackerFolder = trackerInfo.fileName().trimmed();
				if (trackerFolder.size() < 2)
				{
						continue;
				}

				const QChar sideChar = trackerFolder.at(trackerFolder.size() - 1).toUpper();
				if (sideChar != QChar('L') && sideChar != QChar('R'))
				{
						continue;
				}

				const QString rawSerial = trackerFolder.left(trackerFolder.size() - 1);
				if (rawSerial.isEmpty())
				{
						continue;
				}

				const QString pairSerial = normalize_pair_serial_for_ui(rawSerial);
				if (!bySerial.contains(pairSerial))
				{
						ArsPairProcessedData pairData;
						pairData.pairSerial = pairSerial;
						bySerial.insert(pairSerial, pairData);
				}

				ArsFootProcessedData footData;
				footData.trackerFolderName = trackerFolder;
				footData.processedStrPath = QDir(trackerInfo.absoluteFilePath()).filePath("processedStr.csv");

				if (!QFileInfo::exists(footData.processedStrPath))
				{
						if (warnings != nullptr)
						{
								warnings->append(QString("processedStr.csv not found: %1").arg(footData.processedStrPath));
						}
				}
				else
				{
						QString parseError;
						if (!ArsProcessedStrParser::parseFile(footData.processedStrPath, &footData.data, &parseError))
						{
								if (warnings != nullptr)
								{
										warnings->append(parseError);
								}
						}
				}

				ArsPairProcessedData &pairRef = bySerial[pairSerial];
				if (sideChar == QChar('L'))
				{
						pairRef.left = footData;
				}
				else
				{
						pairRef.right = footData;
				}
		}

		for (auto it = bySerial.cbegin(); it != bySerial.cend(); ++it)
		{
				out.append(it.value());
		}

		std::sort(out.begin(), out.end(), [](const ArsPairProcessedData &a, const ArsPairProcessedData &b) {
				return QString::compare(a.pairSerial, b.pairSerial, Qt::CaseInsensitive) < 0;
		});

		return out;
}
