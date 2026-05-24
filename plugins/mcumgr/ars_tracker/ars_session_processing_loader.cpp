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

QList<ArsSessionPairInput> ArsSessionProcessingLoader::scanSessionPairs(const QString &sessionPath,
																																 QStringList *warnings)
{
		QList<ArsSessionPairInput> out;
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

		QMap<QString, ArsSessionPairInput> bySerial;
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
						ArsSessionPairInput pairData;
						pairData.pairSerial = pairSerial;
						bySerial.insert(pairSerial, pairData);
				}

				ArsSessionPairInput &pairRef = bySerial[pairSerial];
				const QString processedPath = QDir(trackerInfo.absoluteFilePath()).filePath("processedStr.csv");
				if (sideChar == QChar('L'))
				{
						pairRef.hasLeft = true;
						pairRef.leftTrackerFolderName = trackerFolder;
						pairRef.leftProcessedStrPath = processedPath;
				}
				else
				{
						pairRef.hasRight = true;
						pairRef.rightTrackerFolderName = trackerFolder;
						pairRef.rightProcessedStrPath = processedPath;
				}
		}

		for (auto it = bySerial.cbegin(); it != bySerial.cend(); ++it)
		{
				out.append(it.value());
		}

		std::sort(out.begin(), out.end(), [](const ArsSessionPairInput &a, const ArsSessionPairInput &b) {
				return QString::compare(a.pairSerial, b.pairSerial, Qt::CaseInsensitive) < 0;
		});

		return out;
}

ArsPairProcessedData ArsSessionProcessingLoader::loadPair(const ArsSessionPairInput &pairInput,
																													QStringList *warnings)
{
		ArsPairProcessedData out;
		out.pairSerial = pairInput.pairSerial;

		if (pairInput.hasLeft)
		{
				ArsFootProcessedData leftData;
				leftData.trackerFolderName = pairInput.leftTrackerFolderName;
				leftData.processedStrPath = pairInput.leftProcessedStrPath;
				if (!leftData.processedStrPath.trimmed().isEmpty() && QFileInfo::exists(leftData.processedStrPath))
				{
						QString parseError;
						if (!ArsProcessedStrParser::parseFile(leftData.processedStrPath, &leftData.data, &parseError))
						{
								if (warnings != nullptr)
								{
										warnings->append(parseError);
								}
						}
				}
				else if (warnings != nullptr)
				{
						warnings->append(QString("pair %1: missing left processedStr.csv").arg(pairInput.pairSerial));
				}
				out.left = leftData;
		}

		if (pairInput.hasRight)
		{
				ArsFootProcessedData rightData;
				rightData.trackerFolderName = pairInput.rightTrackerFolderName;
				rightData.processedStrPath = pairInput.rightProcessedStrPath;
				if (!rightData.processedStrPath.trimmed().isEmpty() && QFileInfo::exists(rightData.processedStrPath))
				{
						QString parseError;
						if (!ArsProcessedStrParser::parseFile(rightData.processedStrPath, &rightData.data, &parseError))
						{
								if (warnings != nullptr)
								{
										warnings->append(parseError);
								}
						}
				}
				else if (warnings != nullptr)
				{
						warnings->append(QString("pair %1: missing right processedStr.csv").arg(pairInput.pairSerial));
				}
				out.right = rightData;
		}

		return out;
}

QList<ArsPairProcessedData> ArsSessionProcessingLoader::loadSession(const QString &sessionPath,
																																QStringList *warnings)
{
		QList<ArsPairProcessedData> out;
		const QList<ArsSessionPairInput> pairs = scanSessionPairs(sessionPath, warnings);
		for (const ArsSessionPairInput &pairInput : pairs)
		{
				out.append(loadPair(pairInput, warnings));
		}
		return out;
}
