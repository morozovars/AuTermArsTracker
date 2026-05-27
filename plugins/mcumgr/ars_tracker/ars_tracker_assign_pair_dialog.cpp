#include "ars_tracker_assign_pair_dialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QVBoxLayout>

ArsTrackerAssignPairDialog::ArsTrackerAssignPairDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Assign tracker pair");
    resize(420, 220);

    QVBoxLayout *root = new QVBoxLayout(this);
    QFormLayout *form = new QFormLayout();

    m_playerLabel = new QLabel(this);
    form->addRow("Player", m_playerLabel);
    m_teamLabel = new QLabel(this);
    form->addRow("Team", m_teamLabel);

    m_pairId = new QLineEdit(this);
    form->addRow("pairId*", m_pairId);
    m_leftSerial = new QLineEdit(this);
    form->addRow("leftTrackerSerial", m_leftSerial);
    m_rightSerial = new QLineEdit(this);
    form->addRow("rightTrackerSerial", m_rightSerial);
    root->addLayout(form);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);

    connect(m_pairId, &QLineEdit::textChanged, this, &ArsTrackerAssignPairDialog::onPairChanged);
    connect(buttons, &QDialogButtonBox::accepted, this, &ArsTrackerAssignPairDialog::onAcceptClicked);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ArsTrackerAssignPairDialog::setPlayerContext(const QString &playerName, const QString &teamName)
{
    m_playerLabel->setText(playerName);
    m_teamLabel->setText(teamName);
}

void ArsTrackerAssignPairDialog::setInitialPair(const QString &pairId, const QString &leftSerial, const QString &rightSerial)
{
    m_pairId->setText(pairId);
    m_leftSerial->setText(leftSerial);
    m_rightSerial->setText(rightSerial);
}

QString ArsTrackerAssignPairDialog::pairId() const
{
    return m_pairId->text().trimmed();
}

QString ArsTrackerAssignPairDialog::leftTrackerSerial() const
{
    return m_leftSerial->text().trimmed();
}

QString ArsTrackerAssignPairDialog::rightTrackerSerial() const
{
    return m_rightSerial->text().trimmed();
}

void ArsTrackerAssignPairDialog::onPairChanged(const QString &value)
{
    const QString pair = value.trimmed();
    if (pair.isEmpty()) return;
    if (m_autoFillSerials || m_leftSerial->text().trimmed().isEmpty())
    {
        m_leftSerial->setText(pair + "L");
    }
    if (m_autoFillSerials || m_rightSerial->text().trimmed().isEmpty())
    {
        m_rightSerial->setText(pair + "R");
    }
    m_autoFillSerials = false;
}

void ArsTrackerAssignPairDialog::onAcceptClicked()
{
    if (pairId().isEmpty())
    {
        QMessageBox::warning(this, "Assign tracker pair", "pairId is required.");
        return;
    }
    if (leftTrackerSerial().isEmpty())
    {
        m_leftSerial->setText(pairId() + "L");
    }
    if (rightTrackerSerial().isEmpty())
    {
        m_rightSerial->setText(pairId() + "R");
    }
    accept();
}
