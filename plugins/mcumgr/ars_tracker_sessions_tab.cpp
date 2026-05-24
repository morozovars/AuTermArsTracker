#include "ars_tracker_sessions_tab.h"

#include <QDateTime>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimeEdit>
#include <QUrl>
#include <QDoubleSpinBox>
#include <QApplication>
#include <QTimer>
#include <QComboBox>
#include <QVBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QStringList>
#include <algorithm>

#include "ars/workspace/ArsLocalWorkspace.h"
#include "ars_tracker/ars_session_processing_loader.h"

namespace
{
constexpr int kMaxMalformedLinesToLog = 10;

QString normalize_pair_serial(const QString &serial)
{
		if (serial.size() >= 8)
		{
				return serial;
		}
		return serial.rightJustified(8, '0');
}

bool parse_unix_timestamp_session_name(const QString &name, QDateTime *out)
{
		bool ok = false;
		const qint64 seconds = name.trimmed().toLongLong(&ok);
		if (!ok || seconds <= 0)
		{
				return false;
		}

		const QDateTime dt = QDateTime::fromSecsSinceEpoch(seconds);
		if (!dt.isValid())
		{
				return false;
		}

		if (out != nullptr)
		{
				*out = dt;
		}
		return true;
}
}

ArsTrackerSessionsTab::ArsTrackerSessionsTab(QWidget *parent) : QWidget(parent)
{
		buildUi();
		reloadSessions("init");
}

void ArsTrackerSessionsTab::buildUi()
{
		QGridLayout *root = new QGridLayout(this);
		root->setContentsMargins(8, 8, 8, 8);
		root->setHorizontalSpacing(8);
		root->setVerticalSpacing(8);

		pagesStack = new QStackedWidget(this);
		root->addWidget(pagesStack, 0, 0, 1, 1);

		buildListPage();
		buildDetailsPage();
}

void ArsTrackerSessionsTab::buildListPage()
{
		listPage = new QWidget(this);
		QGridLayout *layout = new QGridLayout(listPage);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setHorizontalSpacing(8);
		layout->setVerticalSpacing(8);

		QHBoxLayout *actions = new QHBoxLayout();
		openFolderButton = new QPushButton("Open folder", listPage);
		actions->addWidget(openFolderButton);
		reloadButton = new QPushButton("Reload", listPage);
		actions->addWidget(reloadButton);
		actions->addStretch(1);
		layout->addLayout(actions, 0, 0, 1, 1);

		sessionsTable = new QTableWidget(listPage);
		sessionsTable->setColumnCount(2);
		sessionsTable->setHorizontalHeaderLabels(QStringList() << "Session" << "Trackers");
		sessionsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
		sessionsTable->setSelectionMode(QAbstractItemView::SingleSelection);
		sessionsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
		sessionsTable->verticalHeader()->setVisible(false);
		sessionsTable->horizontalHeader()->setStretchLastSection(true);
		sessionsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
		sessionsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
		layout->addWidget(sessionsTable, 1, 0, 1, 1);

		statusLabel = new QLabel("No local sessions found", listPage);
		layout->addWidget(statusLabel, 2, 0, 1, 1);

		connect(openFolderButton, &QPushButton::clicked, this,
						&ArsTrackerSessionsTab::openSessionsFolder);
		connect(reloadButton, &QPushButton::clicked, this, [this]() { reloadSessions("reload"); });

		pagesStack->addWidget(listPage);
}

void ArsTrackerSessionsTab::buildDetailsPage()
{
		detailsPage = new QWidget(this);
		QGridLayout *layout = new QGridLayout(detailsPage);
		layout->setContentsMargins(0, 0, 0, 0);
		layout->setHorizontalSpacing(8);
		layout->setVerticalSpacing(8);

		QHBoxLayout *header = new QHBoxLayout();
		backButton = new QPushButton("Back", detailsPage);
		header->addWidget(backButton);
		processButton = new QPushButton("Process", detailsPage);
		header->addWidget(processButton);
		sessionTitleLabel = new QLabel(detailsPage);
		header->addWidget(sessionTitleLabel, 1);
		layout->addLayout(header, 0, 0, 1, 1);

		QGroupBox *sessionInfoBox = new QGroupBox("Session Information", detailsPage);
		QHBoxLayout *sessionInfoLayout = new QHBoxLayout(sessionInfoBox);
		QGroupBox *parametersBox = new QGroupBox("Parameters", sessionInfoBox);
		QFormLayout *parametersLayout = new QFormLayout(parametersBox);

		comboSessionType = new QComboBox(parametersBox);
		comboSessionType->setObjectName("combo_session_type");
		comboSessionType->addItem("Training session", "training");
		comboSessionType->addItem("Match", "match");
		parametersLayout->addRow("Type", comboSessionType);

		QHBoxLayout *periodLayout = new QHBoxLayout();
		timeSessionStart = new QTimeEdit(parametersBox);
		timeSessionStart->setObjectName("time_session_start");
		timeSessionStart->setDisplayFormat("HH:mm");
		timeSessionStart->setTime(QTime(10, 0));
		timeSessionFinish = new QTimeEdit(parametersBox);
		timeSessionFinish->setObjectName("time_session_finish");
		timeSessionFinish->setDisplayFormat("HH:mm");
		timeSessionFinish->setTime(QTime(11, 30));
		periodLayout->addWidget(timeSessionStart);
		periodLayout->addWidget(new QLabel("-", parametersBox));
		periodLayout->addWidget(timeSessionFinish);
		parametersLayout->addRow("Planned session period", periodLayout);

		editSessionLocation = new QLineEdit(parametersBox);
		editSessionLocation->setObjectName("edit_session_location");
		editSessionLocation->setPlaceholderText("Казань, манеж");
		parametersLayout->addRow("Location", editSessionLocation);

		editSessionGoals = new QPlainTextEdit(parametersBox);
		editSessionGoals->setObjectName("edit_session_goals");
		editSessionGoals->setPlaceholderText(
				"Развитие скоростной выносливости\nРабота с мячом под нагрузкой");
		editSessionGoals->setFixedHeight(110);
		parametersLayout->addRow("Goals", editSessionGoals);
		sessionInfoLayout->addWidget(parametersBox, 1);

		QGroupBox *targetsBox = new QGroupBox("Target", sessionInfoBox);
		QFormLayout *targetsLayout = new QFormLayout(targetsBox);
		spinTargetDistanceKm = new QDoubleSpinBox(targetsBox);
		spinTargetDistanceKm->setObjectName("spin_session_target_distance_km");
		spinTargetDistanceKm->setDecimals(3);
		spinTargetDistanceKm->setRange(0.0, 1000.0);
		targetsLayout->addRow("Target distance, km", spinTargetDistanceKm);

		spinTargetAccelerationDistanceM = new QSpinBox(targetsBox);
		spinTargetAccelerationDistanceM->setObjectName("spin_session_target_acceleration_distance_m");
		spinTargetAccelerationDistanceM->setRange(0, 100000);
		targetsLayout->addRow("Target acceleration distance, m", spinTargetAccelerationDistanceM);

		spinTargetFootload10_3g = new QSpinBox(targetsBox);
		spinTargetFootload10_3g->setObjectName("spin_session_target_footload_10_3g");
		spinTargetFootload10_3g->setRange(0, 1000000);
		targetsLayout->addRow("Target footload, 10^3g", spinTargetFootload10_3g);

		spinTargetTouchesCount = new QSpinBox(targetsBox);
		spinTargetTouchesCount->setObjectName("spin_session_target_touches_count");
		spinTargetTouchesCount->setRange(0, 100000);
		targetsLayout->addRow("Target touches count", spinTargetTouchesCount);

		spinTargetFootloadPerMin = new QDoubleSpinBox(targetsBox);
		spinTargetFootloadPerMin->setObjectName("spin_session_target_footload_per_min");
		spinTargetFootloadPerMin->setDecimals(2);
		spinTargetFootloadPerMin->setRange(0.0, 100000.0);
		targetsLayout->addRow("Target footload intensity footload/min", spinTargetFootloadPerMin);
		sessionInfoLayout->addWidget(targetsBox, 1);
		layout->addWidget(sessionInfoBox, 1, 0, 1, 1);

		sessionTrackersTable = new QTableWidget(detailsPage);
		sessionTrackersTable->setColumnCount(3);
		sessionTrackersTable->setHorizontalHeaderLabels(
				QStringList() << "Pair serial" << "Left tracker" << "Right tracker");
		sessionTrackersTable->setSelectionBehavior(QAbstractItemView::SelectRows);
		sessionTrackersTable->setSelectionMode(QAbstractItemView::SingleSelection);
		sessionTrackersTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
		sessionTrackersTable->verticalHeader()->setVisible(false);
		sessionTrackersTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
		sessionTrackersTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
		sessionTrackersTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
		layout->addWidget(sessionTrackersTable, 2, 0, 1, 1);

		sessionTrackersEmptyLabel = new QLabel("No trackers found", detailsPage);
		sessionTrackersEmptyLabel->setVisible(false);
		layout->addWidget(sessionTrackersEmptyLabel, 3, 0, 1, 1);

		detailsStatusLabel = new QLabel(detailsPage);
		layout->addWidget(detailsStatusLabel, 4, 0, 1, 1);

		connect(backButton, &QPushButton::clicked, this,
						&ArsTrackerSessionsTab::onBackFromSessionDetails);
		connect(processButton, &QPushButton::clicked, this,
						&ArsTrackerSessionsTab::onProcessSessionClicked);

		pagesStack->addWidget(detailsPage);
}

QString ArsTrackerSessionsTab::sessionsPath() const
{
		ArsLocalWorkspace workspace;
		if (!workspace.initialize())
		{
				return QString();
		}
		return workspace.sessionsPath();
}

void ArsTrackerSessionsTab::openSessionsFolder()
{
		const QString sessions_path = sessionsPath();
		if (sessions_path.trimmed().isEmpty())
		{
				statusLabel->setText("Workspace sessions path is unavailable");
				qWarning() << "Sessions tab: workspace sessions path is empty";
				return;
		}

		if (!QDir(sessions_path).exists() && !QDir().mkpath(sessions_path))
		{
				statusLabel->setText(QString("Failed to create sessions folder: %1").arg(sessions_path));
				qWarning() << "Sessions tab: failed to create sessions folder" << sessions_path;
				return;
		}

		const bool opened =
				QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::toNativeSeparators(sessions_path)));
		if (!opened)
		{
				statusLabel->setText(QString("Failed to open folder: %1").arg(sessions_path));
				qWarning() << "Sessions tab: failed to open folder in explorer" << sessions_path;
				return;
		}
		statusLabel->setText(QString("Opened: %1").arg(sessions_path));
}

QString ArsTrackerSessionsTab::formatSessionDisplayName(const QString &sessionFolderName) const
{
		QDateTime ts;
		if (!parse_unix_timestamp_session_name(sessionFolderName, &ts))
		{
				return sessionFolderName;
		}
		return QString("%1 - %2")
				.arg(sessionFolderName,
						 ts.toLocalTime().toString("dd MMM yyyy HH:mm"));
}

QList<SessionTrackerPair> ArsTrackerSessionsTab::scanSessionTrackers(const QString &sessionPath) const
{
		QList<SessionTrackerPair> out;
		const QDir sessionDir(sessionPath);
		if (!sessionDir.exists())
		{
				qWarning() << "Sessions tab: session path is not found" << sessionPath;
				return out;
		}

		const QFileInfoList entries =
				sessionDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
																 QDir::Name | QDir::IgnoreCase);
		QMap<QString, SessionTrackerPair> bySerial;
		const QRegularExpression trackerRx("^(.+)([LlRr])$");

		for (const QFileInfo &entry : entries)
		{
				const QString folderName = entry.fileName().trimmed();
				const QRegularExpressionMatch match = trackerRx.match(folderName);
				if (!match.hasMatch())
				{
						continue;
				}

				const QString rawSerial = match.captured(1);
				const QString side = match.captured(2).toUpper();
				if (rawSerial.isEmpty() || (side != "L" && side != "R"))
				{
						continue;
				}

				if (!bySerial.contains(rawSerial))
				{
						SessionTrackerPair pair;
						pair.pairSerial = normalize_pair_serial(rawSerial);
						pair.leftTracker = "-";
						pair.rightTracker = "-";
						bySerial.insert(rawSerial, pair);
				}

				SessionTrackerPair &pair = bySerial[rawSerial];
				if (side == "L")
				{
						pair.leftTracker = folderName;
				}
				else
				{
						pair.rightTracker = folderName;
				}
		}

		for (auto it = bySerial.cbegin(); it != bySerial.cend(); ++it)
		{
				out.append(it.value());
		}

		std::sort(out.begin(), out.end(), [](const SessionTrackerPair &a, const SessionTrackerPair &b) {
				const int cmp = QString::compare(a.pairSerial, b.pairSerial, Qt::CaseInsensitive);
				if (cmp != 0)
				{
						return cmp < 0;
				}
				return QString::compare(a.leftTracker + a.rightTracker, b.leftTracker + b.rightTracker,
																Qt::CaseInsensitive) < 0;
		});

		return out;
}

QString ArsTrackerSessionsTab::buildTrackersDisplayText(const QList<SessionTrackerPair> &pairs) const
{
		QStringList parts;
		for (const SessionTrackerPair &pair : pairs)
		{
				QString sides = "-";
				const bool hasL = pair.leftTracker != "-";
				const bool hasR = pair.rightTracker != "-";
				if (hasL && hasR)
				{
						sides = "L+R";
				}
				else if (hasL)
				{
						sides = "L";
				}
				else if (hasR)
				{
						sides = "R";
				}
				parts.append(QString("%1: %2").arg(pair.pairSerial, sides));
		}
		return parts.join(", ");
}

QList<LocalSessionInfo> ArsTrackerSessionsTab::scanLocalSessions(const QString &sessionsPath) const
{
		QList<LocalSessionInfo> out;
		const QDir sessionsDir(sessionsPath);
		if (!sessionsDir.exists())
		{
				return out;
		}

		const QFileInfoList sessionDirs =
				sessionsDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
																	QDir::Name | QDir::IgnoreCase);
		for (const QFileInfo &sessionInfo : sessionDirs)
		{
				LocalSessionInfo local;
				local.folderName = sessionInfo.fileName();
				local.displayName = formatSessionDisplayName(local.folderName);
				local.absolutePath = sessionInfo.absoluteFilePath();
				const QList<SessionTrackerPair> pairs = scanSessionTrackers(local.absolutePath);
				local.trackersDisplayText = buildTrackersDisplayText(pairs);
				out.append(local);
		}

		std::sort(out.begin(), out.end(), [](const LocalSessionInfo &a, const LocalSessionInfo &b) {
				QDateTime aTs;
				QDateTime bTs;
				const bool aHasTs = parse_unix_timestamp_session_name(a.folderName, &aTs);
				const bool bHasTs = parse_unix_timestamp_session_name(b.folderName, &bTs);
				if (aHasTs != bHasTs)
				{
						return aHasTs && !bHasTs;
				}
				if (aHasTs && bHasTs)
				{
						return aTs > bTs;
				}
				return a.folderName.compare(b.folderName, Qt::CaseInsensitive) > 0;
		});

		return out;
}

void ArsTrackerSessionsTab::fillSessionsTable(const QList<LocalSessionInfo> &sessions)
{
		sessionsTable->setSortingEnabled(false);
		sessionsTable->clearContents();
		sessionsTable->setRowCount(sessions.size());

		for (int row = 0; row < sessions.size(); ++row)
		{
				const LocalSessionInfo &session = sessions.at(row);
				QPushButton *sessionLink = new QPushButton(session.displayName, sessionsTable);
				sessionLink->setFlat(true);
				sessionLink->setCursor(Qt::PointingHandCursor);
				sessionLink->setStyleSheet("QPushButton { text-align: left; color: #1e5aa8; border: none; }");
				sessionLink->setProperty("sessionId", session.folderName);
				sessionLink->setToolTip(session.absolutePath);
				connect(sessionLink, &QPushButton::clicked, this,
								&ArsTrackerSessionsTab::onSessionNameClicked);
				sessionsTable->setCellWidget(row, 0, sessionLink);

				sessionsTable->setItem(
						row, 1,
						new QTableWidgetItem(session.trackersDisplayText.isEmpty() ? "-" :
																												session.trackersDisplayText));
		}
}

void ArsTrackerSessionsTab::fillSessionTrackersTable(const QList<SessionTrackerPair> &pairs)
{
		sessionTrackersTable->setSortingEnabled(false);
		sessionTrackersTable->clearContents();
		sessionTrackersTable->setRowCount(pairs.size());

		for (int row = 0; row < pairs.size(); ++row)
		{
				const SessionTrackerPair &pair = pairs.at(row);
				sessionTrackersTable->setItem(row, 0, new QTableWidgetItem(pair.pairSerial));
				sessionTrackersTable->setItem(row, 1, new QTableWidgetItem(pair.leftTracker));
				sessionTrackersTable->setItem(row, 2, new QTableWidgetItem(pair.rightTracker));
		}

		sessionTrackersEmptyLabel->setVisible(pairs.isEmpty());
}

void ArsTrackerSessionsTab::reloadSessions(const QString &reason)
{
		const QString sessions_path = sessionsPath();
		if (sessions_path.trimmed().isEmpty())
		{
				sessionsTable->setRowCount(0);
				statusLabel->setText("Workspace sessions path is unavailable");
				qWarning() << "Sessions tab scan failed: workspace sessions path is empty";
				return;
		}

		if (!QDir(sessions_path).exists() && !QDir().mkpath(sessions_path))
		{
				sessionsTable->setRowCount(0);
				statusLabel->setText(QString("Failed to create sessions folder: %1").arg(sessions_path));
				qWarning() << "Sessions tab scan failed: cannot create path" << sessions_path;
				return;
		}

		const QList<LocalSessionInfo> sessions = scanLocalSessions(sessions_path);
		fillSessionsTable(sessions);

		if (sessions.isEmpty())
		{
				statusLabel->setText("No local sessions found");
		}
		else
		{
				statusLabel->setText(QString("Local sessions: %1").arg(sessions.size()));
		}

		qDebug() << "Sessions tab list loaded"
						 << "reason=" << reason
						 << "path=" << sessions_path
						 << "count=" << sessions.size();
}

void ArsTrackerSessionsTab::showSessionsListPage(bool forceReload)
{
		if (forceReload)
		{
				reloadSessions("back-to-list");
		}
		pagesStack->setCurrentWidget(listPage);
}

void ArsTrackerSessionsTab::showSessionDetailsPage(const QString &sessionId)
{
		const QString baseSessionsPath = sessionsPath();
		const QString sessionPath = QDir(baseSessionsPath).filePath(sessionId);
		if (baseSessionsPath.trimmed().isEmpty() || !QDir(sessionPath).exists())
		{
				qWarning() << "Sessions tab: session path is not found or unavailable" << sessionPath;
				return;
		}

		currentSessionId = sessionId;
		sessionTitleLabel->setText(formatSessionDisplayName(sessionId));

		const QList<SessionTrackerPair> pairs = scanSessionTrackers(sessionPath);
		fillSessionTrackersTable(pairs);
		detailsStatusLabel->setText("Ready to process");

		qDebug() << "Sessions tab session page opened"
						 << "session=" << sessionId
						 << "path=" << sessionPath
						 << "pairs=" << pairs.size();

		pagesStack->setCurrentWidget(detailsPage);
}

ArsSessionTargetSettings ArsTrackerSessionsTab::readTargetSettingsFromUi() const
{
		ArsSessionTargetSettings settings;
		settings.targetDistanceKm = spinTargetDistanceKm->value();
		settings.targetAccelerationDistanceM = spinTargetAccelerationDistanceM->value();
		settings.targetFootload10_3g = spinTargetFootload10_3g->value();
		settings.targetTouchesCount = spinTargetTouchesCount->value();
		settings.targetFootloadPerMin = spinTargetFootloadPerMin->value();
		return settings;
}

void ArsTrackerSessionsTab::onProcessSessionClicked()
{
		if (currentSessionId.trimmed().isEmpty())
		{
				return;
		}

		const QString baseSessionsPath = sessionsPath();
		const QString sessionPath = QDir(baseSessionsPath).filePath(currentSessionId);
		if (baseSessionsPath.trimmed().isEmpty() || !QDir(sessionPath).exists())
		{
				qWarning() << "Sessions tab: cannot process missing session path" << sessionPath;
				detailsStatusLabel->setText("Session path is unavailable");
				return;
		}

		processButton->setEnabled(false);

		QDialog *progressDialog = new QDialog(this);
		progressDialog->setWindowTitle("Processing session");
		progressDialog->setModal(true);
		QVBoxLayout *dialogLayout = new QVBoxLayout(progressDialog);
		QLabel *progressTextLabel = new QLabel("Processing session data...", progressDialog);
		dialogLayout->addWidget(progressTextLabel);
		QProgressBar *progressBar = new QProgressBar(progressDialog);
		progressBar->setMinimum(0);
		progressBar->setMaximum(0);
		dialogLayout->addWidget(progressBar);
		QPlainTextEdit *problemsView = new QPlainTextEdit(progressDialog);
		problemsView->setReadOnly(true);
		problemsView->setVisible(false);
		dialogLayout->addWidget(problemsView);
		QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Close, progressDialog);
		QPushButton *closeButton = buttons->button(QDialogButtonBox::Close);
		closeButton->setVisible(false);
		closeButton->setEnabled(false);
		connect(closeButton, &QPushButton::clicked, progressDialog, &QDialog::accept);
		dialogLayout->addWidget(buttons);

		QTimer::singleShot(0, this, [this, progressTextLabel, progressBar, problemsView, closeButton]() {
				runProcessForSession(progressTextLabel, progressBar, problemsView, closeButton);
		});

		progressDialog->exec();
		progressDialog->deleteLater();
		processButton->setEnabled(true);
}

ArsSessionInfo ArsTrackerSessionsTab::readSessionInfoFromUi() const
{
		ArsSessionInfo info;
		info.parameters.type = comboSessionType->currentData().toString();
		info.parameters.startTime = timeSessionStart->time();
		info.parameters.endTime = timeSessionFinish->time();
		info.parameters.location = editSessionLocation->text().trimmed();

		const QStringList lines = editSessionGoals->toPlainText().split('\n');
		for (const QString &line : lines)
		{
				const QString trimmed = line.trimmed();
				if (!trimmed.isEmpty())
				{
						info.parameters.goals.append(trimmed);
				}
		}

		const ArsSessionTargetSettings targets = readTargetSettingsFromUi();
		info.plannedMetrics.distanceKm = targets.targetDistanceKm;
		info.plannedMetrics.accelerationDistanceM = targets.targetAccelerationDistanceM;
		info.plannedMetrics.footloadPerLeg = targets.targetFootload10_3g;
		info.plannedMetrics.loadIntensityGPerMin = targets.targetFootloadPerMin;
		info.plannedMetrics.touches = targets.targetTouchesCount;
		// TODO: add UI fields for maxSpeedMps, shots, dribbles and field size.
		info.plannedMetrics.maxSpeedMps = 0.0;
		info.plannedMetrics.shots = 0;
		info.plannedMetrics.dribbles = 0;
		info.field.widthM = 40;
		info.field.lengthM = 60;
		return info;
}

bool ArsTrackerSessionsTab::validateSessionInfo(const ArsSessionInfo &info, QStringList *problems) const
{
		if (info.parameters.endTime <= info.parameters.startTime)
		{
				if (problems != nullptr)
				{
						problems->append("Planned session finish time must be later than start time.");
				}
				return false;
		}
		return true;
}

bool ArsTrackerSessionsTab::saveSessionInfoJson(const QString &sessionPath,
																								const ArsSessionInfo &info,
																								QString *errorMessage) const
{
		const QString filePath = QDir(sessionPath).filePath("SessionInfo.json");
		QJsonObject planned;
		planned["distanceKm"] = info.plannedMetrics.distanceKm;
		planned["accelerationDistanceM"] = info.plannedMetrics.accelerationDistanceM;
		planned["footloadPerLeg"] = info.plannedMetrics.footloadPerLeg;
		planned["loadIntensityGPerMin"] = info.plannedMetrics.loadIntensityGPerMin;
		planned["maxSpeedMps"] = info.plannedMetrics.maxSpeedMps;
		planned["touches"] = info.plannedMetrics.touches;
		planned["shots"] = info.plannedMetrics.shots;
		planned["dribbles"] = info.plannedMetrics.dribbles;

		QJsonObject field;
		field["widthM"] = info.field.widthM;
		field["lengthM"] = info.field.lengthM;

		QJsonArray goals;
		for (const QString &goal : info.parameters.goals)
		{
				goals.append(goal);
		}

		QJsonObject root;
		root["plannedMetrics"] = planned;
		root["startTime"] = info.parameters.startTime.toString("HH:mm:ss");
		root["endTime"] = info.parameters.endTime.toString("HH:mm:ss");
		root["type"] = info.parameters.type;
		root["location"] = info.parameters.location;
		root["field"] = field;
		root["goals"] = goals;

		QFile file(filePath);
		if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
		{
				if (errorMessage != nullptr)
				{
						*errorMessage = QString("Failed to open file for write: %1").arg(filePath);
				}
				return false;
		}
		file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
		file.close();
		return true;
}

void ArsTrackerSessionsTab::runProcessForSession(QLabel *progressTextLabel,
																								 QProgressBar *progressBar,
																								 QPlainTextEdit *problemsView,
																								 QPushButton *closeButton)
{
		QApplication::setOverrideCursor(Qt::WaitCursor);
		QStringList problems;

		const QString baseSessionsPath = sessionsPath();
		const QString sessionPath = QDir(baseSessionsPath).filePath(currentSessionId);
		qDebug() << "Sessions tab process begin"
						 << "sessionPath=" << sessionPath;

		const ArsSessionInfo info = readSessionInfoFromUi();
		qDebug() << "Sessions tab session info"
						 << "type=" << info.parameters.type
						 << "startTime=" << info.parameters.startTime.toString("HH:mm:ss")
						 << "endTime=" << info.parameters.endTime.toString("HH:mm:ss")
						 << "location=" << info.parameters.location;
		qDebug() << "Sessions tab planned metrics"
						 << "distanceKm=" << info.plannedMetrics.distanceKm
						 << "accelerationDistanceM=" << info.plannedMetrics.accelerationDistanceM
						 << "footloadPerLeg=" << info.plannedMetrics.footloadPerLeg
						 << "loadIntensityGPerMin=" << info.plannedMetrics.loadIntensityGPerMin
						 << "touches=" << info.plannedMetrics.touches;

		if (!validateSessionInfo(info, &problems))
		{
				qWarning() << "Sessions tab process validation failed";
		}
		else
		{
				QString saveError;
				if (!saveSessionInfoJson(sessionPath, info, &saveError))
				{
						qWarning() << "Sessions tab SessionInfo.json save failed"
											 << "path=" << QDir(sessionPath).filePath("SessionInfo.json")
											 << "error=" << saveError;
						problems.append(QString("SessionInfo.json save failed: %1").arg(saveError));
				}
				else
				{
						qDebug() << "Sessions tab SessionInfo.json saved"
										 << "path=" << QDir(sessionPath).filePath("SessionInfo.json");
				}
		}

		int pairCount = 0;
		if (problems.isEmpty())
		{
				// TODO: move session processing to worker thread.
				QStringList warnings;
				const QList<ArsPairProcessedData> loadedPairs =
						ArsSessionProcessingLoader::loadSession(sessionPath, &warnings);
				pairCount = loadedPairs.size();
				qDebug() << "Sessions tab processedStr load"
								 << "sessionPath=" << sessionPath
								 << "pairs=" << pairCount;

				for (const ArsPairProcessedData &pair : loadedPairs)
				{
						const bool hasLeft = pair.left.has_value();
						const bool hasRight = pair.right.has_value();
						const int leftIntegral = hasLeft ? static_cast<int>(pair.left->data.integralStates.size()) : 0;
						const int leftSplash = hasLeft ? static_cast<int>(pair.left->data.splashRecords.size()) : 0;
						const int rightIntegral = hasRight ? static_cast<int>(pair.right->data.integralStates.size()) : 0;
						const int rightSplash = hasRight ? static_cast<int>(pair.right->data.splashRecords.size()) : 0;
						const int leftIgnored = hasLeft ? pair.left->data.ignoredLines : 0;
						const int leftMalformed = hasLeft ? pair.left->data.malformedLines : 0;
						const int rightIgnored = hasRight ? pair.right->data.ignoredLines : 0;
						const int rightMalformed = hasRight ? pair.right->data.malformedLines : 0;

						qDebug() << "Sessions tab processed pair"
										 << "serial=" << pair.pairSerial
										 << "left=" << hasLeft
										 << "right=" << hasRight
										 << "leftIntegralStates=" << leftIntegral
										 << "leftSplashRecords=" << leftSplash
										 << "leftIgnoredLines=" << leftIgnored
										 << "leftMalformedLines=" << leftMalformed
										 << "rightIntegralStates=" << rightIntegral
										 << "rightSplashRecords=" << rightSplash
										 << "rightIgnoredLines=" << rightIgnored
										 << "rightMalformedLines=" << rightMalformed;

						if (!hasLeft)
						{
								problems.append(QString("pair %1: left tracker data is missing").arg(pair.pairSerial));
						}
						if (!hasRight)
						{
								problems.append(QString("pair %1: right tracker data is missing").arg(pair.pairSerial));
						}
						if (hasLeft && leftMalformed > 0)
						{
								problems.append(QString("pair %1 L: malformed lines: %2").arg(pair.pairSerial).arg(leftMalformed));
						}
						if (hasRight && rightMalformed > 0)
						{
								problems.append(QString("pair %1 R: malformed lines: %2").arg(pair.pairSerial).arg(rightMalformed));
						}

						if (hasLeft && leftMalformed > 0)
						{
								const QList<ArsMalformedProcessedStrLine> details = pair.left->data.malformedLineDetails;
								const int countToLog = std::min(static_cast<int>(details.size()), kMaxMalformedLinesToLog);
								for (int i = 0; i < countToLog; ++i)
								{
										const ArsMalformedProcessedStrLine &d = details.at(i);
										qWarning() << "Sessions tab malformed line"
																 << "pair=" << pair.pairSerial
																 << "foot=L"
																 << "file=" << pair.left->processedStrPath
																 << "line=" << d.lineNumber
																 << "prefix=" << d.prefix
																 << "reason=" << d.reason
																 << "text=" << d.text;
								}
								if (details.size() > countToLog)
								{
										qWarning() << "Sessions tab malformed line"
																 << "pair=" << pair.pairSerial
																 << "foot=L"
																 << "... and" << (details.size() - countToLog) << "more malformed lines";
								}
						}

						if (hasRight && rightMalformed > 0)
						{
								const QList<ArsMalformedProcessedStrLine> details = pair.right->data.malformedLineDetails;
								const int countToLog = std::min(static_cast<int>(details.size()), kMaxMalformedLinesToLog);
								for (int i = 0; i < countToLog; ++i)
								{
										const ArsMalformedProcessedStrLine &d = details.at(i);
										qWarning() << "Sessions tab malformed line"
																 << "pair=" << pair.pairSerial
																 << "foot=R"
																 << "file=" << pair.right->processedStrPath
																 << "line=" << d.lineNumber
																 << "prefix=" << d.prefix
																 << "reason=" << d.reason
																 << "text=" << d.text;
								}
								if (details.size() > countToLog)
								{
										qWarning() << "Sessions tab malformed line"
																 << "pair=" << pair.pairSerial
																 << "foot=R"
																 << "... and" << (details.size() - countToLog) << "more malformed lines";
								}
						}
				}

				for (const QString &warning : warnings)
				{
						qWarning() << "Sessions tab processedStr warning:" << warning;
						problems.append(warning);
				}
		}

		const bool ok = problems.isEmpty();
		progressBar->setMaximum(1);
		progressBar->setValue(1);
		progressBar->setVisible(false);
		closeButton->setVisible(true);
		closeButton->setEnabled(true);
		problemsView->setVisible(!ok);

		if (ok)
		{
				progressTextLabel->setText(
						QString("Processing completed successfully.\nPairs loaded: %1").arg(pairCount));
				detailsStatusLabel->setText(QString("Processing completed successfully. Pairs=%1").arg(pairCount));
		}
		else
		{
				progressTextLabel->setText(
						QString("Processing completed with errors.\nPairs loaded: %1").arg(pairCount));
				QString text = "Problems:\n";
				for (const QString &p : problems)
				{
						text += QString("- %1\n").arg(p);
				}
				problemsView->setPlainText(text.trimmed());
				detailsStatusLabel->setText("Processing completed with errors. See dialog/logs.");
		}

		qDebug() << "Sessions tab process done"
						 << "ok=" << ok
						 << "problems=" << problems.size();
		QApplication::restoreOverrideCursor();
}

void ArsTrackerSessionsTab::onSessionNameClicked()
{
		QPushButton *btn = qobject_cast<QPushButton *>(sender());
		if (btn == nullptr)
		{
				return;
		}
		const QString sessionId = btn->property("sessionId").toString();
		if (sessionId.trimmed().isEmpty())
		{
				return;
		}
		showSessionDetailsPage(sessionId);
}

void ArsTrackerSessionsTab::onBackFromSessionDetails()
{
		showSessionsListPage(true);
}
