#include "ars_team_targets_dialog.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QLabel>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "ars/workspace/ArsTargetsByPosition.h"

ArsTeamTargetsDialog::ArsTeamTargetsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Edit targets by position");
    resize(1100, 420);

    QVBoxLayout *root = new QVBoxLayout(this);
    root->addWidget(new QLabel("Targets by position", this));

    QTableWidget *table = new QTableWidget(this);
    table->setColumnCount(9);
    table->setHorizontalHeaderLabels(QStringList()
                                     << "Position"
                                     << "Distance, km"
                                     << "Acceleration distance, m"
                                     << "Footload, 10^3g"
                                     << "Touches"
                                     << "Intensity, footload/min"
                                     << "Max speed, m/s"
                                     << "Shots"
                                     << "Possessions");
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    for (int i = 1; i < 9; ++i) table->horizontalHeader()->setSectionResizeMode(i, QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setRowCount(arsTargetPositionKeys().size());

    int row = 0;
    for (const QString &key : arsTargetPositionKeys())
    {
        table->setItem(row, 0, new QTableWidgetItem(arsTargetPositionLabel(key)));
        RowWidgets widgets;
        widgets.distanceKm = new QDoubleSpinBox(table);
        widgets.distanceKm->setRange(0.0, 1000.0);
        widgets.distanceKm->setDecimals(3);

        widgets.accelDistanceM = new QDoubleSpinBox(table);
        widgets.accelDistanceM->setRange(0.0, 1000000.0);
        widgets.accelDistanceM->setDecimals(1);

        widgets.footload = new QDoubleSpinBox(table);
        widgets.footload->setRange(0.0, 1000000.0);
        widgets.footload->setDecimals(1);

        widgets.touches = new QSpinBox(table);
        widgets.touches->setRange(0, 1000000);

        widgets.intensity = new QDoubleSpinBox(table);
        widgets.intensity->setRange(0.0, 100000.0);
        widgets.intensity->setDecimals(2);

        widgets.maxSpeed = new QDoubleSpinBox(table);
        widgets.maxSpeed->setRange(0.0, 100.0);
        widgets.maxSpeed->setDecimals(2);

        widgets.shots = new QSpinBox(table);
        widgets.shots->setRange(0, 1000000);

        widgets.possessions = new QSpinBox(table);
        widgets.possessions->setRange(0, 1000000);

        table->setCellWidget(row, 1, widgets.distanceKm);
        table->setCellWidget(row, 2, widgets.accelDistanceM);
        table->setCellWidget(row, 3, widgets.footload);
        table->setCellWidget(row, 4, widgets.touches);
        table->setCellWidget(row, 5, widgets.intensity);
        table->setCellWidget(row, 6, widgets.maxSpeed);
        table->setCellWidget(row, 7, widgets.shots);
        table->setCellWidget(row, 8, widgets.possessions);

        m_rows.insert(key, widgets);
        ++row;
    }

    root->addWidget(table, 1);
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    root->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void ArsTeamTargetsDialog::setTargetsByPosition(const QMap<QString, ArsPlannedMetrics> &targets)
{
    for (const QString &key : arsTargetPositionKeys())
    {
        const ArsPlannedMetrics m = targets.value(key, ArsPlannedMetrics{});
        const RowWidgets widgets = m_rows.value(key);
        widgets.distanceKm->setValue(m.distanceKm);
        widgets.accelDistanceM->setValue(m.accelerationDistanceM);
        widgets.footload->setValue(m.footloadPerLeg);
        widgets.touches->setValue(m.touches);
        widgets.intensity->setValue(m.loadIntensityGPerMin);
        widgets.maxSpeed->setValue(m.maxSpeedMps);
        widgets.shots->setValue(m.shots);
        widgets.possessions->setValue(m.dribbles);
    }
}

QMap<QString, ArsPlannedMetrics> ArsTeamTargetsDialog::targetsByPosition() const
{
    QMap<QString, ArsPlannedMetrics> out;
    for (const QString &key : arsTargetPositionKeys())
    {
        const RowWidgets widgets = m_rows.value(key);
        ArsPlannedMetrics m;
        m.distanceKm = widgets.distanceKm->value();
        m.accelerationDistanceM = static_cast<int>(widgets.accelDistanceM->value());
        m.footloadPerLeg = static_cast<int>(widgets.footload->value());
        m.touches = widgets.touches->value();
        m.loadIntensityGPerMin = widgets.intensity->value();
        m.maxSpeedMps = widgets.maxSpeed->value();
        m.shots = widgets.shots->value();
        m.dribbles = widgets.possessions->value();
        out.insert(key, m);
    }
    return out;
}
