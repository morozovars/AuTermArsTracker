#include "ars_processed_str_parser.h"

#include <QFile>
#include <QLocale>
#include <QTextStream>
#include <QStringList>

namespace
{
bool parse_uint(const QString &text, uint32_t *out)
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

bool parse_double_c_locale(const QString &text, double *out)
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

QStringList split_csv_preserve_empty(const QString &body)
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
		while (!stream.atEnd())
		{
				const QString line = stream.readLine().trimmed();
				if (line.isEmpty())
				{
						continue;
				}

				if (line.startsWith("i:"))
				{
						const QStringList fields = split_csv_preserve_empty(line.mid(2));
						if (fields.size() < 10)
						{
								out->malformedLines++;
								continue;
						}

						IntegralState state;
						if (!parse_uint(fields.at(0), &state.timestamp) ||
								!parse_double_c_locale(fields.at(1), &state.distance) ||
								!parse_double_c_locale(fields.at(2), &state.ax) ||
								!parse_double_c_locale(fields.at(3), &state.ay) ||
								!parse_double_c_locale(fields.at(4), &state.az) ||
								!parse_double_c_locale(fields.at(5), &state.yaw) ||
								!parse_double_c_locale(fields.at(6), &state.pitch) ||
								!parse_double_c_locale(fields.at(7), &state.roll) ||
								!parse_uint(fields.at(8), &state.step) ||
								!parse_uint(fields.at(9), &state.load))
						{
								out->malformedLines++;
								continue;
						}

						out->integralStates.push_back(state);
						continue;
				}

				if (line.startsWith("sp:"))
				{
						const QStringList fields = split_csv_preserve_empty(line.mid(3));
						if (fields.size() < 15)
						{
								out->malformedLines++;
								continue;
						}

						SplashData splash;
						uint32_t tPeak = 0;
						uint32_t shotType = 0;
						if (!parse_uint(fields.at(0), &tPeak) ||
								!parse_double_c_locale(fields.at(1), &splash.maxAccel) ||
								!parse_uint(fields.at(2), &splash.tStart) ||
								!parse_uint(fields.at(3), &splash.tFootStart) ||
								!parse_uint(fields.at(4), &splash.tTouch) ||
								!parse_double_c_locale(fields.at(5), &splash.postAmp) ||
								!parse_double_c_locale(fields.at(6), &splash.integral) ||
								!parse_uint(fields.at(7), &splash.duration) ||
								!parse_double_c_locale(fields.at(8), &splash.x) ||
								!parse_double_c_locale(fields.at(9), &splash.y) ||
								!parse_double_c_locale(fields.at(10), &splash.z) ||
								!parse_uint(fields.at(11), &shotType) ||
								!parse_double_c_locale(fields.at(12), &splash.energy) ||
								!parse_double_c_locale(fields.at(13), &splash.delta) ||
								!parse_double_c_locale(fields.at(14), &splash.abp))
						{
								out->malformedLines++;
								continue;
						}

						splash.tPeak = tPeak;
						// CSV format writes tPeak first after "sp:" and does not carry separate timestamp.
						// For now we map timestamp=tPeak explicitly until format contract is clarified.
						splash.timestamp = tPeak;
						if (shotType > UINT8_MAX)
						{
								out->malformedLines++;
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
