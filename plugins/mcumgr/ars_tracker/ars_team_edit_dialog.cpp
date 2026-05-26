#include "ars_team_edit_dialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QVBoxLayout>
#include <QDebug>

namespace
{
QStringList coaches_from_text(const QString &text)
{
    QStringList out;
    const QStringList lines = text.split('\n');
    for (const QString &line : lines)
    {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty())
        {
            out.append(trimmed);
        }
    }
    return out;
}
}

ArsTeamEditDialog::ArsTeamEditDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Team");
    resize(680, 620);

    QVBoxLayout *root = new QVBoxLayout(this);
    QFormLayout *main = new QFormLayout();

    m_teamIdLabel = new QLabel("will be assigned automatically", this);
    main->addRow("team_id", m_teamIdLabel);

    m_teamNum = new QSpinBox(this);
    m_teamNum->setRange(0, 1000000);
    main->addRow("team_num", m_teamNum);

    m_name = new QLineEdit(this);
    main->addRow("name*", m_name);

    m_ageCategory = new QLineEdit(this);
    main->addRow("age_category", m_ageCategory);

    m_coaches = new QPlainTextEdit(this);
    m_coaches->setPlaceholderText("One coach per line");
    m_coaches->setFixedHeight(90);
    main->addRow("default_coaches", m_coaches);

    QWidget *logoRow = new QWidget(this);
    QHBoxLayout *logoLayout = new QHBoxLayout(logoRow);
    logoLayout->setContentsMargins(0, 0, 0, 0);
    m_logoPath = new QLineEdit(logoRow);
    QPushButton *browseButton = new QPushButton("Browse", logoRow);
    logoLayout->addWidget(m_logoPath, 1);
    logoLayout->addWidget(browseButton);
    main->addRow("team_logo.path", logoRow);
    root->addLayout(main);

    QGroupBox *thresholdsBox = new QGroupBox("Default thresholds", this);
    QFormLayout *thresholds = new QFormLayout(thresholdsBox);
    m_accelerationDistanceM = new QSpinBox(thresholdsBox);
    m_accelerationDistanceM->setRange(0, 1000000);
    thresholds->addRow("accelerationDistanceM", m_accelerationDistanceM);
    m_distanceKm = new QDoubleSpinBox(thresholdsBox);
    m_distanceKm->setRange(0.0, 100000.0);
    m_distanceKm->setDecimals(3);
    thresholds->addRow("distanceKm", m_distanceKm);
    m_dribbles = new QSpinBox(thresholdsBox);
    m_dribbles->setRange(0, 1000000);
    thresholds->addRow("dribbles", m_dribbles);
    m_footloadPerLeg = new QSpinBox(thresholdsBox);
    m_footloadPerLeg->setRange(0, 1000000);
    thresholds->addRow("footloadPerLeg", m_footloadPerLeg);
    m_loadIntensity = new QDoubleSpinBox(thresholdsBox);
    m_loadIntensity->setRange(0.0, 100000.0);
    m_loadIntensity->setDecimals(3);
    thresholds->addRow("loadIntensityGPerMin", m_loadIntensity);
    m_maxSpeed = new QDoubleSpinBox(thresholdsBox);
    m_maxSpeed->setRange(0.0, 1000.0);
    m_maxSpeed->setDecimals(3);
    thresholds->addRow("maxSpeedMps", m_maxSpeed);
    m_shots = new QSpinBox(thresholdsBox);
    m_shots->setRange(0, 1000000);
    thresholds->addRow("shots", m_shots);
    m_touches = new QSpinBox(thresholdsBox);
    m_touches->setRange(0, 1000000);
    thresholds->addRow("touches", m_touches);
    root->addWidget(thresholdsBox);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);

    connect(browseButton, &QPushButton::clicked, this, &ArsTeamEditDialog::onBrowseLogo);
    connect(buttons, &QDialogButtonBox::accepted, this, &ArsTeamEditDialog::onAcceptClicked);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setCreateMode();
}

void ArsTeamEditDialog::setCreateMode()
{
    m_editMode = false;
    m_existingTeamId = 0;
    m_selectedLogoSourcePath.clear();
    m_logoSelectionChanged = false;
    m_logoPath->clear();
    m_teamIdLabel->setText("will be assigned automatically");
    setWindowTitle("Create Team");
}

void ArsTeamEditDialog::setEditMode(const ArsTeam &team)
{
    m_editMode = true;
    m_existingTeamId = team.teamId;
    m_teamIdLabel->setText(QString::number(team.teamId));
    m_teamNum->setValue(team.teamNum);
    m_name->setText(team.name);
    m_ageCategory->setText(team.ageCategory);
    m_coaches->setPlainText(team.defaultCoaches.join('\n'));
    m_logoPath->setText(team.teamLogoPath);
    m_selectedLogoSourcePath.clear();
    m_logoSelectionChanged = false;
    m_accelerationDistanceM->setValue(team.defaultPlannedMetrics.accelerationDistanceM);
    m_distanceKm->setValue(team.defaultPlannedMetrics.distanceKm);
    m_dribbles->setValue(team.defaultPlannedMetrics.dribbles);
    m_footloadPerLeg->setValue(team.defaultPlannedMetrics.footloadPerLeg);
    m_loadIntensity->setValue(team.defaultPlannedMetrics.loadIntensityGPerMin);
    m_maxSpeed->setValue(team.defaultPlannedMetrics.maxSpeedMps);
    m_shots->setValue(team.defaultPlannedMetrics.shots);
    m_touches->setValue(team.defaultPlannedMetrics.touches);
    setWindowTitle("Edit Team");
}

ArsTeam ArsTeamEditDialog::teamFromUi() const
{
    ArsTeam team;
    team.teamId = m_editMode ? m_existingTeamId : 0;
    team.teamNum = m_teamNum->value();
    team.name = m_name->text().trimmed();
    team.ageCategory = m_ageCategory->text().trimmed();
    team.defaultCoaches = coaches_from_text(m_coaches->toPlainText());
    team.teamLogoPath = m_logoPath->text().trimmed();
    team.defaultPlannedMetrics.accelerationDistanceM = m_accelerationDistanceM->value();
    team.defaultPlannedMetrics.distanceKm = m_distanceKm->value();
    team.defaultPlannedMetrics.dribbles = m_dribbles->value();
    team.defaultPlannedMetrics.footloadPerLeg = m_footloadPerLeg->value();
    team.defaultPlannedMetrics.loadIntensityGPerMin = m_loadIntensity->value();
    team.defaultPlannedMetrics.maxSpeedMps = m_maxSpeed->value();
    team.defaultPlannedMetrics.shots = m_shots->value();
    team.defaultPlannedMetrics.touches = m_touches->value();
    return team;
}

void ArsTeamEditDialog::onBrowseLogo()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        "Select team logo",
        m_logoPath->text().trimmed(),
        "Images (*.png *.jpg *.jpeg *.bmp *.svg)");
    if (!path.trimmed().isEmpty())
    {
        qDebug() << "Ars Team logo selected source=" << path;
        m_selectedLogoSourcePath = path;
        m_logoSelectionChanged = true;
        m_logoPath->setText(path);
    }
}

QString ArsTeamEditDialog::selectedLogoSourcePath() const
{
    return m_selectedLogoSourcePath;
}

bool ArsTeamEditDialog::logoSelectionChanged() const
{
    return m_logoSelectionChanged;
}

void ArsTeamEditDialog::onAcceptClicked()
{
    if (m_name->text().trimmed().isEmpty())
    {
        QMessageBox::warning(this, "Team", "Field 'name' is required.");
        return;
    }
    accept();
}
