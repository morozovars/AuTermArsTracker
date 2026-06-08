#include "ars_tracker_player_edit_dialog.h"

#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QDir>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QVBoxLayout>
#include <QPixmap>
#include <QDebug>

#include "ars/workspace/ArsPlayerPosition.h"

namespace
{
QString defaultPositionDisplayName()
{
    return arsPlayerPositionDisplayName(QStringLiteral("central_midfielder"));
}

QString normalize_foot(const QString &value)
{
    const QString v = value.trimmed();
    if (v.compare("L", Qt::CaseInsensitive) == 0 || v.compare("Left", Qt::CaseInsensitive) == 0 ||
        v.compare(QString::fromUtf8("Р вЂєР ВµР Р†Р В°РЎРЏ"), Qt::CaseInsensitive) == 0)
    {
        return "L";
    }
    return "R";
}
}

ArsTrackerPlayerEditDialog::ArsTrackerPlayerEditDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Player");
    resize(640, 600);

    QVBoxLayout *root = new QVBoxLayout(this);
    QFormLayout *form = new QFormLayout();

    m_surname = new QLineEdit(this);
    form->addRow(QString::fromUtf8("Р В¤Р В°Р СР С‘Р В»Р С‘РЎРЏ*"), m_surname);

    m_name = new QLineEdit(this);
    form->addRow(QString::fromUtf8("Р ВР СРЎРЏ*"), m_name);

    QWidget *photoRow = new QWidget(this);
    QHBoxLayout *photoLayout = new QHBoxLayout(photoRow);
    photoLayout->setContentsMargins(0, 0, 0, 0);
    m_photoPath = new QLineEdit(photoRow);
    m_photoPath->setReadOnly(true);
    QPushButton *browsePhoto = new QPushButton("Browse", photoRow);
    photoLayout->addWidget(m_photoPath, 1);
    photoLayout->addWidget(browsePhoto);
    form->addRow("photo", photoRow);

    m_photoPreview = new QLabel(this);
    m_photoPreview->setMinimumSize(60, 100);
    m_photoPreview->setAlignment(Qt::AlignCenter);
    m_photoPreview->setText("No photo");
    form->addRow("preview", m_photoPreview);

    m_number = new QSpinBox(this);
    m_number->setRange(0, 999);
    form->addRow("number", m_number);

    m_birthDate = new QDateEdit(this);
    m_birthDate->setCalendarPopup(true);
    m_birthDate->setDisplayFormat("yyyy-MM-dd");
    m_birthDate->setDate(QDate::currentDate());
    form->addRow("birthDate", m_birthDate);

    m_position = new QComboBox(this);
    for (const ArsPlayerPositionInfo &position : arsPlayerPositions())
    {
        m_position->addItem(position.displayName, position.key);
    }
    form->addRow(QString::fromUtf8("position*"), m_position);

    m_dominantFoot = new QComboBox(this);
    m_dominantFoot->addItem(QString::fromUtf8("Правая"), "R");
    m_dominantFoot->addItem(QString::fromUtf8("Левая"), "L");
    form->addRow(QString::fromUtf8("dominantFoot"), m_dominantFoot);

    m_heightCm = new QSpinBox(this);
    m_heightCm->setRange(0, 250);
    form->addRow("heightCm", m_heightCm);

    m_weightKg = new QSpinBox(this);
    m_weightKg->setRange(0, 200);
    form->addRow("weightKg", m_weightKg);

    m_teamId = new QComboBox(this);
    form->addRow(QString::fromUtf8("team*"), m_teamId);

    m_maxSpeedMps = new QDoubleSpinBox(this);
    m_maxSpeedMps->setDecimals(2);
    m_maxSpeedMps->setRange(0.0, 20.0);
    form->addRow("maxSpeedMps", m_maxSpeedMps);

    m_maxShotLoadG = new QDoubleSpinBox(this);
    m_maxShotLoadG->setDecimals(1);
    m_maxShotLoadG->setRange(0.0, 1000.0);
    form->addRow("maxShotLoadG", m_maxShotLoadG);

    root->addLayout(form);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);

    connect(browsePhoto, &QPushButton::clicked, this, &ArsTrackerPlayerEditDialog::onBrowsePhoto);
    connect(buttons, &QDialogButtonBox::accepted, this, &ArsTrackerPlayerEditDialog::onAcceptClicked);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ArsTrackerPlayerEditDialog::setWorkspaceRootPath(const QString &workspaceRootPath)
{
    m_workspaceRootPath = workspaceRootPath.trimmed();
}

void ArsTrackerPlayerEditDialog::setTeams(const QList<ArsTeam> &teams)
{
    m_teams = teams;
    m_teamId->clear();
    for (const ArsTeam &team : m_teams)
    {
        m_teamId->addItem(QString("%1 (%2)").arg(team.name, team.ageCategory), team.teamId);
    }
}

void ArsTrackerPlayerEditDialog::setCreateMode(int defaultTeamId)
{
    m_editMode = false;
    m_existingPlayer = ArsPlayer();
    m_selectedPhotoSourcePath.clear();
    m_photoSelectionChanged = false;
    m_surname->clear();
    m_name->clear();
    m_photoPath->clear();
    m_number->setValue(0);
    m_birthDate->setDate(QDate::currentDate());
    const int defaultPositionIndex = m_position->findText(defaultPositionDisplayName());
    m_position->setCurrentIndex(defaultPositionIndex >= 0 ? defaultPositionIndex : 0);
    m_dominantFoot->setCurrentIndex(0);
    m_heightCm->setValue(0);
    m_weightKg->setValue(0);
    m_maxSpeedMps->setValue(0.0);
    m_maxShotLoadG->setValue(0.0);
    selectTeamIdOrAddMissing(defaultTeamId);
    updatePhotoPreview(QString());
    setWindowTitle("Create Player");
}

void ArsTrackerPlayerEditDialog::setEditMode(const ArsPlayer &player)
{
    m_editMode = true;
    m_existingPlayer = player;
    m_selectedPhotoSourcePath.clear();
    m_photoSelectionChanged = false;

    m_surname->setText(player.surname);
    m_name->setText(player.name);
    m_photoPath->setText(player.photoPath);
    m_number->setValue(player.number);
    m_birthDate->setDate(player.birthDate.isValid() ? player.birthDate : QDate::currentDate());
    const QString displayPosition = arsPlayerPositionDisplayNameFromAny(player.position);
    const int positionIndex = m_position->findText(displayPosition);
    m_position->setCurrentIndex(positionIndex >= 0 ? positionIndex : 0);
    const QString foot = normalize_foot(player.dominantFoot);
    const int footIndex = m_dominantFoot->findData(foot);
    m_dominantFoot->setCurrentIndex(footIndex >= 0 ? footIndex : 0);
    m_heightCm->setValue(player.heightCm);
    m_weightKg->setValue(player.weightKg);
    m_maxSpeedMps->setValue(player.personalNorms.maxSpeedMps);
    m_maxShotLoadG->setValue(player.personalNorms.maxShotLoadG);
    selectTeamIdOrAddMissing(player.teamId);
    updatePhotoPreview(player.photoPath);
    setWindowTitle("Edit Player");
}

ArsPlayer ArsTrackerPlayerEditDialog::playerFromUi() const
{
    ArsPlayer player = m_existingPlayer;
    player.surname = m_surname->text().trimmed();
    player.name = m_name->text().trimmed();
    player.number = m_number->value();
    player.birthDate = m_birthDate->date();
    player.position = arsPlayerPositionDisplayNameFromAny(m_position->currentText().trimmed());
    player.dominantFoot = m_dominantFoot->currentData().toString();
    player.heightCm = m_heightCm->value();
    player.weightKg = m_weightKg->value();
    player.teamId = m_teamId->currentData().toInt();
    player.personalNorms.maxSpeedMps = m_maxSpeedMps->value();
    player.personalNorms.maxShotLoadG = m_maxShotLoadG->value();
    player.photoPath = m_existingPlayer.photoPath;
    return player;
}

QString ArsTrackerPlayerEditDialog::selectedPhotoSourcePath() const
{
    return m_selectedPhotoSourcePath;
}

bool ArsTrackerPlayerEditDialog::photoSelectionChanged() const
{
    return m_photoSelectionChanged;
}

void ArsTrackerPlayerEditDialog::onBrowsePhoto()
{
    const QString path = QFileDialog::getOpenFileName(this,
                                                      "Select player photo",
                                                      QString(),
                                                      "Images (*.png *.jpg *.jpeg *.bmp *.webp)");
    if (path.trimmed().isEmpty())
    {
        return;
    }
    qDebug() << "Ars Player photo selected source=" << path;
    m_selectedPhotoSourcePath = path;
    m_photoSelectionChanged = true;
    m_photoPath->setText(path);
    updatePhotoPreview(path);
}

void ArsTrackerPlayerEditDialog::updatePhotoPreview(const QString &path)
{
    const QString normalizedPath = path.trimmed();
    if (normalizedPath.isEmpty())
    {
        m_photoPreview->setPixmap(QPixmap());
        m_photoPreview->setText("No photo");
        return;
    }
    QString previewPath = normalizedPath;
    if (!QFileInfo(previewPath).isAbsolute() && !m_workspaceRootPath.trimmed().isEmpty())
    {
        previewPath = QDir(m_workspaceRootPath).filePath(previewPath);
    }
    QPixmap pixmap(previewPath);
    if (pixmap.isNull())
    {
        m_photoPreview->setPixmap(QPixmap());
        m_photoPreview->setText("Invalid");
        return;
    }
    m_photoPreview->setPixmap(pixmap.scaled(60, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void ArsTrackerPlayerEditDialog::selectTeamIdOrAddMissing(int teamId)
{
    int index = m_teamId->findData(teamId);
    if (index >= 0)
    {
        m_teamId->setCurrentIndex(index);
        return;
    }
    if (teamId >= 0)
    {
        m_teamId->insertItem(0, QString("Missing team id %1").arg(teamId), -1);
        m_teamId->setCurrentIndex(0);
        return;
    }
    if (m_teamId->count() > 0)
    {
        m_teamId->setCurrentIndex(0);
    }
}

bool ArsTrackerPlayerEditDialog::validateForm(QString *errorMessage) const
{
    if (m_surname->text().trimmed().isEmpty())
    {
        if (errorMessage != nullptr) *errorMessage = "surname is required";
        return false;
    }
    if (m_name->text().trimmed().isEmpty())
    {
        if (errorMessage != nullptr) *errorMessage = "name is required";
        return false;
    }
    if (m_position->currentText().trimmed().isEmpty())
    {
        if (errorMessage != nullptr) *errorMessage = "position is required";
        return false;
    }
    if (!m_birthDate->date().isValid())
    {
        if (errorMessage != nullptr) *errorMessage = "birthDate is invalid";
        return false;
    }
    if (m_birthDate->date() > QDate::currentDate())
    {
        if (errorMessage != nullptr) *errorMessage = "birthDate cannot be in the future";
        return false;
    }
    if (m_teamId->currentData().toInt() <= 0)
    {
        if (errorMessage != nullptr) *errorMessage = "team is required";
        return false;
    }
    return true;
}

void ArsTrackerPlayerEditDialog::onAcceptClicked()
{
    QString error;
    if (!validateForm(&error))
    {
        QMessageBox::warning(this, "Player", QString("Validation failed: %1").arg(error));
        return;
    }
    accept();
}
