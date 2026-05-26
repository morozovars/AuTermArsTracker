#include "ars_tracker_team_tab.h"

#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QDialog>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QDebug>

#include "ars/workspace/ArsLocalWorkspace.h"
#include "ars/workspace/ArsAppSettings.h"
#include "ars/workspace/ArsTeamRepository.h"
#include "ars_team_edit_dialog.h"

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
    m_table->horizontalHeader()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(7, QHeaderView::ResizeToContents);
    root->addWidget(m_table, 1);

    connect(m_reloadButton, &QPushButton::clicked, this, [this]() { reloadTeams("reload-click"); });
    connect(m_createButton, &QPushButton::clicked, this, &ArsTrackerTeamTab::onCreateTeam);
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

QString ArsTrackerTeamTab::coachesDisplay(const ArsTeam &team) const
{
    return team.defaultCoaches.isEmpty() ? QString("-") : team.defaultCoaches.join(", ");
}

QString ArsTrackerTeamTab::logoDisplay(const ArsTeam &team) const
{
    if (team.teamLogoPath.trimmed().isEmpty())
    {
        return "-";
    }
    return QFileInfo(team.teamLogoPath).fileName();
}

void ArsTrackerTeamTab::rebuildTeamsTable()
{
    m_table->setSortingEnabled(false);
    m_table->clearContents();
    m_table->setRowCount(m_teams.size());

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
        m_table->setItem(row, 6, new QTableWidgetItem(logoDisplay(team)));

        QPushButton *edit = new QPushButton("Edit", m_table);
        edit->setProperty("teamId", team.teamId);
        connect(edit, &QPushButton::clicked, this, &ArsTrackerTeamTab::onEditTeam);
        m_table->setCellWidget(row, 7, edit);
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

    return saveTeamWithLog(teamToSave);
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
