#include "ars_processed_str_parser.h"

#include <QFile>
#include <QLocale>
#include <QTextStream>
#include <QStringList>

namespace
{
constexpr int kMaxMalformedLineTextLength = 300;

QString clipped_line_text(const QString &line)
{
		if (line.size() <= kMaxMalformedLineTextLength)
		{
				return line;
		}
		return line.left(kMaxMalformedLineTextLength);
}

void append_malformed(ArsProcessedStrData *out,
											int lineNumber,
											const QString &prefix,
											const QString &reason,
											const QString &lineText)
{
		out->malformedLines++;
		ArsMalformedProcessedStrLine detail;
		detail.lineNumber = lineNumber;
		detail.prefix = prefix;
		detail.reason = reason;
		detail.text = clipped_line_text(lineText);
		out->malformedLineDetails.append(detail);
}

bool parseUIntField(const QString &text, uint32_t *out)
{
		bool ok = false;
		const quint64 value = text.toULongLong(&ok, 10);
		if (!ok || value > UINT32_MAX)
		{
				return false;
		}
		*out = static_cast<uint32_t>(value);
		return true;
}

bool parseDoubleField(const QString &text, double *out)
{
		bool ok = false;
		const double value = QLocale::c().toDouble(text, &ok);
		if (!ok)
		{
				return false;
		}
		*out = value;
		return true;
}

QStringList splitCsvPayload(const QString &body)
{
		return body.split(',', Qt::KeepEmptyParts);
}
}

bool ArsProcessedStrParser::parseFile(const QString &filePath,
																			ArsProcessedStrData *out,
																			QString *errorMessage)
{
		if (out == nullptr)
		{
				if (errorMessage != nullptr)
				{
						*errorMessage = "Output pointer is null";
				}
				return false;
		}

		*out = ArsProcessedStrData();

		QFile file(filePath);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
		{
				if (errorMessage != nullptr)
				{
						*errorMessage = QString("Failed to open file: %1").arg(filePath);
				}
				return false;
		}

		QTextStream stream(&file);
		int lineNumber = 0;
		while (!stream.atEnd())
		{
				lineNumber++;
				const QString line = stream.readLine().trimmed();
				if (line.isEmpty())
				{
						continue;
				}

				if (line.startsWith("i:"))
				{
						const QStringList fields = splitCsvPayload(line.mid(2));
						if (fields.size() < 10)
						{
								append_malformed(out, lineNumber, "i", "invalid field count", line);
								continue;
						}

						IntegralState state;
						if (!parseUIntField(fields.at(0), &state.timestamp))
						{
								append_malformed(out, lineNumber, "i", "invalid integer field", line);
								continue;
						}
						if (!parseDoubleField(fields.at(1), &state.distance) ||
								!parseDoubleField(fields.at(2), &state.ax) ||
								!parseDoubleField(fields.at(3), &state.ay) ||
								!parseDoubleField(fields.at(4), &state.az) ||
								!parseDoubleField(fields.at(5), &state.yaw) ||
								!parseDoubleField(fields.at(6), &state.pitch) ||
								!parseDoubleField(fields.at(7), &state.roll))
						{
								append_malformed(out, lineNumber, "i", "invalid double field", line);
								continue;
						}
						if (!parseUIntField(fields.at(8), &state.step) ||
								!parseUIntField(fields.at(9), &state.load))
						{
								append_malformed(out, lineNumber, "i", "invalid integer field", line);
								continue;
						}

						out->integralStates.push_back(state);
						continue;
				}

				if (line.startsWith("sp:"))
				{
						const QStringList fields = splitCsvPayload(line.mid(3));
						if (fields.size() < 15)
						{
								append_malformed(out, lineNumber, "sp", "invalid field count", line);
								continue;
						}

						SplashData splash;
						uint32_t tPeak = 0;
						uint32_t shotType = 0;
						if (!parseUIntField(fields.at(0), &tPeak) ||
								!parseUIntField(fields.at(2), &splash.tStart) ||
								!parseUIntField(fields.at(3), &splash.tFootStart) ||
								!parseUIntField(fields.at(4), &splash.tTouch) ||
								!parseUIntField(fields.at(7), &splash.duration) ||
								!parseUIntField(fields.at(11), &shotType))
						{
								append_malformed(out, lineNumber, "sp", "invalid integer field", line);
								continue;
						}
						if (!parseDoubleField(fields.at(1), &splash.maxAccel) ||
								!parseDoubleField(fields.at(5), &splash.postAmp) ||
								!parseDoubleField(fields.at(6), &splash.integral) ||
								!parseDoubleField(fields.at(8), &splash.x) ||
								!parseDoubleField(fields.at(9), &splash.y) ||
								!parseDoubleField(fields.at(10), &splash.z) ||
								!parseDoubleField(fields.at(12), &splash.energy) ||
								!parseDoubleField(fields.at(13), &splash.delta) ||
								!parseDoubleField(fields.at(14), &splash.abp))
						{
								append_malformed(out, lineNumber, "sp", "invalid double field", line);
								continue;
						}

						splash.tPeak = tPeak;
						// CSV format writes tPeak first after "sp:" and does not carry separate timestamp.
						// For now we map timestamp=tPeak explicitly until format contract is clarified.
						splash.timestamp = tPeak;
						if (shotType > UINT8_MAX)
						{
								append_malformed(out, lineNumber, "sp", "invalid integer field", line);
								continue;
						}
						splash.shotType = static_cast<uint8_t>(shotType);
						out->splashRecords.push_back(splash);
						continue;
				}

				out->ignoredLines++;
		}

		return true;
}
