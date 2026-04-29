/******************************************************************************
** Copyright (C) 2021-2025 Jamie M.
**
** Project: AuTerm
**
** Module:  plugin_mcumgr.cpp
**
** Notes:   Conversion of a conversion of an unfinished multi transport
**          testing application
**
** License: This program is free software: you can redistribute it and/or
**          modify it under the terms of the GNU General Public License as
**          published by the Free Software Foundation, version 3.
**
**          This program is distributed in the hope that it will be useful,
**          but WITHOUT ANY WARRANTY; without even the implied warranty of
**          MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**          GNU General Public License for more details.
**
**          You should have received a copy of the GNU General Public License
**          along with this program.  If not, see http://www.gnu.org/licenses/
**
*******************************************************************************/
#include <QFileDialog>
#include <QStandardItemModel>
#include <QRegularExpression>
#include <QClipboard>
#include <QTimeZone>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTimer>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QFileInfo>
#include "plugin_mcumgr.h"
#include "ars_tracker_parser.h"

static const uint16_t timeout_erase_ms = 14000;
static const uint32_t timeout_ars_tracker_metadata_ms = 60000;
static const uint8_t retries_ars_tracker_metadata = 0;
static const uint32_t ars_tracker_port_scan_open_delay_ms = 100;
static const uint32_t timeout_ars_tracker_port_scan_ms = 2000;
static const uint8_t retries_ars_tracker_port_scan = 0;

static QString ars_tracker_scan_status_to_string(group_status status)
{
		switch (status)
		{
		case STATUS_COMPLETE:
				return "complete";
		case STATUS_ERROR:
				return "error";
		case STATUS_TIMEOUT:
				return "timeout";
		case STATUS_CANCELLED:
				return "cancelled";
		case STATUS_PROCESSOR_TRANSPORT_ERROR:
				return "processor_transport_error";
		default:
				return QString("unknown(%1)").arg(int(status));
		}
}

struct ars_tracker_parsed_status_t
{
		int code = -1;
		QString name = "Unknown";
		QString color = "808080";
};

static ars_tracker_parsed_status_t parse_ars_tracker_status_text(const QString &raw_status)
{
		ars_tracker_parsed_status_t parsed_status;
		QString cleaned_status = raw_status.trimmed();
		if (cleaned_status.isEmpty() || cleaned_status.compare("Not loaded", Qt::CaseInsensitive) == 0 ||
				cleaned_status.compare("N/A", Qt::CaseInsensitive) == 0 ||
				cleaned_status.startsWith("Loading", Qt::CaseInsensitive) ||
				cleaned_status.startsWith("Error", Qt::CaseInsensitive))
		{
				return parsed_status;
		}
		QString first_token = cleaned_status.section(',', 0, 0).trimmed();
		bool conversion_ok = false;
		int code = first_token.toInt(&conversion_ok);

		if (conversion_ok == false)
		{
				qWarning() << "ArsTracker status parse failed for raw value" << raw_status;
				return parsed_status;
		}

		parsed_status.code = code;
		switch (code)
		{
		case 0:
				parsed_status.name = "Init";
				parsed_status.color = "808080";
				break;
		case 1:
				parsed_status.name = "Ready";
				parsed_status.color = "1976D2";
				break;
		case 2:
				parsed_status.name = "Active";
				parsed_status.color = "2E7D32";
				break;
		case 3:
				parsed_status.name = "Error";
				parsed_status.color = "D32F2F";
				break;
		default:
				qWarning() << "ArsTracker status code is unknown for raw value" << raw_status
											<< "code" << code;
				break;
		}

		return parsed_status;
}

enum tree_img_slot_info_columns
{
		TREE_IMG_SLOT_INFO_COLUMN_IMAGE_SLOT,
		TREE_IMG_SLOT_INFO_COLUMN_SIZE,
		TREE_IMG_SLOT_INFO_COLUMN_UPLOAD_ID,
};

static QMainWindow *parent_window;

void plugin_mcumgr::setup(QMainWindow *main_window)
{
		parent_window = main_window;

		// Initialise transports
		uart_transport = new smp_uart_auterm(this);

#if defined(PLUGIN_MCUMGR_TRANSPORT_UDP)
		udp_transport = new smp_udp(this);
#endif

#if defined(PLUGIN_MCUMGR_TRANSPORT_BLUETOOTH)
		bluetooth_transport = new smp_bluetooth(this);
#endif

#if defined(PLUGIN_MCUMGR_TRANSPORT_LORAWAN)
		lora_transport = new smp_lorawan(this);
#endif

		// Initialise SMP-related objects
		log_json                 = new smp_json(this);
		processor                = new smp_processor(this);
		smp_groups.fs_mgmt       = new smp_group_fs_mgmt(processor);
		smp_groups.img_mgmt      = new smp_group_img_mgmt(processor);
		smp_groups.os_mgmt       = new smp_group_os_mgmt(processor);
		smp_groups.settings_mgmt = new smp_group_settings_mgmt(processor);
		smp_groups.shell_mgmt    = new smp_group_shell_mgmt(processor);
		smp_groups.stat_mgmt     = new smp_group_stat_mgmt(processor);
		smp_groups.zephyr_mgmt   = new smp_group_zephyr_mgmt(processor);
		smp_groups.enum_mgmt     = new smp_group_enum_mgmt(processor);
		ars_tracker              = new ars_tracker_backend(this);
		error_lookup_form        = new error_lookup(parent_window, &smp_groups);

		processor->set_json(log_json);
		connect(log_json, SIGNAL(log(bool, QString*)), this, SLOT(custom_log(bool, QString*)));

#ifndef SKIPPLUGIN_LOGGER
		logger = new debug_logger(this);
#endif

		// Set defaults
		mode                  = ACTION_IDLE;
		uart_transport_locked = false;
		parent_row            = -1;
		parent_column         = -1;
		child_row             = -1;
		child_column          = -1;
		ars_tracker_shell_rc   = 0;
		ars_tracker_shell_command_rc = 0;
		ars_tracker_port_scan_shell_rc = 0;
		ars_tracker_info_loading = false;
		ars_tracker_loading    = false;
		ars_tracker_delete_loading = false;
		ars_tracker_export_loading = false;
		ars_tracker_firmware_upload_active = false;
		ars_tracker_firmware_erase_active = false;
		ars_tracker_shell_command_active = false;
		ars_tracker_firmware_refresh_after_erase_pending = false;
		ars_tracker_port_scan_active = false;
		ars_tracker_serial_transition_active = false;
		ars_tracker_auto_info_refresh_pending = false;
		ars_tracker_info_refresh_started_for_current_connection = false;
		ars_tracker_auto_info_refresh_in_progress = false;
		ars_tracker_auto_info_refresh_attempts = 0;
		ars_tracker_clear_selection_on_next_refresh = false;
		ars_tracker_export_fs_active = false;
		ars_tracker_export_fs_phase = ARS_TRACKER_EXPORT_FS_IDLE;
		ars_tracker_export_fs_sequence = 0;
		ars_tracker_export_fs_size_response = 0;
		ars_tracker_scan_serial_port = nullptr;
		ars_tracker_scan_transport = nullptr;
		ars_tracker_log_monitor_transport = new smp_uart_auterm(this);
		ars_tracker_scan_processor = nullptr;
		ars_tracker_scan_shell_mgmt = nullptr;
		ars_tracker_scan_port_index = 0;
		ars_tracker_scan_main_serial_open = false;
		ars_tracker_scan_probe_active = false;
		ars_tracker_scan_command_started = false;
		initialize_ars_tracker_scan_probe_context();

		QTabWidget* tabWidget_orig = parent_window->findChild<QTabWidget*>("selector_Tab");
		selector_tab_root = tabWidget_orig;
		//    QPushButton *other = main_window->findChild<QPushButton *>("btn_TermClose");

		/// AUTOGEN_START_INIT
		//    gridLayout = new QGridLayout(Form);
		//    gridLayout->setSpacing(2);
		//    gridLayout->setObjectName("gridLayout");
		//    gridLayout->setContentsMargins(6, 6, 6, 6);
		//    tabWidget = new QTabWidget(Form);
		//    tabWidget->setObjectName("tabWidget");
		tab = new QWidget(tabWidget_orig);
		tab->setObjectName("tab");
		verticalLayout_2 = new QVBoxLayout(tab);
		verticalLayout_2->setSpacing(2);
		verticalLayout_2->setObjectName("verticalLayout_2");
		verticalLayout_2->setContentsMargins(3, 3, 3, 3);
		horizontalLayout_7 = new QHBoxLayout();
		horizontalLayout_7->setSpacing(2);
		horizontalLayout_7->setObjectName("horizontalLayout_7");
		label = new QLabel(tab);
		label->setObjectName("label");
		label->setMaximumSize(QSize(16777215, 16777215));

		horizontalLayout_7->addWidget(label);

		edit_MTU = new QSpinBox(tab);
		edit_MTU->setObjectName("edit_MTU");
		edit_MTU->setMinimumSize(QSize(50, 0));
		edit_MTU->setMinimum(32);
		edit_MTU->setMaximum(8192);
		edit_MTU->setValue(256);

		horizontalLayout_7->addWidget(edit_MTU);

		line_9 = new QFrame(tab);
		line_9->setObjectName("line_9");
		line_9->setFrameShape(QFrame::Shape::VLine);
		line_9->setFrameShadow(QFrame::Shadow::Sunken);

		horizontalLayout_7->addWidget(line_9);

		check_V2_Protocol = new QCheckBox(tab);
		check_V2_Protocol->setObjectName("check_V2_Protocol");
		check_V2_Protocol->setChecked(true);

		horizontalLayout_7->addWidget(check_V2_Protocol);

		line_8 = new QFrame(tab);
		line_8->setObjectName("line_8");
		line_8->setFrameShape(QFrame::Shape::VLine);
		line_8->setFrameShadow(QFrame::Shadow::Sunken);

		horizontalLayout_7->addWidget(line_8);

		radio_transport_uart = new QRadioButton(tab);
		buttonGroup_3        = new QButtonGroup(tab);
		buttonGroup_3->setObjectName("buttonGroup_3");
		buttonGroup_3->addButton(radio_transport_uart);
		radio_transport_uart->setObjectName("radio_transport_uart");
		radio_transport_uart->setChecked(true);

		horizontalLayout_7->addWidget(radio_transport_uart);

		radio_transport_udp = new QRadioButton(tab);
		buttonGroup_3->addButton(radio_transport_udp);
		radio_transport_udp->setObjectName("radio_transport_udp");
		radio_transport_udp->setAutoExclusive(true);

		horizontalLayout_7->addWidget(radio_transport_udp);

		radio_transport_bluetooth = new QRadioButton(tab);
		buttonGroup_3->addButton(radio_transport_bluetooth);
		radio_transport_bluetooth->setObjectName("radio_transport_bluetooth");
		radio_transport_bluetooth->setAutoExclusive(true);

		horizontalLayout_7->addWidget(radio_transport_bluetooth);

		radio_transport_lora = new QRadioButton(tab);
		buttonGroup_3->addButton(radio_transport_lora);
		radio_transport_lora->setObjectName("radio_transport_lora");
		radio_transport_lora->setAutoExclusive(true);

		horizontalLayout_7->addWidget(radio_transport_lora);

		btn_transport_connect = new QPushButton(tab);
		btn_transport_connect->setObjectName("btn_transport_connect");

		horizontalLayout_7->addWidget(btn_transport_connect);

		btn_error_lookup = new QPushButton(tab);
		btn_error_lookup->setObjectName("btn_error_lookup");

		horizontalLayout_7->addWidget(btn_error_lookup);

		horizontalSpacer_6 =
				new QSpacerItem(20, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_7->addItem(horizontalSpacer_6);

		verticalLayout_2->addLayout(horizontalLayout_7);

		horizontalLayout_27 = new QHBoxLayout();
		horizontalLayout_27->setSpacing(2);
		horizontalLayout_27->setObjectName("horizontalLayout_27");
		horizontalLayout_27->setContentsMargins(-1, 0, -1, -1);
		horizontalSpacer_27 =
				new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_27->addItem(horizontalSpacer_27);

		btn_cancel = new QPushButton(tab);
		btn_cancel->setObjectName("btn_cancel");
		btn_cancel->setEnabled(false);
		btn_cancel->setMaximumSize(QSize(66, 16777215));

		horizontalLayout_27->addWidget(btn_cancel);

		verticalLayout_2->addLayout(horizontalLayout_27);

		selector_group = new QTabWidget(tab);
		selector_group->setObjectName("selector_group");
		selector_group->setTabPosition(QTabWidget::TabPosition::West);
		tab_IMG = new QWidget();
		tab_IMG->setObjectName("tab_IMG");
		gridLayout_3 = new QGridLayout(tab_IMG);
		gridLayout_3->setSpacing(2);
		gridLayout_3->setObjectName("gridLayout_3");
		gridLayout_3->setContentsMargins(6, 6, 6, 6);
		selector_img = new QTabWidget(tab_IMG);
		selector_img->setObjectName("selector_img");
		tab_IMG_Upload = new QWidget();
		tab_IMG_Upload->setObjectName("tab_IMG_Upload");
		gridLayout_4 = new QGridLayout(tab_IMG_Upload);
		gridLayout_4->setSpacing(2);
		gridLayout_4->setObjectName("gridLayout_4");
		gridLayout_4->setContentsMargins(6, 6, 6, 6);
		label_6 = new QLabel(tab_IMG_Upload);
		label_6->setObjectName("label_6");

		gridLayout_4->addWidget(label_6, 3, 0, 1, 1);

		check_IMG_Reset = new QCheckBox(tab_IMG_Upload);
		check_IMG_Reset->setObjectName("check_IMG_Reset");
		check_IMG_Reset->setChecked(false);

		gridLayout_4->addWidget(check_IMG_Reset, 2, 1, 1, 1);

		progress_IMG_Complete = new QProgressBar(tab_IMG_Upload);
		progress_IMG_Complete->setObjectName("progress_IMG_Complete");
		progress_IMG_Complete->setValue(0);

		gridLayout_4->addWidget(progress_IMG_Complete, 3, 1, 1, 1);

		label_9 = new QLabel(tab_IMG_Upload);
		label_9->setObjectName("label_9");

		gridLayout_4->addWidget(label_9, 2, 0, 1, 1);

		label_4 = new QLabel(tab_IMG_Upload);
		label_4->setObjectName("label_4");

		gridLayout_4->addWidget(label_4, 1, 0, 1, 1);

		horizontalLayout_5 = new QHBoxLayout();
		horizontalLayout_5->setSpacing(2);
		horizontalLayout_5->setObjectName("horizontalLayout_5");
		edit_IMG_Local = new QLineEdit(tab_IMG_Upload);
		edit_IMG_Local->setObjectName("edit_IMG_Local");

		horizontalLayout_5->addWidget(edit_IMG_Local);

		btn_IMG_Local = new QToolButton(tab_IMG_Upload);
		btn_IMG_Local->setObjectName("btn_IMG_Local");

		horizontalLayout_5->addWidget(btn_IMG_Local);

		gridLayout_4->addLayout(horizontalLayout_5, 0, 1, 1, 1);

		label_43 = new QLabel(tab_IMG_Upload);
		label_43->setObjectName("label_43");

		gridLayout_4->addWidget(label_43, 0, 0, 1, 1);

		horizontalLayout_4 = new QHBoxLayout();
		horizontalLayout_4->setSpacing(2);
		horizontalLayout_4->setObjectName("horizontalLayout_4");
		edit_IMG_Image = new QSpinBox(tab_IMG_Upload);
		edit_IMG_Image->setObjectName("edit_IMG_Image");
		edit_IMG_Image->setMaximumSize(QSize(60, 16777215));

		horizontalLayout_4->addWidget(edit_IMG_Image);

		radio_IMG_No_Action = new QRadioButton(tab_IMG_Upload);
		radio_IMG_No_Action->setObjectName("radio_IMG_No_Action");

		horizontalLayout_4->addWidget(radio_IMG_No_Action);

		radio_IMG_Test = new QRadioButton(tab_IMG_Upload);
		radio_IMG_Test->setObjectName("radio_IMG_Test");
		radio_IMG_Test->setChecked(true);

		horizontalLayout_4->addWidget(radio_IMG_Test);

		radio_IMG_Confirm = new QRadioButton(tab_IMG_Upload);
		radio_IMG_Confirm->setObjectName("radio_IMG_Confirm");

		horizontalLayout_4->addWidget(radio_IMG_Confirm);

		gridLayout_4->addLayout(horizontalLayout_4, 1, 1, 1, 1);

		verticalSpacer_4 =
				new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

		gridLayout_4->addItem(verticalSpacer_4, 4, 0, 1, 1);

		selector_img->addTab(tab_IMG_Upload, QString());
		tab_IMG_Images = new QWidget();
		tab_IMG_Images->setObjectName("tab_IMG_Images");
		gridLayout_5 = new QGridLayout(tab_IMG_Images);
		gridLayout_5->setSpacing(2);
		gridLayout_5->setObjectName("gridLayout_5");
		gridLayout_5->setContentsMargins(6, 6, 6, 6);
		colview_IMG_Images = new QColumnView(tab_IMG_Images);
		colview_IMG_Images->setObjectName("colview_IMG_Images");

		gridLayout_5->addWidget(colview_IMG_Images, 0, 0, 1, 1);

		horizontalLayout_6 = new QHBoxLayout();
		horizontalLayout_6->setSpacing(2);
		horizontalLayout_6->setObjectName("horizontalLayout_6");
		label_5 = new QLabel(tab_IMG_Images);
		label_5->setObjectName("label_5");

		horizontalLayout_6->addWidget(label_5);

		radio_IMG_Get = new QRadioButton(tab_IMG_Images);
		radio_IMG_Get->setObjectName("radio_IMG_Get");
		radio_IMG_Get->setChecked(true);

		horizontalLayout_6->addWidget(radio_IMG_Get);

		radio_IMG_Set = new QRadioButton(tab_IMG_Images);
		radio_IMG_Set->setObjectName("radio_IMG_Set");

		horizontalLayout_6->addWidget(radio_IMG_Set);

		radio_img_images_erase = new QRadioButton(tab_IMG_Images);
		radio_img_images_erase->setObjectName("radio_img_images_erase");
		radio_img_images_erase->setEnabled(false);
		radio_img_images_erase->setCheckable(true);

		horizontalLayout_6->addWidget(radio_img_images_erase);

		line = new QFrame(tab_IMG_Images);
		line->setObjectName("line");
		line->setFrameShape(QFrame::Shape::VLine);
		line->setFrameShadow(QFrame::Shadow::Sunken);

		horizontalLayout_6->addWidget(line);

		check_IMG_Confirm = new QCheckBox(tab_IMG_Images);
		check_IMG_Confirm->setObjectName("check_IMG_Confirm");
		check_IMG_Confirm->setEnabled(false);

		horizontalLayout_6->addWidget(check_IMG_Confirm);

		horizontalSpacer_5 =
				new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_6->addItem(horizontalSpacer_5);

		gridLayout_5->addLayout(horizontalLayout_6, 1, 0, 1, 1);

		selector_img->addTab(tab_IMG_Images, QString());
		tab_IMG_Erase = new QWidget();
		tab_IMG_Erase->setObjectName("tab_IMG_Erase");
		gridLayout_10 = new QGridLayout(tab_IMG_Erase);
		gridLayout_10->setObjectName("gridLayout_10");
		label_14 = new QLabel(tab_IMG_Erase);
		label_14->setObjectName("label_14");

		gridLayout_10->addWidget(label_14, 0, 0, 1, 1);

		horizontalSpacer_9 =
				new QSpacerItem(235, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		gridLayout_10->addItem(horizontalSpacer_9, 0, 2, 1, 1);

		edit_IMG_Erase_Slot = new QSpinBox(tab_IMG_Erase);
		edit_IMG_Erase_Slot->setObjectName("edit_IMG_Erase_Slot");
		edit_IMG_Erase_Slot->setMinimumSize(QSize(40, 0));
		edit_IMG_Erase_Slot->setMaximumSize(QSize(16777215, 16777215));

		gridLayout_10->addWidget(edit_IMG_Erase_Slot, 0, 1, 1, 1);

		verticalSpacer_2 =
				new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

		gridLayout_10->addItem(verticalSpacer_2, 1, 0, 1, 1);

		selector_img->addTab(tab_IMG_Erase, QString());
		tab_IMG_Slots = new QWidget();
		tab_IMG_Slots->setObjectName("tab_IMG_Slots");
		verticalLayout_6 = new QVBoxLayout(tab_IMG_Slots);
		verticalLayout_6->setSpacing(2);
		verticalLayout_6->setObjectName("verticalLayout_6");
		verticalLayout_6->setContentsMargins(6, 6, 6, 6);
		tree_IMG_Slot_Info                 = new QTreeWidget(tab_IMG_Slots);
		QTreeWidgetItem* __qtreewidgetitem = new QTreeWidgetItem();
		__qtreewidgetitem->setText(0, QString::fromUtf8("Images/Slots"));
		tree_IMG_Slot_Info->setHeaderItem(__qtreewidgetitem);
		tree_IMG_Slot_Info->setObjectName("tree_IMG_Slot_Info");
		tree_IMG_Slot_Info->setEditTriggers(QAbstractItemView::EditTrigger::NoEditTriggers);
		tree_IMG_Slot_Info->setItemsExpandable(true);
		tree_IMG_Slot_Info->setSortingEnabled(true);
		tree_IMG_Slot_Info->setHeaderHidden(false);
		tree_IMG_Slot_Info->header()->setVisible(true);

		verticalLayout_6->addWidget(tree_IMG_Slot_Info);

		selector_img->addTab(tab_IMG_Slots, QString());

		gridLayout_3->addWidget(selector_img, 0, 0, 1, 1);

		horizontalLayout_3 = new QHBoxLayout();
		horizontalLayout_3->setSpacing(2);
		horizontalLayout_3->setObjectName("horizontalLayout_3");
		horizontalLayout_3->setContentsMargins(-1, -1, -1, 0);
		horizontalSpacer_3 =
				new QSpacerItem(20, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_3->addItem(horizontalSpacer_3);

		btn_IMG_Go = new QPushButton(tab_IMG);
		btn_IMG_Go->setObjectName("btn_IMG_Go");

		horizontalLayout_3->addWidget(btn_IMG_Go);

		horizontalSpacer_4 =
				new QSpacerItem(20, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_3->addItem(horizontalSpacer_4);

		gridLayout_3->addLayout(horizontalLayout_3, 5, 0, 1, 3);

		lbl_IMG_Status = new QLabel(tab_IMG);
		lbl_IMG_Status->setObjectName("lbl_IMG_Status");

		gridLayout_3->addWidget(lbl_IMG_Status, 3, 0, 1, 2);

		selector_group->addTab(tab_IMG, QString());
		tab_FS = new QWidget();
		tab_FS->setObjectName("tab_FS");
		gridLayout_2 = new QGridLayout(tab_FS);
		gridLayout_2->setSpacing(2);
		gridLayout_2->setObjectName("gridLayout_2");
		gridLayout_2->setContentsMargins(6, 6, 6, 6);
		label_28 = new QLabel(tab_FS);
		label_28->setObjectName("label_28");

		gridLayout_2->addWidget(label_28, 3, 0, 1, 1);

		lbl_FS_Status = new QLabel(tab_FS);
		lbl_FS_Status->setObjectName("lbl_FS_Status");

		gridLayout_2->addWidget(lbl_FS_Status, 8, 0, 1, 2);

		label_29 = new QLabel(tab_FS);
		label_29->setObjectName("label_29");

		gridLayout_2->addWidget(label_29, 4, 0, 1, 1);

		label_2 = new QLabel(tab_FS);
		label_2->setObjectName("label_2");

		gridLayout_2->addWidget(label_2, 0, 0, 1, 1);

		progress_FS_Complete = new QProgressBar(tab_FS);
		progress_FS_Complete->setObjectName("progress_FS_Complete");
		progress_FS_Complete->setValue(0);

		gridLayout_2->addWidget(progress_FS_Complete, 6, 0, 1, 3);

		btn_FS_Local = new QToolButton(tab_FS);
		btn_FS_Local->setObjectName("btn_FS_Local");

		gridLayout_2->addWidget(btn_FS_Local, 0, 2, 1, 1);

		label_3 = new QLabel(tab_FS);
		label_3->setObjectName("label_3");

		gridLayout_2->addWidget(label_3, 1, 0, 1, 1);

		combo_FS_type = new QComboBox(tab_FS);
		combo_FS_type->setObjectName("combo_FS_type");
		combo_FS_type->setEnabled(false);
		combo_FS_type->setEditable(true);

		gridLayout_2->addWidget(combo_FS_type, 2, 1, 1, 2);

		horizontalLayout = new QHBoxLayout();
		horizontalLayout->setSpacing(2);
		horizontalLayout->setObjectName("horizontalLayout");
		radio_FS_Upload = new QRadioButton(tab_FS);
		radio_FS_Upload->setObjectName("radio_FS_Upload");
		radio_FS_Upload->setChecked(true);

		horizontalLayout->addWidget(radio_FS_Upload);

		radio_FS_Download = new QRadioButton(tab_FS);
		radio_FS_Download->setObjectName("radio_FS_Download");

		horizontalLayout->addWidget(radio_FS_Download);

		radio_FS_Size = new QRadioButton(tab_FS);
		radio_FS_Size->setObjectName("radio_FS_Size");

		horizontalLayout->addWidget(radio_FS_Size);

		radio_FS_HashChecksum = new QRadioButton(tab_FS);
		radio_FS_HashChecksum->setObjectName("radio_FS_HashChecksum");

		horizontalLayout->addWidget(radio_FS_HashChecksum);

		radio_FS_Hash_Checksum_Types = new QRadioButton(tab_FS);
		radio_FS_Hash_Checksum_Types->setObjectName("radio_FS_Hash_Checksum_Types");

		horizontalLayout->addWidget(radio_FS_Hash_Checksum_Types);

		gridLayout_2->addLayout(horizontalLayout, 5, 0, 1, 3);

		label_19 = new QLabel(tab_FS);
		label_19->setObjectName("label_19");

		gridLayout_2->addWidget(label_19, 2, 0, 1, 1);

		edit_FS_Remote = new QLineEdit(tab_FS);
		edit_FS_Remote->setObjectName("edit_FS_Remote");

		gridLayout_2->addWidget(edit_FS_Remote, 1, 1, 1, 2);

		horizontalLayout_2 = new QHBoxLayout();
		horizontalLayout_2->setSpacing(2);
		horizontalLayout_2->setObjectName("horizontalLayout_2");
		horizontalLayout_2->setContentsMargins(-1, -1, -1, 0);
		horizontalSpacer =
				new QSpacerItem(20, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_2->addItem(horizontalSpacer);

		btn_FS_Go = new QPushButton(tab_FS);
		btn_FS_Go->setObjectName("btn_FS_Go");

		horizontalLayout_2->addWidget(btn_FS_Go);

		horizontalSpacer_2 =
				new QSpacerItem(20, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_2->addItem(horizontalSpacer_2);

		gridLayout_2->addLayout(horizontalLayout_2, 9, 0, 1, 3);

		verticalSpacer_6 =
				new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

		gridLayout_2->addItem(verticalSpacer_6, 7, 0, 1, 1);

		edit_FS_Local = new QLineEdit(tab_FS);
		edit_FS_Local->setObjectName("edit_FS_Local");

		gridLayout_2->addWidget(edit_FS_Local, 0, 1, 1, 1);

		edit_FS_Result = new QLineEdit(tab_FS);
		edit_FS_Result->setObjectName("edit_FS_Result");
		edit_FS_Result->setEnabled(false);
		edit_FS_Result->setReadOnly(true);

		gridLayout_2->addWidget(edit_FS_Result, 3, 1, 1, 2);

		edit_FS_Size = new QLineEdit(tab_FS);
		edit_FS_Size->setObjectName("edit_FS_Size");
		edit_FS_Size->setEnabled(false);
		edit_FS_Size->setMaximumSize(QSize(80, 16777215));
		edit_FS_Size->setReadOnly(true);

		gridLayout_2->addWidget(edit_FS_Size, 4, 1, 1, 1);

		selector_group->addTab(tab_FS, QString());
		tab_OS = new QWidget();
		tab_OS->setObjectName("tab_OS");
		gridLayout_7 = new QGridLayout(tab_OS);
		gridLayout_7->setSpacing(2);
		gridLayout_7->setObjectName("gridLayout_7");
		gridLayout_7->setContentsMargins(6, 6, 6, 6);
		horizontalLayout_13 = new QHBoxLayout();
		horizontalLayout_13->setSpacing(2);
		horizontalLayout_13->setObjectName("horizontalLayout_13");
		horizontalLayout_13->setContentsMargins(-1, -1, -1, 0);
		horizontalSpacer_17 =
				new QSpacerItem(20, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_13->addItem(horizontalSpacer_17);

		btn_OS_Go = new QPushButton(tab_OS);
		btn_OS_Go->setObjectName("btn_OS_Go");

		horizontalLayout_13->addWidget(btn_OS_Go);

		horizontalSpacer_18 =
				new QSpacerItem(20, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_13->addItem(horizontalSpacer_18);

		gridLayout_7->addLayout(horizontalLayout_13, 2, 0, 1, 1);

		lbl_OS_Status = new QLabel(tab_OS);
		lbl_OS_Status->setObjectName("lbl_OS_Status");

		gridLayout_7->addWidget(lbl_OS_Status, 1, 0, 1, 1);

		selector_OS = new QTabWidget(tab_OS);
		selector_OS->setObjectName("selector_OS");
		tab_OS_Echo = new QWidget();
		tab_OS_Echo->setObjectName("tab_OS_Echo");
		gridLayout_8 = new QGridLayout(tab_OS_Echo);
		gridLayout_8->setObjectName("gridLayout_8");
		label_10 = new QLabel(tab_OS_Echo);
		label_10->setObjectName("label_10");

		gridLayout_8->addWidget(label_10, 0, 0, 1, 1);

		edit_OS_Echo_Input = new QPlainTextEdit(tab_OS_Echo);
		edit_OS_Echo_Input->setObjectName("edit_OS_Echo_Input");

		gridLayout_8->addWidget(edit_OS_Echo_Input, 0, 1, 1, 1);

		label_11 = new QLabel(tab_OS_Echo);
		label_11->setObjectName("label_11");

		gridLayout_8->addWidget(label_11, 1, 0, 1, 1);

		edit_OS_Echo_Output = new QPlainTextEdit(tab_OS_Echo);
		edit_OS_Echo_Output->setObjectName("edit_OS_Echo_Output");
		edit_OS_Echo_Output->setUndoRedoEnabled(false);
		edit_OS_Echo_Output->setReadOnly(true);

		gridLayout_8->addWidget(edit_OS_Echo_Output, 1, 1, 1, 1);

		selector_OS->addTab(tab_OS_Echo, QString());
		tab_OS_Tasks = new QWidget();
		tab_OS_Tasks->setObjectName("tab_OS_Tasks");
		gridLayout_14 = new QGridLayout(tab_OS_Tasks);
		gridLayout_14->setObjectName("gridLayout_14");
		table_OS_Tasks = new QTableWidget(tab_OS_Tasks);
		if (table_OS_Tasks->columnCount() < 8)
				table_OS_Tasks->setColumnCount(8);
		QTableWidgetItem* __qtablewidgetitem = new QTableWidgetItem();
		table_OS_Tasks->setHorizontalHeaderItem(0, __qtablewidgetitem);
		QTableWidgetItem* __qtablewidgetitem1 = new QTableWidgetItem();
		table_OS_Tasks->setHorizontalHeaderItem(1, __qtablewidgetitem1);
		QTableWidgetItem* __qtablewidgetitem2 = new QTableWidgetItem();
		table_OS_Tasks->setHorizontalHeaderItem(2, __qtablewidgetitem2);
		QTableWidgetItem* __qtablewidgetitem3 = new QTableWidgetItem();
		table_OS_Tasks->setHorizontalHeaderItem(3, __qtablewidgetitem3);
		QTableWidgetItem* __qtablewidgetitem4 = new QTableWidgetItem();
		table_OS_Tasks->setHorizontalHeaderItem(4, __qtablewidgetitem4);
		QTableWidgetItem* __qtablewidgetitem5 = new QTableWidgetItem();
		table_OS_Tasks->setHorizontalHeaderItem(5, __qtablewidgetitem5);
		QTableWidgetItem* __qtablewidgetitem6 = new QTableWidgetItem();
		table_OS_Tasks->setHorizontalHeaderItem(6, __qtablewidgetitem6);
		QTableWidgetItem* __qtablewidgetitem7 = new QTableWidgetItem();
		table_OS_Tasks->setHorizontalHeaderItem(7, __qtablewidgetitem7);
		table_OS_Tasks->setObjectName("table_OS_Tasks");
		table_OS_Tasks->setEditTriggers(QAbstractItemView::EditTrigger::NoEditTriggers);
		table_OS_Tasks->setProperty("showDropIndicator", QVariant(false));
		table_OS_Tasks->setDragDropOverwriteMode(false);
		table_OS_Tasks->setAlternatingRowColors(true);
		table_OS_Tasks->setSelectionMode(QAbstractItemView::SelectionMode::NoSelection);
		table_OS_Tasks->setSortingEnabled(true);
		table_OS_Tasks->setCornerButtonEnabled(false);

		gridLayout_14->addWidget(table_OS_Tasks, 0, 0, 1, 1);

		selector_OS->addTab(tab_OS_Tasks, QString());
		tab_OS_Memory = new QWidget();
		tab_OS_Memory->setObjectName("tab_OS_Memory");
		verticalLayout_4 = new QVBoxLayout(tab_OS_Memory);
		verticalLayout_4->setObjectName("verticalLayout_4");
		table_OS_Memory = new QTableWidget(tab_OS_Memory);
		if (table_OS_Memory->columnCount() < 4)
				table_OS_Memory->setColumnCount(4);
		QTableWidgetItem* __qtablewidgetitem8 = new QTableWidgetItem();
		table_OS_Memory->setHorizontalHeaderItem(0, __qtablewidgetitem8);
		QTableWidgetItem* __qtablewidgetitem9 = new QTableWidgetItem();
		table_OS_Memory->setHorizontalHeaderItem(1, __qtablewidgetitem9);
		QTableWidgetItem* __qtablewidgetitem10 = new QTableWidgetItem();
		table_OS_Memory->setHorizontalHeaderItem(2, __qtablewidgetitem10);
		QTableWidgetItem* __qtablewidgetitem11 = new QTableWidgetItem();
		table_OS_Memory->setHorizontalHeaderItem(3, __qtablewidgetitem11);
		table_OS_Memory->setObjectName("table_OS_Memory");
		table_OS_Memory->setEditTriggers(QAbstractItemView::EditTrigger::NoEditTriggers);
		table_OS_Memory->setProperty("showDropIndicator", QVariant(false));
		table_OS_Memory->setDragDropOverwriteMode(false);
		table_OS_Memory->setAlternatingRowColors(true);
		table_OS_Memory->setSelectionMode(QAbstractItemView::SelectionMode::NoSelection);
		table_OS_Memory->setSortingEnabled(true);
		table_OS_Memory->setCornerButtonEnabled(false);

		verticalLayout_4->addWidget(table_OS_Memory);

		selector_OS->addTab(tab_OS_Memory, QString());
		tab_OS_Reset = new QWidget();
		tab_OS_Reset->setObjectName("tab_OS_Reset");
		gridLayout_12 = new QGridLayout(tab_OS_Reset);
		gridLayout_12->setObjectName("gridLayout_12");
		label_44 = new QLabel(tab_OS_Reset);
		label_44->setObjectName("label_44");

		gridLayout_12->addWidget(label_44, 1, 0, 1, 1);

		edit_os_boot_mode = new QSpinBox(tab_OS_Reset);
		edit_os_boot_mode->setObjectName("edit_os_boot_mode");
		edit_os_boot_mode->setMaximum(255);

		gridLayout_12->addWidget(edit_os_boot_mode, 1, 1, 1, 1);

		verticalSpacer_3 =
				new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

		gridLayout_12->addItem(verticalSpacer_3, 2, 0, 1, 1);

		horizontalSpacer_29 =
				new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		gridLayout_12->addItem(horizontalSpacer_29, 1, 2, 1, 1);

		check_OS_Force_Reboot = new QCheckBox(tab_OS_Reset);
		check_OS_Force_Reboot->setObjectName("check_OS_Force_Reboot");

		gridLayout_12->addWidget(check_OS_Force_Reboot, 0, 0, 1, 3);

		selector_OS->addTab(tab_OS_Reset, QString());
		tab_os_datetime = new QWidget();
		tab_os_datetime->setObjectName("tab_os_datetime");
		gridLayout_18 = new QGridLayout(tab_os_datetime);
		gridLayout_18->setSpacing(2);
		gridLayout_18->setObjectName("gridLayout_18");
		gridLayout_18->setContentsMargins(6, 6, 6, 6);
		verticalSpacer_8 =
				new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

		gridLayout_18->addItem(verticalSpacer_8, 5, 1, 1, 1);

		combo_os_datetime_timezone = new QComboBox(tab_os_datetime);
		combo_os_datetime_timezone->setObjectName("combo_os_datetime_timezone");
		combo_os_datetime_timezone->setEnabled(false);

		gridLayout_18->addWidget(combo_os_datetime_timezone, 1, 1, 1, 1);

		horizontalLayout_19 = new QHBoxLayout();
		horizontalLayout_19->setSpacing(2);
		horizontalLayout_19->setObjectName("horizontalLayout_19");
		horizontalLayout_19->setContentsMargins(-1, 0, -1, -1);
		radio_os_datetime_get = new QRadioButton(tab_os_datetime);
		radio_os_datetime_get->setObjectName("radio_os_datetime_get");
		radio_os_datetime_get->setChecked(true);

		horizontalLayout_19->addWidget(radio_os_datetime_get);

		radio_os_datetime_set = new QRadioButton(tab_os_datetime);
		radio_os_datetime_set->setObjectName("radio_os_datetime_set");

		horizontalLayout_19->addWidget(radio_os_datetime_set);

		horizontalSpacer_15 =
				new QSpacerItem(40, 20, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

		horizontalLayout_19->addItem(horizontalSpacer_15);

		gridLayout_18->addLayout(horizontalLayout_19, 3, 0, 1, 2);

		label_13 = new QLabel(tab_os_datetime);
		label_13->setObjectName("label_13");
		label_13->setMaximumSize(QSize(80, 16777215));

		gridLayout_18->addWidget(label_13, 0, 0, 1, 1);

		label_31 = new QLabel(tab_os_datetime);
		label_31->setObjectName("label_31");
		label_31->setMaximumSize(QSize(100, 16777215));

		gridLayout_18->addWidget(label_31, 2, 0, 1, 1);

		edit_os_datetime_date_time = new QDateTimeEdit(tab_os_datetime);
		edit_os_datetime_date_time->setObjectName("edit_os_datetime_date_time");
		edit_os_datetime_date_time->setMinimumSize(QSize(250, 0));
		edit_os_datetime_date_time->setMaximumSize(QSize(300, 16777215));
		edit_os_datetime_date_time->setReadOnly(true);
		edit_os_datetime_date_time->setAccelerated(true);
		edit_os_datetime_date_time->setProperty("showGroupSeparator", QVariant(true));
		edit_os_datetime_date_time->setMinimumDate(QDate(1970, 1, 1));
		edit_os_datetime_date_time->setCurrentSection(QDateTimeEdit::Section::YearSection);
		edit_os_datetime_date_time->setCalendarPopup(true);
		edit_os_datetime_date_time->setTimeSpec(Qt::TimeSpec::LocalTime);

		gridLayout_18->addWidget(edit_os_datetime_date_time, 0, 1, 1, 1);

		label_30 = new QLabel(tab_os_datetime);
		label_30->setObjectName("label_30");
		label_30->setMaximumSize(QSize(100, 16777215));

		gridLayout_18->addWidget(label_30, 1, 0, 1, 1);

		check_os_datetime_use_pc_date_time = new QCheckBox(tab_os_datetime);
		check_os_datetime_use_pc_date_time->setObjectName("check_os_datetime_use_pc_date_time");
		check_os_datetime_use_pc_date_time->setEnabled(false);
		check_os_datetime_use_pc_date_time->setChecked(true);

		gridLayout_18->addWidget(check_os_datetime_use_pc_date_time, 2, 1, 1, 1);

		selector_OS->addTab(tab_os_datetime, QString());
		tab_OS_Info = new QWidget();
		tab_OS_Info->setObjectName("tab_OS_Info");
		gridLayout_13 = new QGridLayout(tab_OS_Info);
		gridLayout_13->setSpacing(2);
		gridLayout_13->setObjectName("gridLayout_13");
		label_17 = new QLabel(tab_OS_Info);
		label_17->setObjectName("label_17");

		gridLayout_13->addWidget(label_17, 0, 0, 1, 1);

		edit_OS_UName = new QLineEdit(tab_OS_Info);
		edit_OS_UName->setObjectName("edit_OS_UName");
		edit_OS_UName->setEnabled(false);
		edit_OS_UName->setReadOnly(false);

		gridLayout_13->addWidget(edit_OS_UName, 0, 1, 1, 1);

		horizontalLayout_10 = new QHBoxLayout();
		horizontalLayout_10->setSpacing(2);
		horizontalLayout_10->setObjectName("horizontalLayout_10");
		radio_OS_Buffer_Info = new QRadioButton(tab_OS_Info);
		radio_OS_Buffer_Info->setObjectName("radio_OS_Buffer_Info");
		radio_OS_Buffer_Info->setChecked(true);

		horizontalLayout_10->addWidget(radio_OS_Buffer_Info);

		radio_OS_uname = new QRadioButton(tab_OS_Info);
		radio_OS_uname->setObjectName("radio_OS_uname");

		horizontalLayout_10->addWidget(radio_OS_uname);

		gridLayout_13->addLayout(horizontalLayout_10, 1, 0, 1, 2);

		label_18 = new QLabel(tab_OS_Info);
		label_18->setObjectName("label_18");

		gridLayout_13->addWidget(label_18, 2, 0, 1, 1);

		edit_OS_Info_Output = new QPlainTextEdit(tab_OS_Info);
		edit_OS_Info_Output->setObjectName("edit_OS_Info_Output");
		edit_OS_Info_Output->setUndoRedoEnabled(false);
		edit_OS_Info_Output->setReadOnly(true);

		gridLayout_13->addWidget(edit_OS_Info_Output, 2, 1, 1, 1);

		selector_OS->addTab(tab_OS_Info, QString());
		tab_OS_Bootloader = new QWidget();
		tab_OS_Bootloader->setObjectName("tab_OS_Bootloader");
		formLayout_2 = new QFormLayout(tab_OS_Bootloader);
		formLayout_2->setObjectName("formLayout_2");
		formLayout_2->setFormAlignment(Qt::AlignmentFlag::AlignLeading | Qt::AlignmentFlag::AlignLeft |
																	 Qt::AlignmentFlag::AlignTop);
		formLayout_2->setHorizontalSpacing(2);
		formLayout_2->setVerticalSpacing(2);
		formLayout_2->setContentsMargins(6, 6, 6, 6);
		label_20 = new QLabel(tab_OS_Bootloader);
		label_20->setObjectName("label_20");

		formLayout_2->setWidget(0, QFormLayout::ItemRole::LabelRole, label_20);

		edit_os_bootloader_query = new QLineEdit(tab_OS_Bootloader);
		edit_os_bootloader_query->setObjectName("edit_os_bootloader_query");

		formLayout_2->setWidget(0, QFormLayout::ItemRole::FieldRole, edit_os_bootloader_query);

		label_21 = new QLabel(tab_OS_Bootloader);
		label_21->setObjectName("label_21");

		formLayout_2->setWidget(1, QFormLayout::ItemRole::LabelRole, label_21);

		edit_os_bootloader_response = new QLineEdit(tab_OS_Bootloader);
		edit_os_bootloader_response->setObjectName("edit_os_bootloader_response");
		edit_os_bootloader_response->setReadOnly(true);

		formLayout_2->setWidget(1, QFormLayout::ItemRole::FieldRole, edit_os_bootloader_response);

		selector_OS->addTab(tab_OS_Bootloader, QString());

		gridLayout_7->addWidget(selector_OS, 0, 0, 1, 1);

		selector_group->addTab(tab_OS, QString());
		tab_Stats = new QWidget();
		tab_Stats->setObjectName("tab_Stats");
		gridLayout_11 = new QGridLayout(tab_Stats);
		gridLayout_11->setSpacing(2);
		gridLayout_11->setObjectName("gridLayout_11");
		gridLayout_11->setContentsMargins(6, 6, 6, 6);
		lbl_STAT_Status = new QLabel(tab_Stats);
		lbl_STAT_Status->setObjectName("lbl_STAT_Status");

		gridLayout_11->addWidget(lbl_STAT_Status, 3, 0, 1, 2);

		horizontalLayout_9 = new QHBoxLayout();
		horizontalLayout_9->setSpacing(2);
		horizontalLayout_9->setObjectName("horizontalLayout_9");
		radio_STAT_List = new QRadioButton(tab_Stats);
		radio_STAT_List->setObjectName("radio_STAT_List");
		radio_STAT_List->setChecked(true);

		horizontalLayout_9->addWidget(radio_STAT_List);

		radio_STAT_Fetch = new QRadioButton(tab_Stats);
		radio_STAT_Fetch->setObjectName("radio_STAT_Fetch");

		horizontalLayout_9->addWidget(radio_STAT_Fetch);

		gridLayout_11->addLayout(horizontalLayout_9, 2, 0, 1, 2);

		combo_STAT_Group = new QComboBox(tab_Stats);
		combo_STAT_Group->setObjectName("combo_STAT_Group");
		combo_STAT_Group->setEditable(true);

		gridLayout_11->addWidget(combo_STAT_Group, 0, 1, 1, 1);

		horizontalLayout_14 = new QHBoxLayout();
		horizontalLayout_14->setSpacing(2);
		horizontalLayout_14->setObjectName("horizontalLayout_14");
		horizontalLayout_14->setContentsMargins(-1, -1, -1, 0);
		horizontalSpacer_19 =
				new QSpacerItem(20, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_14->addItem(horizontalSpacer_19);

		btn_STAT_Go = new QPushButton(tab_Stats);
		btn_STAT_Go->setObjectName("btn_STAT_Go");

		horizontalLayout_14->addWidget(btn_STAT_Go);

		horizontalSpacer_20 =
				new QSpacerItem(20, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_14->addItem(horizontalSpacer_20);

		gridLayout_11->addLayout(horizontalLayout_14, 4, 0, 1, 2);

		label_16 = new QLabel(tab_Stats);
		label_16->setObjectName("label_16");

		gridLayout_11->addWidget(label_16, 1, 0, 1, 1);

		label_15 = new QLabel(tab_Stats);
		label_15->setObjectName("label_15");

		gridLayout_11->addWidget(label_15, 0, 0, 1, 1);

		table_STAT_Values = new QTableWidget(tab_Stats);
		if (table_STAT_Values->columnCount() < 2)
				table_STAT_Values->setColumnCount(2);
		QTableWidgetItem* __qtablewidgetitem12 = new QTableWidgetItem();
		table_STAT_Values->setHorizontalHeaderItem(0, __qtablewidgetitem12);
		QTableWidgetItem* __qtablewidgetitem13 = new QTableWidgetItem();
		table_STAT_Values->setHorizontalHeaderItem(1, __qtablewidgetitem13);
		table_STAT_Values->setObjectName("table_STAT_Values");
		table_STAT_Values->setEditTriggers(QAbstractItemView::EditTrigger::NoEditTriggers);
		table_STAT_Values->setProperty("showDropIndicator", QVariant(false));
		table_STAT_Values->setDragDropOverwriteMode(false);
		table_STAT_Values->setAlternatingRowColors(true);
		table_STAT_Values->setSortingEnabled(true);
		table_STAT_Values->setCornerButtonEnabled(false);
		table_STAT_Values->horizontalHeader()->setCascadingSectionResizes(true);
		table_STAT_Values->horizontalHeader()->setDefaultSectionSize(180);

		gridLayout_11->addWidget(table_STAT_Values, 1, 1, 1, 1);

		selector_group->addTab(tab_Stats, QString());
		tab_Shell = new QWidget();
		tab_Shell->setObjectName("tab_Shell");
		gridLayout_9 = new QGridLayout(tab_Shell);
		gridLayout_9->setSpacing(2);
		gridLayout_9->setObjectName("gridLayout_9");
		gridLayout_9->setContentsMargins(6, 6, 6, 6);
		lbl_SHELL_Status = new QLabel(tab_Shell);
		lbl_SHELL_Status->setObjectName("lbl_SHELL_Status");

		gridLayout_9->addWidget(lbl_SHELL_Status, 3, 0, 1, 2);

		edit_SHELL_Output = new AutScrollEdit(tab_Shell);
		edit_SHELL_Output->setObjectName("edit_SHELL_Output");
		QPalette palette;
		QBrush   brush(QColor(255, 255, 255, 255));
		brush.setStyle(Qt::BrushStyle::SolidPattern);
		palette.setBrush(QPalette::ColorGroup::Active, QPalette::ColorRole::WindowText, brush);
		palette.setBrush(QPalette::ColorGroup::Active, QPalette::ColorRole::Text, brush);
		QBrush brush1(QColor(0, 0, 0, 255));
		brush1.setStyle(Qt::BrushStyle::SolidPattern);
		palette.setBrush(QPalette::ColorGroup::Active, QPalette::ColorRole::Base, brush1);
		QBrush brush2(QColor(255, 255, 255, 128));
		brush2.setStyle(Qt::BrushStyle::SolidPattern);
#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
		palette.setBrush(QPalette::ColorGroup::Active, QPalette::ColorRole::PlaceholderText, brush2);
#endif
		palette.setBrush(QPalette::ColorGroup::Inactive, QPalette::ColorRole::WindowText, brush);
		palette.setBrush(QPalette::ColorGroup::Inactive, QPalette::ColorRole::Text, brush);
		palette.setBrush(QPalette::ColorGroup::Inactive, QPalette::ColorRole::Base, brush1);
#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
		palette.setBrush(QPalette::ColorGroup::Inactive, QPalette::ColorRole::PlaceholderText, brush2);
#endif
		palette.setBrush(QPalette::ColorGroup::Disabled, QPalette::ColorRole::Base, brush1);
#if QT_VERSION >= QT_VERSION_CHECK(5, 12, 0)
		palette.setBrush(QPalette::ColorGroup::Disabled, QPalette::ColorRole::PlaceholderText, brush2);
#endif
		edit_SHELL_Output->setPalette(palette);
		edit_SHELL_Output->setUndoRedoEnabled(false);
		edit_SHELL_Output->setReadOnly(false);

		gridLayout_9->addWidget(edit_SHELL_Output, 1, 1, 1, 1);

		horizontalLayout_8 = new QHBoxLayout();
		horizontalLayout_8->setSpacing(2);
		horizontalLayout_8->setObjectName("horizontalLayout_8");
		horizontalLayout_8->setContentsMargins(-1, -1, -1, 0);
		horizontalSpacer_7 =
				new QSpacerItem(20, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_8->addItem(horizontalSpacer_7);

		btn_SHELL_Clear = new QToolButton(tab_Shell);
		btn_SHELL_Clear->setObjectName("btn_SHELL_Clear");

		horizontalLayout_8->addWidget(btn_SHELL_Clear);

		btn_SHELL_Copy = new QToolButton(tab_Shell);
		btn_SHELL_Copy->setObjectName("btn_SHELL_Copy");

		horizontalLayout_8->addWidget(btn_SHELL_Copy);

		horizontalSpacer_8 =
				new QSpacerItem(20, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_8->addItem(horizontalSpacer_8);

		gridLayout_9->addLayout(horizontalLayout_8, 4, 0, 1, 2);

		horizontalLayout_17 = new QHBoxLayout();
		horizontalLayout_17->setSpacing(2);
		horizontalLayout_17->setObjectName("horizontalLayout_17");
		horizontalLayout_17->setContentsMargins(-1, 0, -1, -1);
		check_shell_vt100_decoding = new QCheckBox(tab_Shell);
		check_shell_vt100_decoding->setObjectName("check_shell_vt100_decoding");
		check_shell_vt100_decoding->setEnabled(false);
		check_shell_vt100_decoding->setChecked(true);

		horizontalLayout_17->addWidget(check_shell_vt100_decoding);

		check_shel_unescape_strings = new QCheckBox(tab_Shell);
		check_shel_unescape_strings->setObjectName("check_shel_unescape_strings");
		check_shel_unescape_strings->setEnabled(false);

		horizontalLayout_17->addWidget(check_shel_unescape_strings);

		horizontalSpacer_12 =
				new QSpacerItem(20, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_17->addItem(horizontalSpacer_12);

		gridLayout_9->addLayout(horizontalLayout_17, 2, 0, 1, 2);

		selector_group->addTab(tab_Shell, QString());
		tab_Settings = new QWidget();
		tab_Settings->setObjectName("tab_Settings");
		gridLayout_15 = new QGridLayout(tab_Settings);
		gridLayout_15->setSpacing(2);
		gridLayout_15->setObjectName("gridLayout_15");
		gridLayout_15->setContentsMargins(6, 6, 6, 6);
		edit_settings_key = new QLineEdit(tab_Settings);
		edit_settings_key->setObjectName("edit_settings_key");

		gridLayout_15->addWidget(edit_settings_key, 0, 2, 1, 1);

		label_22 = new QLabel(tab_Settings);
		label_22->setObjectName("label_22");

		gridLayout_15->addWidget(label_22, 3, 0, 1, 1);

		lbl_settings_status = new QLabel(tab_Settings);
		lbl_settings_status->setObjectName("lbl_settings_status");

		gridLayout_15->addWidget(lbl_settings_status, 10, 0, 1, 3);

		verticalSpacer_5 =
				new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

		gridLayout_15->addItem(verticalSpacer_5, 9, 0, 1, 1);

		label_26 = new QLabel(tab_Settings);
		label_26->setObjectName("label_26");

		gridLayout_15->addWidget(label_26, 8, 0, 1, 1);

		horizontalLayout_16 = new QHBoxLayout();
		horizontalLayout_16->setSpacing(2);
		horizontalLayout_16->setObjectName("horizontalLayout_16");
		radio_settings_none = new QRadioButton(tab_Settings);
		buttonGroup_2       = new QButtonGroup(tab_Settings);
		buttonGroup_2->setObjectName("buttonGroup_2");
		buttonGroup_2->addButton(radio_settings_none);
		radio_settings_none->setObjectName("radio_settings_none");
		radio_settings_none->setChecked(true);

		horizontalLayout_16->addWidget(radio_settings_none);

		radio_settings_text = new QRadioButton(tab_Settings);
		buttonGroup_2->addButton(radio_settings_text);
		radio_settings_text->setObjectName("radio_settings_text");
		radio_settings_text->setChecked(false);

		horizontalLayout_16->addWidget(radio_settings_text);

		radio_settings_decimal = new QRadioButton(tab_Settings);
		buttonGroup_2->addButton(radio_settings_decimal);
		radio_settings_decimal->setObjectName("radio_settings_decimal");

		horizontalLayout_16->addWidget(radio_settings_decimal);

		gridLayout_15->addLayout(horizontalLayout_16, 6, 2, 1, 1);

		label_23 = new QLabel(tab_Settings);
		label_23->setObjectName("label_23");

		gridLayout_15->addWidget(label_23, 0, 0, 1, 1);

		label_24 = new QLabel(tab_Settings);
		label_24->setObjectName("label_24");

		gridLayout_15->addWidget(label_24, 1, 0, 1, 1);

		horizontalLayout_15 = new QHBoxLayout();
		horizontalLayout_15->setSpacing(2);
		horizontalLayout_15->setObjectName("horizontalLayout_15");
		horizontalSpacer_10 =
				new QSpacerItem(20, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_15->addItem(horizontalSpacer_10);

		btn_settings_go = new QPushButton(tab_Settings);
		btn_settings_go->setObjectName("btn_settings_go");

		horizontalLayout_15->addWidget(btn_settings_go);

		horizontalSpacer_11 =
				new QSpacerItem(20, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_15->addItem(horizontalSpacer_11);

		gridLayout_15->addLayout(horizontalLayout_15, 11, 0, 1, 3);

		label_25 = new QLabel(tab_Settings);
		label_25->setObjectName("label_25");

		gridLayout_15->addWidget(label_25, 6, 0, 1, 1);

		horizontalLayout_12 = new QHBoxLayout();
		horizontalLayout_12->setSpacing(2);
		horizontalLayout_12->setObjectName("horizontalLayout_12");
		check_settings_big_endian = new QCheckBox(tab_Settings);
		check_settings_big_endian->setObjectName("check_settings_big_endian");
		check_settings_big_endian->setEnabled(false);

		horizontalLayout_12->addWidget(check_settings_big_endian);

		check_settings_signed_decimal_value = new QCheckBox(tab_Settings);
		check_settings_signed_decimal_value->setObjectName("check_settings_signed_decimal_value");
		check_settings_signed_decimal_value->setEnabled(false);

		horizontalLayout_12->addWidget(check_settings_signed_decimal_value);

		gridLayout_15->addLayout(horizontalLayout_12, 7, 2, 1, 1);

		label_27 = new QLabel(tab_Settings);
		label_27->setObjectName("label_27");

		gridLayout_15->addWidget(label_27, 7, 0, 1, 1);

		horizontalLayout_11 = new QHBoxLayout();
		horizontalLayout_11->setSpacing(2);
		horizontalLayout_11->setObjectName("horizontalLayout_11");
		radio_settings_read = new QRadioButton(tab_Settings);
		buttonGroup         = new QButtonGroup(tab_Settings);
		buttonGroup->setObjectName("buttonGroup");
		buttonGroup->addButton(radio_settings_read);
		radio_settings_read->setObjectName("radio_settings_read");
		radio_settings_read->setChecked(true);

		horizontalLayout_11->addWidget(radio_settings_read);

		radio_settings_write = new QRadioButton(tab_Settings);
		buttonGroup->addButton(radio_settings_write);
		radio_settings_write->setObjectName("radio_settings_write");

		horizontalLayout_11->addWidget(radio_settings_write);

		radio_settings_delete = new QRadioButton(tab_Settings);
		buttonGroup->addButton(radio_settings_delete);
		radio_settings_delete->setObjectName("radio_settings_delete");

		horizontalLayout_11->addWidget(radio_settings_delete);

		radio_settings_commit = new QRadioButton(tab_Settings);
		buttonGroup->addButton(radio_settings_commit);
		radio_settings_commit->setObjectName("radio_settings_commit");

		horizontalLayout_11->addWidget(radio_settings_commit);

		radio_settings_load = new QRadioButton(tab_Settings);
		buttonGroup->addButton(radio_settings_load);
		radio_settings_load->setObjectName("radio_settings_load");

		horizontalLayout_11->addWidget(radio_settings_load);

		radio_settings_save = new QRadioButton(tab_Settings);
		buttonGroup->addButton(radio_settings_save);
		radio_settings_save->setObjectName("radio_settings_save");

		horizontalLayout_11->addWidget(radio_settings_save);

		gridLayout_15->addLayout(horizontalLayout_11, 3, 2, 1, 1);

		edit_settings_value = new QLineEdit(tab_Settings);
		edit_settings_value->setObjectName("edit_settings_value");
		edit_settings_value->setReadOnly(true);

		gridLayout_15->addWidget(edit_settings_value, 1, 2, 1, 1);

		edit_settings_decoded = new QLineEdit(tab_Settings);
		edit_settings_decoded->setObjectName("edit_settings_decoded");
		edit_settings_decoded->setEnabled(false);
		edit_settings_decoded->setReadOnly(true);

		gridLayout_15->addWidget(edit_settings_decoded, 8, 2, 1, 1);

		line_2 = new QFrame(tab_Settings);
		line_2->setObjectName("line_2");
		line_2->setLineWidth(1);
		line_2->setFrameShape(QFrame::Shape::VLine);
		line_2->setFrameShadow(QFrame::Shadow::Sunken);

		gridLayout_15->addWidget(line_2, 4, 0, 2, 3);

		selector_group->addTab(tab_Settings, QString());
		tab_zephyr = new QWidget();
		tab_zephyr->setObjectName("tab_zephyr");
		gridLayout_16 = new QGridLayout(tab_zephyr);
		gridLayout_16->setSpacing(2);
		gridLayout_16->setObjectName("gridLayout_16");
		gridLayout_16->setContentsMargins(6, 6, 6, 6);
		horizontalLayout_18 = new QHBoxLayout();
		horizontalLayout_18->setSpacing(2);
		horizontalLayout_18->setObjectName("horizontalLayout_18");
		horizontalSpacer_13 =
				new QSpacerItem(20, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_18->addItem(horizontalSpacer_13);

		btn_zephyr_go = new QPushButton(tab_zephyr);
		btn_zephyr_go->setObjectName("btn_zephyr_go");

		horizontalLayout_18->addWidget(btn_zephyr_go);

		horizontalSpacer_14 =
				new QSpacerItem(20, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_18->addItem(horizontalSpacer_14);

		gridLayout_16->addLayout(horizontalLayout_18, 2, 0, 1, 1);

		tabWidget_4 = new QTabWidget(tab_zephyr);
		tabWidget_4->setObjectName("tabWidget_4");
		tabWidget_4->setDocumentMode(false);
		tabWidget_4->setTabsClosable(false);
		tabWidget_4->setMovable(false);
		tabWidget_4->setTabBarAutoHide(false);
		tab_zephyr_storage_erase = new QWidget();
		tab_zephyr_storage_erase->setObjectName("tab_zephyr_storage_erase");
		gridLayout_17 = new QGridLayout(tab_zephyr_storage_erase);
		gridLayout_17->setObjectName("gridLayout_17");
		label_12 = new QLabel(tab_zephyr_storage_erase);
		label_12->setObjectName("label_12");
		label_12->setWordWrap(true);

		gridLayout_17->addWidget(label_12, 0, 0, 1, 1);

		verticalSpacer_7 =
				new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

		gridLayout_17->addItem(verticalSpacer_7, 1, 0, 1, 1);

		tabWidget_4->addTab(tab_zephyr_storage_erase, QString());

		gridLayout_16->addWidget(tabWidget_4, 0, 0, 1, 1);

		lbl_zephyr_status = new QLabel(tab_zephyr);
		lbl_zephyr_status->setObjectName("lbl_zephyr_status");

		gridLayout_16->addWidget(lbl_zephyr_status, 1, 0, 1, 1);

		selector_group->addTab(tab_zephyr, QString());
		tab_Enum = new QWidget();
		tab_Enum->setObjectName("tab_Enum");
		verticalLayout_3 = new QVBoxLayout(tab_Enum);
		verticalLayout_3->setSpacing(2);
		verticalLayout_3->setObjectName("verticalLayout_3");
		verticalLayout_3->setContentsMargins(6, 6, 6, 6);
		horizontalLayout_21 = new QHBoxLayout();
		horizontalLayout_21->setObjectName("horizontalLayout_21");
		radio_Enum_Count = new QRadioButton(tab_Enum);
		radio_Enum_Count->setObjectName("radio_Enum_Count");
		radio_Enum_Count->setChecked(true);

		horizontalLayout_21->addWidget(radio_Enum_Count);

		radio_Enum_List = new QRadioButton(tab_Enum);
		radio_Enum_List->setObjectName("radio_Enum_List");

		horizontalLayout_21->addWidget(radio_Enum_List);

		radio_Enum_Single = new QRadioButton(tab_Enum);
		radio_Enum_Single->setObjectName("radio_Enum_Single");

		horizontalLayout_21->addWidget(radio_Enum_Single);

		radio_Enum_Details = new QRadioButton(tab_Enum);
		radio_Enum_Details->setObjectName("radio_Enum_Details");

		horizontalLayout_21->addWidget(radio_Enum_Details);

		verticalLayout_3->addLayout(horizontalLayout_21);

		horizontalLayout_22 = new QHBoxLayout();
		horizontalLayout_22->setObjectName("horizontalLayout_22");
		label_32 = new QLabel(tab_Enum);
		label_32->setObjectName("label_32");

		horizontalLayout_22->addWidget(label_32);

		edit_Enum_Count = new QLineEdit(tab_Enum);
		edit_Enum_Count->setObjectName("edit_Enum_Count");
		edit_Enum_Count->setReadOnly(true);

		horizontalLayout_22->addWidget(edit_Enum_Count);

		verticalLayout_3->addLayout(horizontalLayout_22);

		horizontalLayout_23 = new QHBoxLayout();
		horizontalLayout_23->setObjectName("horizontalLayout_23");
		label_33 = new QLabel(tab_Enum);
		label_33->setObjectName("label_33");

		horizontalLayout_23->addWidget(label_33);

		edit_Enum_Index = new QSpinBox(tab_Enum);
		edit_Enum_Index->setObjectName("edit_Enum_Index");
		edit_Enum_Index->setMinimumSize(QSize(60, 0));

		horizontalLayout_23->addWidget(edit_Enum_Index);

		line_3 = new QFrame(tab_Enum);
		line_3->setObjectName("line_3");
		line_3->setFrameShape(QFrame::Shape::VLine);
		line_3->setFrameShadow(QFrame::Shadow::Sunken);

		horizontalLayout_23->addWidget(line_3);

		label_34 = new QLabel(tab_Enum);
		label_34->setObjectName("label_34");

		horizontalLayout_23->addWidget(label_34);

		edit_Enum_Group_ID = new QLineEdit(tab_Enum);
		edit_Enum_Group_ID->setObjectName("edit_Enum_Group_ID");
		edit_Enum_Group_ID->setMaxLength(6);
		edit_Enum_Group_ID->setReadOnly(true);

		horizontalLayout_23->addWidget(edit_Enum_Group_ID);

		check_Enum_Group_Additional = new QCheckBox(tab_Enum);
		check_Enum_Group_Additional->setObjectName("check_Enum_Group_Additional");
		check_Enum_Group_Additional->setEnabled(true);
		check_Enum_Group_Additional->setCheckable(true);
		check_Enum_Group_Additional->setChecked(false);

		horizontalLayout_23->addWidget(check_Enum_Group_Additional);

		horizontalSpacer_22 =
				new QSpacerItem(20, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_23->addItem(horizontalSpacer_22);

		verticalLayout_3->addLayout(horizontalLayout_23);

		table_Enum_List_Details = new QTableWidget(tab_Enum);
		if (table_Enum_List_Details->columnCount() < 3)
				table_Enum_List_Details->setColumnCount(3);
		QTableWidgetItem* __qtablewidgetitem14 = new QTableWidgetItem();
		table_Enum_List_Details->setHorizontalHeaderItem(0, __qtablewidgetitem14);
		QTableWidgetItem* __qtablewidgetitem15 = new QTableWidgetItem();
		table_Enum_List_Details->setHorizontalHeaderItem(1, __qtablewidgetitem15);
		QTableWidgetItem* __qtablewidgetitem16 = new QTableWidgetItem();
		table_Enum_List_Details->setHorizontalHeaderItem(2, __qtablewidgetitem16);
		table_Enum_List_Details->setObjectName("table_Enum_List_Details");
		table_Enum_List_Details->setEditTriggers(QAbstractItemView::EditTrigger::NoEditTriggers);
		table_Enum_List_Details->setDragDropOverwriteMode(false);
		table_Enum_List_Details->setAlternatingRowColors(true);
		table_Enum_List_Details->setSortingEnabled(true);
		table_Enum_List_Details->setWordWrap(false);

		verticalLayout_3->addWidget(table_Enum_List_Details);

		lbl_enum_status = new QLabel(tab_Enum);
		lbl_enum_status->setObjectName("lbl_enum_status");

		verticalLayout_3->addWidget(lbl_enum_status);

		horizontalLayout_20 = new QHBoxLayout();
		horizontalLayout_20->setSpacing(2);
		horizontalLayout_20->setObjectName("horizontalLayout_20");
		horizontalSpacer_16 =
				new QSpacerItem(20, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_20->addItem(horizontalSpacer_16);

		btn_enum_go = new QPushButton(tab_Enum);
		btn_enum_go->setObjectName("btn_enum_go");

		horizontalLayout_20->addWidget(btn_enum_go);

		horizontalSpacer_21 =
				new QSpacerItem(20, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_20->addItem(horizontalSpacer_21);

		verticalLayout_3->addLayout(horizontalLayout_20);

		selector_group->addTab(tab_Enum, QString());
		tab_custom = new QWidget();
		tab_custom->setObjectName("tab_custom");
		verticalLayout_5 = new QVBoxLayout(tab_custom);
		verticalLayout_5->setSpacing(2);
		verticalLayout_5->setObjectName("verticalLayout_5");
		verticalLayout_5->setContentsMargins(6, 6, 6, 6);
		formLayout_3 = new QFormLayout();
		formLayout_3->setObjectName("formLayout_3");
		formLayout_3->setHorizontalSpacing(2);
		formLayout_3->setVerticalSpacing(2);
		label_38 = new QLabel(tab_custom);
		label_38->setObjectName("label_38");

		formLayout_3->setWidget(0, QFormLayout::ItemRole::LabelRole, label_38);

		horizontalLayout_26 = new QHBoxLayout();
		horizontalLayout_26->setSpacing(2);
		horizontalLayout_26->setObjectName("horizontalLayout_26");
		radio_custom_custom = new QRadioButton(tab_custom);
		buttonGroup_4       = new QButtonGroup(tab_custom);
		buttonGroup_4->setObjectName("buttonGroup_4");
		buttonGroup_4->addButton(radio_custom_custom);
		radio_custom_custom->setObjectName("radio_custom_custom");
		radio_custom_custom->setChecked(true);

		horizontalLayout_26->addWidget(radio_custom_custom);

		radio_custom_logging = new QRadioButton(tab_custom);
		buttonGroup_4->addButton(radio_custom_logging);
		radio_custom_logging->setObjectName("radio_custom_logging");

		horizontalLayout_26->addWidget(radio_custom_logging);

		line_7 = new QFrame(tab_custom);
		line_7->setObjectName("line_7");
		line_7->setFrameShape(QFrame::Shape::VLine);
		line_7->setFrameShadow(QFrame::Shadow::Sunken);

		horizontalLayout_26->addWidget(line_7);

		label_39 = new QLabel(tab_custom);
		label_39->setObjectName("label_39");

		horizontalLayout_26->addWidget(label_39);

		radio_custom_json = new QRadioButton(tab_custom);
		buttonGroup_5     = new QButtonGroup(tab_custom);
		buttonGroup_5->setObjectName("buttonGroup_5");
		buttonGroup_5->addButton(radio_custom_json);
		radio_custom_json->setObjectName("radio_custom_json");
		radio_custom_json->setChecked(true);

		horizontalLayout_26->addWidget(radio_custom_json);

		radio_custom_yaml = new QRadioButton(tab_custom);
		buttonGroup_5->addButton(radio_custom_yaml);
		radio_custom_yaml->setObjectName("radio_custom_yaml");
		radio_custom_yaml->setEnabled(true);

		horizontalLayout_26->addWidget(radio_custom_yaml);

		radio_custom_cbor = new QRadioButton(tab_custom);
		buttonGroup_5->addButton(radio_custom_cbor);
		radio_custom_cbor->setObjectName("radio_custom_cbor");

		horizontalLayout_26->addWidget(radio_custom_cbor);

		horizontalSpacer_26 =
				new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_26->addItem(horizontalSpacer_26);

		formLayout_3->setLayout(0, QFormLayout::ItemRole::FieldRole, horizontalLayout_26);

		label_40 = new QLabel(tab_custom);
		label_40->setObjectName("label_40");

		formLayout_3->setWidget(1, QFormLayout::ItemRole::LabelRole, label_40);

		horizontalLayout_28 = new QHBoxLayout();
		horizontalLayout_28->setObjectName("horizontalLayout_28");
		radio_custom_read = new QRadioButton(tab_custom);
		buttonGroup_6     = new QButtonGroup(tab_custom);
		buttonGroup_6->setObjectName("buttonGroup_6");
		buttonGroup_6->addButton(radio_custom_read);
		radio_custom_read->setObjectName("radio_custom_read");
		radio_custom_read->setChecked(true);

		horizontalLayout_28->addWidget(radio_custom_read);

		radio_custom_write = new QRadioButton(tab_custom);
		buttonGroup_6->addButton(radio_custom_write);
		radio_custom_write->setObjectName("radio_custom_write");

		horizontalLayout_28->addWidget(radio_custom_write);

		line_5 = new QFrame(tab_custom);
		line_5->setObjectName("line_5");
		line_5->setFrameShape(QFrame::Shape::VLine);
		line_5->setFrameShadow(QFrame::Shadow::Sunken);

		horizontalLayout_28->addWidget(line_5);

		label_41 = new QLabel(tab_custom);
		label_41->setObjectName("label_41");

		horizontalLayout_28->addWidget(label_41);

		edit_custom_group = new QSpinBox(tab_custom);
		edit_custom_group->setObjectName("edit_custom_group");
		edit_custom_group->setMinimumSize(QSize(60, 0));
		edit_custom_group->setMaximumSize(QSize(16777215, 16777215));
		edit_custom_group->setMaximum(65535);

		horizontalLayout_28->addWidget(edit_custom_group);

		line_6 = new QFrame(tab_custom);
		line_6->setObjectName("line_6");
		line_6->setFrameShape(QFrame::Shape::VLine);
		line_6->setFrameShadow(QFrame::Shadow::Sunken);

		horizontalLayout_28->addWidget(line_6);

		label_42 = new QLabel(tab_custom);
		label_42->setObjectName("label_42");

		horizontalLayout_28->addWidget(label_42);

		edit_custom_command = new QSpinBox(tab_custom);
		edit_custom_command->setObjectName("edit_custom_command");
		edit_custom_command->setMinimumSize(QSize(50, 0));
		edit_custom_command->setMaximum(255);

		horizontalLayout_28->addWidget(edit_custom_command);

		horizontalSpacer_28 =
				new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_28->addItem(horizontalSpacer_28);

		formLayout_3->setLayout(1, QFormLayout::ItemRole::FieldRole, horizontalLayout_28);

		label_35 = new QLabel(tab_custom);
		label_35->setObjectName("label_35");

		formLayout_3->setWidget(2, QFormLayout::ItemRole::LabelRole, label_35);

		edit_custom_send = new QPlainTextEdit(tab_custom);
		edit_custom_send->setObjectName("edit_custom_send");
		edit_custom_send->setUndoRedoEnabled(false);
		edit_custom_send->setReadOnly(true);

		formLayout_3->setWidget(2, QFormLayout::ItemRole::FieldRole, edit_custom_send);

		label_36 = new QLabel(tab_custom);
		label_36->setObjectName("label_36");

		formLayout_3->setWidget(3, QFormLayout::ItemRole::LabelRole, label_36);

		edit_custom_receive = new QPlainTextEdit(tab_custom);
		edit_custom_receive->setObjectName("edit_custom_receive");
		edit_custom_receive->setUndoRedoEnabled(false);
		edit_custom_receive->setReadOnly(true);

		formLayout_3->setWidget(3, QFormLayout::ItemRole::FieldRole, edit_custom_receive);

		label_37 = new QLabel(tab_custom);
		label_37->setObjectName("label_37");

		formLayout_3->setWidget(4, QFormLayout::ItemRole::LabelRole, label_37);

		horizontalLayout_25 = new QHBoxLayout();
		horizontalLayout_25->setSpacing(2);
		horizontalLayout_25->setObjectName("horizontalLayout_25");
		edit_custom_indent = new QSpinBox(tab_custom);
		edit_custom_indent->setObjectName("edit_custom_indent");
		edit_custom_indent->setMinimumSize(QSize(40, 0));
		edit_custom_indent->setMinimum(1);
		edit_custom_indent->setMaximum(16);
		edit_custom_indent->setValue(4);

		horizontalLayout_25->addWidget(edit_custom_indent);

		line_4 = new QFrame(tab_custom);
		line_4->setObjectName("line_4");
		line_4->setFrameShape(QFrame::Shape::VLine);
		line_4->setFrameShadow(QFrame::Shadow::Sunken);

		horizontalLayout_25->addWidget(line_4);

		btn_custom_copy_send = new QPushButton(tab_custom);
		btn_custom_copy_send->setObjectName("btn_custom_copy_send");

		horizontalLayout_25->addWidget(btn_custom_copy_send);

		btn_custom_copy_receive = new QPushButton(tab_custom);
		btn_custom_copy_receive->setObjectName("btn_custom_copy_receive");

		horizontalLayout_25->addWidget(btn_custom_copy_receive);

		btn_custom_copy_both = new QPushButton(tab_custom);
		btn_custom_copy_both->setObjectName("btn_custom_copy_both");

		horizontalLayout_25->addWidget(btn_custom_copy_both);

		btn_custom_clear = new QPushButton(tab_custom);
		btn_custom_clear->setObjectName("btn_custom_clear");

		horizontalLayout_25->addWidget(btn_custom_clear);

		horizontalSpacer_25 =
				new QSpacerItem(40, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_25->addItem(horizontalSpacer_25);

		formLayout_3->setLayout(4, QFormLayout::ItemRole::FieldRole, horizontalLayout_25);

		verticalLayout_5->addLayout(formLayout_3);

		lbl_custom_status = new QLabel(tab_custom);
		lbl_custom_status->setObjectName("lbl_custom_status");

		verticalLayout_5->addWidget(lbl_custom_status);

		horizontalLayout_24 = new QHBoxLayout();
		horizontalLayout_24->setSpacing(2);
		horizontalLayout_24->setObjectName("horizontalLayout_24");
		horizontalSpacer_23 =
				new QSpacerItem(20, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_24->addItem(horizontalSpacer_23);

		btn_custom_go = new QPushButton(tab_custom);
		btn_custom_go->setObjectName("btn_custom_go");
		btn_custom_go->setEnabled(true);

		horizontalLayout_24->addWidget(btn_custom_go);

		horizontalSpacer_24 =
				new QSpacerItem(20, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_24->addItem(horizontalSpacer_24);

		verticalLayout_5->addLayout(horizontalLayout_24);

		selector_group->addTab(tab_custom, QString());

		tab_ars_tracker = new QWidget(tabWidget_orig);
		tab_ars_tracker->setObjectName("tab_ars_tracker");
		gridLayout_ars_tracker = new QGridLayout(tab_ars_tracker);
		gridLayout_ars_tracker->setSpacing(2);
		gridLayout_ars_tracker->setObjectName("gridLayout_ars_tracker");
		gridLayout_ars_tracker->setContentsMargins(6, 6, 6, 6);
		frame_ars_tracker_connection = new QFrame(tab_ars_tracker);
		frame_ars_tracker_connection->setObjectName("frame_ars_tracker_connection");
		frame_ars_tracker_connection->setFrameShape(QFrame::Shape::StyledPanel);
		gridLayout_ars_tracker_connection = new QGridLayout(frame_ars_tracker_connection);
		gridLayout_ars_tracker_connection->setSpacing(2);
		gridLayout_ars_tracker_connection->setObjectName("gridLayout_ars_tracker_connection");
		gridLayout_ars_tracker_connection->setContentsMargins(6, 6, 6, 6);

		label_ars_tracker_port = new QLabel(frame_ars_tracker_connection);
		label_ars_tracker_port->setObjectName("label_ars_tracker_port");

		gridLayout_ars_tracker_connection->addWidget(label_ars_tracker_port, 1, 0, 1, 1);

		combo_ars_tracker_port = new ars_tracker_port_combo_box(frame_ars_tracker_connection);
		combo_ars_tracker_port->setObjectName("combo_ars_tracker_port");

		gridLayout_ars_tracker_connection->addWidget(combo_ars_tracker_port, 1, 1, 1, 1);

		btn_ars_tracker_connect = new QPushButton(frame_ars_tracker_connection);
		btn_ars_tracker_connect->setObjectName("btn_ars_tracker_connect");

		gridLayout_ars_tracker_connection->addWidget(btn_ars_tracker_connect, 1, 2, 1, 2);

		gridLayout_ars_tracker->addWidget(frame_ars_tracker_connection, 0, 0, 1, 3);

		frame_ars_tracker_info = new QFrame(tab_ars_tracker);
		frame_ars_tracker_info->setObjectName("frame_ars_tracker_info");
		frame_ars_tracker_info->setFrameShape(QFrame::Shape::StyledPanel);
		gridLayout_ars_tracker_info = new QGridLayout(frame_ars_tracker_info);
		gridLayout_ars_tracker_info->setSpacing(2);
		gridLayout_ars_tracker_info->setObjectName("gridLayout_ars_tracker_info");
		gridLayout_ars_tracker_info->setContentsMargins(6, 6, 6, 6);
		label_ars_tracker_info_header = new QLabel(frame_ars_tracker_info);
		label_ars_tracker_info_header->setObjectName("label_ars_tracker_info_header");

		gridLayout_ars_tracker_info->addWidget(label_ars_tracker_info_header, 0, 0, 1, 3);

		btn_ars_tracker_info_refresh = new QPushButton(frame_ars_tracker_info);
		btn_ars_tracker_info_refresh->setObjectName("btn_ars_tracker_info_refresh");

		gridLayout_ars_tracker_info->addWidget(btn_ars_tracker_info_refresh, 0, 3, 1, 1);

		label_ars_tracker_serial_number = new QLabel(frame_ars_tracker_info);
		label_ars_tracker_serial_number->setObjectName("label_ars_tracker_serial_number");

		gridLayout_ars_tracker_info->addWidget(label_ars_tracker_serial_number, 1, 0, 1, 1);

		edit_ars_tracker_serial_number = new QLineEdit(frame_ars_tracker_info);
		edit_ars_tracker_serial_number->setObjectName("edit_ars_tracker_serial_number");
		edit_ars_tracker_serial_number->setReadOnly(true);

		gridLayout_ars_tracker_info->addWidget(edit_ars_tracker_serial_number, 1, 1, 1, 1);

		label_ars_tracker_board_id = new QLabel(frame_ars_tracker_info);
		label_ars_tracker_board_id->setObjectName("label_ars_tracker_board_id");

		gridLayout_ars_tracker_info->addWidget(label_ars_tracker_board_id, 2, 0, 1, 1);

		edit_ars_tracker_board_id = new QLineEdit(frame_ars_tracker_info);
		edit_ars_tracker_board_id->setObjectName("edit_ars_tracker_board_id");
		edit_ars_tracker_board_id->setReadOnly(true);

		gridLayout_ars_tracker_info->addWidget(edit_ars_tracker_board_id, 2, 1, 1, 1);

		label_ars_tracker_type = new QLabel(frame_ars_tracker_info);
		label_ars_tracker_type->setObjectName("label_ars_tracker_type");

		gridLayout_ars_tracker_info->addWidget(label_ars_tracker_type, 3, 0, 1, 1);

		edit_ars_tracker_type = new QLineEdit(frame_ars_tracker_info);
		edit_ars_tracker_type->setObjectName("edit_ars_tracker_type");
		edit_ars_tracker_type->setReadOnly(true);

		gridLayout_ars_tracker_info->addWidget(edit_ars_tracker_type, 3, 1, 1, 1);

		label_ars_tracker_status_value = new QLabel(frame_ars_tracker_info);
		label_ars_tracker_status_value->setObjectName("label_ars_tracker_status_value");

		gridLayout_ars_tracker_info->addWidget(label_ars_tracker_status_value, 4, 0, 1, 1,
																					 Qt::AlignTop | Qt::AlignLeft);

		widget_ars_tracker_status_value = new QWidget(frame_ars_tracker_info);
		widget_ars_tracker_status_value->setObjectName("widget_ars_tracker_status_value");
		verticalLayout_ars_tracker_status_value = new QVBoxLayout(widget_ars_tracker_status_value);
		verticalLayout_ars_tracker_status_value->setObjectName(
				"verticalLayout_ars_tracker_status_value");
		verticalLayout_ars_tracker_status_value->setContentsMargins(0, 0, 0, 0);
		verticalLayout_ars_tracker_status_value->setSpacing(2);

		edit_ars_tracker_status_value = new QLineEdit(widget_ars_tracker_status_value);
		edit_ars_tracker_status_value->setObjectName("edit_ars_tracker_status_value");
		edit_ars_tracker_status_value->setReadOnly(true);

		verticalLayout_ars_tracker_status_value->addWidget(edit_ars_tracker_status_value);

		label_ars_tracker_status_state = new QLabel(widget_ars_tracker_status_value);
		label_ars_tracker_status_state->setObjectName("label_ars_tracker_status_state");

		verticalLayout_ars_tracker_status_value->addWidget(label_ars_tracker_status_state, 0,
																							 Qt::AlignLeft | Qt::AlignTop);
		gridLayout_ars_tracker_info->addWidget(widget_ars_tracker_status_value, 4, 1, 1, 1,
																					 Qt::AlignTop);

		label_ars_tracker_battery_info = new QLabel(frame_ars_tracker_info);
		label_ars_tracker_battery_info->setObjectName("label_ars_tracker_battery_info");

		gridLayout_ars_tracker_info->addWidget(label_ars_tracker_battery_info, 1, 2, 1, 1);

		edit_ars_tracker_battery_info = new QLineEdit(frame_ars_tracker_info);
		edit_ars_tracker_battery_info->setObjectName("edit_ars_tracker_battery_info");
		edit_ars_tracker_battery_info->setReadOnly(true);

		gridLayout_ars_tracker_info->addWidget(edit_ars_tracker_battery_info, 1, 3, 1, 1);

		label_ars_tracker_memory_usage = new QLabel(frame_ars_tracker_info);
		label_ars_tracker_memory_usage->setObjectName("label_ars_tracker_memory_usage");

		gridLayout_ars_tracker_info->addWidget(label_ars_tracker_memory_usage, 2, 2, 1, 1);

		edit_ars_tracker_memory_usage = new QLineEdit(frame_ars_tracker_info);
		edit_ars_tracker_memory_usage->setObjectName("edit_ars_tracker_memory_usage");
		edit_ars_tracker_memory_usage->setReadOnly(true);

		gridLayout_ars_tracker_info->addWidget(edit_ars_tracker_memory_usage, 2, 3, 1, 1);

		label_ars_tracker_bad_blocks = new QLabel(frame_ars_tracker_info);
		label_ars_tracker_bad_blocks->setObjectName("label_ars_tracker_bad_blocks");

		gridLayout_ars_tracker_info->addWidget(label_ars_tracker_bad_blocks, 3, 2, 1, 1);

		edit_ars_tracker_bad_blocks = new QLineEdit(frame_ars_tracker_info);
		edit_ars_tracker_bad_blocks->setObjectName("edit_ars_tracker_bad_blocks");
		edit_ars_tracker_bad_blocks->setReadOnly(true);

		gridLayout_ars_tracker_info->addWidget(edit_ars_tracker_bad_blocks, 3, 3, 1, 1);

		widget_ars_tracker_firmware_section = new QWidget(frame_ars_tracker_info);
		widget_ars_tracker_firmware_section->setObjectName(
				"widget_ars_tracker_firmware_section");
		gridLayout_ars_tracker_firmware_section =
				new QGridLayout(widget_ars_tracker_firmware_section);
		gridLayout_ars_tracker_firmware_section->setObjectName(
				"gridLayout_ars_tracker_firmware_section");
		gridLayout_ars_tracker_firmware_section->setContentsMargins(0, 0, 0, 0);
		gridLayout_ars_tracker_firmware_section->setHorizontalSpacing(2);
		gridLayout_ars_tracker_firmware_section->setVerticalSpacing(2);

		label_ars_tracker_firmware_header = new QLabel(widget_ars_tracker_firmware_section);
		label_ars_tracker_firmware_header->setObjectName("label_ars_tracker_firmware_header");

		gridLayout_ars_tracker_firmware_section->addWidget(label_ars_tracker_firmware_header, 0, 0,
																							 1, 2, Qt::AlignTop | Qt::AlignLeft);

		label_ars_tracker_firmware_current_version =
				new QLabel(widget_ars_tracker_firmware_section);
		label_ars_tracker_firmware_current_version->setObjectName(
				"label_ars_tracker_firmware_current_version");

		gridLayout_ars_tracker_firmware_section->addWidget(
				label_ars_tracker_firmware_current_version, 1, 0, 1, 1);

		edit_ars_tracker_firmware_current_version =
				new QLineEdit(widget_ars_tracker_firmware_section);
		edit_ars_tracker_firmware_current_version->setObjectName(
				"edit_ars_tracker_firmware_current_version");
		edit_ars_tracker_firmware_current_version->setReadOnly(true);

		gridLayout_ars_tracker_firmware_section->addWidget(
				edit_ars_tracker_firmware_current_version, 1, 1, 1, 1);

		label_ars_tracker_firmware_second_slot = new QLabel(widget_ars_tracker_firmware_section);
		label_ars_tracker_firmware_second_slot->setObjectName(
				"label_ars_tracker_firmware_second_slot");

		gridLayout_ars_tracker_firmware_section->addWidget(label_ars_tracker_firmware_second_slot,
																							 2, 0, 1, 1);

		widget_ars_tracker_firmware_second_slot =
				new QWidget(widget_ars_tracker_firmware_section);
		widget_ars_tracker_firmware_second_slot->setObjectName(
				"widget_ars_tracker_firmware_second_slot");
		horizontalLayout_ars_tracker_firmware_second_slot =
				new QHBoxLayout(widget_ars_tracker_firmware_second_slot);
		horizontalLayout_ars_tracker_firmware_second_slot->setObjectName(
				"horizontalLayout_ars_tracker_firmware_second_slot");
		horizontalLayout_ars_tracker_firmware_second_slot->setContentsMargins(0, 0, 0, 0);
		horizontalLayout_ars_tracker_firmware_second_slot->setSpacing(2);

		edit_ars_tracker_firmware_second_slot =
				new QLineEdit(widget_ars_tracker_firmware_second_slot);
		edit_ars_tracker_firmware_second_slot->setObjectName(
				"edit_ars_tracker_firmware_second_slot");
		edit_ars_tracker_firmware_second_slot->setReadOnly(true);

		horizontalLayout_ars_tracker_firmware_second_slot->addWidget(
				edit_ars_tracker_firmware_second_slot);
		horizontalLayout_ars_tracker_firmware_second_slot->setStretch(0, 1);

		btn_ars_tracker_firmware_erase =
				new QPushButton(widget_ars_tracker_firmware_second_slot);
		btn_ars_tracker_firmware_erase->setObjectName("btn_ars_tracker_firmware_erase");
		btn_ars_tracker_firmware_erase->setEnabled(false);

		horizontalLayout_ars_tracker_firmware_second_slot->addWidget(
				btn_ars_tracker_firmware_erase);
		gridLayout_ars_tracker_firmware_section->addWidget(
				widget_ars_tracker_firmware_second_slot, 2, 1, 1, 1);

		label_ars_tracker_firmware_file = new QLabel(widget_ars_tracker_firmware_section);
		label_ars_tracker_firmware_file->setObjectName("label_ars_tracker_firmware_file");

		gridLayout_ars_tracker_firmware_section->addWidget(label_ars_tracker_firmware_file, 3, 0,
																							 1, 1);

		widget_ars_tracker_firmware_file = new QWidget(widget_ars_tracker_firmware_section);
		widget_ars_tracker_firmware_file->setObjectName("widget_ars_tracker_firmware_file");
		horizontalLayout_ars_tracker_firmware_file =
				new QHBoxLayout(widget_ars_tracker_firmware_file);
		horizontalLayout_ars_tracker_firmware_file->setObjectName(
				"horizontalLayout_ars_tracker_firmware_file");
		horizontalLayout_ars_tracker_firmware_file->setContentsMargins(0, 0, 0, 0);
		horizontalLayout_ars_tracker_firmware_file->setSpacing(2);

		edit_ars_tracker_firmware_file = new QLineEdit(widget_ars_tracker_firmware_file);
		edit_ars_tracker_firmware_file->setObjectName("edit_ars_tracker_firmware_file");
		edit_ars_tracker_firmware_file->setReadOnly(true);

		horizontalLayout_ars_tracker_firmware_file->addWidget(edit_ars_tracker_firmware_file);

		btn_ars_tracker_firmware_browse = new QToolButton(widget_ars_tracker_firmware_file);
		btn_ars_tracker_firmware_browse->setObjectName("btn_ars_tracker_firmware_browse");

		horizontalLayout_ars_tracker_firmware_file->addWidget(btn_ars_tracker_firmware_browse);

		gridLayout_ars_tracker_firmware_section->addWidget(widget_ars_tracker_firmware_file, 3, 1,
																							 1, 1);

		btn_ars_tracker_firmware_upload = new QPushButton(widget_ars_tracker_firmware_section);
		btn_ars_tracker_firmware_upload->setObjectName("btn_ars_tracker_firmware_upload");

		gridLayout_ars_tracker_firmware_section->addWidget(btn_ars_tracker_firmware_upload, 4, 1, 1,
																							 1, Qt::AlignLeft | Qt::AlignTop);
		gridLayout_ars_tracker_firmware_section->setColumnStretch(1, 1);
		gridLayout_ars_tracker_firmware_section->setRowStretch(5, 0);
		widget_ars_tracker_firmware_section->setSizePolicy(QSizePolicy::Policy::Preferred,
																									 QSizePolicy::Policy::Maximum);

		gridLayout_ars_tracker_info->setRowStretch(0, 0);
		gridLayout_ars_tracker_info->setRowStretch(1, 0);
		gridLayout_ars_tracker_info->setRowStretch(2, 0);
		gridLayout_ars_tracker_info->setRowStretch(3, 0);
		gridLayout_ars_tracker_info->setRowStretch(4, 0);
		gridLayout_ars_tracker_info->setRowStretch(5, 0);
		gridLayout_ars_tracker_info->setRowStretch(6, 0);
		gridLayout_ars_tracker_info->setRowStretch(7, 0);
		gridLayout_ars_tracker_info->setRowStretch(8, 0);
		gridLayout_ars_tracker_info->setRowStretch(9, 0);
		gridLayout_ars_tracker_info->setColumnStretch(1, 1);
		gridLayout_ars_tracker_info->setColumnStretch(3, 1);
		frame_ars_tracker_info->setSizePolicy(QSizePolicy::Policy::Preferred,
																					 QSizePolicy::Policy::Maximum);

		gridLayout_ars_tracker->addWidget(frame_ars_tracker_info, 1, 0, 1, 1, Qt::AlignTop);

		frame_ars_tracker_firmware = new QFrame(tab_ars_tracker);
		frame_ars_tracker_firmware->setObjectName("frame_ars_tracker_firmware");
		frame_ars_tracker_firmware->setFrameShape(QFrame::Shape::StyledPanel);
		frame_ars_tracker_firmware->setSizePolicy(QSizePolicy::Policy::Preferred,
																						 QSizePolicy::Policy::Maximum);
		gridLayout_ars_tracker_firmware = new QGridLayout(frame_ars_tracker_firmware);
		gridLayout_ars_tracker_firmware->setSpacing(2);
		gridLayout_ars_tracker_firmware->setObjectName("gridLayout_ars_tracker_firmware");
		gridLayout_ars_tracker_firmware->setContentsMargins(6, 6, 6, 6);
		gridLayout_ars_tracker_firmware->addWidget(widget_ars_tracker_firmware_section, 0, 0, 1, 1,
																						 Qt::AlignTop);
		gridLayout_ars_tracker_firmware->setColumnStretch(0, 1);

		gridLayout_ars_tracker->addWidget(frame_ars_tracker_firmware, 1, 1, 1, 1, Qt::AlignTop);

		frame_ars_tracker_sessions = new QFrame(tab_ars_tracker);
		frame_ars_tracker_sessions->setObjectName("frame_ars_tracker_sessions");
		frame_ars_tracker_sessions->setFrameShape(QFrame::Shape::StyledPanel);
		frame_ars_tracker_sessions->setSizePolicy(QSizePolicy::Policy::Preferred,
																						 QSizePolicy::Policy::Maximum);
		gridLayout_ars_tracker_sessions = new QGridLayout(frame_ars_tracker_sessions);
		gridLayout_ars_tracker_sessions->setSpacing(2);
		gridLayout_ars_tracker_sessions->setObjectName("gridLayout_ars_tracker_sessions");
		gridLayout_ars_tracker_sessions->setContentsMargins(6, 6, 6, 6);

		label_ars_tracker_sessions = new QLabel(frame_ars_tracker_sessions);
		label_ars_tracker_sessions->setObjectName("label_ars_tracker_sessions");

		gridLayout_ars_tracker_sessions->addWidget(label_ars_tracker_sessions, 0, 0, 1, 1);

		label_ars_tracker_destination = new QLabel(frame_ars_tracker_sessions);
		label_ars_tracker_destination->setObjectName("label_ars_tracker_destination");

		gridLayout_ars_tracker_sessions->addWidget(label_ars_tracker_destination, 0, 1, 1, 1);

		edit_ars_tracker_destination = new QLineEdit(frame_ars_tracker_sessions);
		edit_ars_tracker_destination->setObjectName("edit_ars_tracker_destination");

		gridLayout_ars_tracker_sessions->addWidget(edit_ars_tracker_destination, 0, 2, 1, 1);

		btn_ars_tracker_destination = new QToolButton(frame_ars_tracker_sessions);
		btn_ars_tracker_destination->setObjectName("btn_ars_tracker_destination");

		gridLayout_ars_tracker_sessions->addWidget(btn_ars_tracker_destination, 0, 3, 1, 1);

		list_ars_tracker_sessions = new QListWidget(frame_ars_tracker_sessions);
		list_ars_tracker_sessions->setObjectName("list_ars_tracker_sessions");
		list_ars_tracker_sessions->setSizePolicy(QSizePolicy::Policy::Expanding,
																						 QSizePolicy::Policy::Preferred);
		list_ars_tracker_sessions->setMinimumHeight(120);
		list_ars_tracker_sessions->setMaximumHeight(160);

		gridLayout_ars_tracker_sessions->addWidget(list_ars_tracker_sessions, 1, 0, 6, 1);

		label_ars_tracker_files = new QLabel(frame_ars_tracker_sessions);
		label_ars_tracker_files->setObjectName("label_ars_tracker_files");

		gridLayout_ars_tracker_sessions->addWidget(label_ars_tracker_files, 1, 1, 1, 1);

		list_ars_tracker_files = new QListWidget(frame_ars_tracker_sessions);
		list_ars_tracker_files->setObjectName("list_ars_tracker_files");
		list_ars_tracker_files->setSizePolicy(QSizePolicy::Policy::Expanding,
																					 QSizePolicy::Policy::Preferred);
		list_ars_tracker_files->setMinimumHeight(70);
		list_ars_tracker_files->setMaximumHeight(90);

		gridLayout_ars_tracker_sessions->addWidget(list_ars_tracker_files, 1, 2, 3, 2);

		horizontalLayout_ars_tracker_actions = new QHBoxLayout();
		horizontalLayout_ars_tracker_actions->setSpacing(2);
		horizontalLayout_ars_tracker_actions->setObjectName("horizontalLayout_ars_tracker_actions");
		horizontalSpacer_ars_tracker_actions =
				new QSpacerItem(20, 20, QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Minimum);

		horizontalLayout_ars_tracker_actions->addItem(horizontalSpacer_ars_tracker_actions);

		btn_ars_tracker_delete = new QPushButton(frame_ars_tracker_sessions);
		btn_ars_tracker_delete->setObjectName("btn_ars_tracker_delete");
		btn_ars_tracker_delete->setEnabled(false);

		horizontalLayout_ars_tracker_actions->addWidget(btn_ars_tracker_delete);

		btn_ars_tracker_download = new QPushButton(frame_ars_tracker_sessions);
		btn_ars_tracker_download->setObjectName("btn_ars_tracker_download");
		btn_ars_tracker_download->setEnabled(false);

		horizontalLayout_ars_tracker_actions->addWidget(btn_ars_tracker_download);

		btn_ars_tracker_cancel = new QPushButton(frame_ars_tracker_sessions);
		btn_ars_tracker_cancel->setObjectName("btn_ars_tracker_cancel");
		btn_ars_tracker_cancel->setEnabled(false);

		horizontalLayout_ars_tracker_actions->addWidget(btn_ars_tracker_cancel);

		gridLayout_ars_tracker_sessions->addLayout(horizontalLayout_ars_tracker_actions, 6, 1, 1, 3);
		gridLayout_ars_tracker->addWidget(frame_ars_tracker_sessions, 1, 2, 1, 1, Qt::AlignTop);

		group_ars_tracker_shell = new QGroupBox(tab_ars_tracker);
		group_ars_tracker_shell->setObjectName("group_ars_tracker_shell");
		gridLayout_ars_tracker_shell = new QGridLayout(group_ars_tracker_shell);
		gridLayout_ars_tracker_shell->setSpacing(2);
		gridLayout_ars_tracker_shell->setObjectName("gridLayout_ars_tracker_shell");
		gridLayout_ars_tracker_shell->setContentsMargins(6, 6, 6, 6);

		label_ars_tracker_shell_command = new QLabel(group_ars_tracker_shell);
		label_ars_tracker_shell_command->setObjectName("label_ars_tracker_shell_command");
		gridLayout_ars_tracker_shell->addWidget(label_ars_tracker_shell_command, 0, 0, 1, 1);

		edit_ars_tracker_shell_command = new QLineEdit(group_ars_tracker_shell);
		edit_ars_tracker_shell_command->setObjectName("edit_ars_tracker_shell_command");
		gridLayout_ars_tracker_shell->addWidget(edit_ars_tracker_shell_command, 0, 1, 1, 1);

		button_ars_tracker_shell_send = new QPushButton(group_ars_tracker_shell);
		button_ars_tracker_shell_send->setObjectName("button_ars_tracker_shell_send");
		button_ars_tracker_shell_send->setEnabled(false);
		gridLayout_ars_tracker_shell->addWidget(button_ars_tracker_shell_send, 0, 2, 1, 1);

		button_ars_tracker_shell_clear = new QToolButton(group_ars_tracker_shell);
		button_ars_tracker_shell_clear->setObjectName("button_ars_tracker_shell_clear");
		gridLayout_ars_tracker_shell->addWidget(button_ars_tracker_shell_clear, 0, 3, 1, 1);

		text_ars_tracker_shell_output = new AutScrollEdit(group_ars_tracker_shell);
		text_ars_tracker_shell_output->setObjectName("text_ars_tracker_shell_output");
		text_ars_tracker_shell_output->setPalette(palette);
		text_ars_tracker_shell_output->setUndoRedoEnabled(false);
		text_ars_tracker_shell_output->setReadOnly(true);
		text_ars_tracker_shell_output->setMinimumHeight(140);
		gridLayout_ars_tracker_shell->addWidget(text_ars_tracker_shell_output, 1, 0, 1, 4);
		gridLayout_ars_tracker_shell->setColumnStretch(1, 1);

		gridLayout_ars_tracker->addWidget(group_ars_tracker_shell, 2, 0, 1, 3);

		group_ars_tracker_device_logs = new QGroupBox(tab_ars_tracker);
		group_ars_tracker_device_logs->setObjectName("group_ars_tracker_device_logs");
		gridLayout_ars_tracker_device_logs = new QGridLayout(group_ars_tracker_device_logs);
		gridLayout_ars_tracker_device_logs->setSpacing(2);
		gridLayout_ars_tracker_device_logs->setObjectName("gridLayout_ars_tracker_device_logs");
		gridLayout_ars_tracker_device_logs->setContentsMargins(6, 6, 6, 6);

		button_ars_tracker_device_logs_clear = new QPushButton(group_ars_tracker_device_logs);
		button_ars_tracker_device_logs_clear->setObjectName("button_ars_tracker_device_logs_clear");
		gridLayout_ars_tracker_device_logs->addWidget(button_ars_tracker_device_logs_clear, 0, 0, 1, 1,
																							 Qt::AlignLeft);

		text_ars_tracker_device_logs = new AutScrollEdit(group_ars_tracker_device_logs);
		text_ars_tracker_device_logs->setObjectName("text_ars_tracker_device_logs");
		text_ars_tracker_device_logs->setPalette(palette);
		text_ars_tracker_device_logs->setUndoRedoEnabled(false);
		text_ars_tracker_device_logs->setReadOnly(true);
		text_ars_tracker_device_logs->setMinimumHeight(160);
		gridLayout_ars_tracker_device_logs->addWidget(text_ars_tracker_device_logs, 1, 0, 1, 1);
		gridLayout_ars_tracker_device_logs->setColumnStretch(0, 1);

		gridLayout_ars_tracker->addWidget(group_ars_tracker_device_logs, 3, 0, 1, 3);

		lbl_ars_tracker_progress = new QLabel(tab_ars_tracker);
		lbl_ars_tracker_progress->setObjectName("lbl_ars_tracker_progress");

		gridLayout_ars_tracker->addWidget(lbl_ars_tracker_progress, 5, 0, 1, 3);

		lbl_ars_tracker_status = new QLabel(tab_ars_tracker);
		lbl_ars_tracker_status->setObjectName("lbl_ars_tracker_status");

		gridLayout_ars_tracker->addWidget(lbl_ars_tracker_status, 6, 0, 1, 3);

		gridLayout_ars_tracker_sessions->setColumnStretch(0, 2);
		gridLayout_ars_tracker_sessions->setColumnStretch(2, 3);
		gridLayout_ars_tracker_sessions->setRowStretch(0, 0);
		gridLayout_ars_tracker_sessions->setRowStretch(1, 0);
		gridLayout_ars_tracker_sessions->setRowStretch(2, 0);
		gridLayout_ars_tracker_sessions->setRowStretch(3, 0);
		gridLayout_ars_tracker_sessions->setRowStretch(4, 0);
		gridLayout_ars_tracker_sessions->setRowStretch(5, 0);
		gridLayout_ars_tracker_sessions->setRowStretch(6, 0);
		gridLayout_ars_tracker_sessions->setRowStretch(7, 0);

		verticalSpacer_ars_tracker_status =
				new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

		gridLayout_ars_tracker->addItem(verticalSpacer_ars_tracker_status, 4, 0, 1, 3);
		gridLayout_ars_tracker->setColumnStretch(0, 4);
		gridLayout_ars_tracker->setColumnStretch(1, 3);
		gridLayout_ars_tracker->setColumnStretch(2, 4);
		gridLayout_ars_tracker->setRowStretch(4, 1);

		qDebug() << "ArsTracker UI layout regrouped: Tracker info + Firmware + Sessions";
		qDebug() << "ArsTracker shell block added below top row";
		qDebug() << "ArsTracker device logs widget initialized";
		qDebug() << "ArsTracker Sessions block initialized with compact session list and file status widgets";
		qDebug() << "ArsTracker operation status labels moved to bottom status area";


		verticalLayout_2->addWidget(selector_group);

		//    tabWidget->addTab(tab, QString());
		tab_2 = new QWidget();
		tab_2->setObjectName("tab_2");
		verticalLayoutWidget = new QWidget(tab_2);
		verticalLayoutWidget->setObjectName("verticalLayoutWidget");
		verticalLayoutWidget->setGeometry(QRect(2, 2, 229, 182));
		verticalLayout = new QVBoxLayout(verticalLayoutWidget);
		verticalLayout->setSpacing(2);
		verticalLayout->setObjectName("verticalLayout");
		verticalLayout->setContentsMargins(4, 4, 10, 4);
		formLayout = new QFormLayout();
		formLayout->setObjectName("formLayout");
		formLayout->setFieldGrowthPolicy(QFormLayout::FieldGrowthPolicy::ExpandingFieldsGrow);
		formLayout->setHorizontalSpacing(2);
		formLayout->setVerticalSpacing(2);
		label_7 = new QLabel(verticalLayoutWidget);
		label_7->setObjectName("label_7");

		formLayout->setWidget(0, QFormLayout::ItemRole::LabelRole, label_7);

		edit_IMG_Preview_Hash = new QLineEdit(verticalLayoutWidget);
		edit_IMG_Preview_Hash->setObjectName("edit_IMG_Preview_Hash");
		edit_IMG_Preview_Hash->setReadOnly(true);

		formLayout->setWidget(0, QFormLayout::ItemRole::FieldRole, edit_IMG_Preview_Hash);

		label_8 = new QLabel(verticalLayoutWidget);
		label_8->setObjectName("label_8");

		formLayout->setWidget(1, QFormLayout::ItemRole::LabelRole, label_8);

		edit_IMG_Preview_Version = new QLineEdit(verticalLayoutWidget);
		edit_IMG_Preview_Version->setObjectName("edit_IMG_Preview_Version");
		edit_IMG_Preview_Version->setReadOnly(true);

		formLayout->setWidget(1, QFormLayout::ItemRole::FieldRole, edit_IMG_Preview_Version);

		verticalLayout->addLayout(formLayout);

		gridLayout_6 = new QGridLayout();
		gridLayout_6->setSpacing(2);
		gridLayout_6->setObjectName("gridLayout_6");
		check_IMG_Preview_Confirmed = new QCheckBox(verticalLayoutWidget);
		check_IMG_Preview_Confirmed->setObjectName("check_IMG_Preview_Confirmed");

		gridLayout_6->addWidget(check_IMG_Preview_Confirmed, 1, 0, 1, 1);

		check_IMG_Preview_Active = new QCheckBox(verticalLayoutWidget);
		check_IMG_Preview_Active->setObjectName("check_IMG_Preview_Active");
		check_IMG_Preview_Active->setEnabled(true);
		check_IMG_Preview_Active->setCheckable(true);

		gridLayout_6->addWidget(check_IMG_Preview_Active, 0, 0, 1, 1);

		check_IMG_Preview_Pending = new QCheckBox(verticalLayoutWidget);
		check_IMG_Preview_Pending->setObjectName("check_IMG_Preview_Pending");

		gridLayout_6->addWidget(check_IMG_Preview_Pending, 1, 1, 1, 1);

		check_IMG_Preview_Bootable = new QCheckBox(verticalLayoutWidget);
		check_IMG_Preview_Bootable->setObjectName("check_IMG_Preview_Bootable");

		gridLayout_6->addWidget(check_IMG_Preview_Bootable, 0, 1, 1, 1);

		check_IMG_Preview_Permanent = new QCheckBox(verticalLayoutWidget);
		check_IMG_Preview_Permanent->setObjectName("check_IMG_Preview_Permanent");

		gridLayout_6->addWidget(check_IMG_Preview_Permanent, 2, 0, 1, 1);

		verticalLayout->addLayout(gridLayout_6);

		btn_IMG_Preview_Copy = new QPushButton(verticalLayoutWidget);
		btn_IMG_Preview_Copy->setObjectName("btn_IMG_Preview_Copy");

		verticalLayout->addWidget(btn_IMG_Preview_Copy);

		verticalSpacer =
				new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

		verticalLayout->addItem(verticalSpacer);

		//    tabWidget->addTab(tab_2, QString());

		//    gridLayout->addWidget(tabWidget, 0, 0, 1, 1);

		//    QWidget::setTabOrder(tabWidget, edit_MTU);
		QWidget::setTabOrder(edit_MTU, check_V2_Protocol);
		QWidget::setTabOrder(check_V2_Protocol, radio_transport_uart);
		QWidget::setTabOrder(radio_transport_uart, radio_transport_udp);
		QWidget::setTabOrder(radio_transport_udp, radio_transport_bluetooth);
		QWidget::setTabOrder(radio_transport_bluetooth, btn_transport_connect);
		QWidget::setTabOrder(btn_transport_connect, selector_group);
		QWidget::setTabOrder(selector_group, selector_img);
		QWidget::setTabOrder(selector_img, edit_IMG_Local);
		QWidget::setTabOrder(edit_IMG_Local, btn_IMG_Local);
		QWidget::setTabOrder(btn_IMG_Local, edit_IMG_Image);
		QWidget::setTabOrder(edit_IMG_Image, radio_IMG_No_Action);
		QWidget::setTabOrder(radio_IMG_No_Action, radio_IMG_Test);
		QWidget::setTabOrder(radio_IMG_Test, radio_IMG_Confirm);
		QWidget::setTabOrder(radio_IMG_Confirm, check_IMG_Reset);
		QWidget::setTabOrder(check_IMG_Reset, colview_IMG_Images);
		//    QWidget::setTabOrder(colview_IMG_Images, edit_IMG_Preview_Hash);
		QWidget::setTabOrder(edit_IMG_Preview_Hash, edit_IMG_Preview_Version);
		QWidget::setTabOrder(edit_IMG_Preview_Version, check_IMG_Preview_Active);
		QWidget::setTabOrder(check_IMG_Preview_Active, check_IMG_Preview_Bootable);
		QWidget::setTabOrder(check_IMG_Preview_Bootable, check_IMG_Preview_Confirmed);
		QWidget::setTabOrder(check_IMG_Preview_Confirmed, check_IMG_Preview_Pending);
		QWidget::setTabOrder(check_IMG_Preview_Pending, check_IMG_Preview_Permanent);
		QWidget::setTabOrder(check_IMG_Preview_Permanent, btn_IMG_Preview_Copy);
		//    QWidget::setTabOrder(btn_IMG_Preview_Copy, radio_IMG_Get);
		QWidget::setTabOrder(radio_IMG_Get, radio_IMG_Set);
		QWidget::setTabOrder(radio_IMG_Set, radio_img_images_erase);
		QWidget::setTabOrder(radio_img_images_erase, check_IMG_Confirm);
		QWidget::setTabOrder(check_IMG_Confirm, edit_IMG_Erase_Slot);
		QWidget::setTabOrder(edit_IMG_Erase_Slot, tree_IMG_Slot_Info);
		QWidget::setTabOrder(tree_IMG_Slot_Info, btn_IMG_Go);
		QWidget::setTabOrder(btn_IMG_Go, edit_FS_Local);
		QWidget::setTabOrder(edit_FS_Local, btn_FS_Local);
		QWidget::setTabOrder(btn_FS_Local, edit_FS_Remote);
		QWidget::setTabOrder(edit_FS_Remote, combo_FS_type);
		QWidget::setTabOrder(combo_FS_type, edit_FS_Result);
		QWidget::setTabOrder(edit_FS_Result, edit_FS_Size);
		QWidget::setTabOrder(edit_FS_Size, radio_FS_Upload);
		QWidget::setTabOrder(radio_FS_Upload, radio_FS_Download);
		QWidget::setTabOrder(radio_FS_Download, radio_FS_Size);
		QWidget::setTabOrder(radio_FS_Size, radio_FS_HashChecksum);
		QWidget::setTabOrder(radio_FS_HashChecksum, radio_FS_Hash_Checksum_Types);
		QWidget::setTabOrder(radio_FS_Hash_Checksum_Types, btn_FS_Go);
		QWidget::setTabOrder(btn_FS_Go, selector_OS);
		QWidget::setTabOrder(selector_OS, edit_OS_Echo_Input);
		QWidget::setTabOrder(edit_OS_Echo_Input, edit_OS_Echo_Output);
		QWidget::setTabOrder(edit_OS_Echo_Output, table_OS_Tasks);
		QWidget::setTabOrder(table_OS_Tasks, table_OS_Memory);
		QWidget::setTabOrder(table_OS_Memory, check_OS_Force_Reboot);
		QWidget::setTabOrder(check_OS_Force_Reboot, edit_os_datetime_date_time);
		QWidget::setTabOrder(edit_os_datetime_date_time, combo_os_datetime_timezone);
		QWidget::setTabOrder(combo_os_datetime_timezone, check_os_datetime_use_pc_date_time);
		QWidget::setTabOrder(check_os_datetime_use_pc_date_time, radio_os_datetime_get);
		QWidget::setTabOrder(radio_os_datetime_get, radio_os_datetime_set);
		QWidget::setTabOrder(radio_os_datetime_set, edit_OS_UName);
		QWidget::setTabOrder(edit_OS_UName, radio_OS_Buffer_Info);
		QWidget::setTabOrder(radio_OS_Buffer_Info, radio_OS_uname);
		QWidget::setTabOrder(radio_OS_uname, edit_OS_Info_Output);
		QWidget::setTabOrder(edit_OS_Info_Output, edit_os_bootloader_query);
		QWidget::setTabOrder(edit_os_bootloader_query, edit_os_bootloader_response);
		QWidget::setTabOrder(edit_os_bootloader_response, btn_OS_Go);
		QWidget::setTabOrder(btn_OS_Go, combo_STAT_Group);
		QWidget::setTabOrder(combo_STAT_Group, table_STAT_Values);
		QWidget::setTabOrder(table_STAT_Values, radio_STAT_List);
		QWidget::setTabOrder(radio_STAT_List, radio_STAT_Fetch);
		QWidget::setTabOrder(radio_STAT_Fetch, btn_STAT_Go);
		QWidget::setTabOrder(btn_STAT_Go, edit_SHELL_Output);
		QWidget::setTabOrder(edit_SHELL_Output, check_shell_vt100_decoding);
		QWidget::setTabOrder(check_shell_vt100_decoding, check_shel_unescape_strings);
		QWidget::setTabOrder(check_shel_unescape_strings, btn_SHELL_Clear);
		QWidget::setTabOrder(btn_SHELL_Clear, btn_SHELL_Copy);
		QWidget::setTabOrder(btn_SHELL_Copy, edit_settings_key);
		QWidget::setTabOrder(edit_settings_key, edit_settings_value);
		QWidget::setTabOrder(edit_settings_value, radio_settings_read);
		QWidget::setTabOrder(radio_settings_read, radio_settings_write);
		QWidget::setTabOrder(radio_settings_write, radio_settings_delete);
		QWidget::setTabOrder(radio_settings_delete, radio_settings_commit);
		QWidget::setTabOrder(radio_settings_commit, radio_settings_load);
		QWidget::setTabOrder(radio_settings_load, radio_settings_save);
		QWidget::setTabOrder(radio_settings_save, radio_settings_none);
		QWidget::setTabOrder(radio_settings_none, radio_settings_text);
		QWidget::setTabOrder(radio_settings_text, radio_settings_decimal);
		QWidget::setTabOrder(radio_settings_decimal, check_settings_big_endian);
		QWidget::setTabOrder(check_settings_big_endian, check_settings_signed_decimal_value);
		QWidget::setTabOrder(check_settings_signed_decimal_value, edit_settings_decoded);
		QWidget::setTabOrder(edit_settings_decoded, btn_settings_go);
		QWidget::setTabOrder(btn_settings_go, tabWidget_4);
		QWidget::setTabOrder(tabWidget_4, btn_zephyr_go);
		QWidget::setTabOrder(btn_zephyr_go, radio_Enum_Count);
		QWidget::setTabOrder(radio_Enum_Count, radio_Enum_List);
		QWidget::setTabOrder(radio_Enum_List, radio_Enum_Single);
		QWidget::setTabOrder(radio_Enum_Single, radio_Enum_Details);
		QWidget::setTabOrder(radio_Enum_Details, edit_Enum_Count);
		QWidget::setTabOrder(edit_Enum_Count, edit_Enum_Index);
		QWidget::setTabOrder(edit_Enum_Index, edit_Enum_Group_ID);
		QWidget::setTabOrder(edit_Enum_Group_ID, check_Enum_Group_Additional);
		QWidget::setTabOrder(check_Enum_Group_Additional, table_Enum_List_Details);
		QWidget::setTabOrder(table_Enum_List_Details, btn_enum_go);
		QWidget::setTabOrder(btn_enum_go, radio_custom_custom);
		QWidget::setTabOrder(radio_custom_custom, radio_custom_logging);
		QWidget::setTabOrder(radio_custom_logging, radio_custom_json);
		QWidget::setTabOrder(radio_custom_json, radio_custom_yaml);
		QWidget::setTabOrder(radio_custom_yaml, radio_custom_cbor);
		QWidget::setTabOrder(radio_custom_cbor, radio_custom_read);
		QWidget::setTabOrder(radio_custom_read, radio_custom_write);
		QWidget::setTabOrder(radio_custom_write, edit_custom_group);
		QWidget::setTabOrder(edit_custom_group, edit_custom_command);
		QWidget::setTabOrder(edit_custom_command, edit_custom_send);
		QWidget::setTabOrder(edit_custom_send, edit_custom_receive);
		QWidget::setTabOrder(edit_custom_receive, edit_custom_indent);
		QWidget::setTabOrder(edit_custom_indent, btn_custom_copy_send);
		QWidget::setTabOrder(btn_custom_copy_send, btn_custom_copy_receive);
		QWidget::setTabOrder(btn_custom_copy_receive, btn_custom_copy_both);
		QWidget::setTabOrder(btn_custom_copy_both, btn_custom_clear);
		QWidget::setTabOrder(btn_custom_clear, btn_custom_go);

		//    retranslateUi(Form);

		//    tabWidget->setCurrentIndex(0);
		selector_group->setCurrentIndex(0);
		selector_img->setCurrentIndex(0);
		selector_OS->setCurrentIndex(0);
		tabWidget_4->setCurrentIndex(0);
		/// AUTOGEN_END_INIT

		// retranslate code
		/// AUTOGEN_START_TRANSLATE
		//    Form->setWindowTitle(QCoreApplication::translate("Form", "Form", nullptr));
		label->setText(QCoreApplication::translate("Form", "MTU:", nullptr));
		check_V2_Protocol->setText(QCoreApplication::translate("Form", "SMP v2", nullptr));
		radio_transport_uart->setText(QCoreApplication::translate("Form", "UART", nullptr));
		radio_transport_udp->setText(QCoreApplication::translate("Form", "UDP", nullptr));
		radio_transport_bluetooth->setText(QCoreApplication::translate("Form", "Bluetooth", nullptr));
		radio_transport_lora->setText(QCoreApplication::translate("Form", "LoRaWAN", nullptr));
		btn_transport_connect->setText(QCoreApplication::translate("Form", "Connect", nullptr));
		btn_error_lookup->setText(QCoreApplication::translate("Form", "Error lookup", nullptr));
		btn_cancel->setText(QCoreApplication::translate("Form", "&Cancel", nullptr));
		label_6->setText(QCoreApplication::translate("Form", "Progress:", nullptr));
		check_IMG_Reset->setText(QCoreApplication::translate("Form", "After upload", nullptr));
		label_9->setText(QCoreApplication::translate("Form", "Reset:", nullptr));
		label_4->setText(QCoreApplication::translate("Form", "Image:", nullptr));
		btn_IMG_Local->setText(QCoreApplication::translate("Form", "...", nullptr));
		label_43->setText(QCoreApplication::translate("Form", "File:", nullptr));
		radio_IMG_No_Action->setText(QCoreApplication::translate("Form", "No action", nullptr));
		radio_IMG_Test->setText(QCoreApplication::translate("Form", "Test", nullptr));
		radio_IMG_Confirm->setText(QCoreApplication::translate("Form", "Confirm", nullptr));
		selector_img->setTabText(selector_img->indexOf(tab_IMG_Upload),
														 QCoreApplication::translate("Form", "Upload", nullptr));
		label_5->setText(QCoreApplication::translate("Form", "State:", nullptr));
		radio_IMG_Get->setText(QCoreApplication::translate("Form", "Get", nullptr));
		radio_IMG_Set->setText(QCoreApplication::translate("Form", "Set", nullptr));
		radio_img_images_erase->setText(QCoreApplication::translate("Form", "Erase", nullptr));
		check_IMG_Confirm->setText(QCoreApplication::translate("Form", "Confirm", nullptr));
		selector_img->setTabText(selector_img->indexOf(tab_IMG_Images),
														 QCoreApplication::translate("Form", "Images", nullptr));
		label_14->setText(QCoreApplication::translate("Form", "Slot:", nullptr));
		selector_img->setTabText(selector_img->indexOf(tab_IMG_Erase),
														 QCoreApplication::translate("Form", "Erase", nullptr));
		QTreeWidgetItem* ___qtreewidgetitem = tree_IMG_Slot_Info->headerItem();
		___qtreewidgetitem->setText(2, QCoreApplication::translate("Form", "Upload ID", nullptr));
		___qtreewidgetitem->setText(1, QCoreApplication::translate("Form", "Size", nullptr));
		selector_img->setTabText(selector_img->indexOf(tab_IMG_Slots),
														 QCoreApplication::translate("Form", "Slots", nullptr));
		btn_IMG_Go->setText(QCoreApplication::translate("Form", "Go", nullptr));
		lbl_IMG_Status->setText(QCoreApplication::translate("Form", "[Status]", nullptr));
		selector_group->setTabText(selector_group->indexOf(tab_IMG),
															 QCoreApplication::translate("Form", "Img", nullptr));
		label_28->setText(QCoreApplication::translate("Form", "Hash/Checksum:", nullptr));
		lbl_FS_Status->setText(QCoreApplication::translate("Form", "[Status]", nullptr));
		label_29->setText(QCoreApplication::translate("Form", "File size:", nullptr));
		label_2->setText(QCoreApplication::translate("Form", "Local file:", nullptr));
		btn_FS_Local->setText(QCoreApplication::translate("Form", "...", nullptr));
		label_3->setText(QCoreApplication::translate("Form", "Device file:", nullptr));
		radio_FS_Upload->setText(QCoreApplication::translate("Form", "Upload", nullptr));
		radio_FS_Download->setText(QCoreApplication::translate("Form", "Download", nullptr));
		radio_FS_Size->setText(QCoreApplication::translate("Form", "Size", nullptr));
		radio_FS_HashChecksum->setText(QCoreApplication::translate("Form", "Hash/checksum", nullptr));
		radio_FS_Hash_Checksum_Types->setText(QCoreApplication::translate("Form", "Types", nullptr));
		label_19->setText(QCoreApplication::translate("Form", "Type:", nullptr));
		btn_FS_Go->setText(QCoreApplication::translate("Form", "Go", nullptr));
		selector_group->setTabText(selector_group->indexOf(tab_FS),
															 QCoreApplication::translate("Form", "FS", nullptr));
		btn_OS_Go->setText(QCoreApplication::translate("Form", "Go", nullptr));
		lbl_OS_Status->setText(QCoreApplication::translate("Form", "[Status]", nullptr));
		label_10->setText(QCoreApplication::translate("Form", "Input:", nullptr));
		label_11->setText(QCoreApplication::translate("Form", "Output:", nullptr));
		selector_OS->setTabText(selector_OS->indexOf(tab_OS_Echo),
														QCoreApplication::translate("Form", "Echo", nullptr));
		QTableWidgetItem* ___qtablewidgetitem = table_OS_Tasks->horizontalHeaderItem(0);
		___qtablewidgetitem->setText(QCoreApplication::translate("Form", "Task", nullptr));
		QTableWidgetItem* ___qtablewidgetitem1 = table_OS_Tasks->horizontalHeaderItem(1);
		___qtablewidgetitem1->setText(QCoreApplication::translate("Form", "ID", nullptr));
		QTableWidgetItem* ___qtablewidgetitem2 = table_OS_Tasks->horizontalHeaderItem(2);
		___qtablewidgetitem2->setText(QCoreApplication::translate("Form", "Priority", nullptr));
		QTableWidgetItem* ___qtablewidgetitem3 = table_OS_Tasks->horizontalHeaderItem(3);
		___qtablewidgetitem3->setText(QCoreApplication::translate("Form", "State", nullptr));
		QTableWidgetItem* ___qtablewidgetitem4 = table_OS_Tasks->horizontalHeaderItem(4);
		___qtablewidgetitem4->setText(QCoreApplication::translate("Form", "Context Switches", nullptr));
		QTableWidgetItem* ___qtablewidgetitem5 = table_OS_Tasks->horizontalHeaderItem(5);
		___qtablewidgetitem5->setText(QCoreApplication::translate("Form", "Runtime", nullptr));
		QTableWidgetItem* ___qtablewidgetitem6 = table_OS_Tasks->horizontalHeaderItem(6);
		___qtablewidgetitem6->setText(QCoreApplication::translate("Form", "Stack size", nullptr));
		QTableWidgetItem* ___qtablewidgetitem7 = table_OS_Tasks->horizontalHeaderItem(7);
		___qtablewidgetitem7->setText(QCoreApplication::translate("Form", "Stack usage", nullptr));
		selector_OS->setTabText(selector_OS->indexOf(tab_OS_Tasks),
														QCoreApplication::translate("Form", "Tasks", nullptr));
		QTableWidgetItem* ___qtablewidgetitem8 = table_OS_Memory->horizontalHeaderItem(0);
		___qtablewidgetitem8->setText(QCoreApplication::translate("Form", "Name", nullptr));
		QTableWidgetItem* ___qtablewidgetitem9 = table_OS_Memory->horizontalHeaderItem(1);
		___qtablewidgetitem9->setText(QCoreApplication::translate("Form", "Size", nullptr));
		QTableWidgetItem* ___qtablewidgetitem10 = table_OS_Memory->horizontalHeaderItem(2);
		___qtablewidgetitem10->setText(QCoreApplication::translate("Form", "Free", nullptr));
		QTableWidgetItem* ___qtablewidgetitem11 = table_OS_Memory->horizontalHeaderItem(3);
		___qtablewidgetitem11->setText(QCoreApplication::translate("Form", "Minimum", nullptr));
		selector_OS->setTabText(selector_OS->indexOf(tab_OS_Memory),
														QCoreApplication::translate("Form", "Memory", nullptr));
		label_44->setText(QCoreApplication::translate("Form", "Boot mode:", nullptr));
		check_OS_Force_Reboot->setText(QCoreApplication::translate("Form", "Force reboot", nullptr));
		selector_OS->setTabText(selector_OS->indexOf(tab_OS_Reset),
														QCoreApplication::translate("Form", "Reset", nullptr));
		radio_os_datetime_get->setText(QCoreApplication::translate("Form", "Get", nullptr));
		radio_os_datetime_set->setText(QCoreApplication::translate("Form", "Set", nullptr));
		label_13->setText(QCoreApplication::translate("Form", "Date/time:", nullptr));
		label_31->setText(QCoreApplication::translate("Form", "Sync:", nullptr));
		edit_os_datetime_date_time->setDisplayFormat(
				QCoreApplication::translate("Form", "yyyy-MM-dd HH:mm:ss t", nullptr));
		label_30->setText(QCoreApplication::translate("Form", "Timezone:", nullptr));
		check_os_datetime_use_pc_date_time->setText(
				QCoreApplication::translate("Form", "Use PC date and time", nullptr));
		selector_OS->setTabText(selector_OS->indexOf(tab_os_datetime),
														QCoreApplication::translate("Form", "Date/time", nullptr));
		label_17->setText(QCoreApplication::translate("Form", "uname:", nullptr));
		radio_OS_Buffer_Info->setText(QCoreApplication::translate("Form", "Buffer info", nullptr));
		radio_OS_uname->setText(QCoreApplication::translate("Form", "uname", nullptr));
		label_18->setText(QCoreApplication::translate("Form", "Output:", nullptr));
		selector_OS->setTabText(selector_OS->indexOf(tab_OS_Info),
														QCoreApplication::translate("Form", "Info", nullptr));
		label_20->setText(
				QCoreApplication::translate("Form", "Query (blank for bootloader):", nullptr));
		edit_os_bootloader_query->setPlaceholderText(
				QCoreApplication::translate("Form", "Bootloader", nullptr));
		label_21->setText(QCoreApplication::translate("Form", "Response:", nullptr));
		selector_OS->setTabText(selector_OS->indexOf(tab_OS_Bootloader),
														QCoreApplication::translate("Form", "Bootloader", nullptr));
		selector_group->setTabText(selector_group->indexOf(tab_OS),
															 QCoreApplication::translate("Form", "OS", nullptr));
		lbl_STAT_Status->setText(QCoreApplication::translate("Form", "[Status]", nullptr));
		radio_STAT_List->setText(QCoreApplication::translate("Form", "List Groups", nullptr));
		radio_STAT_Fetch->setText(QCoreApplication::translate("Form", "Fetch Stats", nullptr));
		btn_STAT_Go->setText(QCoreApplication::translate("Form", "Go", nullptr));
		label_16->setText(QCoreApplication::translate("Form", "Values:", nullptr));
		label_15->setText(QCoreApplication::translate("Form", "Group:", nullptr));
		QTableWidgetItem* ___qtablewidgetitem12 = table_STAT_Values->horizontalHeaderItem(0);
		___qtablewidgetitem12->setText(QCoreApplication::translate("Form", "Name", nullptr));
		QTableWidgetItem* ___qtablewidgetitem13 = table_STAT_Values->horizontalHeaderItem(1);
		___qtablewidgetitem13->setText(QCoreApplication::translate("Form", "Value", nullptr));
		selector_group->setTabText(selector_group->indexOf(tab_Stats),
															 QCoreApplication::translate("Form", "Stats", nullptr));
		lbl_SHELL_Status->setText(QCoreApplication::translate("Form", "[Status]", nullptr));
		btn_SHELL_Clear->setText(QCoreApplication::translate("Form", "Clear", nullptr));
		btn_SHELL_Copy->setText(QCoreApplication::translate("Form", "Copy", nullptr));
		check_shell_vt100_decoding->setText(
				QCoreApplication::translate("Form", "VT100 decoding", nullptr));
		check_shel_unescape_strings->setText(
				QCoreApplication::translate("Form", "Un-escape strings", nullptr));
		selector_group->setTabText(selector_group->indexOf(tab_Shell),
															 QCoreApplication::translate("Form", "Shell", nullptr));
		label_22->setText(QCoreApplication::translate("Form", "Action:", nullptr));
		lbl_settings_status->setText(QCoreApplication::translate("Form", "[Status]", nullptr));
		label_26->setText(QCoreApplication::translate("Form", "Decoded:", nullptr));
		radio_settings_none->setText(QCoreApplication::translate("Form", "None", nullptr));
		radio_settings_text->setText(QCoreApplication::translate("Form", "Text", nullptr));
		radio_settings_decimal->setText(QCoreApplication::translate("Form", "Decimal", nullptr));
		label_23->setText(QCoreApplication::translate("Form", "Key:", nullptr));
		label_24->setText(QCoreApplication::translate("Form", "Hex value:", nullptr));
		btn_settings_go->setText(QCoreApplication::translate("Form", "Go", nullptr));
		label_25->setText(QCoreApplication::translate("Form", "Decode:", nullptr));
		check_settings_big_endian->setText(QCoreApplication::translate("Form", "Big endian", nullptr));
		check_settings_signed_decimal_value->setText(
				QCoreApplication::translate("Form", "Signed decimal value", nullptr));
		label_27->setText(QCoreApplication::translate("Form", "Decimals:", nullptr));
		radio_settings_read->setText(QCoreApplication::translate("Form", "Read", nullptr));
		radio_settings_write->setText(QCoreApplication::translate("Form", "Write", nullptr));
		radio_settings_delete->setText(QCoreApplication::translate("Form", "Delete", nullptr));
		radio_settings_commit->setText(QCoreApplication::translate("Form", "Commit", nullptr));
		radio_settings_load->setText(QCoreApplication::translate("Form", "Load", nullptr));
		radio_settings_save->setText(QCoreApplication::translate("Form", "Save", nullptr));
		selector_group->setTabText(selector_group->indexOf(tab_Settings),
															 QCoreApplication::translate("Form", "Settings", nullptr));
		btn_zephyr_go->setText(QCoreApplication::translate("Form", "Go", nullptr));
		label_12->setText(QCoreApplication::translate(
				"Form", "This will erase the \"storage_partition\" flash partition on the device.",
				nullptr));
		tabWidget_4->setTabText(tabWidget_4->indexOf(tab_zephyr_storage_erase),
														QCoreApplication::translate("Form", "Storage Erase", nullptr));
		lbl_zephyr_status->setText(QCoreApplication::translate("Form", "[Status]", nullptr));
		selector_group->setTabText(selector_group->indexOf(tab_zephyr),
															 QCoreApplication::translate("Form", "Zephyr", nullptr));
		radio_Enum_Count->setText(QCoreApplication::translate("Form", "Count", nullptr));
		radio_Enum_List->setText(QCoreApplication::translate("Form", "List", nullptr));
		radio_Enum_Single->setText(QCoreApplication::translate("Form", "Single", nullptr));
		radio_Enum_Details->setText(QCoreApplication::translate("Form", "Details", nullptr));
		label_32->setText(QCoreApplication::translate("Form", "Count:", nullptr));
		label_33->setText(QCoreApplication::translate("Form", "Index:", nullptr));
		label_34->setText(QCoreApplication::translate("Form", "Group ID:", nullptr));
		check_Enum_Group_Additional->setText(
				QCoreApplication::translate("Form", "Additional groups", nullptr));
		QTableWidgetItem* ___qtablewidgetitem14 = table_Enum_List_Details->horizontalHeaderItem(0);
		___qtablewidgetitem14->setText(QCoreApplication::translate("Form", "ID", nullptr));
		QTableWidgetItem* ___qtablewidgetitem15 = table_Enum_List_Details->horizontalHeaderItem(1);
		___qtablewidgetitem15->setText(QCoreApplication::translate("Form", "Name", nullptr));
		QTableWidgetItem* ___qtablewidgetitem16 = table_Enum_List_Details->horizontalHeaderItem(2);
		___qtablewidgetitem16->setText(QCoreApplication::translate("Form", "Handlers", nullptr));
		lbl_enum_status->setText(QCoreApplication::translate("Form", "[Status]", nullptr));
		btn_enum_go->setText(QCoreApplication::translate("Form", "Go", nullptr));
		selector_group->setTabText(selector_group->indexOf(tab_Enum),
															 QCoreApplication::translate("Form", "Enum", nullptr));
		label_38->setText(QCoreApplication::translate("Form", "Mode:", nullptr));
		radio_custom_custom->setText(QCoreApplication::translate("Form", "Custom command", nullptr));
		radio_custom_logging->setText(QCoreApplication::translate("Form", "Logging", nullptr));
		label_39->setText(QCoreApplication::translate("Form", "Type: ", nullptr));
		radio_custom_json->setText(QCoreApplication::translate("Form", "JSON", nullptr));
		radio_custom_yaml->setText(QCoreApplication::translate("Form", "YAML", nullptr));
		radio_custom_cbor->setText(QCoreApplication::translate("Form", "CBOR", nullptr));
		label_40->setText(QCoreApplication::translate("Form", "Operation:", nullptr));
		radio_custom_read->setText(QCoreApplication::translate("Form", "Read", nullptr));
		radio_custom_write->setText(QCoreApplication::translate("Form", "Write", nullptr));
		label_41->setText(QCoreApplication::translate("Form", "Group:", nullptr));
		label_42->setText(QCoreApplication::translate("Form", "Command:", nullptr));
		label_35->setText(QCoreApplication::translate("Form", "Send:", nullptr));
		label_36->setText(QCoreApplication::translate("Form", "Receive:", nullptr));
		label_37->setText(QCoreApplication::translate("Form", "Indent:", nullptr));
		btn_custom_copy_send->setText(QCoreApplication::translate("Form", "Copy send", nullptr));
		btn_custom_copy_receive->setText(QCoreApplication::translate("Form", "Copy receive", nullptr));
		btn_custom_copy_both->setText(QCoreApplication::translate("Form", "Copy both", nullptr));
		btn_custom_clear->setText(QCoreApplication::translate("Form", "Clear", nullptr));
		lbl_custom_status->setText(QCoreApplication::translate("Form", "[Status]", nullptr));
		btn_custom_go->setText(QCoreApplication::translate("Form", "Go", nullptr));
		selector_group->setTabText(selector_group->indexOf(tab_custom),
															 QCoreApplication::translate("Form", "Custom", nullptr));
		label_ars_tracker_info_header->setText(
				QCoreApplication::translate("Form", "Tracker info:", nullptr));
		btn_ars_tracker_info_refresh->setText(
				QCoreApplication::translate("Form", "Refresh tracker info", nullptr));
		label_ars_tracker_port->setText(
				QCoreApplication::translate("Form", "Available trackers", nullptr));
		btn_ars_tracker_connect->setText(
				QCoreApplication::translate("Form", "Find trackers", nullptr));
		label_ars_tracker_serial_number->setText(
				QCoreApplication::translate("Form", "Serial number:", nullptr));
		edit_ars_tracker_serial_number->setText(
				QCoreApplication::translate("Form", "Not loaded", nullptr));
		label_ars_tracker_board_id->setText(
				QCoreApplication::translate("Form", "Board id:", nullptr));
		edit_ars_tracker_board_id->setText(QCoreApplication::translate("Form", "Not loaded", nullptr));
		label_ars_tracker_type->setText(QCoreApplication::translate("Form", "Type:", nullptr));
		edit_ars_tracker_type->setText(QCoreApplication::translate("Form", "Not loaded", nullptr));
		label_ars_tracker_status_value->setText(
				QCoreApplication::translate("Form", "Status:", nullptr));
		label_ars_tracker_status_state->setText(
				QString::fromUtf8("\xE2\x97\x8F Unknown"));
		label_ars_tracker_status_state->setStyleSheet("color: #808080;");
		edit_ars_tracker_status_value->setText(
				QCoreApplication::translate("Form", "Not loaded", nullptr));
		label_ars_tracker_sessions->setText(QCoreApplication::translate("Form", "Sessions:", nullptr));
		label_ars_tracker_battery_info->setText(
				QCoreApplication::translate("Form", "Battery Info:", nullptr));
		edit_ars_tracker_battery_info->setText(QCoreApplication::translate("Form", "N/A", nullptr));
		label_ars_tracker_memory_usage->setText(
				QCoreApplication::translate("Form", "Memory usage:", nullptr));
		edit_ars_tracker_memory_usage->setText(QCoreApplication::translate("Form", "N/A", nullptr));
		label_ars_tracker_bad_blocks->setText(
				QCoreApplication::translate("Form", "Bad blocks:", nullptr));
		edit_ars_tracker_bad_blocks->setText(QCoreApplication::translate("Form", "N/A", nullptr));
		label_ars_tracker_firmware_header->setText(
				QCoreApplication::translate("Form", "Firmware", nullptr));
		label_ars_tracker_firmware_current_version->setText(
				QCoreApplication::translate("Form", "Current version:", nullptr));
		edit_ars_tracker_firmware_current_version->setText(
				QCoreApplication::translate("Form", "Not loaded", nullptr));
		label_ars_tracker_firmware_second_slot->setText(
				QCoreApplication::translate("Form", "Second slot:", nullptr));
		edit_ars_tracker_firmware_second_slot->setText(
				QCoreApplication::translate("Form", "Not loaded", nullptr));
		btn_ars_tracker_firmware_erase->setText(
				QCoreApplication::translate("Form", "Erase", nullptr));
		label_ars_tracker_firmware_file->setText(
				QCoreApplication::translate("Form", "Firmware file:", nullptr));
		edit_ars_tracker_firmware_file->setText(QString());
		btn_ars_tracker_firmware_browse->setText(
				QCoreApplication::translate("Form", "...", nullptr));
		btn_ars_tracker_firmware_upload->setText(
				QCoreApplication::translate("Form", "Upload firmware", nullptr));
		label_ars_tracker_destination->setText(
				QCoreApplication::translate("Form", "Destination:", nullptr));
		btn_ars_tracker_destination->setText(QCoreApplication::translate("Form", "...", nullptr));
		label_ars_tracker_files->setText(
				QCoreApplication::translate("Form", "File status:", nullptr));
		group_ars_tracker_shell->setTitle(
				QCoreApplication::translate("Form", "MCUMgr Shell", nullptr));
		label_ars_tracker_shell_command->setText(
				QCoreApplication::translate("Form", "Command:", nullptr));
		edit_ars_tracker_shell_command->setPlaceholderText(
				QCoreApplication::translate("Form", "Enter shell command", nullptr));
		button_ars_tracker_shell_send->setText(
				QCoreApplication::translate("Form", "Send", nullptr));
		button_ars_tracker_shell_clear->setText(
				QCoreApplication::translate("Form", "Clear", nullptr));
		group_ars_tracker_device_logs->setTitle(
				QCoreApplication::translate("Form", "Device logs", nullptr));
		button_ars_tracker_device_logs_clear->setText(
				QCoreApplication::translate("Form", "Clear logs", nullptr));
		btn_ars_tracker_delete->setText(
				QCoreApplication::translate("Form", "Delete session", nullptr));
		btn_ars_tracker_download->setText(
				QCoreApplication::translate("Form", "Download session", nullptr));
		btn_ars_tracker_cancel->setText(QCoreApplication::translate("Form", "Cancel", nullptr));
		lbl_ars_tracker_progress->setText(
				QCoreApplication::translate("Form", "Files finished: 0/0", nullptr));
		lbl_ars_tracker_status->setText(
				QCoreApplication::translate("Form", "Open ArsTracker tab or refresh tracker info.", nullptr));
		//    tabWidget->setTabText(tabWidget->indexOf(tab), QCoreApplication::translate("Form",
		//    "MCUmgr", nullptr));
		label_7->setText(QCoreApplication::translate("Form", "Hash:", nullptr));
		label_8->setText(QCoreApplication::translate("Form", "Version:", nullptr));
		check_IMG_Preview_Confirmed->setText(QCoreApplication::translate("Form", "Confirmed", nullptr));
		check_IMG_Preview_Active->setText(QCoreApplication::translate("Form", "Active", nullptr));
		check_IMG_Preview_Pending->setText(QCoreApplication::translate("Form", "Pending", nullptr));
		check_IMG_Preview_Bootable->setText(QCoreApplication::translate("Form", "Bootable", nullptr));
		check_IMG_Preview_Permanent->setText(QCoreApplication::translate("Form", "Permanent", nullptr));
		btn_IMG_Preview_Copy->setText(QCoreApplication::translate("Form", "Copy", nullptr));
		//    tabWidget->setTabText(tabWidget->indexOf(tab_2), QCoreApplication::translate("Form",
		//    "Page", nullptr));
		/// AUTOGEN_END_TRANSLATE

		// Add code
		tabWidget_orig->addTab(tab, QString("MCUmgr"));
		tabWidget_orig->addTab(tab_ars_tracker, QString("ArsTracker"));

		// Signals
		connect(this, SIGNAL(plugin_set_status(bool, bool, bool*)), parent_window,
						SLOT(plugin_set_status(bool, bool, bool*)));
		connect(this, SIGNAL(plugin_add_open_close_button(QPushButton*)), parent_window,
						SLOT(plugin_add_open_close_button(QPushButton*)));
		connect(this, SIGNAL(plugin_to_hex(QByteArray*)), parent_window,
						SLOT(plugin_to_hex(QByteArray*)));
		connect(this, SIGNAL(plugin_serial_open_close(uint8_t)), parent_window,
						SLOT(plugin_serial_open_close(uint8_t)));
		connect(this, SIGNAL(plugin_serial_ports(QStringList*,QString*)), parent_window,
						SLOT(plugin_serial_ports(QStringList*,QString*)));
		connect(this, SIGNAL(plugin_serial_state(bool*,bool*)), parent_window,
						SLOT(plugin_serial_state(bool*,bool*)));
		connect(this, SIGNAL(plugin_serial_select(QString)), parent_window,
						SLOT(plugin_serial_select(QString)));

		connect(parent_window, SIGNAL(plugin_serial_receive(QByteArray*)), this,
						SLOT(serial_receive(QByteArray*)));
		connect(parent_window, SIGNAL(plugin_serial_receive_monitor(QByteArray*)), this,
						SLOT(serial_receive_monitor(QByteArray*)));
		connect(parent_window, SIGNAL(plugin_serial_error(QSerialPort::SerialPortError)), this,
						SLOT(serial_error(QSerialPort::SerialPortError)));
		connect(parent_window, SIGNAL(plugin_serial_bytes_written(qint64)), this,
						SLOT(serial_bytes_written(qint64)));
		connect(parent_window, SIGNAL(plugin_serial_about_to_close()), this,
						SLOT(serial_about_to_close()));
		connect(parent_window, SIGNAL(plugin_serial_opened()), this, SLOT(serial_opened()));
		connect(parent_window, SIGNAL(plugin_serial_closed()), this, SLOT(serial_closed()));
		connect(selector_tab_root, SIGNAL(currentChanged(int)), this,
						SLOT(on_selector_tab_currentChanged(int)));

		connect(uart_transport, SIGNAL(serial_write(QByteArray*)), parent_window,
						SLOT(plugin_serial_transmit(QByteArray*)));
		connect(uart_transport, SIGNAL(receive_waiting(smp_message*)), processor,
						SLOT(message_received(smp_message*)));
#if defined(PLUGIN_MCUMGR_TRANSPORT_UDP)
		connect(udp_transport, SIGNAL(receive_waiting(smp_message*)), processor,
						SLOT(message_received(smp_message*)));
		connect(udp_transport, SIGNAL(error(int)), processor, SLOT(transport_disconnect(int)));
#endif

#if defined(PLUGIN_MCUMGR_TRANSPORT_BLUETOOTH)
		connect(bluetooth_transport, SIGNAL(receive_waiting(smp_message*)), processor,
						SLOT(message_received(smp_message*)));
		connect(bluetooth_transport, SIGNAL(error(int)), processor, SLOT(transport_disconnect(int)));
#endif

#if defined(PLUGIN_MCUMGR_TRANSPORT_LORAWAN)
		connect(lora_transport, SIGNAL(receive_waiting(smp_message*)), processor,
						SLOT(message_received(smp_message*)));
		connect(lora_transport, SIGNAL(error(int)), processor, SLOT(transport_disconnect(int)));
#endif

		connect(smp_groups.fs_mgmt, SIGNAL(status(uint8_t, group_status, QString)), this,
						SLOT(status(uint8_t, group_status, QString)));
		connect(smp_groups.fs_mgmt, SIGNAL(progress(uint8_t, uint8_t)), this,
						SLOT(progress(uint8_t, uint8_t)));
		connect(smp_groups.img_mgmt, SIGNAL(status(uint8_t, group_status, QString)), this,
						SLOT(status(uint8_t, group_status, QString)));
		connect(smp_groups.img_mgmt, SIGNAL(progress(uint8_t, uint8_t)), this,
						SLOT(progress(uint8_t, uint8_t)));
		connect(smp_groups.img_mgmt, SIGNAL(plugin_to_hex(QByteArray*)), this,
						SLOT(group_to_hex(QByteArray*)));
		connect(smp_groups.os_mgmt, SIGNAL(status(uint8_t, group_status, QString)), this,
						SLOT(status(uint8_t, group_status, QString)));
		connect(smp_groups.os_mgmt, SIGNAL(progress(uint8_t, uint8_t)), this,
						SLOT(progress(uint8_t, uint8_t)));
		connect(smp_groups.settings_mgmt, SIGNAL(status(uint8_t, group_status, QString)), this,
						SLOT(status(uint8_t, group_status, QString)));
		connect(smp_groups.settings_mgmt, SIGNAL(progress(uint8_t, uint8_t)), this,
						SLOT(progress(uint8_t, uint8_t)));
		connect(smp_groups.shell_mgmt, SIGNAL(status(uint8_t, group_status, QString)), this,
						SLOT(status(uint8_t, group_status, QString)));
		connect(smp_groups.shell_mgmt, SIGNAL(progress(uint8_t, uint8_t)), this,
						SLOT(progress(uint8_t, uint8_t)));
		connect(smp_groups.stat_mgmt, SIGNAL(status(uint8_t, group_status, QString)), this,
						SLOT(status(uint8_t, group_status, QString)));
		connect(smp_groups.stat_mgmt, SIGNAL(progress(uint8_t, uint8_t)), this,
						SLOT(progress(uint8_t, uint8_t)));
		connect(smp_groups.zephyr_mgmt, SIGNAL(status(uint8_t, group_status, QString)), this,
						SLOT(status(uint8_t, group_status, QString)));
		connect(smp_groups.zephyr_mgmt, SIGNAL(progress(uint8_t, uint8_t)), this,
						SLOT(progress(uint8_t, uint8_t)));
		connect(smp_groups.enum_mgmt, SIGNAL(status(uint8_t, group_status, QString)), this,
						SLOT(status(uint8_t, group_status, QString)));
		connect(smp_groups.enum_mgmt, SIGNAL(progress(uint8_t, uint8_t)), this,
						SLOT(progress(uint8_t, uint8_t)));

		connect(processor, SIGNAL(custom_message_callback(custom_message_callback_t, smp_error_t*)),
						this, SLOT(custom_message_callback(custom_message_callback_t, smp_error_t*)));

		// Form signals
		connect(btn_FS_Local, SIGNAL(clicked()), this, SLOT(on_btn_FS_Local_clicked()));
		connect(btn_FS_Go, SIGNAL(clicked()), this, SLOT(on_btn_FS_Go_clicked()));
		connect(radio_FS_Upload, SIGNAL(toggled(bool)), this, SLOT(on_radio_FS_Upload_toggled(bool)));
		connect(radio_FS_Download, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_FS_Download_toggled(bool)));
		connect(radio_FS_Size, SIGNAL(toggled(bool)), this, SLOT(on_radio_FS_Size_toggled(bool)));
		connect(radio_FS_HashChecksum, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_FS_HashChecksum_toggled(bool)));
		connect(radio_FS_Hash_Checksum_Types, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_FS_Hash_Checksum_Types_toggled(bool)));
		connect(btn_IMG_Local, SIGNAL(clicked()), this, SLOT(on_btn_IMG_Local_clicked()));
		connect(btn_IMG_Go, SIGNAL(clicked()), this, SLOT(on_btn_IMG_Go_clicked()));
		connect(radio_IMG_No_Action, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_IMG_No_Action_toggled(bool)));
		connect(btn_IMG_Preview_Copy, SIGNAL(clicked()), this, SLOT(on_btn_IMG_Preview_Copy_clicked()));
		connect(btn_OS_Go, SIGNAL(clicked()), this, SLOT(on_btn_OS_Go_clicked()));
		connect(btn_STAT_Go, SIGNAL(clicked()), this, SLOT(on_btn_STAT_Go_clicked()));
		connect(btn_SHELL_Clear, SIGNAL(clicked()), this, SLOT(on_btn_SHELL_Clear_clicked()));
		connect(btn_SHELL_Copy, SIGNAL(clicked()), this, SLOT(on_btn_SHELL_Copy_clicked()));
		connect(btn_transport_connect, SIGNAL(clicked()), this,
						SLOT(on_btn_transport_connect_clicked()));
		connect(colview_IMG_Images, SIGNAL(updatePreviewWidget(QModelIndex)), this,
						SLOT(on_colview_IMG_Images_updatePreviewWidget(QModelIndex)));
		connect(radio_transport_uart, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_transport_uart_toggled(bool)));
#if defined(PLUGIN_MCUMGR_TRANSPORT_UDP)
		connect(radio_transport_udp, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_transport_udp_toggled(bool)));
#endif
#if defined(PLUGIN_MCUMGR_TRANSPORT_BLUETOOTH)
		connect(radio_transport_bluetooth, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_transport_bluetooth_toggled(bool)));
#endif
#if defined(PLUGIN_MCUMGR_TRANSPORT_LORAWAN)
		connect(radio_transport_lora, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_transport_lora_toggled(bool)));
#endif
		connect(radio_OS_Buffer_Info, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_OS_Buffer_Info_toggled(bool)));
		connect(radio_OS_uname, SIGNAL(toggled(bool)), this, SLOT(on_radio_OS_uname_toggled(bool)));
		connect(radio_IMG_Get, SIGNAL(toggled(bool)), this, SLOT(on_radio_IMG_Get_toggled(bool)));
		connect(radio_IMG_Set, SIGNAL(toggled(bool)), this, SLOT(on_radio_IMG_Set_toggled(bool)));
		connect(radio_settings_read, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_settings_read_toggled(bool)));
		connect(radio_settings_write, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_settings_write_toggled(bool)));
		connect(radio_settings_delete, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_settings_delete_toggled(bool)));
		connect(radio_settings_commit, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_settings_commit_toggled(bool)));
		connect(radio_settings_load, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_settings_load_toggled(bool)));
		connect(radio_settings_save, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_settings_save_toggled(bool)));
		connect(btn_settings_go, SIGNAL(clicked()), this, SLOT(on_btn_settings_go_clicked()));
		connect(radio_settings_none, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_settings_none_toggled(bool)));
		connect(radio_settings_text, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_settings_text_toggled(bool)));
		connect(radio_settings_decimal, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_settings_decimal_toggled(bool)));
		connect(check_settings_big_endian, SIGNAL(toggled(bool)), this,
						SLOT(on_check_settings_big_endian_toggled(bool)));
		connect(check_settings_signed_decimal_value, SIGNAL(toggled(bool)), this,
						SLOT(on_check_settings_signed_decimal_value_toggled(bool)));
		connect(edit_SHELL_Output, SIGNAL(enter_pressed()), this, SLOT(enter_pressed()));
		connect(btn_zephyr_go, SIGNAL(clicked()), this, SLOT(on_btn_zephyr_go_clicked()));
		connect(btn_zephyr_go, SIGNAL(clicked()), this, SLOT(on_btn_zephyr_go_clicked()));
		connect(check_os_datetime_use_pc_date_time, SIGNAL(toggled(bool)), this,
						SLOT(on_check_os_datetime_use_pc_date_time_toggled(bool)));
		connect(radio_os_datetime_get, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_os_datetime_get_toggled(bool)));
		connect(radio_os_datetime_set, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_os_datetime_set_toggled(bool)));
		connect(btn_enum_go, SIGNAL(clicked()), this, SLOT(on_btn_enum_go_clicked()));
		connect(radio_custom_custom, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_custom_custom_toggled(bool)));
		connect(radio_custom_logging, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_custom_logging_toggled(bool)));
		connect(radio_custom_json, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_custom_json_toggled(bool)));
		connect(radio_custom_yaml, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_custom_yaml_toggled(bool)));
		connect(radio_custom_cbor, SIGNAL(toggled(bool)), this,
						SLOT(on_radio_custom_cbor_toggled(bool)));
		connect(btn_custom_copy_send, SIGNAL(clicked()), this, SLOT(on_btn_custom_copy_send_clicked()));
		connect(btn_custom_copy_receive, SIGNAL(clicked()), this,
						SLOT(on_btn_custom_copy_receive_clicked()));
		connect(btn_custom_copy_both, SIGNAL(clicked()), this, SLOT(on_btn_custom_copy_both_clicked()));
		connect(btn_custom_clear, SIGNAL(clicked()), this, SLOT(on_btn_custom_clear_clicked()));
		connect(edit_custom_indent, SIGNAL(valueChanged(int)), this,
						SLOT(on_edit_custom_indent_valueChanged(int)));
		connect(btn_custom_go, SIGNAL(clicked()), this, SLOT(on_btn_custom_go_clicked()));
		connect(tree_IMG_Slot_Info, SIGNAL(itemDoubleClicked(QTreeWidgetItem*, int)), this,
						SLOT(on_tree_IMG_Slot_Info_itemDoubleClicked(QTreeWidgetItem*, int)));
		connect(btn_error_lookup, SIGNAL(clicked()), this, SLOT(on_btn_error_lookup_clicked()));
		connect(btn_cancel, SIGNAL(clicked()), this, SLOT(on_btn_cancel_clicked()));
		connect(btn_ars_tracker_info_refresh, SIGNAL(clicked()), this,
						SLOT(on_btn_ars_tracker_info_refresh_clicked()));
		connect(btn_ars_tracker_delete, SIGNAL(clicked()), this,
						SLOT(on_btn_ars_tracker_delete_clicked()));
		connect(btn_ars_tracker_download, SIGNAL(clicked()), this,
						SLOT(on_btn_ars_tracker_download_clicked()));
		connect(btn_ars_tracker_firmware_browse, SIGNAL(clicked()), this,
						SLOT(on_btn_ars_tracker_firmware_browse_clicked()));
		connect(btn_ars_tracker_firmware_upload, SIGNAL(clicked()), this,
						SLOT(on_btn_ars_tracker_firmware_upload_clicked()));
		connect(btn_ars_tracker_firmware_erase, SIGNAL(clicked()), this,
						SLOT(on_btn_ars_tracker_firmware_erase_clicked()));
		connect(button_ars_tracker_shell_send, SIGNAL(clicked()), this,
						SLOT(on_btn_ars_tracker_shell_send_clicked()));
		connect(button_ars_tracker_shell_clear, SIGNAL(clicked()), this,
						SLOT(on_btn_ars_tracker_shell_clear_clicked()));
		connect(button_ars_tracker_device_logs_clear, &QPushButton::clicked, this, [this]() {
				if (text_ars_tracker_device_logs != nullptr)
				{
						text_ars_tracker_device_logs->clear_dat_in();
						text_ars_tracker_device_logs->update_display();
						log_debug() << "ArsTracker device logs cleared";
				}
		});
		connect(ars_tracker_log_monitor_transport, &smp_uart_auterm::non_smp_uart_data_received, this,
						&plugin_mcumgr::append_ars_tracker_device_log);
		connect(edit_ars_tracker_shell_command, &QLineEdit::returnPressed, this,
						&plugin_mcumgr::on_btn_ars_tracker_shell_send_clicked);
		connect(btn_ars_tracker_destination, SIGNAL(clicked()), this,
						SLOT(on_btn_ars_tracker_destination_clicked()));
		connect(combo_ars_tracker_port, &ars_tracker_port_combo_box::popup_about_to_show, this,
						&plugin_mcumgr::refresh_ars_tracker_serial_ports);
		connect(btn_ars_tracker_cancel, SIGNAL(clicked()), this,
						SLOT(on_btn_ars_tracker_cancel_clicked()));
		connect(ars_tracker, &ars_tracker_backend::status_message, this,
						&plugin_mcumgr::ars_tracker_status_message);
		connect(ars_tracker, &ars_tracker_backend::tracker_info_changed, this,
						&plugin_mcumgr::ars_tracker_info_changed);
		connect(ars_tracker, &ars_tracker_backend::tracker_info_loading_changed, this,
						&plugin_mcumgr::ars_tracker_info_loading_changed);
		connect(ars_tracker, &ars_tracker_backend::session_list_ready, this,
						&plugin_mcumgr::ars_tracker_sessions_ready);
		connect(ars_tracker, &ars_tracker_backend::loading_changed, this,
						&plugin_mcumgr::ars_tracker_loading_changed);
		connect(ars_tracker, &ars_tracker_backend::delete_loading_changed, this,
						&plugin_mcumgr::ars_tracker_delete_loading_changed);
		connect(ars_tracker, &ars_tracker_backend::export_loading_changed, this,
						&plugin_mcumgr::ars_tracker_export_loading_changed);
		connect(ars_tracker, &ars_tracker_backend::export_progress_changed, this,
						&plugin_mcumgr::ars_tracker_export_progress_changed);
		connect(ars_tracker, &ars_tracker_backend::export_file_list_changed, this,
						&plugin_mcumgr::ars_tracker_export_file_list_changed);
		connect(ars_tracker, &ars_tracker_backend::export_finished, this,
						&plugin_mcumgr::ars_tracker_export_finished);
		connect(ars_tracker, &ars_tracker_backend::request_file_download, this,
						&plugin_mcumgr::ars_tracker_request_file_download);
		connect(ars_tracker, &ars_tracker_backend::request_cancel_file_download, this,
						&plugin_mcumgr::ars_tracker_request_cancel_file_download);
		connect(ars_tracker, &ars_tracker_backend::request_tracker_info_shell_command, this,
						&plugin_mcumgr::ars_tracker_request_info_shell_command);
		connect(ars_tracker, &ars_tracker_backend::request_cancel_tracker_info_shell_command, this,
						&plugin_mcumgr::ars_tracker_request_cancel_info_shell_command);
		connect(ars_tracker, &ars_tracker_backend::request_tracker_info_image_state, this,
						&plugin_mcumgr::ars_tracker_request_info_image_state);
		connect(ars_tracker, &ars_tracker_backend::request_cancel_tracker_info_image_state, this,
						&plugin_mcumgr::ars_tracker_request_cancel_info_image_state);
		connect(ars_tracker, &ars_tracker_backend::request_session_list_refresh_after_delete, this,
						&plugin_mcumgr::ars_tracker_request_session_refresh_after_delete,
						Qt::QueuedConnection);
		connect(ars_tracker, &ars_tracker_backend::request_file_hash_support, this,
						&plugin_mcumgr::ars_tracker_request_file_hash_support);
		connect(ars_tracker, &ars_tracker_backend::request_file_metadata, this,
						&plugin_mcumgr::ars_tracker_request_file_metadata);
		connect(list_ars_tracker_sessions, SIGNAL(itemSelectionChanged()), this,
						SLOT(on_list_ars_tracker_sessions_itemSelectionChanged()));
		connect(combo_ars_tracker_port,
						QOverload<int>::of(&QComboBox::currentIndexChanged), this,
						[this](int index) {
								if (index < 0)
								{
										return;
								}

								QString port_name =
										combo_ars_tracker_port->itemData(index, Qt::UserRole).toString();
								if (port_name.trimmed().isEmpty() == false)
								{
										set_active_ars_tracker_device(port_name, false);
								}
						});

		btn_ars_tracker_connect->setProperty("auterm_open_close_connect_click", false);
		btn_ars_tracker_connect->setProperty("auterm_open_close_text_style", "connect");
		emit plugin_add_open_close_button(btn_ars_tracker_connect);
		connect(btn_ars_tracker_connect, &QPushButton::clicked, this, [this]() {
				if (ars_tracker_port_scan_active == true)
				{
						return;
				}

				if (ars_tracker_has_connected_devices())
				{
						if (ars_tracker_firmware_upload_active == true ||
								ars_tracker_firmware_erase_active == true)
						{
								lbl_ars_tracker_status->setText(
										"Disconnect from all is not allowed during firmware update/erase.");
								return;
						}

						disconnect_all_ars_tracker_devices();
						ars_tracker_scan_results.clear();
						ars_tracker_auto_info_refresh_pending = false;
						ars_tracker_info_refresh_started_for_current_connection = false;
						ars_tracker_auto_info_refresh_in_progress = false;
						ars_tracker_auto_info_refresh_attempts = 0;
						ars_tracker_serial_transition_active = false;
						ars_tracker_info_changed(ars_tracker_info_t());
						if (list_ars_tracker_sessions != nullptr)
						{
								list_ars_tracker_sessions->clear();
						}
						if (list_ars_tracker_files != nullptr)
						{
								list_ars_tracker_files->clear();
						}
						if (lbl_ars_tracker_progress != nullptr)
						{
								lbl_ars_tracker_progress->clear();
						}
						populate_ars_tracker_serial_ports(QList<ars_tracker_port_scan_result_t>(), QString(),
																				"No ArsTracker devices found");
						lbl_ars_tracker_status->setText("Disconnected from all ArsTracker devices.");
						sync_ars_tracker_serial_controls(ars_tracker_any_loading());
						return;
				}

				ars_tracker_auto_info_refresh_pending = false;
				ars_tracker_info_refresh_started_for_current_connection = false;
				ars_tracker_auto_info_refresh_in_progress = false;
				ars_tracker_auto_info_refresh_attempts = 0;
				start_ars_tracker_port_scan();
		});


		populate_ars_tracker_serial_ports(QList<ars_tracker_port_scan_result_t>(), QString(),
																		"No ArsTracker devices found");
		refresh_ars_tracker_serial_ports();
		set_ars_tracker_controls_loading(false);
		ars_tracker_info_changed(ars_tracker->tracker_info());

		// Use monospace font for shell
		QFont shell_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
		edit_SHELL_Output->setFont(shell_font);

		// Setup font spacing
		QFontMetrics shell_font_metrics(shell_font);
		edit_SHELL_Output->setTabStopDistance(shell_font_metrics.horizontalAdvance(" ") * 8);

		edit_SHELL_Output->setup_scrollback(32);
		edit_SHELL_Output->set_line_mode(true);
		edit_SHELL_Output->set_vt100_mode(VT100_MODE_DECODE);
		text_ars_tracker_shell_output->setFont(shell_font);

		colview_IMG_Images->setModel(&model_image_state);
		colview_IMG_Images->setColumnWidths(QList<int>() << 50 << 50 << 460);

		check_IMG_Preview_Confirmed->setChecked(true);
		check_IMG_Preview_Confirmed->installEventFilter(this);
		check_IMG_Preview_Active->installEventFilter(this);
		check_IMG_Preview_Pending->installEventFilter(this);
		check_IMG_Preview_Bootable->installEventFilter(this);
		check_IMG_Preview_Permanent->installEventFilter(this);
		check_Enum_Group_Additional->installEventFilter(this);

		// Make shell response text edit have a monospace font
		QFont monospace_font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
		monospace_font.setPointSize(8);
		edit_SHELL_Output->setFont(monospace_font);
		text_ars_tracker_shell_output->setFont(monospace_font);
		text_ars_tracker_shell_output->setTabStopDistance(
				shell_font_metrics.horizontalAdvance(" ") * 8);
		text_ars_tracker_shell_output->setup_scrollback(32);
		text_ars_tracker_shell_output->set_line_mode(true);
		text_ars_tracker_shell_output->set_vt100_mode(VT100_MODE_DECODE);
		text_ars_tracker_device_logs->setFont(monospace_font);
		text_ars_tracker_device_logs->setTabStopDistance(
				shell_font_metrics.horizontalAdvance(" ") * 8);
		text_ars_tracker_device_logs->setup_scrollback(32);
		text_ars_tracker_device_logs->set_line_mode(true);
		text_ars_tracker_device_logs->set_vt100_mode(VT100_MODE_DECODE);

#ifndef SKIPPLUGIN_LOGGER
		processor->set_logger(logger);
		uart_transport->set_logger(logger);
		ars_tracker_scan_processor->set_logger(logger);
		ars_tracker_scan_transport->set_logger(logger);
		ars_tracker_log_monitor_transport->set_logger(logger);
		ars_tracker_scan_shell_mgmt->set_logger(logger);

#if defined(PLUGIN_MCUMGR_TRANSPORT_UDP)
		udp_transport->set_logger(logger);
#endif

#if defined(PLUGIN_MCUMGR_TRANSPORT_BLUETOOTH)
		bluetooth_transport->set_logger(logger);
#endif

#if defined(PLUGIN_MCUMGR_TRANSPORT_LORAWAN)
		lora_transport->set_logger(logger);
#endif
		ars_tracker->set_logger(logger);

		smp_groups.fs_mgmt->set_logger(logger);
		smp_groups.img_mgmt->set_logger(logger);
		smp_groups.os_mgmt->set_logger(logger);
		smp_groups.settings_mgmt->set_logger(logger);
		smp_groups.shell_mgmt->set_logger(logger);
		smp_groups.stat_mgmt->set_logger(logger);
		smp_groups.zephyr_mgmt->set_logger(logger);
		smp_groups.enum_mgmt->set_logger(logger);
#endif

		edit_SHELL_Output->set_serial_open(true);
		text_ars_tracker_shell_output->set_serial_open(true);
		text_ars_tracker_device_logs->set_serial_open(true);

		// Setup list of timezones
		QList<QByteArray> items = QTimeZone::availableTimeZoneIds();
		foreach(QByteArray item, items)
		{
				if (item.length() > 3 && (item.left(3) == "Etc" || item.left(3) == "UTC"))
				{
						combo_os_datetime_timezone->addItem(item);
				}
		}

		// Setup widths of enum mgmt table
		table_Enum_List_Details->setColumnWidth(0, 80);
		table_Enum_List_Details->setColumnWidth(1, 300);
		table_Enum_List_Details->setColumnWidth(2, 80);

		// Sort slot info table in ascending (natural) order
		tree_IMG_Slot_Info->sortByColumn(0, Qt::AscendingOrder);

		// Remove non-supported transports
#if !defined(PLUGIN_MCUMGR_TRANSPORT_UDP)
		radio_transport_udp->deleteLater();
#endif

#if !defined(PLUGIN_MCUMGR_TRANSPORT_BLUETOOTH)
		radio_transport_bluetooth->deleteLater();
#endif

#if !defined(PLUGIN_MCUMGR_TRANSPORT_LORAWAN)
		radio_transport_lora->deleteLater();
#endif
}

plugin_mcumgr::~plugin_mcumgr()
{
		// Signals
		disconnect(this, SIGNAL(plugin_set_status(bool, bool, bool*)), parent_window,
							 SLOT(plugin_set_status(bool, bool, bool*)));
		disconnect(this, SIGNAL(plugin_add_open_close_button(QPushButton*)), this,
							 SLOT(plugin_add_open_close_button(QPushButton*)));
		disconnect(this, SIGNAL(plugin_to_hex(QByteArray*)), parent_window,
							 SLOT(plugin_to_hex(QByteArray*)));
		disconnect(this, SIGNAL(plugin_serial_open_close(uint8_t)), parent_window,
							 SLOT(plugin_serial_open_close(uint8_t)));
		disconnect(this, SIGNAL(plugin_serial_state(bool*,bool*)), parent_window,
							 SLOT(plugin_serial_state(bool*,bool*)));
		disconnect(this, SIGNAL(plugin_serial_ports(QStringList*,QString*)), parent_window,
							 SLOT(plugin_serial_ports(QStringList*,QString*)));
		disconnect(this, SIGNAL(plugin_serial_select(QString)), parent_window,
							 SLOT(plugin_serial_select(QString)));
		disconnect(uart_transport, SIGNAL(serial_write(QByteArray*)), parent_window,
							 SLOT(plugin_serial_transmit(QByteArray*)));

		disconnect(parent_window, SIGNAL(plugin_serial_receive(QByteArray*)), this,
							 SLOT(serial_receive(QByteArray*)));
		disconnect(parent_window, SIGNAL(plugin_serial_receive_monitor(QByteArray*)), this,
							 SLOT(serial_receive_monitor(QByteArray*)));
		disconnect(parent_window, SIGNAL(plugin_serial_error(QSerialPort::SerialPortError)), this,
							 SLOT(serial_error(QSerialPort::SerialPortError)));
		disconnect(parent_window, SIGNAL(plugin_serial_bytes_written(qint64)), this,
							 SLOT(serial_bytes_written(qint64)));
		disconnect(parent_window, SIGNAL(plugin_serial_about_to_close()), this,
							 SLOT(serial_about_to_close()));
		disconnect(parent_window, SIGNAL(plugin_serial_opened()), this, SLOT(serial_opened()));
		disconnect(parent_window, SIGNAL(plugin_serial_closed()), this, SLOT(serial_closed()));

		disconnect(processor, SLOT(message_received(smp_message*)));
		disconnect(processor, SLOT(transport_disconnect(int)));

		disconnect(this, SLOT(status(uint8_t, group_status, QString)));
		disconnect(this, SLOT(progress(uint8_t, uint8_t)));
		disconnect(this, SIGNAL(custom_log(bool, QString*)));

		disconnect(this, SLOT(custom_message_callback(custom_message_callback_t, smp_error_t*)));

		// Form signals
		disconnect(this, SLOT(on_btn_FS_Local_clicked()));
		disconnect(this, SLOT(on_btn_FS_Go_clicked()));
		disconnect(this, SLOT(on_radio_FS_Upload_toggled(bool)));
		disconnect(this, SLOT(on_radio_FS_Download_toggled(bool)));
		disconnect(this, SLOT(on_radio_FS_Size_toggled(bool)));
		disconnect(this, SLOT(on_radio_FS_HashChecksum_toggled(bool)));
		disconnect(this, SLOT(on_radio_FS_Hash_Checksum_Types_toggled(bool)));
		disconnect(this, SLOT(on_btn_IMG_Local_clicked()));
		disconnect(this, SLOT(on_btn_IMG_Go_clicked()));
		disconnect(this, SLOT(on_radio_IMG_No_Action_toggled(bool)));
		disconnect(this, SLOT(on_btn_IMG_Preview_Copy_clicked()));
		disconnect(this, SLOT(on_btn_OS_Go_clicked()));
		disconnect(this, SLOT(on_btn_STAT_Go_clicked()));
		disconnect(this, SLOT(on_btn_SHELL_Clear_clicked()));
		disconnect(this, SLOT(on_btn_SHELL_Copy_clicked()));
		disconnect(this, SLOT(on_btn_transport_connect_clicked()));
		disconnect(this, SLOT(on_colview_IMG_Images_updatePreviewWidget(QModelIndex)));
		disconnect(this, SLOT(on_radio_transport_uart_toggled(bool)));
#if defined(PLUGIN_MCUMGR_TRANSPORT_UDP)
		disconnect(this, SLOT(on_radio_transport_udp_toggled(bool)));
#endif
#if defined(PLUGIN_MCUMGR_TRANSPORT_BLUETOOTH)
		disconnect(this, SLOT(on_radio_transport_bluetooth_toggled(bool)));
#endif
#if defined(PLUGIN_MCUMGR_TRANSPORT_LORAWAN)
		disconnect(this, SLOT(on_radio_transport_lora_toggled(bool)));
#endif
		disconnect(this, SLOT(on_radio_OS_Buffer_Info_toggled(bool)));
		disconnect(this, SLOT(on_radio_OS_uname_toggled(bool)));
		disconnect(this, SLOT(on_radio_IMG_Get_toggled(bool)));
		disconnect(this, SLOT(on_radio_IMG_Set_toggled(bool)));
		disconnect(this, SLOT(on_radio_settings_read_toggled(bool)));
		disconnect(this, SLOT(on_radio_settings_write_toggled(bool)));
		disconnect(this, SLOT(on_radio_settings_delete_toggled(bool)));
		disconnect(this, SLOT(on_radio_settings_commit_toggled(bool)));
		disconnect(this, SLOT(on_radio_settings_load_toggled(bool)));
		disconnect(this, SLOT(on_radio_settings_save_toggled(bool)));
		disconnect(this, SLOT(on_btn_settings_go_clicked()));
		disconnect(this, SLOT(on_radio_settings_none_toggled(bool)));
		disconnect(this, SLOT(on_radio_settings_text_toggled(bool)));
		disconnect(this, SLOT(on_radio_settings_decimal_toggled(bool)));
		disconnect(this, SLOT(on_check_settings_big_endian_toggled(bool)));
		disconnect(this, SLOT(on_check_settings_signed_decimal_value_toggled(bool)));
		disconnect(this, SLOT(enter_pressed()));
		disconnect(this, SLOT(on_btn_zephyr_go_clicked()));
		disconnect(this, SLOT(on_check_os_datetime_use_pc_date_time_toggled(bool)));
		disconnect(this, SLOT(on_radio_os_datetime_get_toggled(bool)));
		disconnect(this, SLOT(on_radio_os_datetime_set_toggled(bool)));
		disconnect(this, SLOT(on_btn_enum_go_clicked()));
		disconnect(this, SLOT(on_radio_custom_custom_toggled(bool)));
		disconnect(this, SLOT(on_radio_custom_logging_toggled(bool)));
		disconnect(this, SLOT(on_radio_custom_json_toggled(bool)));
		disconnect(this, SLOT(on_radio_custom_yaml_toggled(bool)));
		disconnect(this, SLOT(on_radio_custom_cbor_toggled(bool)));
		disconnect(this, SLOT(on_btn_custom_copy_send_clicked()));
		disconnect(this, SLOT(on_btn_custom_copy_receive_clicked()));
		disconnect(this, SLOT(on_btn_custom_copy_both_clicked()));
		disconnect(this, SLOT(on_btn_custom_clear_clicked()));
		disconnect(this, SLOT(on_edit_custom_indent_valueChanged(int)));
		disconnect(this, SLOT(on_btn_custom_go_clicked()));
		disconnect(this, SLOT(on_tree_IMG_Slot_Info_itemDoubleClicked(QTreeWidgetItem*, int)));
		disconnect(this, SLOT(on_btn_error_lookup_clicked()));
		disconnect(this, SLOT(on_btn_cancel_clicked()));

		// Clean up GUI
		delete tab_2;

#if defined(PLUGIN_MCUMGR_TRANSPORT_BLUETOOTH)
		delete bluetooth_transport;
#endif

#if defined(PLUGIN_MCUMGR_TRANSPORT_UDP)
		delete udp_transport;
#endif

#if defined(PLUGIN_MCUMGR_TRANSPORT_LORAWAN)
		delete lora_transport;
#endif

		delete error_lookup_form;
		delete smp_groups.enum_mgmt;
		delete smp_groups.zephyr_mgmt;
		delete smp_groups.stat_mgmt;
		delete smp_groups.shell_mgmt;
		delete smp_groups.settings_mgmt;
		delete smp_groups.os_mgmt;
		delete smp_groups.img_mgmt;
		delete smp_groups.fs_mgmt;
		disconnect_all_ars_tracker_devices();
		destroy_ars_tracker_scan_probe_context();
		delete ars_tracker_log_monitor_transport;
		delete processor;
		delete log_json;
		delete uart_transport;

#ifndef SKIPPLUGIN_LOGGER
		delete logger;
#endif
}

bool plugin_mcumgr::eventFilter(QObject *, QEvent *event)
{
		if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease ||
				event->type() == QEvent::MouseButtonDblClick || event->type() == QEvent::KeyPress ||
				event->type() == QEvent::KeyRelease || event->type() == QEvent::InputMethod ||
				event->type() == QEvent::ActivationChange || event->type() == QEvent::ModifiedChange)
		{
				return true;
		}

		return false;
}

const QString plugin_mcumgr::plugin_about()
{
		return "AuTerm MCUmgr plugin\r\nCopyright 2021-2023 Jamie M.\r\n\r\nCan be used to communicate with Zephyr devices with the serial"
#if defined(PLUGIN_MCUMGR_TRANSPORT_UDP)
					 "/UDP"
#endif

#if defined(PLUGIN_MCUMGR_TRANSPORT_BLUETOOTH)
					 "/Bluetooth"
#endif

#if defined(PLUGIN_MCUMGR_TRANSPORT_LORAWAN)
					 "/LoRa"
#endif

					 " MCUmgr transport"
#if defined(PLUGIN_MCUMGR_TRANSPORT_UDP) || defined(PLUGIN_MCUMGR_TRANSPORT_BLUETOOTH) || defined(PLUGIN_MCUMGR_TRANSPORT_LORAWAN)
					 "s"
#endif
					 " enabled.\r\n\r\nUNFINISHED INITIAL TEST USE ONLY, NOT REPRESENTATIVE OF FINAL PRODUCT.\r\n\r\nBuilt using Qt " QT_VERSION_STR;
}

bool plugin_mcumgr::plugin_configuration()
{
		return false;
}

void plugin_mcumgr::serial_error(QSerialPort::SerialPortError serial_error)
{
		log_error() << "Serial error: " << serial_error;
}

void plugin_mcumgr::serial_receive(QByteArray *data)
{
		uart_transport->serial_read(data);
}

void plugin_mcumgr::serial_receive_monitor(QByteArray *data)
{
		if (data == nullptr || ars_tracker_log_monitor_transport == nullptr)
		{
				return;
		}

		if (active_transport() != uart_transport)
		{
				return;
		}

		bool serial_open = false;
		bool serial_opening = false;
		ars_tracker_main_serial_state(&serial_open, &serial_opening);
		if (serial_open == false || serial_opening == true || ars_tracker_has_selected_port() == false)
		{
				return;
		}

		ars_tracker_log_monitor_transport->serial_read(data);
}

void plugin_mcumgr::serial_bytes_written(qint64 bytes)
{
		Q_UNUSED(bytes);
}

void plugin_mcumgr::serial_about_to_close()
{
}

void plugin_mcumgr::serial_opened()
{
		btn_transport_connect->setText("Close");
		ars_tracker_serial_transition_active = false;
		if (ars_tracker_log_monitor_transport != nullptr)
		{
				ars_tracker_log_monitor_transport->reset_state();
		}
		append_ars_tracker_device_log_text(
				QString("===== Connected %1 =====\n").arg(ars_tracker_selected_port_name()));
		sync_ars_tracker_serial_controls(ars_tracker_any_loading());
		if (ars_tracker_auto_info_refresh_pending == true &&
				ars_tracker_info_refresh_started_for_current_connection == false)
		{
				log_debug() << "ArsTracker connected, scheduling automatic tracker info refresh";
				QTimer::singleShot(100, this, [this]() { maybe_auto_refresh_ars_tracker(); });
		}
		else if (ars_tracker_auto_info_refresh_pending == true)
		{
				log_debug() << "ArsTracker automatic tracker info refresh skipped: reason="
										<< "already started for current connection";
		}
}

void plugin_mcumgr::serial_closed()
{
		ars_tracker_serial_transition_active = false;
		ars_tracker_auto_info_refresh_pending = false;
		ars_tracker_info_refresh_started_for_current_connection = false;
		ars_tracker_auto_info_refresh_in_progress = false;
		ars_tracker_auto_info_refresh_attempts = 0;
		ars_tracker_firmware_upload_active = false;
		ars_tracker_firmware_erase_active = false;
		ars_tracker_shell_command_active = false;
		ars_tracker_firmware_refresh_after_erase_pending = false;
		if (ars_tracker_log_monitor_transport != nullptr)
		{
				ars_tracker_log_monitor_transport->reset_state();
		}
		refresh_ars_tracker_serial_ports();
		sync_ars_tracker_serial_controls(ars_tracker_any_loading());
		ars_tracker_info_changed(ars_tracker->tracker_info());

#if defined(PLUGIN_MCUMGR_TRANSPORT_UDP) || defined(PLUGIN_MCUMGR_TRANSPORT_BLUETOOTH) || defined(PLUGIN_MCUMGR_TRANSPORT_LORAWAN)
		if (active_transport() != uart_transport)
		{
				return;
		}
#endif

		switch (mode)
		{
		case ACTION_IMG_UPLOAD:
		case ACTION_IMG_UPLOAD_SET:
		case ACTION_IMG_IMAGE_LIST:
		case ACTION_IMG_IMAGE_SET:
		case ACTION_IMG_IMAGE_ERASE:
		case ACTION_IMG_IMAGE_SLOT_INFO:
		case ACTION_ARS_TRACKER_FIRMWARE_STATE:
		case ACTION_ARS_TRACKER_FIRMWARE_UPLOAD:
		case ACTION_ARS_TRACKER_FIRMWARE_UPLOAD_SET:
		case ACTION_ARS_TRACKER_FIRMWARE_ERASE: {
				smp_groups.img_mgmt->cancel();
				break;
		}

		case ACTION_OS_UPLOAD_RESET:
		case ACTION_ARS_TRACKER_FIRMWARE_RESET:
		case ACTION_OS_ECHO:
		case ACTION_OS_TASK_STATS:
		case ACTION_OS_MEMORY_POOL:
		case ACTION_OS_RESET:
		case ACTION_OS_DATETIME_GET:
		case ACTION_OS_DATETIME_SET:
		case ACTION_OS_MCUMGR_BUFFER:
		case ACTION_OS_OS_APPLICATION_INFO:
		case ACTION_OS_BOOTLOADER_INFO: {
				smp_groups.os_mgmt->cancel();
				break;
		}

		case ACTION_SHELL_EXECUTE: {
				smp_groups.shell_mgmt->cancel();
				break;
		}

		case ACTION_ARS_TRACKER_SHELL_COMMAND: {
				smp_groups.shell_mgmt->cancel();
				break;
		}

		case ACTION_STAT_GROUP_DATA:
		case ACTION_STAT_LIST_GROUPS: {
				smp_groups.stat_mgmt->cancel();
				break;
		}

		case ACTION_FS_UPLOAD:
		case ACTION_FS_DOWNLOAD:
		case ACTION_FS_STATUS:
		case ACTION_FS_HASH_CHECKSUM:
		case ACTION_ARS_TRACKER_EXPORT_HASH_SUPPORT:
		case ACTION_ARS_TRACKER_EXPORT_METADATA:
		case ACTION_ARS_TRACKER_EXPORT_DOWNLOAD:
		case ACTION_FS_SUPPORTED_HASHES_CHECKSUMS: {
				smp_groups.fs_mgmt->cancel();
				break;
		}

		case ACTION_SETTINGS_READ:
		case ACTION_SETTINGS_WRITE:
		case ACTION_SETTINGS_DELETE:
		case ACTION_SETTINGS_COMMIT:
		case ACTION_SETTINGS_LOAD:
		case ACTION_SETTINGS_SAVE: {
				smp_groups.settings_mgmt->cancel();
				break;
		}

		case ACTION_ZEPHYR_STORAGE_ERASE: {
				smp_groups.zephyr_mgmt->cancel();
				break;
		}

		case ACTION_ENUM_COUNT:
		case ACTION_ENUM_LIST:
		case ACTION_ENUM_SINGLE:
		case ACTION_ENUM_DETAILS: {
				smp_groups.enum_mgmt->cancel();
				break;
		}

		default: {
		}
		}

		mode = ACTION_IDLE;
		btn_transport_connect->setText("Open");
		uart_transport_locked = false;
		btn_cancel->setEnabled(false);
}

//Form actions
void plugin_mcumgr::on_btn_FS_Local_clicked()
{
		QString filename;

		if (radio_FS_Upload->isChecked())
		{
				// TODO: load path
				filename = QFileDialog::getOpenFileName(parent_window, "Select source file for transfer",
																								"", "All Files (*)");
		} else
		{
				// TODO: load path
				filename = QFileDialog::getSaveFileName(parent_window, "Select target file for transfer",
																								"", "All Files (*)");
		}

		if (!filename.isEmpty())
		{
				edit_FS_Local->setText(filename);
		}
}

void plugin_mcumgr::on_btn_FS_Go_clicked()
{
		bool started = false;

		if (claim_transport(lbl_FS_Status) == false)
		{
				return;
		}

		if (radio_FS_Upload->isChecked())
		{
				if (edit_FS_Local->text().isEmpty())
				{
						lbl_FS_Status->setText("Error: Local file name is required");
				} else if (edit_FS_Remote->text().isEmpty())
				{
						lbl_FS_Status->setText("Error: Remote file name is required");
				} else if (!QFile(edit_FS_Local->text()).exists())
				{
						lbl_FS_Status->setText("Error: Local file must exist");
				} else
				{
						mode = ACTION_FS_UPLOAD;
						processor->set_transport(active_transport());
						set_group_transport_settings(smp_groups.fs_mgmt);
						started =
								smp_groups.fs_mgmt->start_upload(edit_FS_Local->text(), edit_FS_Remote->text());

						if (started == true)
						{
								lbl_FS_Status->setText("Uploading...");
						}
				}
		} else if (radio_FS_Download->isChecked())
		{
				if (edit_FS_Local->text().isEmpty())
				{
						lbl_FS_Status->setText("Error: Local file name is required");
				} else if (edit_FS_Remote->text().isEmpty())
				{
						lbl_FS_Status->setText("Error: Remote file name is required");
				} else
				{
						mode = ACTION_FS_DOWNLOAD;
						processor->set_transport(active_transport());
						set_group_transport_settings(smp_groups.fs_mgmt);
						started =
								smp_groups.fs_mgmt->start_download(edit_FS_Remote->text(), edit_FS_Local->text());

						if (started == true)
						{
								lbl_FS_Status->setText("Downloading...");
						}
				}
		} else if (radio_FS_Size->isChecked())
		{
				if (edit_FS_Remote->text().isEmpty())
				{
						lbl_FS_Status->setText("Error: Remote file name is required");
				} else
				{
						mode = ACTION_FS_STATUS;
						processor->set_transport(active_transport());
						set_group_transport_settings(smp_groups.fs_mgmt);
						started = smp_groups.fs_mgmt->start_status(edit_FS_Remote->text(), &fs_size_response);

						if (started == true)
						{
								lbl_FS_Status->setText("Statusing...");
						}
				}
		} else if (radio_FS_HashChecksum->isChecked())
		{
				if (edit_FS_Remote->text().isEmpty())
				{
						lbl_FS_Status->setText("Error: Remote file name is required");
				} else if (edit_FS_Remote->text().isEmpty())
				{
						lbl_FS_Status->setText("Error: Hash/checksum type is required");
				} else
				{
						mode = ACTION_FS_HASH_CHECKSUM;
						processor->set_transport(active_transport());
						set_group_transport_settings(smp_groups.fs_mgmt);
						started = smp_groups.fs_mgmt->start_hash_checksum(edit_FS_Remote->text(),
																															combo_FS_type->currentText(),
																															&fs_hash_checksum_response,
																															&fs_size_response);

						if (started == true)
						{
								lbl_FS_Status->setText("Hashing...");
						}
				}
		} else if (radio_FS_Hash_Checksum_Types->isChecked())
		{
				mode = ACTION_FS_SUPPORTED_HASHES_CHECKSUMS;
				processor->set_transport(active_transport());
				set_group_transport_settings(smp_groups.fs_mgmt);
				started =
						smp_groups.fs_mgmt->start_supported_hashes_checksums(&supported_hash_checksum_list);

				if (started == true)
				{
						lbl_FS_Status->setText("Supported...");
				}
		}

		if (started == true)
		{
				progress_FS_Complete->setValue(0);
				btn_cancel->setEnabled(true);
		} else
		{
				relase_transport();
		}
}

void plugin_mcumgr::on_radio_FS_Upload_toggled(bool checked)
{
		if (checked == true)
		{
				edit_FS_Local->setEnabled(true);
				btn_FS_Local->setEnabled(true);
				edit_FS_Remote->setEnabled(true);
				combo_FS_type->setEnabled(false);
				edit_FS_Result->setEnabled(false);
				edit_FS_Size->setEnabled(false);
		}
}

void plugin_mcumgr::on_radio_FS_Download_toggled(bool checked)
{
		if (checked == true)
		{
				edit_FS_Local->setEnabled(true);
				btn_FS_Local->setEnabled(true);
				edit_FS_Remote->setEnabled(true);
				combo_FS_type->setEnabled(false);
				edit_FS_Result->setEnabled(false);
				edit_FS_Size->setEnabled(false);
		}
}

void plugin_mcumgr::on_radio_FS_Size_toggled(bool checked)
{
		if (checked == true)
		{
				edit_FS_Local->setEnabled(false);
				btn_FS_Local->setEnabled(false);
				edit_FS_Remote->setEnabled(true);
				combo_FS_type->setEnabled(false);
				edit_FS_Result->setEnabled(false);
				edit_FS_Size->setEnabled(true);
		}
}

void plugin_mcumgr::on_radio_FS_HashChecksum_toggled(bool checked)
{
		if (checked == true)
		{
				edit_FS_Local->setEnabled(false);
				btn_FS_Local->setEnabled(false);
				edit_FS_Remote->setEnabled(true);
				combo_FS_type->setEnabled(true);
				edit_FS_Result->setEnabled(true);
				edit_FS_Size->setEnabled(true);
		}
}

void plugin_mcumgr::on_radio_FS_Hash_Checksum_Types_toggled(bool checked)
{
		if (checked == true)
		{
				edit_FS_Local->setEnabled(false);
				btn_FS_Local->setEnabled(false);
				edit_FS_Remote->setEnabled(false);
				combo_FS_type->setEnabled(true);
				edit_FS_Result->setEnabled(false);
				edit_FS_Size->setEnabled(false);
		}
}

void plugin_mcumgr::on_btn_IMG_Local_clicked()
{
		QString strFilename = QFileDialog::getOpenFileName(parent_window, tr("Open firmware file"),
																											 edit_IMG_Local->text(),
																											 tr("Binary Files (*.bin);;All Files (*)"));

		if (!strFilename.isEmpty())
		{
				edit_IMG_Local->setText(strFilename);
		}
}

void plugin_mcumgr::on_btn_IMG_Go_clicked()
{
		bool started = false;

		if (claim_transport(lbl_IMG_Status) == false)
		{
				return;
		}

		if (selector_img->currentWidget() == tab_IMG_Upload)
		{
				// Upload
				if (edit_IMG_Local->text().isEmpty())
				{
						lbl_IMG_Status->setText("Error: No file provided");
				} else if (!QFile(edit_IMG_Local->text()).exists())
				{
						lbl_IMG_Status->setText("Error: File does not exist");
				} else
				{
						mode = ACTION_IMG_UPLOAD;
						processor->set_transport(active_transport());
						set_group_transport_settings(smp_groups.img_mgmt);
						started = smp_groups.img_mgmt->start_firmware_update(edit_IMG_Image->value(),
																																 edit_IMG_Local->text(), false,
																																 &upload_hash, timeout_erase_ms);

						if (started == true)
						{
								lbl_IMG_Status->setText("Uploading...");
						}
				}
		} else if (selector_img->currentWidget() == tab_IMG_Images)
		{
				// Image list
				if (radio_IMG_Get->isChecked())
				{
						colview_IMG_Images->previewWidget()->hide();
						model_image_state.clear();
						images_list.clear();
						mode = ACTION_IMG_IMAGE_LIST;
						processor->set_transport(active_transport());
						set_group_transport_settings(smp_groups.img_mgmt);
						started = smp_groups.img_mgmt->start_image_get(&images_list);

						if (started == true)
						{
								lbl_IMG_Status->setText("Querying...");
						}
				} else
				{
						if (colview_IMG_Images->currentIndex().isValid() &&
								colview_IMG_Images->currentIndex().parent().isValid())
						{
								uint8_t i = model_image_state.itemFromIndex(colview_IMG_Images->currentIndex())
																->parent()
																->data()
																.toUInt();
								uint8_t l = model_image_state.itemFromIndex(colview_IMG_Images->currentIndex())
																->data()
																.toUInt();

								if (images_list.length() > i && images_list[i].slot_list.length() > l)
								{
										mode = ACTION_IMG_IMAGE_SET;
										processor->set_transport(active_transport());
										set_group_transport_settings(smp_groups.img_mgmt);

										parent_row    = colview_IMG_Images->currentIndex().parent().row();
										parent_column = colview_IMG_Images->currentIndex().parent().column();
										child_row     = colview_IMG_Images->currentIndex().row();
										child_column  = colview_IMG_Images->currentIndex().column();
										started =
												smp_groups.img_mgmt->start_image_set(&images_list[i].slot_list[l].hash,
																														 check_IMG_Confirm->isChecked(),
																														 &images_list);

										if (started == true)
										{
												lbl_IMG_Status->setText("Setting...");
										}
								} else
								{
										lbl_IMG_Status->setText("Could not find item bounds");
								}
						} else
						{
								lbl_IMG_Status->setText("Invalid selection");
						}
				}
		} else if (selector_img->currentWidget() == tab_IMG_Erase)
		{
				// Erase
				mode = ACTION_IMG_IMAGE_ERASE;
				processor->set_transport(active_transport());
				set_group_transport_settings(smp_groups.img_mgmt, timeout_erase_ms);
				started = smp_groups.img_mgmt->start_image_erase(edit_IMG_Erase_Slot->value());

				if (started == true)
				{
						lbl_IMG_Status->setText("Erasing...");
				}
		} else if (selector_img->currentWidget() == tab_IMG_Slots)
		{
				// Slot info
				mode = ACTION_IMG_IMAGE_SLOT_INFO;
				processor->set_transport(active_transport());
				set_group_transport_settings(smp_groups.img_mgmt);
				started = smp_groups.img_mgmt->start_image_slot_info(&img_slot_details);

				if (started == true)
				{
						lbl_IMG_Status->setText("Fetching slot info...");
				}
		}

		if (started == true)
		{
				progress_IMG_Complete->setValue(0);
				btn_cancel->setEnabled(true);
		} else
		{
				relase_transport();
		}
}

void plugin_mcumgr::on_radio_IMG_No_Action_toggled(bool checked)
{
}

void plugin_mcumgr::on_btn_IMG_Preview_Copy_clicked()
{
}

void plugin_mcumgr::on_btn_OS_Go_clicked()
{
		bool started = false;

		if (claim_transport(lbl_OS_Status) == false)
		{
				return;
		}

		if (selector_OS->currentWidget() == tab_OS_Echo)
		{
				if (edit_OS_Echo_Input->toPlainText().isEmpty())
				{
						lbl_OS_Status->setText("Error: No text to echo");
				} else
				{
						edit_OS_Echo_Output->clear();
						mode = ACTION_OS_ECHO;
						processor->set_transport(active_transport());
						set_group_transport_settings(smp_groups.os_mgmt);
						started = smp_groups.os_mgmt->start_echo(edit_OS_Echo_Input->toPlainText());

						if (started == true)
						{
								lbl_OS_Status->setText("Echo command sent...");
						}
				}
		} else if (selector_OS->currentWidget() == tab_OS_Tasks)
		{
				mode = ACTION_OS_TASK_STATS;
				processor->set_transport(active_transport());
				set_group_transport_settings(smp_groups.os_mgmt);
				started = smp_groups.os_mgmt->start_task_stats(&task_list);

				if (started == true)
				{
						lbl_OS_Status->setText("Task list command sent...");
				}
		} else if (selector_OS->currentWidget() == tab_OS_Memory)
		{
				mode = ACTION_OS_MEMORY_POOL;
				processor->set_transport(active_transport());
				set_group_transport_settings(smp_groups.os_mgmt);
				started = smp_groups.os_mgmt->start_memory_pool(&memory_list);

				if (started == true)
				{
						lbl_OS_Status->setText("Memory pool list command sent...");
				}
		} else if (selector_OS->currentWidget() == tab_OS_Reset)
		{
				mode = ACTION_OS_RESET;
				processor->set_transport(active_transport());
				set_group_transport_settings(smp_groups.os_mgmt);
				started = smp_groups.os_mgmt->start_reset(check_OS_Force_Reboot->isChecked(),
																									edit_os_boot_mode->value());

				if (started == true)
				{
						lbl_OS_Status->setText("Reset command...");
				}
		} else if (selector_OS->currentWidget() == tab_os_datetime)
		{
				processor->set_transport(active_transport());

				if (radio_os_datetime_get->isChecked())
				{
						mode = ACTION_OS_DATETIME_GET;

						set_group_transport_settings(smp_groups.os_mgmt);
						started = smp_groups.os_mgmt->start_date_time_get(&rtc_time_date_response);

						if (started == true)
						{
								lbl_OS_Status->setText("RTC get...");
						}
				} else if (radio_os_datetime_set->isChecked())
				{
						QDateTime date_time;
						mode = ACTION_OS_DATETIME_SET;

						if (check_os_datetime_use_pc_date_time->isChecked())
						{
								date_time = QDateTime::currentDateTime();
								date_time.setTimeZone(date_time.timeZone());
						} else
						{
								date_time = edit_os_datetime_date_time->dateTime();
								date_time.setTimeZone(
										QTimeZone(combo_os_datetime_timezone->currentText().toUtf8()));
						}

						set_group_transport_settings(smp_groups.os_mgmt);
						started = smp_groups.os_mgmt->start_date_time_set(date_time);

						if (started == true)
						{
								lbl_OS_Status->setText("RTC set...");
						}
				}
		} else if (selector_OS->currentWidget() == tab_OS_Info)
		{
				if (radio_OS_uname->isChecked())
				{
						// uname
						mode = ACTION_OS_OS_APPLICATION_INFO;
						processor->set_transport(active_transport());
						set_group_transport_settings(smp_groups.os_mgmt);
						started = smp_groups.os_mgmt->start_os_application_info(edit_OS_UName->text(),
																																		&os_info_response);

						if (started == true)
						{
								lbl_OS_Status->setText("uname command sent...");
						}
				} else
				{
						// Buffer details
						mode = ACTION_OS_MCUMGR_BUFFER;
						processor->set_transport(active_transport());
						set_group_transport_settings(smp_groups.os_mgmt);
						started =
								smp_groups.os_mgmt->start_mcumgr_parameters(&os_buffer_size, &os_buffer_count);

						if (started == true)
						{
								lbl_OS_Status->setText("MCUmgr buffer command sent...");
						}
				}
		} else if (selector_OS->currentWidget() == tab_OS_Bootloader)
		{
				// bootloader info
				mode = ACTION_OS_BOOTLOADER_INFO;
				processor->set_transport(active_transport());
				set_group_transport_settings(smp_groups.os_mgmt);
				started = smp_groups.os_mgmt->start_bootloader_info(edit_os_bootloader_query->text(),
																														&bootloader_info_response);

				if (started == true)
				{
						lbl_OS_Status->setText("bootloader info command sent...");
				}
		}

		if (started == true)
		{
				btn_cancel->setEnabled(true);
		} else
		{
				relase_transport();
		}
}

void plugin_mcumgr::on_btn_STAT_Go_clicked()
{
		bool started = false;

		if (claim_transport(lbl_STAT_Status) == false)
		{
				return;
		}

		if (radio_STAT_List->isChecked())
		{
				// Execute stat list command
				mode = ACTION_STAT_LIST_GROUPS;
				processor->set_transport(active_transport());
				set_group_transport_settings(smp_groups.stat_mgmt);
				started = smp_groups.stat_mgmt->start_list_groups(&group_list);

				if (started == true)
				{
						lbl_STAT_Status->setText("Stat list command sent...");
				}
		} else if (radio_STAT_Fetch->isChecked())
		{
				// Execute stat get command
				if (combo_STAT_Group->currentText().isEmpty())
				{
						lbl_STAT_Status->setText("Error: No group name provided");
				} else
				{
						mode = ACTION_STAT_GROUP_DATA;
						processor->set_transport(active_transport());
						set_group_transport_settings(smp_groups.stat_mgmt);
						started =
								smp_groups.stat_mgmt->start_group_data(combo_STAT_Group->currentText(), &stat_list);

						if (started == true)
						{
								lbl_STAT_Status->setText("Stat get command sent...");
						}
				}
		}

		if (started == true)
		{
				btn_cancel->setEnabled(true);
		} else
		{
				relase_transport();
		}
}

void plugin_mcumgr::on_btn_SHELL_Clear_clicked()
{
		edit_SHELL_Output->clear_dat_in();
}

void plugin_mcumgr::on_btn_SHELL_Copy_clicked()
{
		QApplication::clipboard()->setText(edit_SHELL_Output->toPlainText());
}

void plugin_mcumgr::on_colview_IMG_Images_updatePreviewWidget(const QModelIndex &index)
{
		uint8_t i = model_image_state.itemFromIndex(index)->parent()->data().toUInt();
		uint8_t l = model_image_state.itemFromIndex(index)->data().toUInt();

		if (images_list.length() > i && images_list[i].slot_list.length() > l)
		{
				QByteArray escaped_hash = images_list[i].slot_list[l].hash;

				emit plugin_to_hex(&escaped_hash);
				edit_IMG_Preview_Hash->setText(escaped_hash);
				edit_IMG_Preview_Version->setText(images_list[i].slot_list[l].version);
				check_IMG_Preview_Active->setChecked(images_list[i].slot_list[l].active);
				check_IMG_Preview_Bootable->setChecked(images_list[i].slot_list[l].bootable);
				check_IMG_Preview_Confirmed->setChecked(images_list[i].slot_list[l].confirmed);
				check_IMG_Preview_Pending->setChecked(images_list[i].slot_list[l].pending);
				check_IMG_Preview_Permanent->setChecked(images_list[i].slot_list[l].permanent);

				if (colview_IMG_Images->previewWidget() != verticalLayoutWidget)
				{
						colview_IMG_Images->setPreviewWidget(verticalLayoutWidget);
				} else
				{
						colview_IMG_Images->previewWidget()->show();
				}
		} else
		{
				lbl_IMG_Status->setText("Could not find item bounds");
		}
}

void plugin_mcumgr::status(uint8_t user_data, group_status status, QString error_string)
{
		/*
		 *     STATUS_COMPLETE = 0,
		STATUS_ERROR,
		STATUS_TIMEOUT,
		STATUS_CANCELLED
*/

		QLabel* label_status      = nullptr;
		bool    finished          = true;
		bool    skip_error_string = false;
		bool    ars_tracker_img_action =
				(user_data == ACTION_ARS_TRACKER_FIRMWARE_STATE ||
				 user_data == ACTION_ARS_TRACKER_FIRMWARE_UPLOAD ||
				 user_data == ACTION_ARS_TRACKER_FIRMWARE_UPLOAD_SET ||
				 user_data == ACTION_ARS_TRACKER_FIRMWARE_ERASE);
		bool ars_tracker_os_action = (user_data == ACTION_ARS_TRACKER_FIRMWARE_RESET);

		log_debug() << "Status: " << status;

		if (sender() == smp_groups.img_mgmt)
		{
				log_debug() << "img sender";
				label_status = ars_tracker_img_action ? lbl_ars_tracker_status : lbl_IMG_Status;

				if (status == STATUS_COMPLETE)
				{
						log_debug() << "complete";

						// Advance to next stage of image upload
						if (user_data == ACTION_ARS_TRACKER_FIRMWARE_STATE)
						{
								for (int i = 0; i < ars_tracker_image_state_list.length(); ++i)
								{
										log_debug() << "ArsTracker image state image index=" << i
																<< "imageSet="
																<< ars_tracker_image_state_list.at(i).image_set
																<< "image=" << ars_tracker_image_state_list.at(i).image;
										for (int l = 0; l < ars_tracker_image_state_list.at(i).slot_list.length();
												 ++l)
										{
												const slot_state_t &slot =
														ars_tracker_image_state_list.at(i).slot_list.at(l);
												log_debug() << "ArsTracker image state slot=" << slot.slot
																		<< "version=" << QString::fromUtf8(slot.version)
																		<< "bootable=" << slot.bootable
																		<< "pending=" << slot.pending
																		<< "confirmed=" << slot.confirmed
																		<< "active=" << slot.active
																		<< "permanent=" << slot.permanent;
										}
								}
								ars_tracker->handle_tracker_firmware_state_response(
										status, QString(), ars_tracker_image_state_list);
								finished = !ars_tracker_info_loading;
								skip_error_string = true;
						}
						else if (user_data == ACTION_ARS_TRACKER_FIRMWARE_UPLOAD)
						{
								log_debug() << "ArsTracker firmware upload completed, hash="
														<< ars_tracker_firmware_upload_hash;
								lbl_ars_tracker_progress->setText("Firmware upload: 100%");
								finished = false;
								mode = ACTION_ARS_TRACKER_FIRMWARE_UPLOAD_SET;
								processor->set_transport(active_transport());
								set_group_transport_settings(smp_groups.img_mgmt);
								bool started = smp_groups.img_mgmt->start_image_set(
										&ars_tracker_firmware_upload_hash, true, nullptr);

								if (started == true)
								{
										log_debug() << "ArsTracker firmware image confirm/test step started";
										lbl_ars_tracker_status->setText("Confirming uploaded firmware...");
								}
								else
								{
										finished = true;
										error_string = "Firmware upload failed: could not start image state step.";
										ars_tracker_firmware_upload_active = false;
								}
						}
						else if (user_data == ACTION_ARS_TRACKER_FIRMWARE_UPLOAD_SET)
						{
								log_debug() << "ArsTracker firmware image confirm/test step completed";
								lbl_ars_tracker_progress->setText("Image confirm/test step completed");
								finished = false;
								mode = ACTION_ARS_TRACKER_FIRMWARE_RESET;
								processor->set_transport(active_transport());
								set_group_transport_settings(smp_groups.os_mgmt);
								log_debug() << "ArsTracker firmware reset after upload requested";
								bool started = smp_groups.os_mgmt->start_reset(false, 0);

								if (started == true)
								{
										lbl_ars_tracker_status->setText("Resetting after firmware upload...");
								}
								else
								{
										finished = true;
										error_string =
												"Firmware upload completed, but failed to send reset.";
										ars_tracker_firmware_upload_active = false;
								}
						}
						else if (user_data == ACTION_ARS_TRACKER_FIRMWARE_ERASE)
						{
								log_debug() << "ArsTracker firmware erase second slot completed";
								lbl_ars_tracker_progress->setText("Second slot erased");
								error_string = "Second slot erased";
								ars_tracker_firmware_erase_active = false;
								ars_tracker_firmware_refresh_after_erase_pending = true;
						}
						else if (user_data == ACTION_IMG_UPLOAD)
						{
								log_debug() << "is upload";

								if (radio_IMG_Test->isChecked() || radio_IMG_Confirm->isChecked())
								{
										// Mark image for test or confirmation
										finished = false;

										mode = ACTION_IMG_UPLOAD_SET;
										processor->set_transport(active_transport());
										set_group_transport_settings(smp_groups.img_mgmt);
										bool started = smp_groups.img_mgmt->start_image_set(
												&upload_hash, (radio_IMG_Confirm->isChecked() ? true : false), nullptr);
										// todo: check status

										log_debug() << "do upload of " << upload_hash;
								}
						}
						else if (user_data == ACTION_IMG_UPLOAD_SET)
						{
								if (check_IMG_Reset->isChecked())
								{
										// Reboot device
										finished = false;

										mode = ACTION_OS_UPLOAD_RESET;
										processor->set_transport(active_transport());
										set_group_transport_settings(smp_groups.os_mgmt);
										bool started = smp_groups.os_mgmt->start_reset(false, 0);
										// todo: check status

										log_debug() << "do reset";
								}
						}
						else if (user_data == ACTION_IMG_IMAGE_LIST)
						{
								update_img_state_table();
						}
						else if (user_data == ACTION_IMG_IMAGE_SET)
						{
								if (parent_row != -1 && parent_column != -1 && child_row != -1 &&
										child_column != -1)
								{
										model_image_state.clear();
										update_img_state_table();

										if (model_image_state.hasIndex(parent_row, parent_column) == true &&
												model_image_state
																.index(child_row, child_column,
																			 model_image_state.index(parent_row, parent_column))
																.isValid() == true)
										{
												colview_IMG_Images->setCurrentIndex(model_image_state.index(
														child_row, child_column,
														model_image_state.index(parent_row, parent_column)));
										} else
										{
												colview_IMG_Images->previewWidget()->hide();
										}

										parent_row    = -1;
										parent_column = -1;
										child_row     = -1;
										child_column  = -1;
								} else
								{
										colview_IMG_Images->previewWidget()->hide();
								}
						}
						else if (user_data == ACTION_IMG_IMAGE_SLOT_INFO)
						{
								uint16_t i = 0;

								tree_IMG_Slot_Info->clear();

								while (i < img_slot_details.length())
								{
										uint16_t         l = 0;
										QStringList      list_item_text;
										QTreeWidgetItem* row_image;
										QString          field_size;

										list_item_text
												<< QString("Image ").append(QString::number(img_slot_details.at(i).image));

										if (img_slot_details.at(i).max_image_size_present == true)
										{
												size_abbreviation(img_slot_details.at(i).max_image_size, &field_size);
												list_item_text << field_size;
										}

										row_image = new QTreeWidgetItem((QTreeWidget*)nullptr, list_item_text);

										while (l < img_slot_details.at(i).slot_data.length())
										{
												QTreeWidgetItem* row_slot;

												list_item_text.clear();
												list_item_text << QString("Slot ").append(
														QString::number(img_slot_details.at(i).slot_data.at(l).slot));

												if (img_slot_details.at(i).slot_data.at(l).size_present)
												{
														field_size.clear();
														size_abbreviation(img_slot_details.at(i).slot_data.at(l).size,
																							&field_size);
														list_item_text << field_size;
												}

												if (img_slot_details.at(i).slot_data.at(l).upload_image_id_present)
												{
														list_item_text << QString::number(
																img_slot_details.at(i).slot_data.at(l).upload_image_id);
												}

												row_slot = new QTreeWidgetItem(row_image, list_item_text);

												++l;
										}

										tree_IMG_Slot_Info->addTopLevelItem(row_image);

										++i;
								}

								// Expand all entries
								tree_IMG_Slot_Info->expandAll();
						}
				}
				else if (status == STATUS_UNSUPPORTED)
				{
						log_debug() << "unsupported";

						// Advance to next stage of image upload, this is likely to occur in MCUboot serial
						// recovery whereby the image state functionality is not included
						if (user_data == ACTION_ARS_TRACKER_FIRMWARE_UPLOAD_SET)
						{
								skip_error_string = true;
								finished = false;
								mode = ACTION_ARS_TRACKER_FIRMWARE_RESET;
								processor->set_transport(active_transport());
								set_group_transport_settings(smp_groups.os_mgmt);
								log_debug() << "ArsTracker firmware reset after upload requested";
								bool started = smp_groups.os_mgmt->start_reset(false, 0);

								if (started == true)
								{
										lbl_ars_tracker_status->setText("Resetting after firmware upload...");
								}
								else
								{
										finished = true;
										error_string =
												"Firmware upload completed, but image state command is unsupported and reset failed to start.";
										ars_tracker_firmware_upload_active = false;
										lbl_ars_tracker_status->setText(error_string);
								}
						}
						else if (user_data == ACTION_IMG_UPLOAD_SET)
						{
								skip_error_string = true;

								if (check_IMG_Reset->isChecked())
								{
										// Reboot device
										finished = false;

										mode = ACTION_OS_UPLOAD_RESET;
										processor->set_transport(active_transport());
										set_group_transport_settings(smp_groups.os_mgmt);
										bool started = smp_groups.os_mgmt->start_reset(false, 0);
										// todo: check status

										log_debug() << "do reset";

										lbl_IMG_Status->setText("Resetting...");
								} else
								{
										lbl_IMG_Status->setText(
												"Upload finished, set image state failed: command not supported (likely MCUboot serial recovery)");
								}
						}
				}
				else if (ars_tracker_img_action)
				{
						if (user_data == ACTION_ARS_TRACKER_FIRMWARE_STATE)
						{
								ars_tracker->handle_tracker_firmware_state_response(
										status, error_string, ars_tracker_image_state_list);
								finished = !ars_tracker_info_loading;
								skip_error_string = true;
						}
						else
						{
								ars_tracker_firmware_upload_active = false;
								ars_tracker_firmware_erase_active = false;
						}
				}
		} else if (sender() == smp_groups.os_mgmt)
		{
				log_debug() << "os sender";
				label_status = ars_tracker_os_action ? lbl_ars_tracker_status : lbl_OS_Status;

				if (status == STATUS_COMPLETE)
				{
						log_debug() << "complete";

						if (user_data == ACTION_OS_ECHO)
						{
								edit_OS_Echo_Output->appendPlainText(error_string);
								error_string = nullptr;
						} else if (user_data == ACTION_ARS_TRACKER_FIRMWARE_RESET)
						{
								log_debug() << "ArsTracker firmware reset command sent";
								error_string = "Reset command sent";
								ars_tracker_firmware_upload_active = false;
						}
						else if (user_data == ACTION_OS_UPLOAD_RESET)
						{
						} else if (user_data == ACTION_OS_RESET)
						{
						} else if (user_data == ACTION_OS_MEMORY_POOL)
						{
								uint16_t i = 0;
								uint16_t l = table_OS_Memory->rowCount();

								table_OS_Memory->setSortingEnabled(false);

								while (i < memory_list.length())
								{
										if (i >= l)
										{
												table_OS_Memory->insertRow(i);

												QTableWidgetItem* row_name = new QTableWidgetItem(memory_list[i].name);
												QTableWidgetItem* row_size = new QTableWidgetItem(
														QString::number(memory_list[i].blocks * memory_list[i].size));
												QTableWidgetItem* row_free = new QTableWidgetItem(
														QString::number(memory_list[i].free * memory_list[i].size));
												QTableWidgetItem* row_minimum = new QTableWidgetItem(
														QString::number(memory_list[i].minimum * memory_list[i].size));

												table_OS_Memory->setItem(i, 0, row_name);
												table_OS_Memory->setItem(i, 1, row_size);
												table_OS_Memory->setItem(i, 2, row_free);
												table_OS_Memory->setItem(i, 3, row_minimum);
										} else
										{
												table_OS_Memory->item(i, 0)->setText(memory_list[i].name);
												table_OS_Memory->item(i, 1)->setText(
														QString::number(memory_list[i].blocks * memory_list[i].size));
												table_OS_Memory->item(i, 2)->setText(
														QString::number(memory_list[i].free * memory_list[i].size));
												table_OS_Memory->item(i, 3)->setText(
														QString::number(memory_list[i].minimum * memory_list[i].size));
										}

										++i;
								}

								while (i < l)
								{
										table_OS_Memory->removeRow((table_OS_Memory->rowCount() - 1));
										++i;
								}

								table_OS_Memory->setSortingEnabled(true);
						} else if (user_data == ACTION_OS_TASK_STATS)
						{
								uint16_t i = 0;
								uint16_t l = table_OS_Tasks->rowCount();

								table_OS_Tasks->setSortingEnabled(false);

								while (i < task_list.length())
								{
										if (i >= l)
										{
												table_OS_Tasks->insertRow(i);

												QTableWidgetItem* row_name = new QTableWidgetItem(task_list[i].name);
												QTableWidgetItem* row_id =
														new QTableWidgetItem(QString::number(task_list[i].id));
												QTableWidgetItem* row_priority =
														new QTableWidgetItem(QString::number(task_list[i].priority));
												QTableWidgetItem* row_state =
														new QTableWidgetItem(QString::number(task_list[i].state));
												QTableWidgetItem* row_context_switches =
														new QTableWidgetItem(QString::number(task_list[i].context_switches));
												QTableWidgetItem* row_runtime =
														new QTableWidgetItem(QString::number(task_list[i].runtime));
												QTableWidgetItem* row_stack_size =
														new QTableWidgetItem(QString::number(task_list[i].stack_size * 4));
												QTableWidgetItem* row_stack_usage =
														new QTableWidgetItem(QString::number(task_list[i].stack_usage * 4));

												table_OS_Tasks->setItem(i, 0, row_name);
												table_OS_Tasks->setItem(i, 1, row_id);
												table_OS_Tasks->setItem(i, 2, row_priority);
												table_OS_Tasks->setItem(i, 3, row_state);
												table_OS_Tasks->setItem(i, 4, row_context_switches);
												table_OS_Tasks->setItem(i, 5, row_runtime);
												table_OS_Tasks->setItem(i, 6, row_stack_size);
												table_OS_Tasks->setItem(i, 7, row_stack_usage);
										} else
										{
												table_OS_Tasks->item(i, 0)->setText(task_list[i].name);
												table_OS_Tasks->item(i, 1)->setText(QString::number(task_list[i].id));
												table_OS_Tasks->item(i, 2)->setText(QString::number(task_list[i].priority));
												table_OS_Tasks->item(i, 3)->setText(QString::number(task_list[i].state));
												table_OS_Tasks->item(i, 4)->setText(
														QString::number(task_list[i].context_switches));
												table_OS_Tasks->item(i, 5)->setText(QString::number(task_list[i].runtime));
												table_OS_Tasks->item(i, 6)->setText(
														QString::number(task_list[i].stack_size * sizeof(uint32_t)));
												table_OS_Tasks->item(i, 7)->setText(
														QString::number(task_list[i].stack_usage * sizeof(uint32_t)));
										}

										++i;
								}

								while (i < l)
								{
										table_OS_Tasks->removeRow((table_OS_Tasks->rowCount() - 1));
										++i;
								}

								table_OS_Tasks->setSortingEnabled(true);
						} else if (user_data == ACTION_OS_MCUMGR_BUFFER)
						{
								edit_OS_Info_Output->clear();
								edit_OS_Info_Output->appendPlainText(
										QString::number(os_buffer_count) % " buffers of " %
										QString::number(os_buffer_size) % " bytes each");
								error_string = nullptr;
						} else if (user_data == ACTION_OS_OS_APPLICATION_INFO)
						{
								edit_OS_Info_Output->clear();
								edit_OS_Info_Output->appendPlainText(os_info_response);
								error_string = nullptr;
						} else if (user_data == ACTION_OS_BOOTLOADER_INFO)
						{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
								switch (bootloader_info_response.typeId())
#else
								switch (bootloader_info_response.type())
#endif
								{
								case QMetaType::Bool: {
										edit_os_bootloader_response->setText(
												bootloader_info_response.toBool() == true ? "True" : "False");
										break;
								}

								case QMetaType::Int: {
										edit_os_bootloader_response->setText(
												QString::number(bootloader_info_response.toInt()));
										break;
								}

								case QMetaType::LongLong: {
										edit_os_bootloader_response->setText(
												QString::number(bootloader_info_response.toLongLong()));
										break;
								}

								case QMetaType::UInt: {
										edit_os_bootloader_response->setText(
												QString::number(bootloader_info_response.toUInt()));
										break;
								}

								case QMetaType::ULongLong: {
										edit_os_bootloader_response->setText(
												QString::number(bootloader_info_response.toULongLong()));
										break;
								}

								case QMetaType::Double: {
										edit_os_bootloader_response->setText(
												QString::number(bootloader_info_response.toDouble()));
										break;
								}

								case QMetaType::QString: {
										edit_os_bootloader_response->setText(bootloader_info_response.toString());
										break;
								}

								default: {
										error_string = "Invalid";
								}
								}
						} else if (user_data == ACTION_OS_DATETIME_GET)
						{
								int index;
								log_debug() << "RTC response: " << rtc_time_date_response;

								rtc_time_date_response.setTimeZone(rtc_time_date_response.timeZone());
								index = combo_os_datetime_timezone->findText(
										rtc_time_date_response.timeZone().displayName(rtc_time_date_response,
																																	QTimeZone::OffsetName));

								if (index >= 0)
								{
										combo_os_datetime_timezone->setCurrentIndex(index);
								}

								edit_os_datetime_date_time->setDateTime(rtc_time_date_response);
						} else if (user_data == ACTION_OS_DATETIME_SET)
						{
						}
				}
		} else if (sender() == smp_groups.shell_mgmt)
		{
				log_debug() << "shell sender";
				label_status = ((user_data == ACTION_ARS_TRACKER_INFO_REFRESH ||
												 user_data == ACTION_ARS_TRACKER_SESSION_LIST ||
												 user_data == ACTION_ARS_TRACKER_DELETE_SESSION ||
												 user_data == ACTION_ARS_TRACKER_SHELL_COMMAND) ?
														lbl_ars_tracker_status :
														lbl_SHELL_Status);

				if (status == STATUS_COMPLETE)
				{
						log_debug() << "complete";

						if (user_data == ACTION_SHELL_EXECUTE)
						{
								edit_SHELL_Output->add_dat_in_text(error_string.toUtf8());

								if (shell_rc == 0)
								{
										error_string = nullptr;
								} else
								{
										error_string =
												QString("Finished, error (ret): ").append(QString::number(shell_rc));
								}
						} else if (user_data == ACTION_ARS_TRACKER_SHELL_COMMAND)
						{
								if (error_string.isEmpty() == false)
								{
										log_debug() << "ArsTracker shell response received:" << error_string;
										append_ars_tracker_shell_output(error_string);
								}

								ars_tracker_shell_command_active = false;
								set_ars_tracker_controls_loading(ars_tracker_any_loading());

								if (ars_tracker_shell_command_rc == 0)
								{
										error_string = nullptr;
								}
								else
								{
										error_string = QString("Finished, error (ret): ")
																 .append(QString::number(ars_tracker_shell_command_rc));
										append_ars_tracker_shell_output(QString("Error: %1").arg(error_string));
								}
						} else if (user_data == ACTION_ARS_TRACKER_INFO_REFRESH ||
											 user_data == ACTION_ARS_TRACKER_SESSION_LIST ||
											 user_data == ACTION_ARS_TRACKER_DELETE_SESSION)
						{
								handle_ars_tracker_shell_status(user_data, status, &error_string);
								if (user_data == ACTION_ARS_TRACKER_INFO_REFRESH)
								{
										finished = !ars_tracker_info_loading;
								}
								skip_error_string = true;
						}
				} else if (user_data == ACTION_ARS_TRACKER_SHELL_COMMAND)
				{
						ars_tracker_shell_command_active = false;
						set_ars_tracker_controls_loading(ars_tracker_any_loading());

						QString shell_error = error_string;
						if (shell_error.isEmpty())
						{
								if (status == STATUS_TIMEOUT)
								{
										shell_error = "Command timed out";
								}
								else if (status == STATUS_CANCELLED)
								{
										shell_error = "Cancelled";
								}
								else
								{
										shell_error = "Shell command failed";
								}
						}

						log_debug() << "ArsTracker shell command failed:" << shell_error;
						append_ars_tracker_shell_output(QString("Error: %1").arg(shell_error));
				} else if (user_data == ACTION_ARS_TRACKER_INFO_REFRESH ||
									 user_data == ACTION_ARS_TRACKER_SESSION_LIST ||
									 user_data == ACTION_ARS_TRACKER_DELETE_SESSION)
				{
						handle_ars_tracker_shell_status(user_data, status, &error_string);
						if (user_data == ACTION_ARS_TRACKER_INFO_REFRESH)
						{
								finished = !ars_tracker_info_loading;
						}
						skip_error_string = true;
				}
		} else if (sender() == smp_groups.stat_mgmt)
		{
				log_debug() << "stat sender";
				label_status = lbl_STAT_Status;

				if (status == STATUS_COMPLETE)
				{
						log_debug() << "complete";

						if (user_data == ACTION_STAT_GROUP_DATA)
						{
								uint16_t i = 0;
								uint16_t l = table_STAT_Values->rowCount();

								table_STAT_Values->setSortingEnabled(false);

								while (i < stat_list.length())
								{
										if (i >= l)
										{
												table_STAT_Values->insertRow(i);

												QTableWidgetItem* row_name = new QTableWidgetItem(stat_list[i].name);
												QTableWidgetItem* row_value =
														new QTableWidgetItem(QString::number(stat_list[i].value));

												table_STAT_Values->setItem(i, 0, row_name);
												table_STAT_Values->setItem(i, 1, row_value);
										} else
										{
												table_STAT_Values->item(i, 0)->setText(stat_list[i].name);
												table_STAT_Values->item(i, 1)->setText(QString::number(stat_list[i].value));
										}

										++i;
								}

								while (i < l)
								{
										table_STAT_Values->removeRow((table_STAT_Values->rowCount() - 1));
										++i;
								}

								table_STAT_Values->setSortingEnabled(true);
						} else if (user_data == ACTION_STAT_LIST_GROUPS)
						{
								combo_STAT_Group->clear();
								combo_STAT_Group->addItems(group_list);
						}
				}
		} else if (sender() == smp_groups.fs_mgmt)
		{
				log_debug() << "fs sender";

		if (user_data == ACTION_ARS_TRACKER_EXPORT_DOWNLOAD)
		{
				label_status = lbl_ars_tracker_status;
				handle_ars_tracker_export_fs_status(user_data, status, error_string);
				skip_error_string = true;
				finished = false;
		}
		else if (user_data == ACTION_ARS_TRACKER_EXPORT_METADATA)
		{
				label_status = lbl_ars_tracker_status;
				handle_ars_tracker_export_fs_status(user_data, status, error_string);
				skip_error_string = true;
				finished = false;
		}
		else if (user_data == ACTION_ARS_TRACKER_EXPORT_HASH_SUPPORT)
		{
				label_status = lbl_ars_tracker_status;
				handle_ars_tracker_export_fs_status(user_data, status, error_string);
				skip_error_string = true;
				finished = false;
		}
		else
		{
				label_status = lbl_FS_Status;

						if (status == STATUS_COMPLETE)
						{
								log_debug() << "complete";

								if (user_data == ACTION_FS_UPLOAD)
								{
										// edit_FS_Log->appendPlainText("todo");
								} else if (user_data == ACTION_FS_DOWNLOAD)
								{
										// edit_FS_Log->appendPlainText("todo2");
								} else if (user_data == ACTION_FS_HASH_CHECKSUM)
								{
										error_string.prepend("Finished hash/checksum using ");
										edit_FS_Result->setText(fs_hash_checksum_response.toHex());
										edit_FS_Size->setText(QString::number(fs_size_response));
								} else if (user_data == ACTION_FS_SUPPORTED_HASHES_CHECKSUMS)
								{
										uint8_t i = 0;

										combo_FS_type->clear();

										while (i < supported_hash_checksum_list.length())
										{
												combo_FS_type->addItem(supported_hash_checksum_list.at(i).name);
												log_debug() << supported_hash_checksum_list.at(i).format << ", "
																		<< supported_hash_checksum_list.at(i).size;
												++i;
										}
								} else if (user_data == ACTION_FS_STATUS)
								{
										edit_FS_Size->setText(QString::number(fs_size_response));
								}
						}
				}
		} else if (sender() == smp_groups.settings_mgmt)
		{
				log_debug() << "settings sender";
				label_status = lbl_settings_status;

				if (status == STATUS_COMPLETE)
				{
						log_debug() << "complete";

						if (user_data == ACTION_SETTINGS_READ)
						{
								edit_settings_value->setText(settings_read_response.toHex());

								if (update_settings_display() == false)
								{
										error_string =
												QString("Error: data is %1 bytes, cannot convert to decimal number")
														.arg(QString::number(settings_read_response.length()));
								}
						} else if (user_data == ACTION_SETTINGS_WRITE || user_data == ACTION_SETTINGS_DELETE ||
											 user_data == ACTION_SETTINGS_COMMIT || user_data == ACTION_SETTINGS_LOAD ||
											 user_data == ACTION_SETTINGS_SAVE)
						{
						}
				}
		} else if (sender() == smp_groups.zephyr_mgmt)
		{
				log_debug() << "zephyr sender";
				label_status = lbl_zephyr_status;

				if (status == STATUS_COMPLETE)
				{
						log_debug() << "complete";

						if (user_data == ACTION_ZEPHYR_STORAGE_ERASE)
						{
						}
				}
		} else if (sender() == smp_groups.enum_mgmt)
		{
				log_debug() << "enum sender";
				label_status = lbl_enum_status;

				if (status == STATUS_COMPLETE)
				{
						log_debug() << "complete";

						if (user_data == ACTION_ENUM_COUNT)
						{
								edit_Enum_Count->setText(QString::number(enum_count));
						} else if (user_data == ACTION_ENUM_LIST)
						{
								uint16_t i = 0;
								uint16_t l = table_Enum_List_Details->rowCount();

								table_Enum_List_Details->setSortingEnabled(false);

								while (i < enum_groups.length())
								{
										if (i >= l)
										{
												table_Enum_List_Details->insertRow(i);

												QTableWidgetItem* row_id =
														new QTableWidgetItem(QString::number(enum_groups[i]));
												QTableWidgetItem* row_name     = new QTableWidgetItem("");
												QTableWidgetItem* row_handlers = new QTableWidgetItem("");

												table_Enum_List_Details->setItem(i, 0, row_id);
												table_Enum_List_Details->setItem(i, 1, row_name);
												table_Enum_List_Details->setItem(i, 2, row_handlers);
										} else
										{
												table_Enum_List_Details->item(i, 0)->setText(
														QString::number(enum_groups[i]));
												table_Enum_List_Details->item(i, 1)->setText("");
												table_Enum_List_Details->item(i, 2)->setText("");
										}

										++i;
								}

								while (i < l)
								{
										table_Enum_List_Details->removeRow((table_Enum_List_Details->rowCount() - 1));
										++i;
								}

								table_Enum_List_Details->setSortingEnabled(true);
						} else if (user_data == ACTION_ENUM_SINGLE)
						{
								edit_Enum_Group_ID->setText(QString::number(enum_single_id));
								check_Enum_Group_Additional->setChecked(!enum_single_end);
						} else if (user_data == ACTION_ENUM_DETAILS)
						{
								uint16_t i = 0;
								uint16_t l = table_Enum_List_Details->rowCount();

								table_Enum_List_Details->setSortingEnabled(false);

								while (i < enum_details.length())
								{
										if (i >= l)
										{
												table_Enum_List_Details->insertRow(i);

												QTableWidgetItem* row_id =
														new QTableWidgetItem(QString::number(enum_details[i].id));
												QTableWidgetItem* row_name = new QTableWidgetItem(enum_details[i].name);
												QTableWidgetItem* row_handlers =
														new QTableWidgetItem(QString::number(enum_details[i].handlers));

												table_Enum_List_Details->setItem(i, 0, row_id);
												table_Enum_List_Details->setItem(i, 1, row_name);
												table_Enum_List_Details->setItem(i, 2, row_handlers);
										} else
										{
												table_Enum_List_Details->item(i, 0)->setText(
														QString::number(enum_details[i].id));
												table_Enum_List_Details->item(i, 1)->setText(enum_details[i].name);
												table_Enum_List_Details->item(i, 2)->setText(
														QString::number(enum_details[i].handlers));
										}

										++i;
								}

								while (i < l)
								{
										table_Enum_List_Details->removeRow((table_Enum_List_Details->rowCount() - 1));
										++i;
								}

								table_Enum_List_Details->setSortingEnabled(true);
						}
				}
		}

		if (finished == true)
		{
				mode = ACTION_IDLE;
				relase_transport();
				btn_cancel->setEnabled(false);

				if (error_string == nullptr)
				{
						if (status == STATUS_COMPLETE)
						{
								error_string = QString("Finished");
						} else if (status == STATUS_ERROR)
						{
								error_string = QString("Error");
						} else if (status == STATUS_TIMEOUT)
						{
								error_string = QString("Command timed out");
						} else if (status == STATUS_CANCELLED)
						{
								error_string = QString("Cancelled");
						}
				}
		}

		if ((ars_tracker_img_action || ars_tracker_os_action) && finished == true)
		{
				if (ars_tracker_img_action)
				{
						ars_tracker_firmware_upload_active = false;
						if (user_data == ACTION_ARS_TRACKER_FIRMWARE_ERASE)
						{
								ars_tracker_firmware_erase_active = false;
						}
				}

				if (ars_tracker_os_action)
				{
						ars_tracker_firmware_upload_active = false;
						ars_tracker_firmware_erase_active = false;
				}

				set_ars_tracker_controls_loading(ars_tracker_any_loading());

				if (user_data == ACTION_ARS_TRACKER_FIRMWARE_ERASE &&
						ars_tracker_firmware_refresh_after_erase_pending == true)
				{
						ars_tracker_firmware_refresh_after_erase_pending = false;
						QTimer::singleShot(0, this, [this]() {
								log_debug() << "ArsTracker firmware image state refresh after erase";
								QString error_message;

								if (start_ars_tracker_info_refresh(&error_message) == false &&
										error_message.isEmpty() == false)
								{
										log_warning() << "ArsTracker firmware image state refresh after erase failed:"
																	<< error_message;
										lbl_ars_tracker_status->setText(error_message);
								}
						});
				}
		}

		if (error_string != nullptr && skip_error_string == false)
		{
				if (label_status != nullptr)
				{
						label_status->setText(error_string);
				} else
				{
						log_error() << "Status message (no receiver): " << error_string;
				}
		}
}

void plugin_mcumgr::progress(uint8_t user_data, uint8_t percent)
{
		log_debug() << "Progress " << percent << " from " << this->sender();

		if (this->sender() == smp_groups.img_mgmt)
		{
				log_debug() << "img sender";

				if (user_data == ACTION_ARS_TRACKER_FIRMWARE_UPLOAD ||
						user_data == ACTION_ARS_TRACKER_FIRMWARE_UPLOAD_SET)
				{
						log_debug() << "ArsTracker firmware upload progress:" << int(percent);
						lbl_ars_tracker_progress->setText(
								QString("Firmware upload: %1%").arg(QString::number(percent)));
						lbl_ars_tracker_status->setText(
								QString("Firmware upload: %1%").arg(QString::number(percent)));
				}
				else
				{
						progress_IMG_Complete->setValue(percent);
				}
		} else if (this->sender() == smp_groups.fs_mgmt)
		{
				log_debug() << "fs sender";

				if (user_data == ACTION_ARS_TRACKER_EXPORT_DOWNLOAD)
				{
						if (ars_tracker_export_fs_active == false ||
								ars_tracker_export_fs_phase != ARS_TRACKER_EXPORT_FS_DOWNLOAD)
						{
								log_debug() << "ArsTracker export fs progress ignored as stale:"
														<< "user_data" << int(user_data)
														<< "percent" << int(percent)
														<< "active" << ars_tracker_export_fs_active
														<< "phase"
														<< ars_tracker_export_fs_phase_name(ars_tracker_export_fs_phase)
														<< "seq" << ars_tracker_export_fs_sequence;
								return;
						}

						log_debug() << "ArsTracker export fs progress seq"
												<< ars_tracker_export_fs_sequence
												<< "phase"
												<< ars_tracker_export_fs_phase_name(ars_tracker_export_fs_phase)
												<< "remote" << ars_tracker_export_fs_remote_file
												<< "percent" << int(percent);
						ars_tracker->handle_file_download_progress(percent);
				}
				else
				{
						progress_FS_Complete->setValue(percent);
				}
		}
}

void plugin_mcumgr::group_to_hex(QByteArray *data)
{
		emit plugin_to_hex(data);
}

void plugin_mcumgr::on_btn_transport_connect_clicked()
{
		smp_transport* transport = active_transport();

		if (transport == uart_transport)
		{
				emit plugin_serial_open_close(2);
		} else
		{
				transport->open_connect_dialog();
		}
}

smp_transport *plugin_mcumgr::active_transport()
{
		if (0)
		{
		}
#if defined(PLUGIN_MCUMGR_TRANSPORT_UDP)
		else if (radio_transport_udp->isChecked() == true)
		{
				return udp_transport;
		}
#endif
#if defined(PLUGIN_MCUMGR_TRANSPORT_BLUETOOTH)
		else if (radio_transport_bluetooth->isChecked() == true)
		{
				return bluetooth_transport;
		}
#endif
#if defined(PLUGIN_MCUMGR_TRANSPORT_LORAWAN)
		else if (radio_transport_lora->isChecked() == true)
		{
				return lora_transport;
		}
#endif
		else
		{
				return uart_transport;
		}
}

QMainWindow *plugin_mcumgr::get_main_window()
{
		return parent_window;
}

void plugin_mcumgr::close_transport_windows()
{
#if defined(PLUGIN_MCUMGR_TRANSPORT_UDP)
		if (radio_transport_udp->isChecked() == false)
		{
				udp_transport->close_connect_dialog();
		}
#endif
#if defined(PLUGIN_MCUMGR_TRANSPORT_BLUETOOTH)
		if (radio_transport_bluetooth->isChecked() == false)
		{
				bluetooth_transport->close_connect_dialog();
		}
#endif
#if defined(PLUGIN_MCUMGR_TRANSPORT_LORAWAN)
		if (radio_transport_lora->isChecked() == false)
		{
				lora_transport->close_connect_dialog();
		}
#endif
}

void plugin_mcumgr::on_radio_transport_uart_toggled(bool checked)
{
		if (checked == true)
		{
				close_transport_windows();
				show_transport_open_status();
		}
}

#if defined(PLUGIN_MCUMGR_TRANSPORT_UDP)
void plugin_mcumgr::on_radio_transport_udp_toggled(bool checked)
{
		if (checked == true)
		{
				close_transport_windows();
				show_transport_open_status();
				udp_transport->open_connect_dialog();
		}
}
#endif

#if defined(PLUGIN_MCUMGR_TRANSPORT_BLUETOOTH)
void plugin_mcumgr::on_radio_transport_bluetooth_toggled(bool checked)
{
		if (checked == true)
		{
				close_transport_windows();
				show_transport_open_status();
				bluetooth_transport->open_connect_dialog();
		}
}
#endif

#if defined(PLUGIN_MCUMGR_TRANSPORT_LORAWAN)
void plugin_mcumgr::on_radio_transport_lora_toggled(bool checked)
{
		if (checked == true)
		{
				close_transport_windows();
				show_transport_open_status();
				lora_transport->open_connect_dialog();
		}
}
#endif

void plugin_mcumgr::on_radio_OS_Buffer_Info_toggled(bool checked)
{
		if (checked == true)
		{
				edit_OS_UName->setEnabled(false);
		}
}

void plugin_mcumgr::on_radio_OS_uname_toggled(bool checked)
{
		if (checked == true)
		{
				edit_OS_UName->setEnabled(true);
		}
}

void plugin_mcumgr::on_radio_IMG_Get_toggled(bool checked)
{
		if (checked == true)
		{
				check_IMG_Confirm->setEnabled(false);
		}
}

void plugin_mcumgr::on_radio_IMG_Set_toggled(bool checked)
{
		if (checked == true)
		{
				check_IMG_Confirm->setEnabled(true);
		}
}

bool plugin_mcumgr::claim_transport(QLabel *status)
{
		bool successful = false;

		if (active_transport() == uart_transport)
		{
				emit plugin_set_status(true, false, &successful);

				if (successful == true)
				{
						uart_transport_locked = true;
				} else
				{
						status->setText("Error: Could not claim transport");
				}
		} else
		{
				successful = true;
		}

		return successful;
}

void plugin_mcumgr::relase_transport(void)
{
		if (active_transport() == uart_transport && uart_transport_locked == true)
		{
				bool successful = false;

				emit plugin_set_status(false, false, &successful);

				if (successful == false)
				{
						log_error() << "Failed to release UART transport";
				}
		}
}

void plugin_mcumgr::on_radio_settings_read_toggled(bool checked)
{
		if (checked == true)
		{
				edit_settings_key->setEnabled(true);
				edit_settings_value->setEnabled(true);
				edit_settings_value->setReadOnly(true);
		}
}

void plugin_mcumgr::on_radio_settings_write_toggled(bool checked)
{
		if (checked == true)
		{
				edit_settings_key->setEnabled(true);
				edit_settings_value->setEnabled(true);
				edit_settings_value->setReadOnly(false);
		}
}

void plugin_mcumgr::on_radio_settings_delete_toggled(bool checked)
{
		if (checked == true)
		{
				edit_settings_key->setEnabled(true);
				edit_settings_value->setEnabled(false);
		}
}

void plugin_mcumgr::on_radio_settings_commit_toggled(bool checked)
{
		if (checked == true)
		{
				edit_settings_key->setEnabled(false);
				edit_settings_value->setEnabled(false);
		}
}

void plugin_mcumgr::on_radio_settings_load_toggled(bool checked)
{
		if (checked == true)
		{
				edit_settings_key->setEnabled(false);
				edit_settings_value->setEnabled(false);
		}
}

void plugin_mcumgr::on_radio_settings_save_toggled(bool checked)
{
		if (checked == true)
		{
				edit_settings_key->setEnabled(false);
				edit_settings_value->setEnabled(false);
		}
}

void plugin_mcumgr::on_btn_settings_go_clicked()
{
		bool started = false;

		if (claim_transport(lbl_settings_status) == false)
		{
				return;
		}

		if (radio_settings_read->isChecked())
		{
				if (edit_settings_key->text().isEmpty())
				{
						lbl_settings_status->setText("Error: Key is required");
				} else
				{
						mode = ACTION_SETTINGS_READ;
						processor->set_transport(active_transport());
						set_group_transport_settings(smp_groups.settings_mgmt);
						started = smp_groups.settings_mgmt->start_read(edit_settings_key->text(), 0,
																													 &settings_read_response);

						if (started == true)
						{
								lbl_settings_status->setText("Reading...");
						}
				}
		} else if (radio_settings_write->isChecked())
		{
				if (edit_settings_key->text().isEmpty())
				{
						lbl_settings_status->setText("Error: Key is required");
				} else
				{
						mode = ACTION_SETTINGS_WRITE;
						processor->set_transport(active_transport());
						set_group_transport_settings(smp_groups.settings_mgmt);
						// started = smp_groups.settings_mgmt->start_write(edit_settings_key->text(),
						// edit_settings_value->text().toUtf8());
						started = smp_groups.settings_mgmt->start_write(
								edit_settings_key->text(),
								QByteArray::fromHex(edit_settings_value->text().toLatin1()));

						if (started == true)
						{
								lbl_settings_status->setText("Writing...");
						}
				}
		} else if (radio_settings_delete->isChecked())
		{
				if (edit_settings_key->text().isEmpty())
				{
						lbl_settings_status->setText("Error: Key is required");
				} else
				{
						mode = ACTION_SETTINGS_DELETE;
						processor->set_transport(active_transport());
						set_group_transport_settings(smp_groups.settings_mgmt);
						started = smp_groups.settings_mgmt->start_delete(edit_settings_key->text());

						if (started == true)
						{
								lbl_settings_status->setText("Deleting...");
						}
				}
		} else if (radio_settings_commit->isChecked())
		{
				mode = ACTION_SETTINGS_COMMIT;
				processor->set_transport(active_transport());
				set_group_transport_settings(smp_groups.settings_mgmt);
				started = smp_groups.settings_mgmt->start_commit();

				if (started == true)
				{
						lbl_settings_status->setText("Committing...");
				}
		} else if (radio_settings_load->isChecked())
		{
				mode = ACTION_SETTINGS_LOAD;
				processor->set_transport(active_transport());
				set_group_transport_settings(smp_groups.settings_mgmt);
				started = smp_groups.settings_mgmt->start_load();

				if (started == true)
				{
						lbl_settings_status->setText("Loading...");
				}
		} else if (radio_settings_save->isChecked())
		{
				mode = ACTION_SETTINGS_SAVE;
				processor->set_transport(active_transport());
				set_group_transport_settings(smp_groups.settings_mgmt);
				started = smp_groups.settings_mgmt->start_save();

				if (started == true)
				{
						lbl_settings_status->setText("Saving...");
				}
		}

		if (started == true)
		{
				btn_cancel->setEnabled(true);
		} else
		{
				relase_transport();
		}
}

void plugin_mcumgr::setup_finished()
{
#ifndef SKIPPLUGIN_LOGGER
		logger->find_logger_plugin(parent_window);
#endif

		AutScrollEdit* terminal_widget = parent_window->findChild<AutScrollEdit*>("text_TermEditData");
		if (terminal_widget != nullptr && text_ars_tracker_device_logs != nullptr)
		{
				text_ars_tracker_device_logs->setPalette(terminal_widget->palette());
				text_ars_tracker_device_logs->setFont(terminal_widget->font());
				text_ars_tracker_device_logs->setTabStopDistance(terminal_widget->tabStopDistance());
				text_ars_tracker_device_logs->setup_scrollback(
						terminal_widget->document()->maximumBlockCount());
				text_ars_tracker_device_logs->set_vt100_mode(VT100_MODE_DECODE);
				text_ars_tracker_device_logs->set_serial_open(true);
				log_debug() << "ArsTracker device logs terminal widget initialized";
		}

#if defined(PLUGIN_MCUMGR_TRANSPORT_BLUETOOTH)
		bluetooth_transport->setup_finished();
#endif

#if defined(PLUGIN_MCUMGR_TRANSPORT_UDP)
		udp_transport->setup_finished();
#endif

#if defined(PLUGIN_MCUMGR_TRANSPORT_LORAWAN)
		lora_transport->setup_finished();
#endif
}

void plugin_mcumgr::flip_endian(uint8_t *data, uint8_t size)
{
		uint8_t i = 0;

		while (i < (size / 2))
		{
				uint8_t temp = data[(size - 1) - i];

				data[(size - 1) - i] = data[i];
				data[i]              = temp;

				++i;
		}
}

//settings_read_response
void plugin_mcumgr::on_radio_settings_none_toggled(bool toggled)
{
		if (toggled == true)
		{
				edit_settings_decoded->setEnabled(false);
				(void)update_settings_display();
		}
}

void plugin_mcumgr::on_radio_settings_text_toggled(bool toggled)
{
		if (toggled == true)
		{
				edit_settings_decoded->setEnabled(true);
				(void)update_settings_display();
		}
}

void plugin_mcumgr::on_radio_settings_decimal_toggled(bool toggled)
{
		if (toggled == true)
		{
				check_settings_big_endian->setEnabled(true);
				check_settings_signed_decimal_value->setEnabled(true);
				edit_settings_decoded->setEnabled(true);
				(void)update_settings_display();
		} else
		{
				check_settings_big_endian->setEnabled(false);
				check_settings_signed_decimal_value->setEnabled(false);
		}
}

void plugin_mcumgr::on_check_settings_big_endian_toggled(bool toggled)
{
		(void)update_settings_display();
}

void plugin_mcumgr::on_check_settings_signed_decimal_value_toggled(bool toggled)
{
		(void)update_settings_display();
}

bool plugin_mcumgr::update_settings_display()
{
		if (settings_read_response.length() == 0 || radio_settings_none->isChecked())
		{
				edit_settings_decoded->clear();
		} else if (radio_settings_text->isChecked())
		{
				edit_settings_decoded->setText(settings_read_response);
		} else if (radio_settings_decimal->isChecked())
		{
				if (settings_read_response.length() == sizeof(uint8_t) ||
						settings_read_response.length() == sizeof(uint16_t) ||
						settings_read_response.length() == sizeof(uint32_t) ||
						settings_read_response.length() == sizeof(uint64_t))
				{
						bool endian_swap_required = false;

#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
						if (check_settings_big_endian->isChecked())
#else
						if (!check_settings_big_endian->isChecked())
#endif
						{
								endian_swap_required = true;
						}

						// Same endian as host, no conversion needed
						if (check_settings_signed_decimal_value->isChecked())
						{
								// Signed integers
								switch (settings_read_response.length())
								{
								case sizeof(int8_t): {
										int8_t value = settings_read_response.constData()[0];
										edit_settings_decoded->setText(QString::number(value));
										break;
								}
								case sizeof(int16_t): {
										int16_t value;
										memcpy(&value, settings_read_response.constData(), sizeof(value));

										if (endian_swap_required == true)
										{
												flip_endian((uint8_t*)&value, sizeof(value));
										}

										edit_settings_decoded->setText(QString::number(value));
										break;
								}
								case sizeof(int32_t): {
										int32_t value;
										memcpy(&value, settings_read_response.constData(), sizeof(value));

										if (endian_swap_required == true)
										{
												flip_endian((uint8_t*)&value, sizeof(value));
										}

										edit_settings_decoded->setText(QString::number(value));
										break;
								}
								case sizeof(int64_t): {
										int64_t value;
										memcpy(&value, settings_read_response.constData(), sizeof(value));

										if (endian_swap_required == true)
										{
												flip_endian((uint8_t*)&value, sizeof(value));
										}

										edit_settings_decoded->setText(QString::number(value));
										break;
								}
								};
						} else
						{
								// Unsigned integers
								switch (settings_read_response.length())
								{
								case sizeof(uint8_t): {
										uint8_t value = settings_read_response.constData()[0];
										edit_settings_decoded->setText(QString::number(value));
										break;
								}
								case sizeof(uint16_t): {
										uint16_t value;
										memcpy(&value, settings_read_response.constData(), sizeof(value));

										if (endian_swap_required == true)
										{
												flip_endian((uint8_t*)&value, sizeof(value));
										}

										edit_settings_decoded->setText(QString::number(value));
										break;
								}
								case sizeof(uint32_t): {
										uint32_t value;
										memcpy(&value, settings_read_response.constData(), sizeof(value));

										if (endian_swap_required == true)
										{
												flip_endian((uint8_t*)&value, sizeof(value));
										}

										edit_settings_decoded->setText(QString::number(value));
										break;
								}
								case sizeof(uint64_t): {
										uint64_t value;
										memcpy(&value, settings_read_response.constData(), sizeof(value));

										if (endian_swap_required == true)
										{
												flip_endian((uint8_t*)&value, sizeof(value));
										}

										edit_settings_decoded->setText(QString::number(value));
										break;
								}
								};
						}
				} else
				{
						return false;
				}
		}

		return true;
}

void plugin_mcumgr::show_transport_open_status()
{
		smp_transport* transport = active_transport();

		if (transport == uart_transport)
		{
				bool open = false;

				emit plugin_serial_is_open(&open);
				btn_transport_connect->setText(open == true ? "Disconnect" : "Connect");
		} else
		{
				btn_transport_connect->setText("Show window");
		}
}

void plugin_mcumgr::enter_pressed()
{
		// Execute shell command
		bool    started = false;
		QString data    = *edit_SHELL_Output->get_dat_out();

		if (data.length() == 0)
		{
				lbl_SHELL_Status->setText("No data to send");
				return;
		}

		if (claim_transport(lbl_SHELL_Status) == false)
		{
				return;
		}

		QRegularExpression reTempRE("\\s+");
		QStringList        list_arguments = data.split(reTempRE);

		mode = ACTION_SHELL_EXECUTE;
		processor->set_transport(active_transport());
		set_group_transport_settings(smp_groups.shell_mgmt);
		started = smp_groups.shell_mgmt->start_execute(&list_arguments, &shell_rc);

		if (started == true)
		{
				edit_SHELL_Output->clear_dat_out();
				edit_SHELL_Output->add_dat_in_text(data.append("\n").toUtf8());
				edit_SHELL_Output->update_display();
				lbl_SHELL_Status->setText("Shell execute command sent...");
				btn_cancel->setEnabled(true);
		} else
		{
				relase_transport();
		}
}

void plugin_mcumgr::on_btn_zephyr_go_clicked()
{
		bool started = false;

		if (claim_transport(lbl_zephyr_status) == false)
		{
				return;
		}

		if (tabWidget_4->currentWidget() == tab_zephyr_storage_erase)
		{
				mode = ACTION_ZEPHYR_STORAGE_ERASE;
				processor->set_transport(active_transport());
				set_group_transport_settings(smp_groups.zephyr_mgmt);
				started = smp_groups.zephyr_mgmt->start_storage_erase();

				if (started == true)
				{
						lbl_zephyr_status->setText("Erasing storage...");
				}
		}

		if (started == true)
		{
				btn_cancel->setEnabled(true);
		} else
		{
				relase_transport();
		}
}

void plugin_mcumgr::on_check_os_datetime_use_pc_date_time_toggled(bool checked)
{
		combo_os_datetime_timezone->setEnabled(!checked);
		edit_os_datetime_date_time->setEnabled(!checked);
		edit_os_datetime_date_time->setReadOnly(checked);
}

void plugin_mcumgr::on_radio_os_datetime_get_toggled(bool checked)
{
		if (checked == true)
		{
				check_os_datetime_use_pc_date_time->setEnabled(false);
				combo_os_datetime_timezone->setEnabled(false);
				edit_os_datetime_date_time->setReadOnly(true);
				edit_os_datetime_date_time->setEnabled(true);
				edit_os_datetime_date_time->setDisplayFormat("yyyy-MM-dd HH:mm:ss t");
		}
}

void plugin_mcumgr::on_radio_os_datetime_set_toggled(bool checked)
{
		if (checked == true)
		{
				check_os_datetime_use_pc_date_time->setEnabled(true);
				edit_os_datetime_date_time->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
				on_check_os_datetime_use_pc_date_time_toggled(
						check_os_datetime_use_pc_date_time->isChecked());
		}
}

void plugin_mcumgr::on_btn_enum_go_clicked()
{
		bool started = false;

		if (claim_transport(lbl_FS_Status) == false)
		{
				return;
		}

		if (radio_Enum_Count->isChecked())
		{
				mode = ACTION_ENUM_COUNT;
				processor->set_transport(active_transport());
				set_group_transport_settings(smp_groups.enum_mgmt);
				started = smp_groups.enum_mgmt->start_enum_count(&enum_count);

				if (started == true)
				{
						lbl_enum_status->setText("Count...");
				}
		} else if (radio_Enum_List->isChecked())
		{
				mode = ACTION_ENUM_LIST;
				processor->set_transport(active_transport());
				set_group_transport_settings(smp_groups.enum_mgmt);
				started = smp_groups.enum_mgmt->start_enum_list(&enum_groups);

				if (started == true)
				{
						lbl_enum_status->setText("List...");
				}
		} else if (radio_Enum_Single->isChecked())
		{
				mode = ACTION_ENUM_SINGLE;
				processor->set_transport(active_transport());
				set_group_transport_settings(smp_groups.enum_mgmt);
				started = smp_groups.enum_mgmt->start_enum_single(edit_Enum_Index->value(), &enum_single_id,
																													&enum_single_end);

				if (started == true)
				{
						lbl_enum_status->setText("Single...");
				}
		} else if (radio_Enum_Details->isChecked())
		{
				mode = ACTION_ENUM_DETAILS;
				processor->set_transport(active_transport());
				set_group_transport_settings(smp_groups.enum_mgmt);
				started =
						smp_groups.enum_mgmt->start_enum_details(&enum_details, &enum_details_present_fields);

				if (started == true)
				{
						lbl_enum_status->setText("Details...");
				}
		}

		if (started == true)
		{
				btn_cancel->setEnabled(true);
		} else
		{
				relase_transport();
		}
}

void plugin_mcumgr::on_radio_custom_custom_toggled(bool checked)
{
		if (!checked)
		{
				return;
		}

		radio_custom_read->setEnabled(true);
		radio_custom_write->setEnabled(true);
		edit_custom_group->setReadOnly(false);
		edit_custom_command->setReadOnly(false);
		edit_custom_send->setReadOnly(false);
		btn_custom_go->setEnabled(true);

		processor->set_message_logging(false);
}

void plugin_mcumgr::on_radio_custom_logging_toggled(bool checked)
{
		if (!checked)
		{
				return;
		}

		radio_custom_read->setEnabled(false);
		radio_custom_write->setEnabled(false);
		edit_custom_group->setReadOnly(true);
		edit_custom_command->setReadOnly(true);
		edit_custom_send->setReadOnly(true);
		btn_custom_go->setEnabled(false);

		processor->set_message_logging(true);
}

void plugin_mcumgr::custom_log(bool sent, QString *data)
{
		QPlainTextEdit* target = (sent == true ? edit_custom_send : edit_custom_receive);

		target->appendPlainText(*data);
}

void plugin_mcumgr::on_radio_custom_json_toggled(bool checked)
{
		if (!checked)
		{
				return;
		}

		log_json->set_mode(SMP_LOGGING_MODE_JSON);
}

void plugin_mcumgr::on_radio_custom_yaml_toggled(bool checked)
{
		if (!checked)
		{
				return;
		}

		log_json->set_mode(SMP_LOGGING_MODE_YAML);
}

void plugin_mcumgr::on_radio_custom_cbor_toggled(bool checked)
{
		if (!checked)
		{
				return;
		}

		log_json->set_mode(SMP_LOGGING_MODE_CBOR);
}

void plugin_mcumgr::on_btn_custom_copy_send_clicked()
{
		QApplication::clipboard()->setText(edit_custom_send->toPlainText());
}

void plugin_mcumgr::on_btn_custom_copy_receive_clicked()
{
		QApplication::clipboard()->setText(edit_custom_receive->toPlainText());
}

void plugin_mcumgr::on_btn_custom_copy_both_clicked()
{
		QApplication::clipboard()->setText(QString(edit_custom_send->toPlainText())
																					 .append("\n")
																					 .append(edit_custom_receive->toPlainText()));
}

void plugin_mcumgr::on_btn_custom_clear_clicked()
{
		edit_custom_send->clear();
		edit_custom_receive->clear();
}

void plugin_mcumgr::on_edit_custom_indent_valueChanged(int value)
{
		log_json->set_indent(value);
}

void plugin_mcumgr::on_btn_custom_go_clicked()
{
		smp_transport* transport = active_transport();
		smp_message*   tmp_message;
		QJsonDocument* json_document = nullptr;

		// Check message validity
		if (radio_custom_json->isChecked())
		{
				QJsonParseError json_error;

				json_document = new QJsonDocument();
				*json_document =
						QJsonDocument::fromJson(edit_custom_send->toPlainText().toUtf8(), &json_error);

				if (json_document->isNull())
				{
						lbl_custom_status->setText(
								QString("Error parsing JSON: ").append(json_error.errorString()));
						delete (json_document);
						return;
				}
		} else if (radio_custom_yaml->isChecked())
		{
				lbl_custom_status->setText("YAML input parsing is currently not supported");
				return;
		} else if (radio_custom_cbor->isChecked())
		{
				QString  string_check = edit_custom_send->toPlainText();
				uint16_t i            = 0;
				uint16_t l            = string_check.length();

				while (i < l)
				{
						if (!((string_check.at(i) >= 'a' && string_check.at(i) <= 'f') ||
									(string_check.at(i) >= 'A' && string_check.at(i) <= 'F') ||
									(string_check.at(i) >= '0' && string_check.at(i) <= '9')))
						{
								lbl_custom_status->setText(
										QString("Invalid hex character at position ").append(QString::number(i + 1)));
								return;
						}

						++i;
				}
		}

		if (claim_transport(lbl_custom_status) == false)
		{
				if (json_document != nullptr)
				{
						delete (json_document);
				}

				return;
		}

		mode = ACTION_CUSTOM;
		processor->set_transport(active_transport());

		lbl_custom_status->setText("Custom...");

		tmp_message = new smp_message();

		if (radio_custom_json->isChecked())
		{
				QCborMap cbor_map = QCborMap::fromJsonObject(json_document->object());

				tmp_message->start_message_no_start_map(
						(radio_custom_read->isChecked() ? SMP_OP_READ : SMP_OP_WRITE),
						(check_V2_Protocol->isChecked() ? 1 : 0), edit_custom_group->value(),
						edit_custom_command->value());
				cbor_map.toCborValue().toCbor(*tmp_message->writer());
				tmp_message->end_message_no_end_map();

				delete (json_document);
		} else if (radio_custom_yaml->isChecked())
		{
		} else if (radio_custom_cbor->isChecked())
		{
				tmp_message->start_message_no_start_map(
						(radio_custom_read->isChecked() ? SMP_OP_READ : SMP_OP_WRITE),
						(check_V2_Protocol->isChecked() ? 1 : 0), edit_custom_group->value(),
						edit_custom_command->value());
				tmp_message->end_custom_message(
						QByteArray::fromHex(edit_custom_send->toPlainText().toUtf8()));
		}

		processor->set_custom_message(true);
		processor->send(tmp_message, transport->get_timeout(), transport->get_retries(), true);
		btn_cancel->setEnabled(true);
}

void plugin_mcumgr::custom_message_callback(enum custom_message_callback_t type, smp_error_t *data)
{
		mode = ACTION_IDLE;
		relase_transport();
		btn_cancel->setEnabled(false);

		if (type == CUSTOM_MESSAGE_CALLBACK_OK)
		{
				lbl_custom_status->setText("Finished");
		} else if (type == CUSTOM_MESSAGE_CALLBACK_ERROR)
		{
				lbl_custom_status->setText(smp_error::error_lookup_string(data));
		} else if (type == CUSTOM_MESSAGE_CALLBACK_TIMEOUT)
		{
				lbl_custom_status->setText("Command timed out");
		}
}

void plugin_mcumgr::size_abbreviation(uint32_t size, QString *output)
{
		const QStringList list_abbreviations = { "B", "KiB", "MiB", "GiB", "TiB" };
		float             converted_size     = size;
		uint8_t           abbreviation_index = 0;

		while (converted_size >= 1024 && abbreviation_index < list_abbreviations.size())
		{
				converted_size /= 1024.0;
				++abbreviation_index;
		}

		output->append(
				QString::number(converted_size, 'g', 3).append(list_abbreviations.at(abbreviation_index)));
}

void plugin_mcumgr::on_tree_IMG_Slot_Info_itemDoubleClicked(QTreeWidgetItem *item, int)
{
		if (!item->text(TREE_IMG_SLOT_INFO_COLUMN_UPLOAD_ID).isEmpty())
		{
				edit_IMG_Image->setValue(item->text(TREE_IMG_SLOT_INFO_COLUMN_UPLOAD_ID).toUInt());
				selector_img->setCurrentIndex(selector_img->indexOf(tab_IMG_Upload));
		}
}

void plugin_mcumgr::set_group_transport_settings(smp_group *group)
{
		smp_transport* transport = active_transport();

		group->set_parameters((check_V2_Protocol->isChecked() ? 1 : 0), edit_MTU->value(),
													transport->get_retries(), transport->get_timeout(), mode);
}

void plugin_mcumgr::set_group_transport_settings(smp_group *group, uint32_t timeout)
{
		smp_transport* transport = active_transport();

		group->set_parameters(
				(check_V2_Protocol->isChecked() ? 1 : 0), edit_MTU->value(), transport->get_retries(),
				(timeout >= transport->get_timeout() ? timeout : transport->get_timeout()), mode);
}

void plugin_mcumgr::set_group_transport_settings(smp_group *group, mcumgr_action_t action)
{
		smp_transport* transport = active_transport();

		group->set_parameters((check_V2_Protocol->isChecked() ? 1 : 0), edit_MTU->value(),
													transport->get_retries(), transport->get_timeout(), action);
}

void plugin_mcumgr::set_group_transport_settings(smp_group *group, mcumgr_action_t action,
																								 uint32_t timeout, uint8_t retries)
{
		smp_transport* transport = active_transport();
		uint32_t effective_timeout =
				(timeout >= transport->get_timeout() ? timeout : transport->get_timeout());

		group->set_parameters((check_V2_Protocol->isChecked() ? 1 : 0), edit_MTU->value(),
													retries, effective_timeout, action);
}

void plugin_mcumgr::on_btn_error_lookup_clicked()
{
		error_lookup_form->show();
}

void plugin_mcumgr::on_btn_cancel_clicked()
{
		processor->cancel();
}

void plugin_mcumgr::on_selector_tab_currentChanged(int index)
{
		Q_UNUSED(index);
		if (ars_tracker_tab_is_active())
		{
				refresh_ars_tracker_serial_ports();
				sync_ars_tracker_serial_controls(ars_tracker_any_loading());
		}
		maybe_auto_refresh_ars_tracker();
}

void plugin_mcumgr::refresh_ars_tracker_serial_ports()
{
		if (combo_ars_tracker_port == nullptr)
		{
				return;
		}

		if (ars_tracker_has_connected_devices())
		{
				QString selected_port = ars_tracker_selected_port_name();
				ars_tracker_device_t *active_device = active_ars_tracker_device();
				if (active_device != nullptr)
				{
						selected_port = active_device->portName;
				}

				populate_ars_tracker_serial_ports(ars_tracker_scan_results, selected_port);
				if (active_device == nullptr)
				{
						for (const ars_tracker_device_t &device : ars_tracker_devices)
						{
								if (device.connected == true)
								{
										set_active_ars_tracker_device(device.portName, true);
										break;
								}
						}
				}
				else
				{
						set_active_ars_tracker_device(active_device->portName, false);
				}
				sync_ars_tracker_serial_controls(ars_tracker_any_loading());
				return;
		}

		bool serial_open = false;
		bool serial_opening = false;
		ars_tracker_main_serial_state(&serial_open, &serial_opening);
		if (serial_open == true || serial_opening == true ||
				ars_tracker_serial_transition_active == true)
		{
				log_debug()
						<< "ArsTracker port scan skipped because main transport is connected/connecting";
				sync_ars_tracker_serial_controls(ars_tracker_any_loading());
				return;
		}

		start_ars_tracker_port_scan();
}

QString plugin_mcumgr::ars_tracker_selected_port_name() const
{
		if (combo_ars_tracker_port == nullptr)
		{
				return QString();
		}

		int current_index = combo_ars_tracker_port->currentIndex();
		if (current_index < 0)
		{
				return QString();
		}

		return combo_ars_tracker_port->itemData(current_index, Qt::UserRole).toString().trimmed();
}

bool plugin_mcumgr::ars_tracker_has_selected_port() const
{
		return ars_tracker_selected_port_name().isEmpty() == false;
}

bool plugin_mcumgr::ars_tracker_combo_has_selectable_port() const
{
		if (combo_ars_tracker_port == nullptr)
		{
				return false;
		}

		for (int i = 0; i < combo_ars_tracker_port->count(); ++i)
		{
				if (combo_ars_tracker_port->itemData(i, Qt::UserRole).toString().trimmed().isEmpty() ==
						false)
				{
						return true;
				}
		}

		return false;
}

bool plugin_mcumgr::ars_tracker_main_serial_state(bool *open, bool *opening)
{
		bool local_open = false;
		bool local_opening = false;
		emit plugin_serial_state(&local_open, &local_opening);

		if (open != nullptr)
		{
				*open = local_open;
		}

		if (opening != nullptr)
		{
				*opening = local_opening;
		}

		return true;
}

ars_tracker_ui_state_t plugin_mcumgr::ars_tracker_current_ui_state(bool loading)
{
		Q_UNUSED(loading);
		bool serial_open = false;
		bool serial_opening = false;
		ars_tracker_main_serial_state(&serial_open, &serial_opening);

		if (ars_tracker_port_scan_active == true)
		{
				return ARS_TRACKER_UI_STATE_SCANNING;
		}

		if (ars_tracker_has_connected_devices())
		{
				return ARS_TRACKER_UI_STATE_CONNECTED;
		}

		if (ars_tracker_serial_transition_active == true || serial_opening == true)
		{
				return ARS_TRACKER_UI_STATE_CONNECTING;
		}

		Q_UNUSED(serial_open);
		return ARS_TRACKER_UI_STATE_DISCONNECTED;
}

QString plugin_mcumgr::ars_tracker_ui_state_to_string(ars_tracker_ui_state_t state)
{
		switch (state)
		{
		case ARS_TRACKER_UI_STATE_DISCONNECTED:
				return "disconnected";
		case ARS_TRACKER_UI_STATE_SCANNING:
				return "scanning";
		case ARS_TRACKER_UI_STATE_CONNECTING:
				return "connecting";
		case ARS_TRACKER_UI_STATE_CONNECTED:
				return "connected";
		case ARS_TRACKER_UI_STATE_DISCONNECTING:
				return "disconnecting";
		default:
				return "unknown";
		}
}

QString plugin_mcumgr::ars_tracker_device_display_text(const QString &serial_number,
																											 const QString &port_name) const
{
		QString trimmed_serial = serial_number.trimmed();
		if (trimmed_serial.isEmpty())
		{
				log_debug() << "ArsTracker UI tracker display fallback to port name because serial is empty."
										<< "port=" << port_name;
				return port_name;
		}

		QStringList serial_parts = trimmed_serial.split('.');
		if (serial_parts.length() >= 4)
		{
				QString tracker_type = serial_parts.at(2).trimmed();
				QString tracker_unique = serial_parts.last().trimmed();
				QString tracker_side;

				if (tracker_type == "1")
				{
						tracker_side = "R";
				}
				else if (tracker_type == "2")
				{
						tracker_side = "L";
				}

				if (tracker_unique.isEmpty() == false && tracker_side.isEmpty() == false)
				{
						QString display_text = tracker_unique % tracker_side;
						log_debug() << "ArsTracker UI tracker display parsed. full serial="
												<< trimmed_serial << "unique=" << tracker_unique
												<< "side=" << tracker_side << "display=" << display_text
												<< "port=" << port_name;
						return display_text;
				}

				log_warning() << "ArsTracker UI tracker display fallback because serial type is unexpected."
											<< "full serial=" << trimmed_serial << "type=" << tracker_type
											<< "unique=" << tracker_unique << "port=" << port_name;
				return trimmed_serial;
		}

		log_warning() << "ArsTracker UI tracker display fallback because serial format is unexpected."
									<< "full serial=" << trimmed_serial << "port=" << port_name;
		return trimmed_serial;
}

QString plugin_mcumgr::ars_tracker_port_display_text(const QString &port_name,
																									 const QString &serial_number) const
{
		return ars_tracker_device_display_text(serial_number, port_name);
}

void plugin_mcumgr::initialize_ars_tracker_scan_probe_context()
{
		destroy_ars_tracker_scan_probe_context();

		ars_tracker_scan_serial_port = new QSerialPort(this);
		ars_tracker_scan_transport = new smp_uart_auterm(this);
		ars_tracker_scan_processor = new smp_processor(this);
		ars_tracker_scan_shell_mgmt = new smp_group_shell_mgmt(ars_tracker_scan_processor);

#ifndef SKIPPLUGIN_LOGGER
		ars_tracker_scan_processor->set_logger(logger);
		ars_tracker_scan_transport->set_logger(logger);
		ars_tracker_scan_shell_mgmt->set_logger(logger);
#endif

		connect(ars_tracker_scan_transport, &smp_uart_auterm::serial_write, this,
						&plugin_mcumgr::handle_ars_tracker_scan_serial_write);
		connect(ars_tracker_scan_transport, &smp_uart_auterm::receive_waiting,
						ars_tracker_scan_processor, &smp_processor::message_received);
		connect(ars_tracker_scan_serial_port, &QSerialPort::readyRead, this,
						&plugin_mcumgr::handle_ars_tracker_scan_serial_ready_read);
		connect(ars_tracker_scan_serial_port, &QSerialPort::errorOccurred, this,
						[this](QSerialPort::SerialPortError error) {
								Q_UNUSED(error);
								handle_ars_tracker_scan_serial_error();
						});
		connect(ars_tracker_scan_shell_mgmt, &smp_group_shell_mgmt::status, this,
						&plugin_mcumgr::handle_ars_tracker_scan_shell_status);
}

void plugin_mcumgr::destroy_ars_tracker_scan_probe_context()
{
		if (ars_tracker_scan_shell_mgmt != nullptr)
		{
				ars_tracker_scan_shell_mgmt->cancel();
				delete ars_tracker_scan_shell_mgmt;
				ars_tracker_scan_shell_mgmt = nullptr;
		}

		if (ars_tracker_scan_processor != nullptr)
		{
				ars_tracker_scan_processor->cancel();
				delete ars_tracker_scan_processor;
				ars_tracker_scan_processor = nullptr;
		}

		if (ars_tracker_scan_transport != nullptr)
		{
				ars_tracker_scan_transport->reset_state();
				delete ars_tracker_scan_transport;
				ars_tracker_scan_transport = nullptr;
		}

		if (ars_tracker_scan_serial_port != nullptr)
		{
				if (ars_tracker_scan_serial_port->isOpen())
				{
						ars_tracker_scan_serial_port->close();
				}
				delete ars_tracker_scan_serial_port;
				ars_tracker_scan_serial_port = nullptr;
		}
}

void plugin_mcumgr::release_ars_tracker_device_resources(ars_tracker_device_t *device)
{
		if (device == nullptr)
		{
				return;
		}

		if (device->imgMgmt != nullptr)
		{
				device->imgMgmt->cancel();
				delete device->imgMgmt;
				device->imgMgmt = nullptr;
		}

		if (device->shell != nullptr)
		{
				device->shell->cancel();
				delete device->shell;
				device->shell = nullptr;
		}

		if (device->processor != nullptr)
		{
				device->processor->cancel();
				delete device->processor;
				device->processor = nullptr;
		}

		if (device->transport != nullptr)
		{
				device->transport->reset_state();
				delete device->transport;
				device->transport = nullptr;
		}

		if (device->serialPort != nullptr)
		{
				if (device->serialPort->isOpen())
				{
						device->serialPort->close();
				}
				delete device->serialPort;
				device->serialPort = nullptr;
		}
}

void plugin_mcumgr::disconnect_all_ars_tracker_devices()
{
		log_debug() << "ArsTracker device model disconnect all";
		ars_tracker_auto_info_refresh_pending = false;
		ars_tracker_info_refresh_started_for_current_connection = false;
		ars_tracker_auto_info_refresh_in_progress = false;
		ars_tracker_auto_info_refresh_attempts = 0;
		for (int i = 0; i < ars_tracker_devices.size(); ++i)
		{
				ars_tracker_device_t &device = ars_tracker_devices[i];
				log_debug() << "ArsTracker device model disconnecting port=" << device.portName
										<< "serial=" << device.serialNumber;
				release_ars_tracker_device_resources(&device);
				device.connected = false;
				device.active = false;
				device.info_refreshing = false;
				device.currentInfoShellCommand.clear();
				device.imageStateList.clear();
		}
		ars_tracker_persistent_info_refresh_port.clear();
}

void plugin_mcumgr::clear_ars_tracker_devices()
{
		disconnect_all_ars_tracker_devices();
		ars_tracker_devices.clear();
		log_debug() << "ArsTracker device model cleared";
}

ars_tracker_device_t *plugin_mcumgr::find_ars_tracker_device_by_port(const QString &port_name)
{
		QString trimmed_port = port_name.trimmed();
		if (trimmed_port.isEmpty())
		{
				return nullptr;
		}

		for (int i = 0; i < ars_tracker_devices.size(); ++i)
		{
				if (ars_tracker_devices[i].portName.trimmed().compare(trimmed_port, Qt::CaseInsensitive) == 0)
				{
						return &ars_tracker_devices[i];
				}
		}

		return nullptr;
}

void plugin_mcumgr::upsert_ars_tracker_device(const QString &port_name,
																							const QString &serial_number, bool connected,
																							bool active)
{
		QString trimmed_port = port_name.trimmed();
		QString trimmed_serial = serial_number.trimmed();
		ars_tracker_device_t *device = find_ars_tracker_device_by_port(trimmed_port);
		if (device == nullptr)
		{
				ars_tracker_device_t new_device;
				new_device.portName = trimmed_port;
				ars_tracker_devices.append(new_device);
				device = &ars_tracker_devices.last();
		}

		device->portName = trimmed_port;
		if (trimmed_serial.isEmpty() == false)
		{
				device->serialNumber = trimmed_serial;
				device->identified = true;
		}
		device->connected = connected;
		device->active = active;

		QStringList serial_parts = device->serialNumber.split('.');
		device->side.clear();
		if (serial_parts.length() >= 4)
		{
				QString tracker_type = serial_parts.at(2).trimmed();
				if (tracker_type == "1")
				{
						device->side = "R";
				}
				else if (tracker_type == "2")
				{
						device->side = "L";
				}
		}

		device->displayName = ars_tracker_device_display_text(device->serialNumber, device->portName);

		if (active == true)
		{
				for (int i = 0; i < ars_tracker_devices.size(); ++i)
				{
						if (&ars_tracker_devices[i] != device)
						{
								ars_tracker_devices[i].active = false;
						}
				}
		}

		log_debug() << "ArsTracker device model upsert"
								<< "port=" << device->portName
								<< "serial=" << device->serialNumber
								<< "display=" << device->displayName
								<< "connected=" << device->connected
								<< "identified=" << device->identified
								<< "active=" << device->active;
}

int plugin_mcumgr::ars_tracker_connected_device_count() const
{
		int connected_count = 0;
		for (const ars_tracker_device_t &device : ars_tracker_devices)
		{
				if (device.connected == true)
				{
						++connected_count;
				}
		}

		return connected_count;
}

bool plugin_mcumgr::ars_tracker_has_connected_devices() const
{
		return ars_tracker_connected_device_count() > 0;
}

bool plugin_mcumgr::set_active_ars_tracker_device(const QString &port_name, bool auto_selected)
{
		QString trimmed_port = port_name.trimmed();
		if (trimmed_port.isEmpty())
		{
				return false;
		}

		ars_tracker_device_t *selected_device = find_ars_tracker_device_by_port(trimmed_port);
		if (selected_device == nullptr || selected_device->connected == false)
		{
				return false;
		}

		bool active_changed = selected_device->active == false;
		for (int i = 0; i < ars_tracker_devices.size(); ++i)
		{
				ars_tracker_devices[i].active =
						ars_tracker_devices[i].portName.compare(trimmed_port, Qt::CaseInsensitive) == 0;
		}

		if (combo_ars_tracker_port != nullptr)
		{
				int index = combo_ars_tracker_port->findData(trimmed_port, Qt::UserRole);
				if (index >= 0 && combo_ars_tracker_port->currentIndex() != index)
				{
						QSignalBlocker blocker(combo_ars_tracker_port);
						combo_ars_tracker_port->setCurrentIndex(index);
				}
		}

		if (active_changed == true)
		{
				log_debug()
						<< (auto_selected ? "ArsTracker active persistent device auto-selected port=" :
														 "ArsTracker active persistent device changed port=")
						<< selected_device->portName << "display=" << selected_device->displayName;

				ars_tracker_info_changed(selected_device->info_loaded ? selected_device->info :
																										 ars_tracker_info_t());
				if (list_ars_tracker_sessions != nullptr)
				{
						list_ars_tracker_sessions->clear();
				}
				if (list_ars_tracker_files != nullptr)
				{
						list_ars_tracker_files->clear();
				}
				if (lbl_ars_tracker_progress != nullptr)
				{
						lbl_ars_tracker_progress->clear();
				}
				if (lbl_ars_tracker_status != nullptr && ars_tracker_port_scan_active == false)
				{
						lbl_ars_tracker_status->setText(
								QString("Selected %1. Persistent tracker operations will be enabled in next iteration.")
										.arg(selected_device->displayName));
				}
		}

		sync_ars_tracker_serial_controls(ars_tracker_any_loading());
		return true;
}

ars_tracker_device_t *plugin_mcumgr::active_ars_tracker_device()
{
		for (int i = 0; i < ars_tracker_devices.size(); ++i)
		{
				if (ars_tracker_devices[i].active == true)
				{
						log_debug() << "ArsTracker active device queried. port="
												<< ars_tracker_devices[i].portName
												<< "serial=" << ars_tracker_devices[i].serialNumber;
						return &ars_tracker_devices[i];
				}
		}

		log_debug() << "ArsTracker active device queried. No active device";
		return nullptr;
}

ars_tracker_device_t *plugin_mcumgr::persistent_ars_tracker_refresh_device()
{
		if (ars_tracker_persistent_info_refresh_port.trimmed().isEmpty() == false)
		{
				ars_tracker_device_t *device =
						find_ars_tracker_device_by_port(ars_tracker_persistent_info_refresh_port);
				if (device != nullptr && device->connected == true)
				{
						return device;
				}
		}

		ars_tracker_device_t *device = active_ars_tracker_device();
		if (device != nullptr && device->connected == true)
		{
				return device;
		}

		return nullptr;
}

void plugin_mcumgr::populate_ars_tracker_serial_ports(
		const QList<ars_tracker_port_scan_result_t> &ports, const QString &selected_port,
		const QString &placeholder_text)
{
		if (combo_ars_tracker_port == nullptr)
		{
				return;
		}

		bool was_popup_visible =
				combo_ars_tracker_port->view() != nullptr && combo_ars_tracker_port->view()->isVisible();
		QSignalBlocker blocker(combo_ars_tracker_port);
		combo_ars_tracker_port->clear();
		log_debug() << "ArsTracker UI updating port combo. ports count:" << ports.count()
								<< "selected port:" << selected_port
								<< "placeholder:" << placeholder_text
								<< "popup visible:" << was_popup_visible;

		if (placeholder_text.isEmpty() == false)
		{
				log_debug() << "ArsTracker UI add placeholder item display=" << placeholder_text;
				combo_ars_tracker_port->addItem(placeholder_text, QString());
				combo_ars_tracker_port->setCurrentIndex(0);
		}
		else
		{
				for (const ars_tracker_port_scan_result_t &port : ports)
				{
						QString display_text =
								ars_tracker_port_display_text(port.port_name, port.serial_number);
						log_debug() << "ArsTracker UI add port item display=" << display_text
												<< "data=" << port.port_name
												<< "fullSerial=" << port.serial_number;
						combo_ars_tracker_port->addItem(display_text, port.port_name);
				}

				int index = -1;
				if (selected_port.trimmed().isEmpty() == false)
				{
						index = combo_ars_tracker_port->findData(selected_port, Qt::UserRole);
				}

				if (index < 0 && combo_ars_tracker_port->count() > 0)
				{
						index = 0;
				}

				if (index >= 0)
				{
						combo_ars_tracker_port->setCurrentIndex(index);
				}
		}

		log_debug() << "ArsTracker UI port combo updated. combo count:"
								<< combo_ars_tracker_port->count() << "current index:"
								<< combo_ars_tracker_port->currentIndex() << "current data:"
								<< combo_ars_tracker_port->currentData(Qt::UserRole).toString();

		if (was_popup_visible == true && placeholder_text.isEmpty() == false)
		{
				combo_ars_tracker_port->hidePopup();
				combo_ars_tracker_port->show_popup_without_refresh();
		}
		else if (was_popup_visible == true && ports.count() > 1)
		{
				combo_ars_tracker_port->hidePopup();
				combo_ars_tracker_port->show_popup_without_refresh();
		}
}

void plugin_mcumgr::start_ars_tracker_port_scan()
{
		if (ars_tracker_port_scan_active == true)
		{
				log_debug() << "ArsTracker port scan request ignored because a scan is already running";
				return;
		}

		bool serial_open = false;
		bool serial_opening = false;
		ars_tracker_main_serial_state(&serial_open, &serial_opening);
		ars_tracker_ui_state_t state = ars_tracker_current_ui_state(false);
		if (serial_open == true || serial_opening == true ||
				ars_tracker_serial_transition_active == true ||
				state == ARS_TRACKER_UI_STATE_CONNECTED || state == ARS_TRACKER_UI_STATE_CONNECTING)
		{
				log_debug() << "ArsTracker port scan skipped because tracker is connected/connecting"
										<< "state=" << ars_tracker_ui_state_to_string(state)
										<< "open=" << serial_open
										<< "opening=" << serial_opening
										<< "transition=" << ars_tracker_serial_transition_active;
				return;
		}

		ars_tracker_scan_results.clear();
		clear_ars_tracker_devices();
		ars_tracker_scan_pending_ports.clear();
		ars_tracker_scan_current_port.clear();
		ars_tracker_scan_port_index = 0;
		ars_tracker_scan_main_serial_open = false;
		ars_tracker_scan_probe_active = false;
		ars_tracker_scan_command_started = false;
		ars_tracker_scan_selected_port.clear();

		QStringList system_ports;
		const QList<QSerialPortInfo> available_ports = QSerialPortInfo::availablePorts();
		for (const QSerialPortInfo &info : available_ports)
		{
				system_ports.append(info.portName());
		}

		ars_tracker_scan_pending_ports = system_ports;
		ars_tracker_scan_selected_port = ars_tracker_selected_port_name();
		if (ars_tracker_scan_selected_port.isEmpty())
		{
				QComboBox *main_combo = parent_window->findChild<QComboBox*>("combo_COM");
				if (main_combo != nullptr)
				{
						ars_tracker_scan_selected_port = main_combo->currentText().trimmed();
				}
		}

		serial_open = false;
		serial_opening = false;
		ars_tracker_main_serial_state(&serial_open, &serial_opening);
		if (serial_open == true || serial_opening == true ||
				ars_tracker_serial_transition_active == true)
		{
				log_debug()
						<< "ArsTracker port scan skipped because main transport is connected/connecting";
				return;
		}
		ars_tracker_scan_main_serial_open = serial_open;
		ars_tracker_port_scan_active = true;
		if (ars_tracker_scan_shell_mgmt == nullptr || ars_tracker_scan_processor == nullptr ||
				ars_tracker_scan_transport == nullptr || ars_tracker_scan_serial_port == nullptr)
		{
				initialize_ars_tracker_scan_probe_context();
		}
		ars_tracker_scan_shell_mgmt->cancel();
		ars_tracker_scan_processor->cancel();
		ars_tracker_scan_transport->reset_state();

		log_debug() << "ArsTracker port scan started. System ports:" << system_ports
								<< "selected port:" << ars_tracker_scan_selected_port
								<< "main serial open:" << ars_tracker_scan_main_serial_open;

		populate_ars_tracker_serial_ports(QList<ars_tracker_port_scan_result_t>(),
																			ars_tracker_scan_selected_port, "Scanning...");
		lbl_ars_tracker_status->setText("Scanning ArsTracker serial ports...");
		set_ars_tracker_controls_loading(
				ars_tracker_any_loading());

		if (ars_tracker_scan_pending_ports.isEmpty())
		{
				finish_ars_tracker_port_scan("No ArsTracker devices found.");
				return;
		}

		QTimer::singleShot(0, this, &plugin_mcumgr::begin_next_ars_tracker_port_probe);
}

void plugin_mcumgr::finish_ars_tracker_port_scan(const QString &status_message)
{
		if (ars_tracker_scan_shell_mgmt != nullptr)
		{
				ars_tracker_scan_shell_mgmt->cancel();
		}

		if (ars_tracker_scan_processor != nullptr)
		{
				ars_tracker_scan_processor->cancel();
		}

		if (ars_tracker_scan_transport != nullptr)
		{
				ars_tracker_scan_transport->reset_state();
		}

		if (ars_tracker_scan_serial_port != nullptr && ars_tracker_scan_serial_port->isOpen())
		{
				ars_tracker_scan_serial_port->close();
		}

		ars_tracker_port_scan_active = false;
		ars_tracker_scan_probe_active = false;
		ars_tracker_scan_command_started = false;
		ars_tracker_scan_current_port.clear();

		QStringList filtered_ports;
		for (const ars_tracker_port_scan_result_t &port : ars_tracker_scan_results)
		{
				filtered_ports.append(ars_tracker_port_display_text(port.port_name, port.serial_number));
		}

		if (ars_tracker_scan_results.isEmpty())
		{
				populate_ars_tracker_serial_ports(ars_tracker_scan_results,
																			ars_tracker_scan_selected_port,
																			"No ArsTracker devices found");
		}
		else
		{
				populate_ars_tracker_serial_ports(ars_tracker_scan_results,
																			ars_tracker_scan_selected_port);
		}

		log_debug() << "ArsTracker port scan finished. Filtered ports:" << filtered_ports;

		if (ars_tracker_has_connected_devices())
		{
				ars_tracker_device_t *active_device = active_ars_tracker_device();
				if (active_device == nullptr)
				{
						for (const ars_tracker_device_t &device : ars_tracker_devices)
						{
								if (device.connected == true)
								{
										set_active_ars_tracker_device(device.portName, true);
										break;
								}
						}
				}
				else
				{
						set_active_ars_tracker_device(active_device->portName, false);
				}
		}

		lbl_ars_tracker_status->setText(status_message);
		set_ars_tracker_controls_loading(ars_tracker_any_loading());
}

void plugin_mcumgr::begin_next_ars_tracker_port_probe()
{
		if (ars_tracker_port_scan_active == false)
		{
				return;
		}

		if (ars_tracker_scan_serial_port == nullptr || ars_tracker_scan_transport == nullptr ||
				ars_tracker_scan_processor == nullptr || ars_tracker_scan_shell_mgmt == nullptr)
		{
				initialize_ars_tracker_scan_probe_context();
		}

		if (ars_tracker_scan_serial_port != nullptr && ars_tracker_scan_serial_port->isOpen())
		{
				ars_tracker_scan_serial_port->close();
		}
		ars_tracker_scan_shell_mgmt->cancel();
		ars_tracker_scan_processor->cancel();
		ars_tracker_scan_transport->reset_state();

		if (ars_tracker_scan_port_index >= ars_tracker_scan_pending_ports.length())
		{
				finish_ars_tracker_port_scan(
						ars_tracker_scan_results.isEmpty() ? "No ArsTracker devices found." :
																								QString("Found %1 ArsTracker device(s).")
																										.arg(ars_tracker_scan_results.length()));
				return;
		}

		ars_tracker_scan_current_port =
				ars_tracker_scan_pending_ports.at(ars_tracker_scan_port_index++);
		ars_tracker_scan_probe_active = false;
		ars_tracker_scan_command_started = false;
		lbl_ars_tracker_status->setText(
				QString("Scanning %1 (%2/%3)...")
						.arg(ars_tracker_scan_current_port)
						.arg(ars_tracker_scan_port_index)
						.arg(ars_tracker_scan_pending_ports.length()));
		log_debug() << "ArsTracker port scan probing port:" << ars_tracker_scan_current_port;

		if (radio_transport_uart->isChecked() == true &&
				ars_tracker_scan_main_serial_open == true &&
				ars_tracker_scan_current_port == ars_tracker_scan_selected_port)
		{
				QString current_serial;
				ars_tracker_scan_probe_active = true;
				if (current_ars_tracker_serial_number(&current_serial) == true)
				{
						log_debug() << "ArsTracker port scan reused current connection serial for port"
												<< ars_tracker_scan_current_port << ":" << current_serial;
						complete_ars_tracker_port_probe(current_serial.startsWith("ARS"), current_serial,
																			 current_serial.startsWith("ARS") ?
																					 QString() :
																					 QString("Current connected device serial does not start with ARS"));
				}
				else
				{
						complete_ars_tracker_port_probe(
								false, QString(),
								"Port is already open in AuTerm and cannot be probed without interfering");
				}
				return;
		}

		log_debug() << "ArsTracker port scan opening port" << ars_tracker_scan_current_port;
		configure_ars_tracker_scan_serial_port(ars_tracker_scan_current_port);
		bool open_result = ars_tracker_scan_serial_port->open(QIODevice::ReadWrite);
		log_debug() << "ArsTracker port scan port" << ars_tracker_scan_current_port
								<< "open result:" << open_result
								<< "error:" << ars_tracker_scan_serial_port->errorString();
		if (open_result == false)
		{
				complete_ars_tracker_port_probe(
						false, QString(),
						QString("Open failed: %1").arg(ars_tracker_scan_serial_port->errorString()));
				return;
		}

		QCheckBox *check_rts = parent_window->findChild<QCheckBox*>("check_RTS");
		QCheckBox *check_dtr = parent_window->findChild<QCheckBox*>("check_DTR");
		QComboBox *combo_handshake = parent_window->findChild<QComboBox*>("combo_Handshake");

		if (combo_handshake == nullptr || combo_handshake->currentIndex() != 1)
		{
				if (check_rts != nullptr)
				{
						if (ars_tracker_scan_serial_port->setRequestToSend(check_rts->isChecked()) == false)
						{
								log_warning() << "ArsTracker port scan RTS setup failed for"
															<< ars_tracker_scan_current_port << ":"
															<< ars_tracker_scan_serial_port->errorString();
						}
				}
		}

		if (check_dtr != nullptr)
		{
				if (ars_tracker_scan_serial_port->setDataTerminalReady(check_dtr->isChecked()) == false)
				{
						log_warning() << "ArsTracker port scan DTR setup failed for"
													<< ars_tracker_scan_current_port << ":"
													<< ars_tracker_scan_serial_port->errorString();
				}
		}

		ars_tracker_scan_probe_active = true;
		QTimer::singleShot(ars_tracker_port_scan_open_delay_ms, this,
											 &plugin_mcumgr::send_ars_tracker_port_probe_command);
}

void plugin_mcumgr::send_ars_tracker_port_probe_command()
{
		if (ars_tracker_port_scan_active == false || ars_tracker_scan_probe_active == false)
		{
				return;
		}

		if (ars_tracker_scan_serial_port == nullptr || ars_tracker_scan_serial_port->isOpen() == false)
		{
				log_warning() << "ArsTracker port scan send skipped because probe transport is not open for"
											<< ars_tracker_scan_current_port;
				complete_ars_tracker_port_probe(false, QString(),
																	 "Probe transport is not open after initialization");
				return;
		}

		ars_tracker_scan_processor->set_transport(ars_tracker_scan_transport);
		ars_tracker_scan_shell_mgmt->set_parameters((check_V2_Protocol->isChecked() ? 1 : 0),
																								edit_MTU->value(),
																								retries_ars_tracker_port_scan,
																								timeout_ars_tracker_port_scan_ms, 0);

		QStringList command_arguments = QStringList() << "param" << "sn";
		ars_tracker_port_scan_shell_rc = 0;
		log_debug() << "ArsTracker port scan sending param sn to"
								<< ars_tracker_scan_current_port;
		if (ars_tracker_scan_shell_mgmt->start_execute(&command_arguments,
																							 &ars_tracker_port_scan_shell_rc) == false)
		{
				QString reason =
						ars_tracker_scan_serial_port->isOpen() == false ?
								QString("Failed to start mcumgr shell command: probe transport is closed") :
								QString("Failed to start mcumgr shell command");
				complete_ars_tracker_port_probe(false, QString(), reason);
				return;
		}

		ars_tracker_scan_command_started = true;
}

void plugin_mcumgr::configure_ars_tracker_scan_serial_port(const QString &port_name)
{
		QComboBox *combo_baud = parent_window->findChild<QComboBox*>("combo_Baud");
		QComboBox *combo_data = parent_window->findChild<QComboBox*>("combo_Data");
		QComboBox *combo_stop = parent_window->findChild<QComboBox*>("combo_Stop");
		QComboBox *combo_parity = parent_window->findChild<QComboBox*>("combo_Parity");
		QComboBox *combo_handshake = parent_window->findChild<QComboBox*>("combo_Handshake");

		ars_tracker_scan_serial_port->close();
		ars_tracker_scan_serial_port->setPortName(port_name);
		ars_tracker_scan_serial_port->setBaudRate(
				combo_baud != nullptr ? combo_baud->currentText().toInt() : 115200);
		ars_tracker_scan_serial_port->setDataBits(combo_data != nullptr ?
																							(QSerialPort::DataBits)combo_data->currentText().toInt() :
																							QSerialPort::Data8);
		ars_tracker_scan_serial_port->setStopBits(combo_stop != nullptr ?
																							(QSerialPort::StopBits)combo_stop->currentText().toInt() :
																							QSerialPort::OneStop);
		ars_tracker_scan_serial_port->setParity(
				combo_parity != nullptr && combo_parity->currentIndex() == 1 ?
						QSerialPort::OddParity :
						(combo_parity != nullptr && combo_parity->currentIndex() == 2 ?
								 QSerialPort::EvenParity :
								 QSerialPort::NoParity));
		ars_tracker_scan_serial_port->setFlowControl(
				combo_handshake != nullptr && combo_handshake->currentIndex() == 1 ?
						QSerialPort::HardwareControl :
						(combo_handshake != nullptr && combo_handshake->currentIndex() == 2 ?
								 QSerialPort::SoftwareControl :
								 QSerialPort::NoFlowControl));
}

void plugin_mcumgr::complete_ars_tracker_port_probe(bool matched, const QString &serial_number,
																										const QString &reason)
{
		if (ars_tracker_scan_probe_active == false)
		{
				return;
		}

		ars_tracker_scan_probe_active = false;
		ars_tracker_scan_command_started = false;

		if (matched == true)
		{
				ars_tracker_port_scan_result_t result;
				result.port_name = ars_tracker_scan_current_port;
				result.serial_number = serial_number.trimmed();
				ars_tracker_scan_results.append(result);
				upsert_ars_tracker_device(result.port_name, result.serial_number, true, false);
				ars_tracker_device_t *device = find_ars_tracker_device_by_port(result.port_name);
				if (device != nullptr)
				{
						QString persistent_port_name = device->portName;
						device->serialPort = ars_tracker_scan_serial_port;
						device->transport = ars_tracker_scan_transport;
						device->processor = ars_tracker_scan_processor;
						device->shell = ars_tracker_scan_shell_mgmt;
						device->imgMgmt = new smp_group_img_mgmt(device->processor);
						device->imageStateList.clear();
						device->shellRc = 0;
						device->currentInfoShellCommand.clear();

						disconnect(device->shell, nullptr, this, nullptr);
						disconnect(device->transport, &smp_uart_auterm::serial_write, this,
										 &plugin_mcumgr::handle_ars_tracker_scan_serial_write);
						disconnect(device->serialPort, nullptr, this, nullptr);
#ifndef SKIPPLUGIN_LOGGER
						device->imgMgmt->set_logger(logger);
#endif
						connect(device->shell, &smp_group_shell_mgmt::status, this,
										&plugin_mcumgr::handle_ars_tracker_persistent_shell_status);
						connect(device->imgMgmt, &smp_group_img_mgmt::status, this,
										&plugin_mcumgr::handle_ars_tracker_persistent_img_status);
						connect(device->transport, &smp_uart_auterm::serial_write, this,
										[this, persistent_port_name](QByteArray *data) {
												ars_tracker_device_t *persistent_device =
														find_ars_tracker_device_by_port(persistent_port_name);
												if (persistent_device == nullptr || persistent_device->serialPort == nullptr ||
														data == nullptr ||
														persistent_device->serialPort->isOpen() == false)
												{
														return;
												}

												qint64 written = persistent_device->serialPort->write(*data);
												if (written < 0)
												{
														log_warning() << "ArsTracker persistent device write failed for"
																					<< persistent_device->portName << ":"
																					<< persistent_device->serialPort->errorString();
												}
										});
						connect(device->serialPort, &QSerialPort::readyRead, this,
										[this, persistent_port_name]() {
								ars_tracker_device_t *persistent_device =
										find_ars_tracker_device_by_port(persistent_port_name);
								if (persistent_device == nullptr || persistent_device->serialPort == nullptr ||
										persistent_device->transport == nullptr ||
										persistent_device->serialPort->isOpen() == false)
								{
										return;
								}

								QByteArray data = persistent_device->serialPort->readAll();
								if (data.isEmpty() == false)
								{
										persistent_device->transport->serial_read(&data);
								}
						});
						connect(device->serialPort, &QSerialPort::errorOccurred, this,
										[this, persistent_port_name](QSerialPort::SerialPortError error) {
												ars_tracker_device_t *persistent_device =
														find_ars_tracker_device_by_port(persistent_port_name);
												if (persistent_device == nullptr ||
														persistent_device->serialPort == nullptr ||
														error == QSerialPort::NoError)
												{
														return;
												}

												log_warning() << "ArsTracker persistent device serial error for"
																					<< persistent_device->portName << ":" << error
																					<< persistent_device->serialPort->errorString();
										});
						connect(device->transport, &smp_uart_auterm::non_smp_uart_data_received, this,
										[this, persistent_port_name](const QByteArray &data) {
												log_debug() << "ArsTracker persistent device" << persistent_port_name
																				<< "received non-SMP UART bytes" << data.size();
										});

						ars_tracker_scan_serial_port = nullptr;
						ars_tracker_scan_transport = nullptr;
						ars_tracker_scan_processor = nullptr;
						ars_tracker_scan_shell_mgmt = nullptr;
						initialize_ars_tracker_scan_probe_context();

						log_debug() << "ArsTracker port scan accepted persistent device"
												<< result.port_name << "serial:" << result.serial_number;
						log_debug() << "ArsTracker port scan keeping port open" << result.port_name;
				}
				else
				{
						log_warning() << "ArsTracker port scan accepted device but failed to store persistent context for"
													<< result.port_name;
				}
		}
		else
		{
				log_debug() << "ArsTracker port scan rejected device, closing transport"
										<< ars_tracker_scan_current_port;
				if (ars_tracker_scan_serial_port != nullptr && ars_tracker_scan_serial_port->isOpen())
				{
						ars_tracker_scan_serial_port->close();
				}
				if (ars_tracker_scan_shell_mgmt != nullptr)
				{
						ars_tracker_scan_shell_mgmt->cancel();
				}
				if (ars_tracker_scan_processor != nullptr)
				{
						ars_tracker_scan_processor->cancel();
				}
				if (ars_tracker_scan_transport != nullptr)
				{
						ars_tracker_scan_transport->reset_state();
				}
				log_debug() << "ArsTracker port scan excluded port:" << ars_tracker_scan_current_port
										<< "reason:" << reason;
		}

		QTimer::singleShot(0, this, &plugin_mcumgr::begin_next_ars_tracker_port_probe);
}

bool plugin_mcumgr::current_ars_tracker_serial_number(QString *serial_number) const
{
		const ars_tracker_info_t &info = ars_tracker->tracker_info();
		if (info.serial_number.status != ARS_TRACKER_INFO_FIELD_READY)
		{
				return false;
		}

		QString value = info.serial_number.value.trimmed();
		if (value.isEmpty())
		{
				return false;
		}

		if (serial_number != nullptr)
		{
				*serial_number = value;
		}

		return true;
}

void plugin_mcumgr::handle_ars_tracker_scan_serial_ready_read()
{
		if (ars_tracker_scan_probe_active == false || ars_tracker_scan_serial_port == nullptr ||
				ars_tracker_scan_serial_port->isOpen() == false)
		{
				return;
		}

		QByteArray data = ars_tracker_scan_serial_port->readAll();
		if (data.isEmpty() == false)
		{
				ars_tracker_scan_transport->serial_read(&data);
		}
}

void plugin_mcumgr::handle_ars_tracker_scan_serial_write(QByteArray *data)
{
		if (ars_tracker_scan_serial_port == nullptr ||
				ars_tracker_scan_serial_port->isOpen() == false || data == nullptr)
		{
				return;
		}

		qint64 written = ars_tracker_scan_serial_port->write(*data);
		if (written < 0)
		{
				log_warning() << "ArsTracker port scan write failed for"
											<< ars_tracker_scan_current_port << ":"
											<< ars_tracker_scan_serial_port->errorString();
		}
}

void plugin_mcumgr::handle_ars_tracker_scan_serial_error()
{
		if (ars_tracker_port_scan_active == false || ars_tracker_scan_probe_active == false ||
				ars_tracker_scan_serial_port == nullptr)
		{
				return;
		}

		if (ars_tracker_scan_serial_port->error() == QSerialPort::NoError)
		{
				return;
		}

		log_warning() << "ArsTracker port scan serial error for" << ars_tracker_scan_current_port
									<< ":" << ars_tracker_scan_serial_port->error()
									<< ars_tracker_scan_serial_port->errorString();
		complete_ars_tracker_port_probe(
				false, QString(),
				QString("Serial error: %1").arg(ars_tracker_scan_serial_port->errorString()));
}

void plugin_mcumgr::handle_ars_tracker_scan_shell_status(uint8_t user_data, group_status status,
																												 QString error_string)
{
		Q_UNUSED(user_data);

		if (ars_tracker_scan_probe_active == false)
		{
				return;
		}

		log_debug() << "ArsTracker port scan param sn callback for"
								<< ars_tracker_scan_current_port << "status="
								<< ars_tracker_scan_status_to_string(status) << "response=" << error_string;

		if (status != STATUS_COMPLETE)
		{
				complete_ars_tracker_port_probe(
						false, QString(),
						QString("mcumgr shell command failed: %1").arg(error_string));
				return;
		}

		if (ars_tracker_port_scan_shell_rc != 0)
		{
				complete_ars_tracker_port_probe(
						false, QString(),
						QString("mcumgr shell returned ret=%1").arg(ars_tracker_port_scan_shell_rc));
				return;
		}

		QString decoded_serial;
		QString parse_error;
		if (ars_tracker_parser::parse_param_sn_output(error_string, &decoded_serial, &parse_error) ==
				false)
		{
				complete_ars_tracker_port_probe(false, QString(), parse_error);
				return;
		}

		log_debug() << "ArsTracker port scan parsed ASCII serial for"
								<< ars_tracker_scan_current_port << ":" << decoded_serial;
		log_debug() << "ArsTracker port scan decoded serial:" << decoded_serial;
		complete_ars_tracker_port_probe(decoded_serial.startsWith("ARS"), decoded_serial,
																	 decoded_serial.startsWith("ARS") ?
																			 QString() :
																			 QString("Decoded serial does not start with ARS"));
}

void plugin_mcumgr::sync_ars_tracker_serial_controls(bool loading)
{
		if (combo_ars_tracker_port == nullptr)
		{
				return;
		}

		bool serial_open = false;
		bool serial_opening = false;
		ars_tracker_main_serial_state(&serial_open, &serial_opening);
		bool has_port = ars_tracker_combo_has_selectable_port();
		int connected_devices = ars_tracker_connected_device_count();
		ars_tracker_device_t *active_device = active_ars_tracker_device();
		QString active_port = active_device != nullptr ? active_device->portName : QString();
		ars_tracker_ui_state_t state = ars_tracker_current_ui_state(loading);
		bool combo_enabled = false;
		bool button_enabled = false;
		QString button_text = "Find trackers";

		switch (state)
		{
		case ARS_TRACKER_UI_STATE_SCANNING:
				combo_enabled = false;
				button_enabled = false;
				button_text = "Finding...";
				break;
		case ARS_TRACKER_UI_STATE_CONNECTED:
				combo_enabled = (loading == false && has_port == true);
				button_enabled = true;
				button_text = "Disconnect from all";
				break;
		case ARS_TRACKER_UI_STATE_CONNECTING:
				combo_enabled = false;
				button_enabled = false;
				button_text = "Finding...";
				break;
		case ARS_TRACKER_UI_STATE_DISCONNECTING:
				combo_enabled = false;
				button_enabled = false;
				button_text = "Disconnect from all";
				break;
		case ARS_TRACKER_UI_STATE_DISCONNECTED:
		default:
				combo_enabled = (loading == false && has_port == true);
				button_enabled = (loading == false);
				button_text = "Find trackers";
				break;
		}

		combo_ars_tracker_port->setEnabled(combo_enabled);
		btn_ars_tracker_connect->setText(button_text);
		btn_ars_tracker_connect->setEnabled(button_enabled);
		log_debug() << "ArsTracker UI state update: state="
								<< ars_tracker_ui_state_to_string(state) << "port="
								<< ars_tracker_selected_port_name() << "main serial open=" << serial_open
								<< "main serial opening=" << serial_opening << "scanRunning="
								<< ars_tracker_port_scan_active << "persistent devices connected="
								<< connected_devices << "active=" << active_port;
		log_debug() << "ArsTracker UI: combo enabled=" << combo_enabled
								<< "connectButton text=" << button_text
								<< "enabled=" << button_enabled;
		update_ars_tracker_firmware_upload_controls(loading || ars_tracker_port_scan_active);
		update_ars_tracker_shell_controls(loading || ars_tracker_port_scan_active);
}

bool plugin_mcumgr::ars_tracker_transport_usable()
{
		smp_transport* transport = active_transport();

		if (transport == nullptr)
		{
				return false;
		}

		if (transport == uart_transport)
		{
				bool open = false;

				emit plugin_serial_is_open(&open);
				return open;
		}

		return transport->is_connected() == SMP_TRANSPORT_ERROR_OK;
}

bool plugin_mcumgr::ars_tracker_tab_is_active() const
{
		return selector_tab_root != nullptr && selector_tab_root->currentWidget() == tab_ars_tracker;
}

bool plugin_mcumgr::start_ars_tracker_info_refresh(QString *error_message)
{
		if (error_message != nullptr)
		{
				error_message->clear();
		}

		if (ars_tracker_has_connected_devices())
		{
				ars_tracker_device_t *device = active_ars_tracker_device();
				if (device == nullptr || device->connected == false)
				{
						if (error_message != nullptr)
						{
								*error_message = "No active tracker selected.";
						}
						return false;
				}

				if (device->processor == nullptr || device->transport == nullptr || device->shell == nullptr ||
						device->imgMgmt == nullptr)
				{
						if (error_message != nullptr)
						{
								*error_message = "Active tracker transport is not initialized.";
						}
						return false;
				}

				if (device->processor->is_busy())
				{
						if (error_message != nullptr)
						{
								*error_message = "Another operation is already running on the active tracker.";
						}
						return false;
				}

				ars_tracker_persistent_info_refresh_port = device->portName;
				device->info_refreshing = true;
				device->currentInfoShellCommand.clear();
				device->imageStateList.clear();
				QString backend_error;
				if (ars_tracker->begin_tracker_info_refresh(&backend_error) == false)
				{
						device->info_refreshing = false;
						ars_tracker_persistent_info_refresh_port.clear();
						if (error_message != nullptr)
						{
								*error_message = backend_error;
						}
						return false;
				}

				log_debug() << "ArsTracker persistent tracker info refresh started port="
										<< device->portName << "display=" << device->displayName;
				return true;
		}

		if (ars_tracker_port_scan_active)
		{
				if (error_message != nullptr)
				{
						*error_message = "Wait for ArsTracker port scan to finish.";
				}
				return false;
		}

		if (claim_transport(lbl_ars_tracker_status) == false)
		{
				if (error_message != nullptr)
				{
						*error_message = lbl_ars_tracker_status->text();
				}
				return false;
		}

		QString backend_error;
		if (ars_tracker->begin_tracker_info_refresh(&backend_error) == false)
		{
				relase_transport();
				if (error_message != nullptr)
				{
						*error_message = backend_error;
				}
				return false;
		}

		if (ars_tracker_info_loading == false)
		{
				mode = ACTION_IDLE;
				relase_transport();
				btn_cancel->setEnabled(false);
				if (error_message != nullptr)
				{
						*error_message = "Tracker info refresh did not start.";
				}
				return false;
		}

		ars_tracker_auto_info_refresh_pending = false;
		ars_tracker_info_refresh_started_for_current_connection = true;

		return true;
}

bool plugin_mcumgr::start_ars_tracker_firmware_upload(QString *error_message)
{
		if (error_message != nullptr)
		{
				error_message->clear();
		}

		if (ars_tracker_port_scan_active)
		{
				if (error_message != nullptr)
				{
						*error_message = "Wait for ArsTracker port scan to finish.";
				}
				return false;
		}

		if (ars_tracker_firmware_upload_active || ars_tracker_firmware_erase_active)
		{
				if (error_message != nullptr)
				{
						*error_message = "Another ArsTracker operation is already running";
				}
				return false;
		}

		if (ars_tracker_any_loading())
		{
				if (error_message != nullptr)
				{
						*error_message = "Another ArsTracker operation is already running";
				}
				return false;
		}

		bool serial_open = false;
		bool serial_opening = false;
		ars_tracker_main_serial_state(&serial_open, &serial_opening);
		if (serial_open == false || serial_opening == true)
		{
				if (error_message != nullptr)
				{
						*error_message = "Connect to ArsTracker before uploading firmware.";
				}
				return false;
		}

		QString firmware_file = edit_ars_tracker_firmware_file->text().trimmed();
		if (firmware_file.isEmpty())
		{
				if (error_message != nullptr)
				{
						*error_message = "Select a .bin firmware file first.";
				}
				return false;
		}

		if (QFileInfo::exists(firmware_file) == false)
		{
				if (error_message != nullptr)
				{
						*error_message = "Selected firmware file does not exist.";
				}
				return false;
		}

		if (claim_transport(lbl_ars_tracker_status) == false)
		{
				if (error_message != nullptr)
				{
						*error_message = lbl_ars_tracker_status->text();
				}
				return false;
		}

		log_debug() << "ArsTracker firmware upload started";
		log_debug() << "ArsTracker firmware upload file:" << firmware_file;
		mode = ACTION_ARS_TRACKER_FIRMWARE_UPLOAD;
		processor->set_transport(active_transport());
		set_group_transport_settings(smp_groups.img_mgmt);
		ars_tracker_firmware_upload_hash.clear();

		bool started = smp_groups.img_mgmt->start_firmware_update(
				0, firmware_file, false, &ars_tracker_firmware_upload_hash, timeout_erase_ms);

		if (started == false)
		{
				relase_transport();
				if (error_message != nullptr && error_message->isEmpty())
				{
						*error_message = "Failed to start firmware upload.";
				}
				return false;
		}

		ars_tracker_firmware_upload_active = true;
		ars_tracker_firmware_refresh_after_erase_pending = false;
		lbl_ars_tracker_progress->setText("Firmware upload: 0%");
		lbl_ars_tracker_status->setText("Firmware upload started");
		set_ars_tracker_controls_loading(ars_tracker_any_loading());
		btn_cancel->setEnabled(true);
		return true;
}

void plugin_mcumgr::maybe_auto_refresh_ars_tracker()
{
		if (ars_tracker_auto_info_refresh_pending == false)
		{
				return;
		}

		if (ars_tracker_has_connected_devices())
		{
				ars_tracker_auto_info_refresh_pending = false;
				ars_tracker_auto_info_refresh_in_progress = false;
				ars_tracker_auto_info_refresh_attempts = 0;
				log_debug() << "ArsTracker automatic tracker info refresh skipped: reason="
										<< "persistent tracker mode is active";
				return;
		}

		QString skip_reason;
		bool serial_open = false;
		bool serial_opening = false;
		ars_tracker_main_serial_state(&serial_open, &serial_opening);

		if (ars_tracker_tab_is_active() == false)
		{
				skip_reason = "ArsTracker tab is not active";
		}
		else if (serial_open == false)
		{
				if (ars_tracker_auto_info_refresh_attempts < 5)
				{
						++ars_tracker_auto_info_refresh_attempts;
						log_debug() << "ArsTracker automatic tracker info refresh skipped: reason="
												<< "transport is not open yet, retry scheduled"
												<< "attempt=" << int(ars_tracker_auto_info_refresh_attempts)
												<< "open=" << serial_open
												<< "opening=" << serial_opening
												<< "transition=" << ars_tracker_serial_transition_active;
						QTimer::singleShot(300, this, [this]() { maybe_auto_refresh_ars_tracker(); });
						return;
				}

				skip_reason =
						QString("transport is not open after %1 retries")
								.arg(QString::number(int(ars_tracker_auto_info_refresh_attempts)));
		}
		else if (mode != ACTION_IDLE)
		{
				skip_reason = QString("mode is %1").arg(QString::number(int(mode)));
		}
		else if (ars_tracker_port_scan_active)
		{
				skip_reason = "port scan is running";
		}
		else if (ars_tracker_serial_transition_active)
		{
				skip_reason = "serial transition is still active";
		}
		else if (ars_tracker_info_loading)
		{
				skip_reason = "tracker info refresh already in progress";
		}
		else if (ars_tracker_loading)
		{
				skip_reason = "session list loading is in progress";
		}
		else if (ars_tracker_delete_loading)
		{
				skip_reason = "session delete is in progress";
		}
		else if (ars_tracker_export_loading)
		{
				skip_reason = "session export is in progress";
		}
		else if (ars_tracker_info_refresh_started_for_current_connection)
		{
				skip_reason = "tracker info refresh already started for current connection";
				ars_tracker_auto_info_refresh_pending = false;
		}

		if (skip_reason.isEmpty() == false)
		{
				if (skip_reason.startsWith("transport is not open after "))
				{
						ars_tracker_auto_info_refresh_pending = false;
				}
				log_debug() << "ArsTracker automatic tracker info refresh skipped: reason="
										<< skip_reason;
				return;
		}

		log_debug() << "ArsTracker automatic tracker info refresh started";
		ars_tracker_auto_info_refresh_in_progress = true;
		ars_tracker_auto_info_refresh_attempts = 0;

		QString start_error;
		if (start_ars_tracker_info_refresh(&start_error) == false)
		{
				ars_tracker_auto_info_refresh_in_progress = false;
				log_debug() << "ArsTracker automatic tracker info refresh skipped: reason="
										<< (start_error.isEmpty() ? QString("start request failed") : start_error);
				return;
		}
}

void plugin_mcumgr::on_btn_ars_tracker_info_refresh_clicked()
{
		QString backend_error;
		if (start_ars_tracker_info_refresh(&backend_error) == false)
		{
				if (backend_error.isEmpty() == false)
				{
						lbl_ars_tracker_status->setText(backend_error);
				}
		}
}

void plugin_mcumgr::on_btn_ars_tracker_refresh_clicked()
{
		if (ars_tracker_has_connected_devices())
		{
				lbl_ars_tracker_status->setText(
						"Persistent tracker operations will be enabled in next iteration.");
				return;
		}

		QString backend_error;

		if (ars_tracker->begin_session_list_request(&backend_error) == false)
		{
				lbl_ars_tracker_status->setText(backend_error);
				return;
		}

		if (claim_transport(lbl_ars_tracker_status) == false)
		{
				ars_tracker->handle_session_list_response(STATUS_ERROR,
																									QString("Error: Could not claim transport"), -1);
				return;
		}

		mode = ACTION_ARS_TRACKER_SESSION_LIST;
		processor->set_transport(active_transport());
		set_group_transport_settings(smp_groups.shell_mgmt);

		QStringList list_arguments = QStringList() << "meas" << "ls";
		bool started = smp_groups.shell_mgmt->start_execute(&list_arguments, &ars_tracker_shell_rc);

		if (started == false)
		{
				relase_transport();
				ars_tracker->handle_session_list_response(STATUS_ERROR,
																									QString("Failed to start shell command."), -1);
		}
}

void plugin_mcumgr::on_btn_ars_tracker_delete_clicked()
{
		QListWidgetItem* selected_item = list_ars_tracker_sessions->currentItem();

		if (selected_item == nullptr)
		{
				lbl_ars_tracker_status->setText(QString("Select a session first."));
				return;
		}

		QString backend_error;
		QString session_name;

		if (ars_tracker->begin_session_delete(selected_item->data(Qt::UserRole).toString(),
																					&session_name, &backend_error) == false)
		{
				lbl_ars_tracker_status->setText(backend_error);
				return;
		}

		if (claim_transport(lbl_ars_tracker_status) == false)
		{
				ars_tracker->handle_session_delete_response(STATUS_ERROR,
																										QString("Error: Could not claim transport"),
																										-1);
				return;
		}

		mode = ACTION_ARS_TRACKER_DELETE_SESSION;
		processor->set_transport(active_transport());
		set_group_transport_settings(smp_groups.shell_mgmt);

		QStringList list_arguments = QStringList() << "meas" << "rm" << session_name;
		bool started = smp_groups.shell_mgmt->start_execute(&list_arguments, &ars_tracker_shell_rc);

		if (started == false)
		{
				relase_transport();
				ars_tracker->handle_session_delete_response(STATUS_ERROR,
																										QString("Failed to start shell command."),
																										-1);
		}
		else
		{
				btn_cancel->setEnabled(true);
		}
}

void plugin_mcumgr::on_btn_ars_tracker_download_clicked()
{
		QListWidgetItem* selected_item = list_ars_tracker_sessions->currentItem();

		if (selected_item == nullptr)
		{
				lbl_ars_tracker_status->setText(QString("Select a session first."));
				return;
		}

		if (claim_transport(lbl_ars_tracker_status) == false)
		{
				return;
		}

		QString backend_error;

		if (ars_tracker->begin_session_export(selected_item->data(Qt::UserRole).toString(),
																					edit_ars_tracker_destination->text(),
																					&backend_error) == false)
		{
				relase_transport();
				lbl_ars_tracker_status->setText(backend_error);
		}
}

void plugin_mcumgr::on_btn_ars_tracker_destination_clicked()
{
		QString selected_directory = QFileDialog::getExistingDirectory(
				parent_window, "Select export folder", edit_ars_tracker_destination->text());

		if (selected_directory.isEmpty() == false)
		{
				edit_ars_tracker_destination->setText(selected_directory);
				set_ars_tracker_controls_loading(ars_tracker_any_loading());
		}
}

void plugin_mcumgr::on_btn_ars_tracker_firmware_browse_clicked()
{
		QString selected_file = QFileDialog::getOpenFileName(
				parent_window, tr("Open firmware file"), edit_ars_tracker_firmware_file->text(),
				tr("Binary Files (*.bin);;All Files (*)"));

		if (selected_file.isEmpty())
		{
				return;
		}

		edit_ars_tracker_firmware_file->setText(selected_file);
		log_debug() << "ArsTracker firmware file selected:" << selected_file;
		lbl_ars_tracker_status->setText(QString("Firmware file selected: %1").arg(selected_file));
		update_ars_tracker_firmware_upload_controls(ars_tracker_any_loading() || ars_tracker_port_scan_active);
}

void plugin_mcumgr::on_btn_ars_tracker_firmware_upload_clicked()
{
		QString error_message;

		if (start_ars_tracker_firmware_upload(&error_message) == false)
		{
				if (error_message.isEmpty() == false)
				{
						lbl_ars_tracker_status->setText(error_message);
				}
		}
}

void plugin_mcumgr::on_btn_ars_tracker_firmware_erase_clicked()
{
		if (ars_tracker_port_scan_active || ars_tracker_any_loading())
		{
				lbl_ars_tracker_status->setText("Another ArsTracker operation is already running");
				return;
		}

		bool serial_open = false;
		bool serial_opening = false;
		ars_tracker_main_serial_state(&serial_open, &serial_opening);
		if (serial_open == false || serial_opening == true)
		{
				lbl_ars_tracker_status->setText("Connect to ArsTracker before erasing second slot.");
				return;
		}

		QString second_slot_value = edit_ars_tracker_firmware_second_slot->text().trimmed();
		if (second_slot_value.isEmpty() || second_slot_value.compare("empty", Qt::CaseInsensitive) == 0 ||
				second_slot_value.compare("Not loaded", Qt::CaseInsensitive) == 0 ||
				second_slot_value.startsWith("Loading", Qt::CaseInsensitive) ||
				second_slot_value.startsWith("Error", Qt::CaseInsensitive))
		{
				lbl_ars_tracker_status->setText("Second slot is already empty.");
				return;
		}

		if (claim_transport(lbl_ars_tracker_status) == false)
		{
				return;
		}

		log_debug() << "ArsTracker firmware erase second slot requested";
		log_debug() << "ArsTracker firmware erase second slot started";
		mode = ACTION_ARS_TRACKER_FIRMWARE_ERASE;
		processor->set_transport(active_transport());
		set_group_transport_settings(smp_groups.img_mgmt, timeout_erase_ms);
		bool started = smp_groups.img_mgmt->start_image_erase(1);

		if (started == false)
		{
				relase_transport();
				lbl_ars_tracker_status->setText("Failed to start second slot erase.");
				return;
		}

		ars_tracker_firmware_erase_active = true;
		ars_tracker_firmware_refresh_after_erase_pending = false;
		lbl_ars_tracker_progress->setText("Erasing second slot...");
		lbl_ars_tracker_status->setText("Erasing second slot...");
		set_ars_tracker_controls_loading(ars_tracker_any_loading());
		btn_cancel->setEnabled(true);
}

void plugin_mcumgr::append_ars_tracker_shell_output(const QString &text)
{
		if (text_ars_tracker_shell_output == nullptr || text.isEmpty())
		{
				return;
		}

		QString output_text = text;
		if (output_text.endsWith('\n') == false)
		{
				output_text.append('\n');
		}

		text_ars_tracker_shell_output->add_dat_in_text(output_text.toUtf8());
		text_ars_tracker_shell_output->update_display();
}

void plugin_mcumgr::append_ars_tracker_device_log(const QByteArray &data)
{
		if (data.isEmpty())
		{
				return;
		}

		log_debug() << "ArsTracker device logs appended bytes:" << data.size();
		if (text_ars_tracker_device_logs == nullptr)
		{
				return;
		}

		text_ars_tracker_device_logs->add_dat_in_text(data);
		text_ars_tracker_device_logs->update_display();
}

void plugin_mcumgr::append_ars_tracker_device_log_text(const QString &text)
{
		if (text_ars_tracker_device_logs == nullptr || text.isEmpty())
		{
				return;
		}

		QString output_text = text;
		output_text.replace("\r\n", "\n");
		output_text.replace('\r', '\n');
		text_ars_tracker_device_logs->add_dat_in_text(output_text.toUtf8());
		text_ars_tracker_device_logs->update_display();
}

bool plugin_mcumgr::start_ars_tracker_shell_command(const QString &command, QString *error_message)
{
		if (error_message != nullptr)
		{
				error_message->clear();
		}

		QString trimmed_command = command.trimmed();
		if (trimmed_command.isEmpty())
		{
				if (error_message != nullptr)
				{
						*error_message = "No shell command to send";
				}
				return false;
		}

		if (ars_tracker_port_scan_active || ars_tracker_any_loading())
		{
				QString busy_message = "Busy: another ArsTracker operation is running";
				log_debug() << "ArsTracker shell command skipped: busy";
				append_ars_tracker_shell_output(busy_message);
				if (error_message != nullptr)
				{
						*error_message = busy_message;
				}
				return false;
		}

		bool serial_open = false;
		bool serial_opening = false;
		ars_tracker_main_serial_state(&serial_open, &serial_opening);
		if (serial_open == false || serial_opening == true)
		{
				QString disconnected_message = "Tracker is not connected";
				log_debug() << "ArsTracker shell command skipped: disconnected";
				if (error_message != nullptr)
				{
						*error_message = disconnected_message;
				}
				return false;
		}

		if (claim_transport(lbl_ars_tracker_status) == false)
		{
				QString claim_message = lbl_ars_tracker_status != nullptr ? lbl_ars_tracker_status->text() :
																												QString("Error: Could not claim transport");
				log_debug() << "ArsTracker shell command failed:" << claim_message;
				append_ars_tracker_shell_output(QString("Error: %1").arg(claim_message));
				if (error_message != nullptr)
				{
						*error_message = claim_message;
				}
				return false;
		}

		QRegularExpression re_temp_re("\\s+");
		QStringList list_arguments = trimmed_command.split(re_temp_re, Qt::SkipEmptyParts);
		if (list_arguments.isEmpty())
		{
				relase_transport();
				if (error_message != nullptr)
				{
						*error_message = "No shell command to send";
				}
				return false;
		}

		log_debug() << "ArsTracker shell command requested:" << trimmed_command;
		mode = ACTION_ARS_TRACKER_SHELL_COMMAND;
		processor->set_transport(active_transport());
		set_group_transport_settings(smp_groups.shell_mgmt);
		ars_tracker_shell_command_rc = 0;
		bool started = smp_groups.shell_mgmt->start_execute(&list_arguments, &ars_tracker_shell_command_rc);
		if (started == false)
		{
				mode = ACTION_IDLE;
				relase_transport();
				QString start_message = "Failed to start shell command.";
				log_debug() << "ArsTracker shell command failed:" << start_message;
				append_ars_tracker_shell_output(QString("> %1").arg(trimmed_command));
				append_ars_tracker_shell_output(QString("Error: %1").arg(start_message));
				if (error_message != nullptr)
				{
						*error_message = start_message;
				}
				return false;
		}

		log_debug() << "ArsTracker shell command sent";
		append_ars_tracker_shell_output(QString("> %1").arg(trimmed_command));
		ars_tracker_shell_command_active = true;
		edit_ars_tracker_shell_command->clear();
		lbl_ars_tracker_status->setText("ArsTracker shell command sent...");
		set_ars_tracker_controls_loading(ars_tracker_any_loading());
		return true;
}

void plugin_mcumgr::on_btn_ars_tracker_shell_send_clicked()
{
		QString error_message;
		QString command = edit_ars_tracker_shell_command != nullptr ?
											edit_ars_tracker_shell_command->text() :
											QString();

		if (start_ars_tracker_shell_command(command, &error_message) == false &&
				error_message.isEmpty() == false)
		{
				lbl_ars_tracker_status->setText(error_message);
		}
}

void plugin_mcumgr::on_btn_ars_tracker_shell_clear_clicked()
{
		if (text_ars_tracker_shell_output != nullptr)
		{
				text_ars_tracker_shell_output->clear_dat_in();
				text_ars_tracker_shell_output->update_display();
		}
}

void plugin_mcumgr::on_btn_ars_tracker_cancel_clicked()
{
		if (ars_tracker_firmware_upload_active == true || ars_tracker_firmware_erase_active == true)
		{
				lbl_ars_tracker_status->setText(ars_tracker_firmware_erase_active ?
																			 "Cancelling firmware erase..." :
																			 "Cancelling firmware upload...");

				if (mode == ACTION_ARS_TRACKER_FIRMWARE_RESET)
				{
						smp_groups.os_mgmt->cancel();
				}
				else
				{
						smp_groups.img_mgmt->cancel();
				}

				return;
		}

		if (ars_tracker_shell_command_active == true)
		{
				lbl_ars_tracker_status->setText("Cancelling ArsTracker shell command...");
				smp_groups.shell_mgmt->cancel();
				return;
		}

		ars_tracker->cancel_all();
}

void plugin_mcumgr::ars_tracker_status_message(const QString &message)
{
		lbl_ars_tracker_status->setText(message);
}

void plugin_mcumgr::update_ars_tracker_status_indicator(const QString &raw_status)
{
		if (label_ars_tracker_status_state == nullptr)
		{
				return;
		}

		log_debug() << "ArsTracker status raw value:" << raw_status;
		ars_tracker_parsed_status_t parsed_status = parse_ars_tracker_status_text(raw_status);
		log_debug() << "ArsTracker parsed status code=" << parsed_status.code << "name="
								<< parsed_status.name << "color=" << parsed_status.color;

		QString indicator_text = QString::fromUtf8("\xE2\x97\x8F ") + parsed_status.name;
		label_ars_tracker_status_state->setText(indicator_text);
		label_ars_tracker_status_state->setStyleSheet(
				QString("color: #%1;").arg(parsed_status.color));

		log_debug() << "ArsTracker UI set parsed status indicator:" << indicator_text
								<< "widget" << label_ars_tracker_status_state << "objectName"
								<< label_ars_tracker_status_state->objectName() << "currentText"
								<< label_ars_tracker_status_state->text();
}

bool plugin_mcumgr::ars_tracker_any_loading() const
{
		return ars_tracker_info_loading || ars_tracker_loading || ars_tracker_delete_loading ||
					 ars_tracker_export_loading || ars_tracker_firmware_upload_active ||
					 ars_tracker_firmware_erase_active || ars_tracker_shell_command_active;
}

void plugin_mcumgr::update_ars_tracker_firmware_upload_controls(bool controls_locked)
{
		if (btn_ars_tracker_firmware_upload == nullptr || btn_ars_tracker_firmware_browse == nullptr ||
				edit_ars_tracker_firmware_file == nullptr || btn_ars_tracker_firmware_erase == nullptr ||
				edit_ars_tracker_firmware_second_slot == nullptr)
		{
				return;
		}

		bool serial_open = false;
		bool serial_opening = false;
		ars_tracker_main_serial_state(&serial_open, &serial_opening);
		bool firmware_file_exists =
				edit_ars_tracker_firmware_file->text().isEmpty() == false &&
				QFileInfo::exists(edit_ars_tracker_firmware_file->text());
		QString second_slot_value = edit_ars_tracker_firmware_second_slot->text().trimmed();
		bool second_slot_has_image =
				second_slot_value.isEmpty() == false &&
				second_slot_value.compare("empty", Qt::CaseInsensitive) != 0 &&
				second_slot_value.compare("Not loaded", Qt::CaseInsensitive) != 0 &&
				second_slot_value.startsWith("Loading", Qt::CaseInsensitive) == false &&
				second_slot_value.startsWith("Error", Qt::CaseInsensitive) == false;

		btn_ars_tracker_firmware_browse->setEnabled(!controls_locked);
		btn_ars_tracker_firmware_upload->setEnabled(!controls_locked && serial_open == true &&
																						 serial_opening == false &&
																						 firmware_file_exists);
		btn_ars_tracker_firmware_erase->setEnabled(!controls_locked && serial_open == true &&
																					 serial_opening == false &&
																					 second_slot_has_image);
		log_debug() << "ArsTracker firmware UI controls:"
								<< "browseEnabled=" << btn_ars_tracker_firmware_browse->isEnabled()
								<< "uploadEnabled=" << btn_ars_tracker_firmware_upload->isEnabled()
								<< "eraseEnabled=" << btn_ars_tracker_firmware_erase->isEnabled()
								<< "serialOpen=" << serial_open << "serialOpening=" << serial_opening
								<< "fileExists=" << firmware_file_exists
								<< "secondSlotHasImage=" << second_slot_has_image
								<< "controlsLocked=" << controls_locked;
}

void plugin_mcumgr::update_ars_tracker_shell_controls(bool controls_locked)
{
		if (button_ars_tracker_shell_send == nullptr || edit_ars_tracker_shell_command == nullptr ||
				button_ars_tracker_shell_clear == nullptr)
		{
				return;
		}

		bool serial_open = false;
		bool serial_opening = false;
		ars_tracker_main_serial_state(&serial_open, &serial_opening);
		bool send_enabled = serial_open == true && serial_opening == false && controls_locked == false;

		edit_ars_tracker_shell_command->setEnabled(true);
		button_ars_tracker_shell_send->setEnabled(send_enabled);
		button_ars_tracker_shell_clear->setEnabled(true);
}

void plugin_mcumgr::ars_tracker_info_changed(const ars_tracker_info_t &info)
{
		auto field_display_text = [](const ars_tracker_info_field_t& field) -> QString {
				if (field.status == ARS_TRACKER_INFO_FIELD_ERROR)
				{
						return field.value.isEmpty() ? QString("Error: %1").arg(field.error) : field.value;
				}

				if (field.status == ARS_TRACKER_INFO_FIELD_LOADING)
				{
						return field.value.isEmpty() ? QString("Loading...") : field.value;
				}

				if (field.value.isEmpty() == false)
				{
						return field.value;
				}

				return "Not loaded";
		};
		auto set_tracker_info_value = [this](QLineEdit* widget, const char* label, const QString& value) {
				if (widget == nullptr)
				{
						return;
				}

				widget->setText(value);
				widget->setCursorPosition(0);
				log_debug() << "ArsTracker UI set" << label << ":" << widget->text()
										<< "widget" << widget << "objectName" << widget->objectName();
		};

		set_tracker_info_value(edit_ars_tracker_serial_number, "Serial number",
												 field_display_text(info.serial_number));
		set_tracker_info_value(edit_ars_tracker_board_id, "Board id",
												 field_display_text(info.board_id));
		set_tracker_info_value(edit_ars_tracker_type, "Type",
												 field_display_text(info.tracker_type));
		set_tracker_info_value(edit_ars_tracker_status_value, "Status",
												 field_display_text(info.tracker_status));
		update_ars_tracker_status_indicator(field_display_text(info.tracker_status));
		set_tracker_info_value(edit_ars_tracker_battery_info, "Battery Info",
												 info.batteryInfoText.isEmpty() ? field_display_text(info.battery_info) :
																										 info.batteryInfoText);

		set_tracker_info_value(edit_ars_tracker_memory_usage, "Memory usage",
												 info.memoryUsageText.isEmpty() ? field_display_text(info.memory_usage) :
																										 info.memoryUsageText);
		set_tracker_info_value(edit_ars_tracker_bad_blocks, "Bad blocks",
												 info.badBlocksText.isEmpty() ? field_display_text(info.bad_blocks) :
																								 info.badBlocksText);
		set_tracker_info_value(edit_ars_tracker_firmware_current_version, "Current version",
												 field_display_text(info.firmware_current_version));
		set_tracker_info_value(edit_ars_tracker_firmware_second_slot, "Second slot",
												 field_display_text(info.firmware_second_slot));
		update_ars_tracker_firmware_upload_controls(ars_tracker_any_loading() ||
																						 ars_tracker_port_scan_active);
}

void plugin_mcumgr::ars_tracker_info_loading_changed(bool loading)
{
		ars_tracker_info_loading = loading;
		if (loading == false && ars_tracker_persistent_info_refresh_port.trimmed().isEmpty() == false)
		{
				ars_tracker_device_t *device =
						find_ars_tracker_device_by_port(ars_tracker_persistent_info_refresh_port);
				if (device != nullptr)
				{
						device->info = ars_tracker->tracker_info();
						device->info_loaded = true;
						device->info_refreshing = false;
						device->currentInfoShellCommand.clear();
						bool has_error =
								device->info.serial_number.status == ARS_TRACKER_INFO_FIELD_ERROR ||
								device->info.board_id.status == ARS_TRACKER_INFO_FIELD_ERROR ||
								device->info.tracker_type.status == ARS_TRACKER_INFO_FIELD_ERROR ||
								device->info.tracker_status.status == ARS_TRACKER_INFO_FIELD_ERROR ||
								device->info.battery_info.status == ARS_TRACKER_INFO_FIELD_ERROR ||
								device->info.memory_usage.status == ARS_TRACKER_INFO_FIELD_ERROR ||
								device->info.bad_blocks.status == ARS_TRACKER_INFO_FIELD_ERROR ||
								device->info.firmware_current_version.status == ARS_TRACKER_INFO_FIELD_ERROR ||
								device->info.firmware_second_slot.status == ARS_TRACKER_INFO_FIELD_ERROR;
						if (has_error)
						{
								log_warning() << "ArsTracker persistent tracker info refresh failed port="
															<< device->portName << "reason=one or more fields failed to load";
						}
						else
						{
								log_debug() << "ArsTracker persistent tracker info refresh finished port="
														<< device->portName;
						}
				}
				ars_tracker_persistent_info_refresh_port.clear();
		}
		if (loading == false && ars_tracker_auto_info_refresh_in_progress == true)
		{
				ars_tracker_auto_info_refresh_in_progress = false;
				log_debug() << "ArsTracker automatic tracker info refresh finished";
		}
		set_ars_tracker_controls_loading(
				ars_tracker_any_loading());
}

void plugin_mcumgr::ars_tracker_sessions_ready(const QList<ars_tracker_session_t> &sessions)
{
		QString previous_selection_id;

		if (ars_tracker_clear_selection_on_next_refresh == false)
		{
				QListWidgetItem* current_item = list_ars_tracker_sessions->currentItem();

				if (current_item != nullptr)
				{
						previous_selection_id = current_item->data(Qt::UserRole).toString();
				}
		}

		list_ars_tracker_sessions->clear();

		for (const ars_tracker_session_t& session : sessions)
		{
				QListWidgetItem* item =
						new QListWidgetItem(session.display_name, list_ars_tracker_sessions);
				item->setData(Qt::UserRole, session.id);
		}

		if (ars_tracker_clear_selection_on_next_refresh == false)
		{
				bool restored_selection = false;

				if (previous_selection_id.isEmpty() == false)
				{
						for (int i = 0; i < list_ars_tracker_sessions->count(); ++i)
						{
								QListWidgetItem* item = list_ars_tracker_sessions->item(i);

								if (item != nullptr && item->data(Qt::UserRole).toString() == previous_selection_id)
								{
										list_ars_tracker_sessions->setCurrentItem(item);
										restored_selection = true;
										break;
								}
						}
				}

				if (restored_selection == false && sessions.length() > 0)
				{
						list_ars_tracker_sessions->setCurrentRow(0);
				}
		}

		ars_tracker_clear_selection_on_next_refresh = false;
}

void plugin_mcumgr::ars_tracker_loading_changed(bool loading)
{
		ars_tracker_loading = loading;
		set_ars_tracker_controls_loading(ars_tracker_any_loading());
}

void plugin_mcumgr::ars_tracker_delete_loading_changed(bool loading)
{
		ars_tracker_delete_loading = loading;
		set_ars_tracker_controls_loading(ars_tracker_any_loading());
}

void plugin_mcumgr::on_list_ars_tracker_sessions_itemSelectionChanged()
{
		if (list_ars_tracker_sessions->currentItem() != nullptr && ars_tracker_loading == false)
		{
				ars_tracker_clear_selection_on_next_refresh = false;
		}

		set_ars_tracker_controls_loading(ars_tracker_any_loading());
}

void plugin_mcumgr::ars_tracker_export_loading_changed(bool loading)
{
		ars_tracker_export_loading = loading;
		set_ars_tracker_controls_loading(ars_tracker_any_loading());
}

void plugin_mcumgr::ars_tracker_export_progress_changed(const QString &progress_text)
{
		lbl_ars_tracker_progress->setText(progress_text);
}

void plugin_mcumgr::ars_tracker_export_file_list_changed(const QStringList &rows)
{
		list_ars_tracker_files->clear();
		list_ars_tracker_files->addItems(rows);
}

void plugin_mcumgr::ars_tracker_export_finished(bool success, bool cancelled, const QString &message)
{
		Q_UNUSED(success);
		Q_UNUSED(cancelled);

		reset_ars_tracker_export_fs_operation();
		mode = ACTION_IDLE;
		relase_transport();
		btn_cancel->setEnabled(false);
		lbl_ars_tracker_status->setText(message);
}

void plugin_mcumgr::ars_tracker_request_info_shell_command(const QStringList &arguments)
{
		if (ars_tracker_has_connected_devices())
		{
				ars_tracker_device_t *device = persistent_ars_tracker_refresh_device();
				QString command_text = arguments.join(' ');
				if (device == nullptr || device->processor == nullptr || device->transport == nullptr ||
						device->shell == nullptr)
				{
						ars_tracker->handle_tracker_info_response(
								STATUS_PROCESSOR_TRANSPORT_ERROR,
								QString("No active persistent tracker transport is available."), -1);
						return;
				}

				device->processor->set_transport(device->transport);
				device->shell->set_parameters((check_V2_Protocol->isChecked() ? 1 : 0), edit_MTU->value(),
																			device->transport->get_retries(),
																			device->transport->get_timeout(),
																			ACTION_ARS_TRACKER_INFO_REFRESH);
				device->shellRc = 0;
				device->currentInfoShellCommand = command_text;
				log_debug() << "ArsTracker persistent shell command sent port=" << device->portName
										<< "command=\"" << command_text << "\"";
				QStringList command_arguments = arguments;
				bool started = device->shell->start_execute(&command_arguments, &device->shellRc);
				if (started == false)
				{
						log_warning() << "ArsTracker persistent tracker info refresh failed port="
													<< device->portName << "reason=failed to start shell command";
						ars_tracker->handle_tracker_info_response(
								STATUS_PROCESSOR_TRANSPORT_ERROR,
								QString("Failed to start persistent shell command."), -1);
				}
				return;
		}

		mode = ACTION_ARS_TRACKER_INFO_REFRESH;
		processor->set_transport(active_transport());
		set_group_transport_settings(smp_groups.shell_mgmt);

		QStringList command_arguments = arguments;
		bool started =
				smp_groups.shell_mgmt->start_execute(&command_arguments, &ars_tracker_shell_rc);

		if (started == false)
		{
				ars_tracker->handle_tracker_info_response(STATUS_PROCESSOR_TRANSPORT_ERROR,
																								 QString("Failed to start shell command."), -1);
		}
		else
		{
				btn_cancel->setEnabled(true);
		}
}

void plugin_mcumgr::ars_tracker_request_cancel_info_shell_command()
{
		if (ars_tracker_has_connected_devices())
		{
				ars_tracker_device_t *device = persistent_ars_tracker_refresh_device();
				if (device != nullptr && device->shell != nullptr)
				{
						device->shell->cancel();
				}
				return;
		}

		smp_groups.shell_mgmt->cancel();
}

void plugin_mcumgr::ars_tracker_request_info_image_state()
{
		if (ars_tracker_has_connected_devices())
		{
				ars_tracker_device_t *device = persistent_ars_tracker_refresh_device();
				if (device == nullptr || device->processor == nullptr || device->transport == nullptr ||
						device->imgMgmt == nullptr)
				{
						ars_tracker->handle_tracker_firmware_state_response(
								STATUS_PROCESSOR_TRANSPORT_ERROR,
								QString("No active persistent tracker image transport is available."),
								QList<image_state_t>());
						return;
				}

				device->processor->set_transport(device->transport);
				device->imgMgmt->set_parameters((check_V2_Protocol->isChecked() ? 1 : 0), edit_MTU->value(),
																			device->transport->get_retries(),
																			device->transport->get_timeout(),
																			ACTION_ARS_TRACKER_FIRMWARE_STATE);
				device->imageStateList.clear();
				log_debug() << "ArsTracker persistent image state request sent port="
										<< device->portName;
				bool started = device->imgMgmt->start_image_get(&device->imageStateList);
				if (started == false)
				{
						log_warning() << "ArsTracker persistent tracker info refresh failed port="
													<< device->portName << "reason=failed to start image state request";
						ars_tracker->handle_tracker_firmware_state_response(
								STATUS_PROCESSOR_TRANSPORT_ERROR,
								QString("Failed to start persistent image state request."),
								device->imageStateList);
				}
				return;
		}

		log_debug() << "ArsTracker firmware image state requested";
		mode = ACTION_ARS_TRACKER_FIRMWARE_STATE;
		processor->set_transport(active_transport());
		set_group_transport_settings(smp_groups.img_mgmt);
		ars_tracker_image_state_list.clear();

		bool started = smp_groups.img_mgmt->start_image_get(&ars_tracker_image_state_list);

		if (started == false)
		{
				ars_tracker->handle_tracker_firmware_state_response(
						STATUS_PROCESSOR_TRANSPORT_ERROR, QString("Failed to start image state request."),
						ars_tracker_image_state_list);

				if (ars_tracker_info_loading == false)
				{
						mode = ACTION_IDLE;
						relase_transport();
						btn_cancel->setEnabled(false);
						set_ars_tracker_controls_loading(ars_tracker_any_loading());
				}
		}
		else
		{
				btn_cancel->setEnabled(true);
		}
}

void plugin_mcumgr::ars_tracker_request_cancel_info_image_state()
{
		if (ars_tracker_has_connected_devices())
		{
				ars_tracker_device_t *device = persistent_ars_tracker_refresh_device();
				if (device != nullptr && device->imgMgmt != nullptr)
				{
						device->imgMgmt->cancel();
				}
				return;
		}

		smp_groups.img_mgmt->cancel();
}

void plugin_mcumgr::handle_ars_tracker_persistent_shell_status(uint8_t user_data,
																															 group_status status,
																															 QString error_string)
{
		if (user_data != ACTION_ARS_TRACKER_INFO_REFRESH)
		{
				return;
		}

		QObject *signal_sender = sender();
		ars_tracker_device_t *device = nullptr;
		for (int i = 0; i < ars_tracker_devices.size(); ++i)
		{
				if (ars_tracker_devices[i].shell == signal_sender)
				{
						device = &ars_tracker_devices[i];
						break;
				}
		}

		if (device == nullptr)
		{
				return;
		}

		if (ars_tracker_persistent_info_refresh_port.compare(device->portName, Qt::CaseInsensitive) != 0)
		{
				log_debug() << "ArsTracker persistent shell status ignored for stale device port="
										<< device->portName;
				return;
		}

		log_debug() << "ArsTracker persistent shell response port=" << device->portName
								<< "command=\"" << device->currentInfoShellCommand << "\""
								<< "response=\"" << error_string << "\"";
		if (status != STATUS_COMPLETE)
		{
				log_warning() << "ArsTracker persistent tracker info refresh failed port="
											<< device->portName << "reason=" << error_string;
		}
		ars_tracker->handle_tracker_info_response(status, error_string, device->shellRc);
}

void plugin_mcumgr::handle_ars_tracker_persistent_img_status(uint8_t user_data,
																														 group_status status,
																														 QString error_string)
{
		if (user_data != ACTION_ARS_TRACKER_FIRMWARE_STATE)
		{
				return;
		}

		QObject *signal_sender = sender();
		ars_tracker_device_t *device = nullptr;
		for (int i = 0; i < ars_tracker_devices.size(); ++i)
		{
				if (ars_tracker_devices[i].imgMgmt == signal_sender)
				{
						device = &ars_tracker_devices[i];
						break;
				}
		}

		if (device == nullptr)
		{
				return;
		}

		if (ars_tracker_persistent_info_refresh_port.compare(device->portName, Qt::CaseInsensitive) != 0)
		{
				log_debug() << "ArsTracker persistent image state status ignored for stale device port="
										<< device->portName;
				return;
		}

		if (status != STATUS_COMPLETE)
		{
				log_warning() << "ArsTracker persistent tracker info refresh failed port="
											<< device->portName << "reason=" << error_string;
		}
		ars_tracker->handle_tracker_firmware_state_response(status, error_string, device->imageStateList);
}

void plugin_mcumgr::ars_tracker_request_session_refresh_after_delete()
{
		ars_tracker_clear_selection_on_next_refresh = true;
		on_btn_ars_tracker_refresh_clicked();
}

QString plugin_mcumgr::ars_tracker_export_fs_phase_name(ars_tracker_export_fs_phase_t phase) const
{
		switch (phase)
		{
		case ARS_TRACKER_EXPORT_FS_IDLE:
				return "idle";
		case ARS_TRACKER_EXPORT_FS_HASH_SUPPORT:
				return "hash_support";
		case ARS_TRACKER_EXPORT_FS_METADATA:
				return "metadata";
		case ARS_TRACKER_EXPORT_FS_DOWNLOAD:
				return "download";
		}

		return "unknown";
}

uint32_t plugin_mcumgr::begin_ars_tracker_export_fs_operation(
		ars_tracker_export_fs_phase_t phase, const QString &remote_file, const QString &local_temp_file)
{
		++ars_tracker_export_fs_sequence;
		ars_tracker_export_fs_active = true;
		ars_tracker_export_fs_phase = phase;
		ars_tracker_export_fs_remote_file = remote_file;
		ars_tracker_export_fs_local_temp_file = local_temp_file;
		ars_tracker_export_fs_hash_checksum_response.clear();
		ars_tracker_export_fs_size_response = 0;

		if (phase == ARS_TRACKER_EXPORT_FS_HASH_SUPPORT)
		{
				ars_tracker_export_supported_hash_checksum_list.clear();
		}

		log_debug() << "ArsTracker export fs op begin seq" << ars_tracker_export_fs_sequence
								<< "phase" << ars_tracker_export_fs_phase_name(phase)
								<< "remote" << remote_file
								<< "local temp" << local_temp_file;

		return ars_tracker_export_fs_sequence;
}

void plugin_mcumgr::reset_ars_tracker_export_fs_operation()
{
		if (ars_tracker_export_fs_active == true ||
				ars_tracker_export_fs_phase != ARS_TRACKER_EXPORT_FS_IDLE)
		{
				log_debug() << "ArsTracker export fs op reset seq" << ars_tracker_export_fs_sequence
										<< "phase" << ars_tracker_export_fs_phase_name(ars_tracker_export_fs_phase)
										<< "remote" << ars_tracker_export_fs_remote_file;
		}

		ars_tracker_export_fs_active = false;
		ars_tracker_export_fs_phase = ARS_TRACKER_EXPORT_FS_IDLE;
		ars_tracker_export_fs_remote_file.clear();
		ars_tracker_export_fs_local_temp_file.clear();
		ars_tracker_export_fs_hash_checksum_response.clear();
		ars_tracker_export_fs_size_response = 0;
}

bool plugin_mcumgr::ars_tracker_export_fs_start_failed(uint32_t sequence,
																											 ars_tracker_export_fs_phase_t phase,
																											 const QString &remote_file,
																											 const QString &error_message)
{
		if (ars_tracker_export_fs_active == false || ars_tracker_export_fs_sequence != sequence ||
				ars_tracker_export_fs_phase != phase || ars_tracker_export_fs_remote_file != remote_file)
		{
				log_debug() << "ArsTracker export fs start failure ignored as stale seq" << sequence
										<< "current seq" << ars_tracker_export_fs_sequence
										<< "phase" << ars_tracker_export_fs_phase_name(phase)
										<< "current phase"
										<< ars_tracker_export_fs_phase_name(ars_tracker_export_fs_phase)
										<< "remote" << remote_file
										<< "current remote" << ars_tracker_export_fs_remote_file;
				return false;
		}

		log_debug() << "ArsTracker export fs start failed seq" << sequence
								<< "phase" << ars_tracker_export_fs_phase_name(phase)
								<< "remote" << remote_file << "error" << error_message;
		reset_ars_tracker_export_fs_operation();
		return true;
}

void plugin_mcumgr::handle_ars_tracker_export_fs_status(uint8_t user_data, group_status status,
																												const QString &error_string)
{
		ars_tracker_export_fs_phase_t expected_phase = ARS_TRACKER_EXPORT_FS_IDLE;

		if (user_data == ACTION_ARS_TRACKER_EXPORT_HASH_SUPPORT)
		{
				expected_phase = ARS_TRACKER_EXPORT_FS_HASH_SUPPORT;
		}
		else if (user_data == ACTION_ARS_TRACKER_EXPORT_METADATA)
		{
				expected_phase = ARS_TRACKER_EXPORT_FS_METADATA;
		}
		else if (user_data == ACTION_ARS_TRACKER_EXPORT_DOWNLOAD)
		{
				expected_phase = ARS_TRACKER_EXPORT_FS_DOWNLOAD;
		}

		if (ars_tracker_export_fs_active == false ||
				ars_tracker_export_fs_phase != expected_phase)
		{
				log_debug() << "ArsTracker export fs status ignored as stale:"
										<< "user_data" << int(user_data)
										<< "status" << int(status)
										<< "expected phase"
										<< ars_tracker_export_fs_phase_name(expected_phase)
										<< "current phase"
										<< ars_tracker_export_fs_phase_name(ars_tracker_export_fs_phase)
										<< "active" << ars_tracker_export_fs_active
										<< "seq" << ars_tracker_export_fs_sequence
										<< "remote" << ars_tracker_export_fs_remote_file;
				return;
		}

		const uint32_t sequence = ars_tracker_export_fs_sequence;
		const QString remote_file = ars_tracker_export_fs_remote_file;
		const QString local_temp_file = ars_tracker_export_fs_local_temp_file;
		const QByteArray hash_response = ars_tracker_export_fs_hash_checksum_response;
		const uint32_t size_response = ars_tracker_export_fs_size_response;
		const QList<hash_checksum_t> supported_hashes =
				ars_tracker_export_supported_hash_checksum_list;

		log_debug() << "ArsTracker export fs status seq" << sequence
								<< "phase" << ars_tracker_export_fs_phase_name(expected_phase)
								<< "remote" << remote_file
								<< "local temp" << local_temp_file
								<< "status" << int(status)
								<< "size" << size_response
								<< "hash" << hash_response.toHex();

		reset_ars_tracker_export_fs_operation();

		if (expected_phase == ARS_TRACKER_EXPORT_FS_HASH_SUPPORT)
		{
				ars_tracker->handle_export_hash_support_result(status, error_string, supported_hashes);
		}
		else if (expected_phase == ARS_TRACKER_EXPORT_FS_METADATA)
		{
				ars_tracker->handle_file_metadata_result(status, error_string, hash_response, size_response);
		}
		else if (expected_phase == ARS_TRACKER_EXPORT_FS_DOWNLOAD)
		{
				ars_tracker->handle_file_download_result(status, error_string);
		}
}

void plugin_mcumgr::ars_tracker_request_file_hash_support()
{
		mode = ACTION_ARS_TRACKER_EXPORT_HASH_SUPPORT;
		processor->set_transport(active_transport());
		set_group_transport_settings(smp_groups.fs_mgmt, ACTION_ARS_TRACKER_EXPORT_HASH_SUPPORT);

		const uint32_t sequence = begin_ars_tracker_export_fs_operation(
				ARS_TRACKER_EXPORT_FS_HASH_SUPPORT, QString());

		bool started =
				smp_groups.fs_mgmt->start_supported_hashes_checksums(
						&ars_tracker_export_supported_hash_checksum_list);

		if (started == false)
		{
				if (ars_tracker_export_fs_start_failed(
								sequence, ARS_TRACKER_EXPORT_FS_HASH_SUPPORT, QString(),
								QString("Could not query tracker file hash support.")) == true)
				{
						ars_tracker->handle_export_hash_support_result(
								STATUS_PROCESSOR_TRANSPORT_ERROR,
								QString("Could not query tracker file hash support."), QList<hash_checksum_t>());
				}
		}
		else
		{
				log_debug() << "ArsTracker export hash support started seq" << sequence;
				btn_cancel->setEnabled(true);
		}
}

void plugin_mcumgr::ars_tracker_request_file_metadata(const QString &remote_file,
																											const QString &hash_name)
{
		mode = ACTION_ARS_TRACKER_EXPORT_METADATA;
		processor->set_transport(active_transport());
		if (hash_name.isEmpty())
		{
				set_group_transport_settings(smp_groups.fs_mgmt, ACTION_ARS_TRACKER_EXPORT_METADATA);
		}
		else
		{
				set_group_transport_settings(smp_groups.fs_mgmt, ACTION_ARS_TRACKER_EXPORT_METADATA,
																		 timeout_ars_tracker_metadata_ms,
																		 retries_ars_tracker_metadata);
				log_debug() << "ArsTracker export metadata hash request using long timeout/no retry"
										<< "remote" << remote_file
										<< "hash type" << hash_name
										<< "timeout ms" << timeout_ars_tracker_metadata_ms
										<< "retries" << int(retries_ars_tracker_metadata);
		}

		const uint32_t sequence = begin_ars_tracker_export_fs_operation(
				ARS_TRACKER_EXPORT_FS_METADATA, remote_file);

		bool started = false;

		if (hash_name.isEmpty())
		{
				started = smp_groups.fs_mgmt->start_status(
						remote_file, &ars_tracker_export_fs_size_response);
		}
		else
		{
				started = smp_groups.fs_mgmt->start_hash_checksum(remote_file, hash_name,
																													&ars_tracker_export_fs_hash_checksum_response,
																													&ars_tracker_export_fs_size_response);
		}

		if (started == false)
		{
				QString error_message = hash_name.isEmpty() ?
																QString("Could not start remote file check.") :
																QString("Could not start remote file verification.");

				if (ars_tracker_export_fs_start_failed(
								sequence, ARS_TRACKER_EXPORT_FS_METADATA, remote_file, error_message) == true)
				{
						ars_tracker->handle_file_metadata_result(STATUS_PROCESSOR_TRANSPORT_ERROR,
																										 error_message, QByteArray(), 0);
				}
		}
		else
		{
				log_debug() << "ArsTracker export metadata started seq" << sequence
										<< "remote" << remote_file
										<< "hash type" << hash_name;
				btn_cancel->setEnabled(true);
		}
}

void plugin_mcumgr::ars_tracker_request_file_download(const QString &remote_file, const QString &local_temp_file)
{
		mode = ACTION_ARS_TRACKER_EXPORT_DOWNLOAD;
		processor->set_transport(active_transport());
		set_group_transport_settings(smp_groups.fs_mgmt, ACTION_ARS_TRACKER_EXPORT_DOWNLOAD);

		const uint32_t sequence = begin_ars_tracker_export_fs_operation(
				ARS_TRACKER_EXPORT_FS_DOWNLOAD, remote_file, local_temp_file);

		bool started = smp_groups.fs_mgmt->start_download(remote_file, local_temp_file);

		if (started == false)
		{
				QString error_message = QString("Could not start file transfer.");

				if (ars_tracker_export_fs_start_failed(
								sequence, ARS_TRACKER_EXPORT_FS_DOWNLOAD, remote_file, error_message) == true)
				{
						ars_tracker->handle_file_download_result(STATUS_PROCESSOR_TRANSPORT_ERROR,
																										 error_message);
				}
		}
		else
		{
				log_debug() << "ArsTracker export download started seq" << sequence
										<< "remote" << remote_file
										<< "local temp" << local_temp_file;
				btn_cancel->setEnabled(true);
		}
}

void plugin_mcumgr::ars_tracker_request_cancel_file_download()
{
		if (ars_tracker_export_fs_active == false)
		{
				log_debug() << "ArsTracker export fs cancel ignored; no active fs op";
				return;
		}

		log_debug() << "ArsTracker export fs cancel seq" << ars_tracker_export_fs_sequence
								<< "phase"
								<< ars_tracker_export_fs_phase_name(ars_tracker_export_fs_phase)
								<< "remote" << ars_tracker_export_fs_remote_file;
		smp_groups.fs_mgmt->cancel();
}

void plugin_mcumgr::handle_ars_tracker_shell_status(uint8_t user_data, group_status status,
																										QString *error_string)
{
		if (user_data == ACTION_ARS_TRACKER_INFO_REFRESH)
		{
				ars_tracker->handle_tracker_info_response(status, *error_string, ars_tracker_shell_rc);
		} else if (user_data == ACTION_ARS_TRACKER_SESSION_LIST)
		{
				ars_tracker->handle_session_list_response(status, *error_string, ars_tracker_shell_rc);
		} else if (user_data == ACTION_ARS_TRACKER_DELETE_SESSION)
		{
				ars_tracker->handle_session_delete_response(status, *error_string, ars_tracker_shell_rc);
		}

		*error_string = nullptr;
}

void plugin_mcumgr::set_ars_tracker_controls_loading(bool loading)
{
		bool controls_locked = loading || ars_tracker_port_scan_active;
		bool persistent_mode_active = ars_tracker_has_connected_devices();
		ars_tracker_device_t *persistent_active_device = active_ars_tracker_device();
		QString persistent_tooltip =
				persistent_mode_active ?
						QString("Persistent tracker operations will be enabled in next iteration.") :
						QString();
		bool refresh_enabled = false;
		QString refresh_tooltip = persistent_tooltip;
		if (persistent_mode_active)
		{
				refresh_enabled = !controls_locked && persistent_active_device != nullptr &&
													persistent_active_device->connected == true;
				refresh_tooltip.clear();
		}
		else
		{
				refresh_enabled = !controls_locked;
		}
		btn_ars_tracker_info_refresh->setEnabled(refresh_enabled);
		btn_ars_tracker_info_refresh->setToolTip(refresh_tooltip);
		list_ars_tracker_sessions->setEnabled(!controls_locked && persistent_mode_active == false);
		edit_ars_tracker_destination->setEnabled(!controls_locked && persistent_mode_active == false);
		btn_ars_tracker_destination->setEnabled(!controls_locked && persistent_mode_active == false);
		edit_ars_tracker_destination->setToolTip(persistent_tooltip);
		btn_ars_tracker_destination->setToolTip(persistent_tooltip);
		sync_ars_tracker_serial_controls(controls_locked);

		bool has_selection = list_ars_tracker_sessions->currentItem() != nullptr;

		btn_ars_tracker_delete->setEnabled(!controls_locked && persistent_mode_active == false &&
																		 has_selection);
		btn_ars_tracker_download->setEnabled(!controls_locked && persistent_mode_active == false &&
																			 has_selection);
		btn_ars_tracker_delete->setToolTip(persistent_tooltip);
		btn_ars_tracker_download->setToolTip(persistent_tooltip);
		update_ars_tracker_firmware_upload_controls(controls_locked);
		update_ars_tracker_shell_controls(controls_locked);
		btn_ars_tracker_cancel->setEnabled(ars_tracker_info_loading || ars_tracker_export_loading ||
																		 ars_tracker_firmware_upload_active ||
																		 ars_tracker_shell_command_active);
}

AutPlugin::PluginType plugin_mcumgr::plugin_type()
{
		return AutPlugin::Feature;
}

QObject *plugin_mcumgr::plugin_object()
{
		return this;
}

void plugin_mcumgr::update_img_state_table()
{
		QStandardItem* table_entry;

		if (images_list.length() > 1)
		{
				// Multiple images
				uint8_t i = 0;

				while (i < images_list.length())
				{
						QStandardItem* table_sub_entry;
						uint8_t        l = 0;

						table_entry =
								new QStandardItem(QString("Image ") % QString::number(images_list[i].image));
						table_entry->setData(i);

						while (l < images_list[i].slot_list.length())
						{
								table_sub_entry = new QStandardItem(
										QString("Slot ") % QString::number(images_list[i].slot_list[l].slot));
								table_sub_entry->setData(l);
								table_entry->appendRow(table_sub_entry);
								++l;
						}

						model_image_state.appendRow(table_entry);
						++i;
				}
		} else if (images_list.length() == 1)
		{
				// Single image
				QStandardItem* table_sub_entry;
				uint8_t        i = 0;

				table_entry = new QStandardItem("Images");
				table_entry->setData(0);

				while (i < images_list[0].slot_list.length())
				{
						table_sub_entry = new QStandardItem(QString("Slot ") %
																								QString::number(images_list[0].slot_list[i].slot));
						table_sub_entry->setData(i);
						table_entry->appendRow(table_sub_entry);
						++i;
				}

				model_image_state.appendRow(table_entry);
		}
}
