#include "ars_tracker_team_tab.h"

#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QDialog>
#include <QPushButton>
#include <QPixmap>
#include <QFormLayout>
#include <QGroupBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QDebug>

#include "ars/workspace/ArsLocalWorkspace.h"
#include "ars/workspace/ArsAppSettings.h"
#include "ars/workspace/ArsTeamRepository.h"
#include "ars/workspace/ArsTargetsByPosition.h"
#include "ars_team_edit_dialog.h"
#include "ars_team_targets_dialog.h"

namespace
{
QString resolve_team_logo_absolute_path(const QString &workspaceRoot, const QString &logoPath)
{
    const QString trimmed = logoPath.trimmed();
    if (trimmed.isEmpty())
    {
        return QString();
    }
    const QFileInfo info(trimmed);
    if (info.isAbsolute())
    {
        return QDir::cleanPath(info.absoluteFilePath());
    }
    return QDir::cleanPath(QDir(workspaceRoot).filePath(trimmed));
}
}

ArsTrackerTeamTab::ArsTrackerTeamTab(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    reloadTeams("init");
}

QString ArsTrackerTeamTab::workspacePath() const
{
    ArsLocalWorkspace workspace;
    if (!workspace.initialize())
    {
        return QString();
    }
    return workspace.rootPath();
}

void ArsTrackerTeamTab::buildUi()
{
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    QHBoxLayout *top = new QHBoxLayout();
    m_defaultLabel = new QLabel("Default team: not selected", this);
    m_reloadButton = new QPushButton("Reload", this);
    m_createButton = new QPushButton("Create Team", this);
    top->addWidget(m_defaultLabel, 1);
    top->addWidget(m_reloadButton);
    top->addWidget(m_createButton);
    root->addLayout(top);

    m_defaultTeamBox = new QGroupBox("Default team", this);
    QVBoxLayout *defaultRoot = new QVBoxLayout(m_defaultTeamBox);
    QHBoxLayout *defaultHead = new QHBoxLayout();
    m_defaultTeamLogo = new QLabel("No logo", m_defaultTeamBox);
    m_defaultTeamLogo->setMinimumSize(72, 72);
    m_defaultTeamLogo->setAlignment(Qt::AlignCenter);
    defaultHead->addWidget(m_defaultTeamLogo, 0);
    QFormLayout *metaForm = new QFormLayout();
    m_defaultTeamName = new QLabel("-", m_defaultTeamBox);
    m_defaultTeamAge = new QLabel("-", m_defaultTeamBox);
    m_defaultTeamCoaches = new QLabel("-", m_defaultTeamBox);
    m_defaultTeamCoaches->setWordWrap(true);
    metaForm->addRow("Name", m_defaultTeamName);
    metaForm->addRow("Age category", m_defaultTeamAge);
    metaForm->addRow("Coaches", m_defaultTeamCoaches);
    defaultHead->addLayout(metaForm, 1);
    defaultRoot->addLayout(defaultHead);

    m_defaultTargetsTable = new QTableWidget(m_defaultTeamBox);
    m_defaultTargetsTable->setColumnCount(9);
    m_defaultTargetsTable->setHorizontalHeaderLabels(QStringList()
                                                     << "Position"
                                                     << "Distance km"
                                                     << "Accel dist m"
                                                     << "Footload"
                                                     << "Touches"
                                                     << "Intensity"
                                                     << "Max speed m/s"
                                                     << "Shots"
                                                     << "Possessions");
    m_defaultTargetsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_defaultTargetsTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_defaultTargetsTable->verticalHeader()->setVisible(false);
    m_defaultTargetsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    for (int i = 1; i < 9; ++i) m_defaultTargetsTable->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Stretch);
    defaultRoot->addWidget(m_defaultTargetsTable);

    m_editTargetsButton = new QPushButton("Edit targets", m_defaultTeamBox);
    defaultRoot->addWidget(m_editTargetsButton, 0, Qt::AlignRight);
    root->addWidget(m_defaultTeamBox);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(8);
    m_table->setHorizontalHeaderLabels(
        QStringList() << "Default" << "team_num" << "team_id" << "name" << "age_category" << "coaches" << "logo"
                      << "actions");
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(7, QHeaderView::ResizeToContents);
    m_table->setColumnWidth(6, 72);
    m_table->verticalHeader()->setDefaultSectionSize(56);
    root->addWidget(m_table, 1);

    connect(m_reloadButton, &QPushButton::clicked, this, [this]() { reloadTeams("reload-click"); });
    connect(m_createButton, &QPushButton::clicked, this, &ArsTrackerTeamTab::onCreateTeam);
    connect(m_editTargetsButton, &QPushButton::clicked, this, &ArsTrackerTeamTab::onEditTargets);
}

void ArsTrackerTeamTab::reloadTeams(const QString &reason)
{
    Q_UNUSED(reason);
    const QString workspace = workspacePath();
    qDebug() << "Ars Team tab reload begin workspace=" << workspace;
    if (workspace.trimmed().isEmpty())
    {
        m_teams.clear();
        m_defaultTeamId = -1;
        updateDefaultTeamLabel();
        rebuildTeamsTable();
        return;
    }

    ArsTeamRepository repository(workspace);
    QStringList warnings;
    m_teams = repository.loadTeams(&warnings);
    for (const QString &warning : warnings)
    {
        qWarning().noquote() << warning;
    }
    qDebug() << "Ars Team tab teams loaded count=" << m_teams.size();

    ArsAppSettings settings(workspace);
    QString settingsError;
    int loadedDefaultId = -1;
    if (!settings.loadDefaultTeamId(&loadedDefaultId, &settingsError))
    {
        qWarning() << "Ars Team settings load failed error=" << settingsError;
        loadedDefaultId = -1;
    }
    qDebug() << "Ars Team settings load default_team_id=" << loadedDefaultId;
    m_defaultTeamId = loadedDefaultId;

    const ArsTeam *defaultTeam = findTeamById(m_defaultTeamId);
    if (m_defaultTeamId < 0)
    {
        qDebug() << "Ars Team tab default team not selected";
    }
    else if (defaultTeam == nullptr)
    {
        qWarning() << "Ars Team tab default team missing id=" << m_defaultTeamId;
    }
    else if (defaultTeam != nullptr)
    {
        qDebug() << "Ars Team tab default team resolved id=" << defaultTeam->teamId << "name=" << defaultTeam->name;
    }

    updateDefaultTeamLabel();
    rebuildDefaultTeamDetails();
    rebuildTeamsTable();
}

void ArsTrackerTeamTab::updateDefaultTeamLabel()
{
    const ArsTeam *team = findTeamById(m_defaultTeamId);
    if (team == nullptr)
    {
        if (m_defaultTeamId >= 0)
        {
            m_defaultLabel->setText(QString("Default team: missing team id %1").arg(m_defaultTeamId));
            return;
        }
        m_defaultLabel->setText("Default team: not selected");
        return;
    }
    m_defaultLabel->setText(QString("Default team: %1 (%2)").arg(team->name, team->ageCategory));
}

void ArsTrackerTeamTab::rebuildDefaultTeamTargetsTable(const ArsTeam *team)
{
    m_defaultTargetsTable->clearContents();
    const QStringList keys = arsTargetPositionKeys();
    m_defaultTargetsTable->setRowCount(keys.size());
    for (int row = 0; row < keys.size(); ++row)
    {
        const QString key = keys.at(row);
        const ArsPlannedMetrics m = team != nullptr
                                        ? team->targetsByPosition.value(key, team->defaultPlannedMetrics)
                                        : ArsPlannedMetrics{};
        m_defaultTargetsTable->setItem(row, 0, new QTableWidgetItem(arsTargetPositionLabel(key)));
        m_defaultTargetsTable->setItem(row, 1, new QTableWidgetItem(QString::number(m.distanceKm, 'f', 2)));
        m_defaultTargetsTable->setItem(row, 2, new QTableWidgetItem(QString::number(m.accelerationDistanceM)));
        m_defaultTargetsTable->setItem(row, 3, new QTableWidgetItem(QString::number(m.footloadPerLeg)));
        m_defaultTargetsTable->setItem(row, 4, new QTableWidgetItem(QString::number(m.touches)));
        m_defaultTargetsTable->setItem(row, 5, new QTableWidgetItem(QString::number(m.loadIntensityGPerMin, 'f', 2)));
        m_defaultTargetsTable->setItem(row, 6, new QTableWidgetItem(QString::number(m.maxSpeedMps, 'f', 2)));
        m_defaultTargetsTable->setItem(row, 7, new QTableWidgetItem(QString::number(m.shots)));
        m_defaultTargetsTable->setItem(row, 8, new QTableWidgetItem(QString::number(m.dribbles)));
    }
}

void ArsTrackerTeamTab::rebuildDefaultTeamDetails()
{
    const ArsTeam *team = findTeamById(m_defaultTeamId);
    if (team == nullptr)
    {
        m_defaultTeamName->setText("-");
        m_defaultTeamAge->setText("-");
        m_defaultTeamCoaches->setText("-");
        m_defaultTeamLogo->setText("No logo");
        m_defaultTeamLogo->setPixmap(QPixmap());
        m_editTargetsButton->setEnabled(false);
        rebuildDefaultTeamTargetsTable(nullptr);
        return;
    }

    m_defaultTeamName->setText(team->name);
    m_defaultTeamAge->setText(team->ageCategory);
    m_defaultTeamCoaches->setText(coachesDisplay(*team));
    const QString root = workspacePath();
    const QString absoluteLogoPath = resolve_team_logo_absolute_path(root, team->teamLogoPath);
    QPixmap pixmap(absoluteLogoPath);
    if (team->teamLogoPath.trimmed().isEmpty() || pixmap.isNull())
    {
        m_defaultTeamLogo->setPixmap(QPixmap());
        m_defaultTeamLogo->setText("No logo");
    }
    else
    {
        m_defaultTeamLogo->setText(QString());
        m_defaultTeamLogo->setPixmap(pixmap.scaled(72, 72, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    m_editTargetsButton->setEnabled(true);
    rebuildDefaultTeamTargetsTable(team);
}

QString ArsTrackerTeamTab::coachesDisplay(const ArsTeam &team) const
{
    return team.defaultCoaches.isEmpty() ? QString("-") : team.defaultCoaches.join(", ");
}

void ArsTrackerTeamTab::rebuildTeamsTable()
{
    m_table->setSortingEnabled(false);
    m_table->clearContents();
    m_table->setRowCount(m_teams.size());
    const QString root = workspacePath();

    for (int row = 0; row < m_teams.size(); ++row)
    {
        const ArsTeam &team = m_teams.at(row);
        if (team.teamId == m_defaultTeamId)
        {
            m_table->setItem(row, 0, new QTableWidgetItem("Default"));
        }
        else
        {
            QPushButton *setDefault = new QPushButton("Set default", m_table);
            setDefault->setProperty("teamId", team.teamId);
            connect(setDefault, &QPushButton::clicked, this, &ArsTrackerTeamTab::onSetDefaultTeam);
            m_table->setCellWidget(row, 0, setDefault);
        }
        m_table->setItem(row, 1, new QTableWidgetItem(QString::number(team.teamNum)));
        m_table->setItem(row, 2, new QTableWidgetItem(QString::number(team.teamId)));
        m_table->setItem(row, 3, new QTableWidgetItem(team.name));
        m_table->setItem(row, 4, new QTableWidgetItem(team.ageCategory));
        m_table->setItem(row, 5, new QTableWidgetItem(coachesDisplay(team)));

        QLabel *logoLabel = new QLabel(m_table);
        logoLabel->setAlignment(Qt::AlignCenter);
        const QString absoluteLogoPath = resolve_team_logo_absolute_path(root, team.teamLogoPath);
        if (team.teamLogoPath.trimmed().isEmpty())
        {
            logoLabel->setText("No logo");
        }
        else if (!QFileInfo::exists(absoluteLogoPath))
        {
            qWarning() << "Ars Team logo missing id=" << team.teamId << "path=" << absoluteLogoPath;
            logoLabel->setText("Missing");
            logoLabel->setToolTip(team.teamLogoPath);
        }
        else
        {
            QPixmap pixmap(absoluteLogoPath);
            if (pixmap.isNull())
            {
                qWarning() << "Ars Team logo invalid id=" << team.teamId << "path=" << absoluteLogoPath;
                logoLabel->setText("Invalid image");
                logoLabel->setToolTip(team.teamLogoPath);
            }
            else
            {
                logoLabel->setPixmap(pixmap.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                logoLabel->setToolTip(absoluteLogoPath);
            }
        }
        m_table->setCellWidget(row, 6, logoLabel);
        m_table->setRowHeight(row, 56);

        QWidget *actionsWidget = new QWidget(m_table);
        QHBoxLayout *actionsLayout = new QHBoxLayout(actionsWidget);
        actionsLayout->setContentsMargins(2, 2, 2, 2);
        actionsLayout->setSpacing(4);
        QPushButton *edit = new QPushButton("Edit", actionsWidget);
        edit->setProperty("teamId", team.teamId);
        connect(edit, &QPushButton::clicked, this, &ArsTrackerTeamTab::onEditTeam);
        QPushButton *del = new QPushButton("Delete", actionsWidget);
        del->setProperty("teamId", team.teamId);
        connect(del, &QPushButton::clicked, this, &ArsTrackerTeamTab::onDeleteTeam);
        actionsLayout->addWidget(edit);
        actionsLayout->addWidget(del);
        m_table->setCellWidget(row, 7, actionsWidget);
    }
}

bool ArsTrackerTeamTab::saveTeamWithLog(const ArsTeam &team)
{
    const QString workspace = workspacePath();
    ArsTeamRepository repository(workspace);
    QString error;
    if (!repository.saveTeam(team, &error))
    {
        qWarning() << "Ars Team save failed id=" << team.teamId << "error=" << error;
        QMessageBox::warning(this, "Team", QString("Failed to save team: %1").arg(error));
        return false;
    }
    const QString teamPath = QDir(repository.teamsPath()).filePath(QString("team_%1.json").arg(team.teamId));
    if (findTeamById(team.teamId) != nullptr)
    {
        qDebug() << "Ars Team saved id=" << team.teamId << "path=" << teamPath << "logoPath=" << team.teamLogoPath;
    }
    else
    {
        qDebug() << "Ars Team created id=" << team.teamId << "path=" << teamPath << "logoPath=" << team.teamLogoPath;
    }
    return true;
}

bool ArsTrackerTeamTab::saveTeamWithLogoSelection(const ArsTeam &team,
                                                  const QString &logoSourcePath,
                                                  bool logoSelectionChanged)
{
    ArsTeam teamToSave = team;
    const QString workspace = workspacePath();
    ArsTeamRepository repository(workspace);

    if (logoSelectionChanged)
    {
        qDebug() << "Ars Team logo copy begin teamId=" << teamToSave.teamId << "source=" << logoSourcePath;
        QString relativePath;
        QString copyError;
        if (!repository.copyTeamLogoToWorkspace(teamToSave.teamId, logoSourcePath, &relativePath, &copyError))
        {
            qWarning() << "Ars Team logo copy failed teamId=" << teamToSave.teamId << "source=" << logoSourcePath
                       << "error=" << copyError;
            QMessageBox::warning(this, "Team", QString("Failed to copy team logo: %1").arg(copyError));
            return false;
        }
        const QString absolutePath = repository.resolveLogoAbsolutePath(relativePath);
        qDebug() << "Ars Team logo copied teamId=" << teamToSave.teamId << "relativePath=" << relativePath
                 << "absolutePath=" << absolutePath;
        teamToSave.teamLogoPath = relativePath;
    }
    else
    {
        const QFileInfo logoInfo(teamToSave.teamLogoPath.trimmed());
        if (logoInfo.isAbsolute() && logoInfo.exists())
        {
            qDebug() << "Ars Team logo copy begin teamId=" << teamToSave.teamId << "source=" << teamToSave.teamLogoPath;
            QString relativePath;
            QString copyError;
            if (repository.copyTeamLogoToWorkspace(teamToSave.teamId, teamToSave.teamLogoPath, &relativePath, &copyError))
            {
                const QString absolutePath = repository.resolveLogoAbsolutePath(relativePath);
                qDebug() << "Ars Team logo copied teamId=" << teamToSave.teamId << "relativePath=" << relativePath
                         << "absolutePath=" << absolutePath;
                teamToSave.teamLogoPath = relativePath;
            }
            else
            {
                qWarning() << "Ars Team logo copy failed teamId=" << teamToSave.teamId << "source=" << teamToSave.teamLogoPath
                           << "error=" << copyError;
            }
        }
    }

    if (!saveTeamWithLog(teamToSave))
    {
        return false;
    }
    QString lastIdError;
    if (!ArsAppSettings(workspace).saveLastTeamId(teamToSave.teamId, &lastIdError))
    {
        qWarning() << "Ars Team settings save last_team_id failed id=" << teamToSave.teamId << "error=" << lastIdError;
    }
    return true;
}

bool ArsTrackerTeamTab::saveDefaultTeamIdWithLog(int teamId)
{
    const ArsTeam *team = findTeamById(teamId);
    qDebug() << "Ars Team set default requested id=" << teamId << "name=" << (team != nullptr ? team->name : QString());
    const QString workspace = workspacePath();
    ArsAppSettings settings(workspace);
    QString error;
    qDebug() << "Ars Team settings save default_team_id begin id=" << teamId;
    if (!settings.saveDefaultTeamId(teamId, &error))
    {
        qWarning() << "Ars Team settings save failed error=" << error;
        QMessageBox::warning(this, "Team", QString("Failed to save default team: %1").arg(error));
        return false;
    }
    qDebug() << "Ars Team settings saved default_team_id=" << teamId;
    m_defaultTeamId = teamId;
    qDebug() << "Ars Team set default done id=" << teamId;
    return true;
}

ArsTeam *ArsTrackerTeamTab::findTeamById(int teamId)
{
    for (ArsTeam &team : m_teams)
    {
        if (team.teamId == teamId)
        {
            return &team;
        }
    }
    return nullptr;
}

const ArsTeam *ArsTrackerTeamTab::findTeamById(int teamId) const
{
    for (const ArsTeam &team : m_teams)
    {
        if (team.teamId == teamId)
        {
            return &team;
        }
    }
    return nullptr;
}

bool ArsTrackerTeamTab::hasDuplicateTeamNum(int teamNum, int excludeTeamId) const
{
    for (const ArsTeam &team : m_teams)
    {
        if (team.teamId != excludeTeamId && team.teamNum == teamNum)
        {
            return true;
        }
    }
    return false;
}

void ArsTrackerTeamTab::onCreateTeam()
{
    qDebug() << "Ars Team create begin";
    const QString workspace = workspacePath();
    ArsTeamRepository repository(workspace);
    ArsTeamEditDialog dialog(this);
    dialog.setCreateMode();
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    ArsTeam team = dialog.teamFromUi();
    QStringList warnings;
    team.teamId = repository.nextTeamId(&warnings);
    for (const QString &warning : warnings)
    {
        qWarning().noquote() << warning;
    }
    if (hasDuplicateTeamNum(team.teamNum, -1))
    {
        QMessageBox::warning(this, "Team", "Warning: team_num already exists.");
    }
    if (!saveTeamWithLogoSelection(team, dialog.selectedLogoSourcePath(), dialog.logoSelectionChanged()))
    {
        return;
    }

    int currentDefaultId = -1;
    QString defaultLoadError;
    const bool defaultLoaded = ArsAppSettings(workspace).loadDefaultTeamId(&currentDefaultId, &defaultLoadError);
    if (!defaultLoaded)
    {
        qWarning() << "Ars Team settings load failed error=" << defaultLoadError;
        currentDefaultId = -1;
    }
    const bool defaultExists = (findTeamById(currentDefaultId) != nullptr);

    reloadTeams("create");
    if (currentDefaultId < 0 || !defaultExists)
    {
        qDebug() << "Ars Team first/default missing, assigning created team as default id=" << team.teamId;
        qDebug() << "Ars Team auto default begin id=" << team.teamId;
        if (saveDefaultTeamIdWithLog(team.teamId))
        {
            qDebug() << "Ars Team auto default done id=" << team.teamId;
            reloadTeams("create-first-default");
        }
    }
}

void ArsTrackerTeamTab::onEditTeam()
{
    QPushButton *button = qobject_cast<QPushButton *>(sender());
    if (button == nullptr)
    {
        return;
    }
    const int teamId = button->property("teamId").toInt();
    qDebug() << "Ars Team edit begin id=" << teamId;
    ArsTeam *existing = findTeamById(teamId);
    if (existing == nullptr)
    {
        return;
    }

    ArsTeamEditDialog dialog(this);
    dialog.setEditMode(*existing);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }
    ArsTeam edited = dialog.teamFromUi();
    edited.teamId = teamId;
    if (hasDuplicateTeamNum(edited.teamNum, teamId))
    {
        QMessageBox::warning(this, "Team", "Warning: team_num already exists.");
    }
    if (!saveTeamWithLogoSelection(edited, dialog.selectedLogoSourcePath(), dialog.logoSelectionChanged()))
    {
        return;
    }
    reloadTeams("edit");
}

void ArsTrackerTeamTab::onSetDefaultTeam()
{
    QPushButton *button = qobject_cast<QPushButton *>(sender());
    if (button == nullptr)
    {
        return;
    }
    const int teamId = button->property("teamId").toInt();
    if (teamId <= 0)
    {
        return;
    }
    if (!saveDefaultTeamIdWithLog(teamId))
    {
        return;
    }
    reloadTeams("set-default");
}

void ArsTrackerTeamTab::onEditTargets()
{
    ArsTeam *team = findTeamById(m_defaultTeamId);
    if (team == nullptr)
    {
        QMessageBox::warning(this, "Team", "Default team is not selected.");
        return;
    }

    ArsTeamTargetsDialog dialog(this);
    QMap<QString, ArsPlannedMetrics> current = team->targetsByPosition;
    if (current.isEmpty())
    {
        current = arsTargetsByPositionFromLegacy(team->defaultPlannedMetrics);
    }
    dialog.setTargetsByPosition(current);
    if (dialog.exec() != QDialog::Accepted)
    {
        return;
    }

    team->targetsByPosition = dialog.targetsByPosition();
    qDebug() << "ArsTeamTargets: saved targetsByPosition for team" << team->teamId;
    if (!saveTeamWithLog(*team))
    {
        return;
    }
    rebuildDefaultTeamDetails();
    rebuildTeamsTable();
}

void ArsTrackerTeamTab::onDeleteTeam()
{
    QPushButton *button = qobject_cast<QPushButton *>(sender());
    if (button == nullptr)
    {
        return;
    }
    const int teamId = button->property("teamId").toInt();
    const ArsTeam *team = findTeamById(teamId);
    if (team == nullptr || teamId <= 0)
    {
        return;
    }
    qDebug() << "Ars Team delete requested id=" << teamId << "name=" << team->name;

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this,
        "Delete team",
        QString("Delete team?\nTeam: %1\nID: %2\n\nThis will delete team JSON and team assets.").arg(team->name).arg(teamId),
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    if (answer != QMessageBox::Yes)
    {
        qDebug() << "Ars Team delete cancelled id=" << teamId;
        return;
    }
    qDebug() << "Ars Team delete confirmed id=" << teamId;

    const QString workspace = workspacePath();
    ArsTeamRepository repository(workspace);
    QString error;
    if (!repository.deleteTeam(teamId, &error))
    {
        QMessageBox::warning(this, "Team", QString("Failed to delete team: %1").arg(error));
        return;
    }

    if (teamId == m_defaultTeamId)
    {
        qDebug() << "Ars Team deleted team was default id=" << teamId << ", clearing default";
        QString clearError;
        if (!ArsAppSettings(workspace).clearDefaultTeamId(&clearError))
        {
            qWarning() << "Ars Team settings save failed error=" << clearError;
            QMessageBox::warning(this, "Team", QString("Failed to clear default team: %1").arg(clearError));
        }
        else
        {
            qDebug() << "Ars Team default cleared after delete id=" << teamId;
        }
    }
    reloadTeams("delete-team");
}
