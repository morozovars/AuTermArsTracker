#include "ars_tracker_backend.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QtGlobal>

#include "ars_tracker_parser.h"
#include "debug_logger.h"

static const QStringList ars_tracker_fixed_files =
    QStringList() << "trace.csv" << "processedStr.csv" << "battery.csv";

static QString format_export_byte_count(qint64 bytes)
{
    static const char* suffixes[] = {"B", "KB", "MB", "GB", "TB"};

    double value = double(qMax<qint64>(bytes, 0));
    int    suffix_index = 0;

    while (value >= 1024.0 && suffix_index < 4)
    {
        value /= 1024.0;
        ++suffix_index;
    }

    int precision = (suffix_index == 0 || value >= 100.0) ? 0 : (value >= 10.0 ? 1 : 2);
    return QString("%1%2").arg(QString::number(value, 'f', precision), suffixes[suffix_index]);
}
static QString normalize_hash_name(const QString& hash_name)
{
    QString normalized = hash_name.trimmed().toLower();
    normalized.remove('_');
    normalized.remove('-');
    normalized.remove(' ');
    normalized.remove('(');
    normalized.remove(')');
    normalized.remove('.');
    return normalized;
}

static bool is_crc32_hash_name(const QString& hash_name)
{
    QString normalized = normalize_hash_name(hash_name);
    return normalized == "crc32" || normalized == "ieeecrc32" ||
           normalized == "crc32ieee" || normalized == "crc32ethernet";
}

static bool local_hash_algorithm_from_name(const QString& hash_name,
                                           QCryptographicHash::Algorithm* algorithm)
{
    QString normalized = normalize_hash_name(hash_name);

    if (normalized == "sha256")
    {
        *algorithm = QCryptographicHash::Sha256;
        return true;
    }

    if (normalized == "sha384")
    {
        *algorithm = QCryptographicHash::Sha384;
        return true;
    }

    if (normalized == "sha512")
    {
        *algorithm = QCryptographicHash::Sha512;
        return true;
    }

    if (normalized == "sha1")
    {
        *algorithm = QCryptographicHash::Sha1;
        return true;
    }

    if (normalized == "md5")
    {
        *algorithm = QCryptographicHash::Md5;
        return true;
    }

    return false;
}

static uint32_t crc32_ieee_update(uint32_t crc, const char* data, qint64 length)
{
    static uint32_t table[256];
    static bool     table_ready = false;

    if (table_ready == false)
    {
        for (uint32_t i = 0; i < 256; ++i)
        {
            uint32_t value = i;

            for (int bit = 0; bit < 8; ++bit)
            {
                value = (value & 1U) ? ((value >> 1U) ^ 0xEDB88320U) : (value >> 1U);
            }

            table[i] = value;
        }

        table_ready = true;
    }

    for (qint64 i = 0; i < length; ++i)
    {
        crc = table[(crc ^ uint8_t(data[i])) & 0xffU] ^ (crc >> 8U);
    }

    return crc;
}

ars_tracker_backend::ars_tracker_backend(QObject* parent) : QObject(parent)
{
    loading                  = false;
    tracker_info_loading     = false;
    active_tracker_info_step = TRACKER_INFO_STEP_NONE;
    tracker_info_session_list_failed = false;
    delete_loading           = false;
    export_transition_sequence = 0;
    reset_tracker_info_state();
    reset_export_state();
#ifndef SKIPPLUGIN_LOGGER
    logger = nullptr;
#endif
}

ars_tracker_backend::~ars_tracker_backend()
{
}

void ars_tracker_backend::reset_tracker_info_state()
{
    latest_tracker_info.serial_number.value.clear();
    latest_tracker_info.serial_number.error.clear();
    latest_tracker_info.serial_number.status = ARS_TRACKER_INFO_FIELD_IDLE;
    latest_tracker_info.board_id.value.clear();
    latest_tracker_info.board_id.error.clear();
    latest_tracker_info.board_id.status      = ARS_TRACKER_INFO_FIELD_IDLE;
    latest_tracker_info.tracker_type.value.clear();
    latest_tracker_info.tracker_type.error.clear();
    latest_tracker_info.tracker_type.status  = ARS_TRACKER_INFO_FIELD_IDLE;
    latest_tracker_info.tracker_status.value.clear();
    latest_tracker_info.tracker_status.error.clear();
    latest_tracker_info.tracker_status.status = ARS_TRACKER_INFO_FIELD_IDLE;
    latest_tracker_info.battery_info.value.clear();
    latest_tracker_info.battery_info.error.clear();
    latest_tracker_info.battery_info.status  = ARS_TRACKER_INFO_FIELD_IDLE;
    latest_tracker_info.batteryInfoText.clear();
    latest_tracker_info.memory_usage.value.clear();
    latest_tracker_info.memory_usage.error.clear();
    latest_tracker_info.memory_usage.status  = ARS_TRACKER_INFO_FIELD_IDLE;
    latest_tracker_info.memoryUsageText.clear();
    latest_tracker_info.bad_blocks.value.clear();
    latest_tracker_info.bad_blocks.error.clear();
    latest_tracker_info.bad_blocks.status    = ARS_TRACKER_INFO_FIELD_IDLE;
    latest_tracker_info.badBlocksText.clear();
}

void ars_tracker_backend::reset_export_state()
{
    ++export_transition_sequence;
    export_loading           = false;
    export_cancel_requested  = false;
    export_failed            = false;
    sensors_enumeration_done = false;
    export_hash_ready        = false;
    active_session_id.clear();
    active_session_remote_root.clear();
    active_destination_path.clear();
    export_hash_name.clear();
    current_download_index = -1;
    next_sensor_index      = 0;
    download_queue.clear();
}

bool ars_tracker_backend::begin_session_list_request(QString* error_message)
{
    if (tracker_info_loading == true)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Tracker info refresh is already in progress.");
        }

        return false;
    }

    if (delete_loading == true)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("A session delete is already in progress.");
        }

        return false;
    }

    if (export_loading == true)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("A session export is already in progress.");
        }

        return false;
    }

    if (loading == true)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Session list request is already in progress.");
        }

        return false;
    }

    loading = true;
    emit loading_changed(loading);
    emit status_message(QString("Loading sessions..."));

    return true;
}

void ars_tracker_backend::handle_session_list_response(group_status   status,
                                                       const QString& shell_output,
                                                       int32_t        shell_ret)
{
    loading = false;
    emit loading_changed(loading);

    if (status == STATUS_COMPLETE)
    {
        if (shell_ret != 0)
        {
            emit status_message(QString("Session list command failed, shell ret: %1")
                                    .arg(QString::number(shell_ret)));
            return;
        }

        QString                      parse_error;
        QList<ars_tracker_session_t> parsed_sessions;

        if (ars_tracker_parser::parse_meas_ls_output(shell_output, &parsed_sessions,
                                                     &parse_error) == false)
        {
            latest_sessions.clear();
            emit session_list_ready(latest_sessions);
            emit status_message(parse_error);
            return;
        }

        latest_sessions = parsed_sessions;
        emit session_list_ready(latest_sessions);
        emit status_message(
            QString("Loaded %1 sessions.").arg(QString::number(latest_sessions.length())));
        return;
    }

    if (status == STATUS_TIMEOUT)
    {
        emit status_message(QString("Session list request timed out."));
    } else if (status == STATUS_CANCELLED)
    {
        emit status_message(QString("Session list request cancelled."));
    } else if (status == STATUS_PROCESSOR_TRANSPORT_ERROR)
    {
        emit status_message(QString("Session list failed due to transport error."));
    } else if (status == STATUS_TRANSPORT_DISCONNECTED)
    {
        emit status_message(QString("Session list failed: transport disconnected."));
    } else
    {
        emit status_message(shell_output.isEmpty() ? QString("Session list request failed.") :
                                                     shell_output);
    }
}

const QList<ars_tracker_session_t>& ars_tracker_backend::sessions() const
{
    return latest_sessions;
}

const ars_tracker_info_t& ars_tracker_backend::tracker_info() const
{
    return latest_tracker_info;
}

ars_tracker_info_field_t* ars_tracker_backend::tracker_info_field_for_step(
    tracker_info_step_t step)
{
    if (step == TRACKER_INFO_STEP_SN)
    {
        return &latest_tracker_info.serial_number;
    } else if (step == TRACKER_INFO_STEP_BID)
    {
        return &latest_tracker_info.board_id;
    } else if (step == TRACKER_INFO_STEP_TYPE)
    {
        return &latest_tracker_info.tracker_type;
    } else if (step == TRACKER_INFO_STEP_STATUS)
    {
        return &latest_tracker_info.tracker_status;
    } else if (step == TRACKER_INFO_STEP_BATTERY_INFO)
    {
        return &latest_tracker_info.battery_info;
    } else if (step == TRACKER_INFO_STEP_MEMORY_USAGE)
    {
        return &latest_tracker_info.memory_usage;
    } else if (step == TRACKER_INFO_STEP_BAD_BLOCKS)
    {
        return &latest_tracker_info.bad_blocks;
    }

    return nullptr;
}

QString* ars_tracker_backend::tracker_info_text_for_step(tracker_info_step_t step)
{
    if (step == TRACKER_INFO_STEP_BATTERY_INFO)
    {
        return &latest_tracker_info.batteryInfoText;
    } else if (step == TRACKER_INFO_STEP_MEMORY_USAGE)
    {
        return &latest_tracker_info.memoryUsageText;
    } else if (step == TRACKER_INFO_STEP_BAD_BLOCKS)
    {
        return &latest_tracker_info.badBlocksText;
    }

    return nullptr;
}

ars_tracker_backend::tracker_info_step_t ars_tracker_backend::next_tracker_info_step(
    tracker_info_step_t step) const
{
    if (step == TRACKER_INFO_STEP_SN)
    {
        return TRACKER_INFO_STEP_BID;
    } else if (step == TRACKER_INFO_STEP_BID)
    {
        return TRACKER_INFO_STEP_TYPE;
    } else if (step == TRACKER_INFO_STEP_TYPE)
    {
        return TRACKER_INFO_STEP_STATUS;
    } else if (step == TRACKER_INFO_STEP_STATUS)
    {
        return TRACKER_INFO_STEP_BATTERY_INFO;
    } else if (step == TRACKER_INFO_STEP_BATTERY_INFO)
    {
        return TRACKER_INFO_STEP_MEMORY_USAGE;
    } else if (step == TRACKER_INFO_STEP_MEMORY_USAGE)
    {
        return TRACKER_INFO_STEP_BAD_BLOCKS;
    } else if (step == TRACKER_INFO_STEP_BAD_BLOCKS)
    {
        return TRACKER_INFO_STEP_SESSION_LIST;
    }

    return TRACKER_INFO_STEP_NONE;
}

QStringList ars_tracker_backend::tracker_info_command_arguments(tracker_info_step_t step) const
{
    if (step == TRACKER_INFO_STEP_SN)
    {
        return QStringList() << "param" << "sn";
    } else if (step == TRACKER_INFO_STEP_BID)
    {
        return QStringList() << "param" << "bid";
    } else if (step == TRACKER_INFO_STEP_TYPE)
    {
        return QStringList() << "param" << "type";
    } else if (step == TRACKER_INFO_STEP_STATUS)
    {
        return QStringList() << "status";
    } else if (step == TRACKER_INFO_STEP_BATTERY_INFO)
    {
        return QStringList() << "bat" << "i";
    } else if (step == TRACKER_INFO_STEP_MEMORY_USAGE)
    {
        return QStringList() << "mem" << "i";
    } else if (step == TRACKER_INFO_STEP_BAD_BLOCKS)
    {
        return QStringList() << "bbm" << "bb";
    } else if (step == TRACKER_INFO_STEP_SESSION_LIST)
    {
        return QStringList() << "meas" << "ls";
    }

    return QStringList();
}

QString ars_tracker_backend::tracker_info_step_name(tracker_info_step_t step) const
{
    if (step == TRACKER_INFO_STEP_SN)
    {
        return "serial number";
    } else if (step == TRACKER_INFO_STEP_BID)
    {
        return "board id";
    } else if (step == TRACKER_INFO_STEP_TYPE)
    {
        return "tracker type";
    } else if (step == TRACKER_INFO_STEP_STATUS)
    {
        return "tracker status";
    } else if (step == TRACKER_INFO_STEP_BATTERY_INFO)
    {
        return "battery info";
    } else if (step == TRACKER_INFO_STEP_MEMORY_USAGE)
    {
        return "memory usage";
    } else if (step == TRACKER_INFO_STEP_BAD_BLOCKS)
    {
        return "bad blocks";
    } else if (step == TRACKER_INFO_STEP_SESSION_LIST)
    {
        return "session list";
    }

    return "tracker info";
}

void ars_tracker_backend::set_tracker_info_field_error(tracker_info_step_t step,
                                                       const QString&      message)
{
    ars_tracker_info_field_t* field = tracker_info_field_for_step(step);
    QString*                  text_field = tracker_info_text_for_step(step);

    if (field == nullptr)
    {
        return;
    }

    field->status = ARS_TRACKER_INFO_FIELD_ERROR;
    field->error  = message;
    field->value  = QString("Error: %1").arg(message);

    if (text_field != nullptr)
    {
        *text_field = field->value;
    }
}

void ars_tracker_backend::begin_tracker_info_step(tracker_info_step_t step)
{
    ars_tracker_info_field_t* field = tracker_info_field_for_step(step);
    QString*                  text_field = tracker_info_text_for_step(step);
    QString                   command_text = tracker_info_command_arguments(step).join(' ');

    active_tracker_info_step = step;

    if (field != nullptr)
    {
        field->status = ARS_TRACKER_INFO_FIELD_LOADING;
        field->error.clear();

        if (text_field != nullptr)
        {
            field->value = QString("Loading...");
            *text_field = field->value;
        }
    }

    if (step == TRACKER_INFO_STEP_BATTERY_INFO || step == TRACKER_INFO_STEP_MEMORY_USAGE ||
        step == TRACKER_INFO_STEP_BAD_BLOCKS)
    {
        log_debug() << "ArsTracker shell command" << command_text << "sent";
    }

    emit tracker_info_changed(latest_tracker_info);
    emit request_tracker_info_shell_command(tracker_info_command_arguments(step));
}

int ars_tracker_backend::tracker_info_error_count() const
{
    int error_count = 0;

    if (latest_tracker_info.serial_number.status == ARS_TRACKER_INFO_FIELD_ERROR)
    {
        ++error_count;
    }

    if (latest_tracker_info.board_id.status == ARS_TRACKER_INFO_FIELD_ERROR)
    {
        ++error_count;
    }

    if (latest_tracker_info.tracker_type.status == ARS_TRACKER_INFO_FIELD_ERROR)
    {
        ++error_count;
    }

    if (latest_tracker_info.tracker_status.status == ARS_TRACKER_INFO_FIELD_ERROR)
    {
        ++error_count;
    }

    if (latest_tracker_info.battery_info.status == ARS_TRACKER_INFO_FIELD_ERROR)
    {
        ++error_count;
    }

    if (latest_tracker_info.memory_usage.status == ARS_TRACKER_INFO_FIELD_ERROR)
    {
        ++error_count;
    }

    if (latest_tracker_info.bad_blocks.status == ARS_TRACKER_INFO_FIELD_ERROR)
    {
        ++error_count;
    }

    if (tracker_info_session_list_failed == true)
    {
        ++error_count;
    }

    return error_count;
}

void ars_tracker_backend::finish_tracker_info_refresh(const QString& message)
{
    tracker_info_loading     = false;
    active_tracker_info_step = TRACKER_INFO_STEP_NONE;
    log_debug() << "ArsTracker tracker info diagnostics final texts:"
                << "battery=" << latest_tracker_info.batteryInfoText
                << "memory=" << latest_tracker_info.memoryUsageText
                << "badBlocks=" << latest_tracker_info.badBlocksText;
    log_debug() << "ArsTracker tracker info diagnostics finished";
    emit tracker_info_changed(latest_tracker_info);
    emit tracker_info_loading_changed(false);
    emit status_message(message);
}

bool ars_tracker_backend::begin_tracker_info_refresh(QString* error_message)
{
    if (tracker_info_loading == true)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Tracker info refresh is already in progress.");
        }

        return false;
    }

    if (loading == true)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("A session list request is already in progress.");
        }

        return false;
    }

    if (delete_loading == true)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("A session delete is already in progress.");
        }

        return false;
    }

    if (export_loading == true)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("A session export is already in progress.");
        }

        return false;
    }

    tracker_info_loading             = true;
    tracker_info_session_list_failed = false;
    log_debug() << "ArsTracker tracker info diagnostics started";
    emit tracker_info_loading_changed(true);
    emit status_message(QString("Loading tracker info and sessions..."));
    begin_tracker_info_step(TRACKER_INFO_STEP_SN);
    return true;
}

void ars_tracker_backend::handle_tracker_info_response(group_status   status,
                                                       const QString& shell_output,
                                                       int32_t        shell_ret)
{
    if (tracker_info_loading == false || active_tracker_info_step == TRACKER_INFO_STEP_NONE)
    {
        return;
    }

    tracker_info_step_t step          = active_tracker_info_step;
    tracker_info_step_t next_step     = next_tracker_info_step(step);
    bool                continue_flow = false;
    QString             parsed_value;
    QString             parse_error;
    QString             final_message;
    QString             command_text  = tracker_info_command_arguments(step).join(' ');
    bool                diagnostics_step =
        (step == TRACKER_INFO_STEP_BATTERY_INFO || step == TRACKER_INFO_STEP_MEMORY_USAGE ||
         step == TRACKER_INFO_STEP_BAD_BLOCKS);

    if (status == STATUS_COMPLETE)
    {
        if (diagnostics_step == true)
        {
            log_debug() << "ArsTracker" << command_text << "raw response:" << shell_output.trimmed();
        }

        if (shell_ret != 0)
        {
            QString shell_error =
                QString("Command failed, shell ret: %1").arg(QString::number(shell_ret));

            if (step == TRACKER_INFO_STEP_SESSION_LIST)
            {
                tracker_info_session_list_failed = true;
                final_message =
                    QString("Session list refresh command failed, shell ret: %1")
                        .arg(QString::number(shell_ret));
            } else
            {
                set_tracker_info_field_error(step, shell_error);

                if (diagnostics_step == true)
                {
                    log_warning() << "ArsTracker" << command_text << "status error:" << shell_error;
                }
            }

            continue_flow = (next_step != TRACKER_INFO_STEP_NONE);
        } else
        {
            bool parsed_ok = false;

            if (step == TRACKER_INFO_STEP_SN)
            {
                parsed_ok = ars_tracker_parser::parse_param_sn_output(shell_output, &parsed_value,
                                                                     &parse_error);
            } else if (step == TRACKER_INFO_STEP_BID)
            {
                parsed_ok = ars_tracker_parser::parse_param_bid_output(shell_output, &parsed_value,
                                                                      &parse_error);
            } else if (step == TRACKER_INFO_STEP_TYPE)
            {
                parsed_ok = ars_tracker_parser::parse_param_type_output(shell_output, &parsed_value,
                                                                       &parse_error);
            } else if (step == TRACKER_INFO_STEP_STATUS)
            {
                parsed_ok = ars_tracker_parser::parse_status_output(shell_output, &parsed_value,
                                                                    &parse_error);
            } else if (step == TRACKER_INFO_STEP_BATTERY_INFO)
            {
                ars_tracker_parser::battery_info_t battery_info;
                parsed_ok = ars_tracker_parser::parse_battery_info_output(
                    shell_output, &battery_info, &parsed_value, &parse_error);

                if (parsed_ok == true)
                {
                    log_debug() << "ArsTracker bat i parsed:"
                                << "volt=" << battery_info.volt_mV
                                << "cur=" << battery_info.cur_mA
                                << "soc=" << battery_info.soc
                                << "full=" << battery_info.fullCap_mAh
                                << "remain=" << battery_info.remainCap_mAh
                                << "t2e=" << battery_info.t2eMin
                                << "t2f=" << battery_info.t2fMin
                                << "available=" << battery_info.availableCap_mAh
                                << "temp=" << battery_info.temp
                                << "cycles=" << battery_info.cycles;
                    log_debug() << "ArsTracker bat i formatted UI value:" << parsed_value;
                }
            } else if (step == TRACKER_INFO_STEP_MEMORY_USAGE)
            {
                ars_tracker_parser::memory_usage_t memory_usage;
                parsed_ok = ars_tracker_parser::parse_memory_usage_output(
                    shell_output, &memory_usage, &parsed_value, &parse_error);

                if (parsed_ok == true)
                {
                    log_debug() << "ArsTracker mem i parsed:"
                                << "total=" << qulonglong(memory_usage.total_bytes)
                                << "used=" << qulonglong(memory_usage.used_bytes)
                                << "percent=" << QString::number(memory_usage.percent, 'f', 1);
                    log_debug() << "ArsTracker mem i formatted UI value:" << parsed_value;
                }
            } else if (step == TRACKER_INFO_STEP_BAD_BLOCKS)
            {
                ars_tracker_parser::bad_blocks_t bad_blocks;
                parsed_ok = ars_tracker_parser::parse_bad_blocks_output(
                    shell_output, &bad_blocks, &parsed_value, &parse_error);

                if (parsed_ok == true)
                {
                    if (bad_blocks.count_mismatch == true)
                    {
                        log_warning() << "ArsTracker bbm bb parsed block count mismatch:"
                                      << "count=" << bad_blocks.count
                                      << "parsed=" << bad_blocks.blocks.length();
                    }

                    log_debug() << "ArsTracker bbm bb parsed:"
                                << "count=" << bad_blocks.count
                                << "blocks=" << bad_blocks.blocks;
                    log_debug() << "ArsTracker bbm bb formatted UI value:" << parsed_value;
                }
            } else if (step == TRACKER_INFO_STEP_SESSION_LIST)
            {
                QList<ars_tracker_session_t> parsed_sessions;
                parsed_ok = ars_tracker_parser::parse_meas_ls_output(shell_output, &parsed_sessions,
                                                                     &parse_error);

                if (parsed_ok == true)
                {
                    latest_sessions = parsed_sessions;
                    emit session_list_ready(latest_sessions);
                } else
                {
                    tracker_info_session_list_failed = true;
                }
            }

            if (parsed_ok == true && step != TRACKER_INFO_STEP_SESSION_LIST)
            {
                ars_tracker_info_field_t* field = tracker_info_field_for_step(step);
                QString*                  text_field = tracker_info_text_for_step(step);

                if (field != nullptr)
                {
                    field->value  = parsed_value;
                    field->error.clear();
                    field->status = ARS_TRACKER_INFO_FIELD_READY;
                }

                if (text_field != nullptr)
                {
                    *text_field = parsed_value;
                }
            } else
            {
                if (step != TRACKER_INFO_STEP_SESSION_LIST)
                {
                    set_tracker_info_field_error(step, parse_error);

                    if (diagnostics_step == true)
                    {
                        log_warning() << "ArsTracker" << command_text << "parse error:"
                                      << parse_error;
                    }
                }
            }

            continue_flow = (next_step != TRACKER_INFO_STEP_NONE);
        }
    } else if (status == STATUS_TIMEOUT)
    {
        if (step == TRACKER_INFO_STEP_SESSION_LIST)
        {
            tracker_info_session_list_failed = true;
        } else
        {
            set_tracker_info_field_error(step,
                                         QString("%1 request timed out.")
                                             .arg(tracker_info_step_name(step)));
        }
        final_message =
            QString("Tracker info refresh timed out while loading %1.")
                .arg(tracker_info_step_name(step));
        if (diagnostics_step == true)
        {
            log_warning() << "ArsTracker" << command_text << "timeout";
        }
    } else if (status == STATUS_CANCELLED)
    {
        if (step == TRACKER_INFO_STEP_SESSION_LIST)
        {
            tracker_info_session_list_failed = true;
        } else
        {
            set_tracker_info_field_error(step,
                                         QString("%1 request cancelled.")
                                             .arg(tracker_info_step_name(step)));
        }
        final_message =
            QString("Tracker info refresh cancelled while loading %1.")
                .arg(tracker_info_step_name(step));
        if (diagnostics_step == true)
        {
            log_warning() << "ArsTracker" << command_text << "cancelled";
        }
    } else if (status == STATUS_PROCESSOR_TRANSPORT_ERROR)
    {
        if (step == TRACKER_INFO_STEP_SESSION_LIST)
        {
            tracker_info_session_list_failed = true;
        } else
        {
            set_tracker_info_field_error(step, QString("Transport send failed."));
        }
        final_message =
            QString("Tracker info refresh failed due to transport error while loading %1.")
                .arg(tracker_info_step_name(step));
        if (diagnostics_step == true)
        {
            log_warning() << "ArsTracker" << command_text << "transport error";
        }
    } else if (status == STATUS_TRANSPORT_DISCONNECTED)
    {
        if (step == TRACKER_INFO_STEP_SESSION_LIST)
        {
            tracker_info_session_list_failed = true;
        } else
        {
            set_tracker_info_field_error(step, QString("Transport disconnected."));
        }
        final_message =
            QString("Tracker info refresh failed: transport disconnected while loading %1.")
                .arg(tracker_info_step_name(step));
        if (diagnostics_step == true)
        {
            log_warning() << "ArsTracker" << command_text << "transport disconnected";
        }
    } else
    {
        if (step == TRACKER_INFO_STEP_SESSION_LIST)
        {
            tracker_info_session_list_failed = true;
        } else
        {
            set_tracker_info_field_error(
                step, shell_output.isEmpty() ? QString("Request failed.") : shell_output);
        }
        final_message =
            QString("Tracker info refresh failed while loading %1.")
                .arg(tracker_info_step_name(step));
        if (diagnostics_step == true)
        {
            log_warning() << "ArsTracker" << command_text << "status failure:" << status
                          << shell_output;
        }
    }

    emit tracker_info_changed(latest_tracker_info);

    if (continue_flow == true)
    {
        begin_tracker_info_step(next_step);
        return;
    }

    if (final_message.isEmpty())
    {
        int error_count = tracker_info_error_count();

        if (error_count == 0)
        {
            final_message = QString("Tracker info refreshed.");
        } else
        {
            final_message =
                QString("Tracker info and sessions refreshed with %1 issue(s).")
                    .arg(QString::number(error_count));
        }
    }

    finish_tracker_info_refresh(final_message);
}

bool ars_tracker_backend::begin_session_delete(const QString& session_id,
                                               QString*       session_name,
                                               QString*       error_message)
{
    if (tracker_info_loading == true)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Tracker info refresh is already in progress.");
        }

        return false;
    }

    if (delete_loading == true)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("A session delete is already in progress.");
        }

        return false;
    }

    if (loading == true)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("A session list request is already in progress.");
        }

        return false;
    }

    if (export_loading == true)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("A session export is already in progress.");
        }

        return false;
    }

    if (session_id.isEmpty())
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Select a session first.");
        }

        return false;
    }

    ars_tracker_session_t session;

    if (resolve_session(session_id, &session) == false)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Selected session is no longer available.");
        }

        return false;
    }

    if (session.id.isEmpty())
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Selected session has no delete identifier.");
        }

        return false;
    }

    delete_loading          = true;
    active_delete_session_id = session.id;
    // The parser currently maps session.id to the raw `meas ls` token, so use it for
    // `meas rm <session_name>`.
    active_delete_session_name = session.id;

    if (session_name != nullptr)
    {
        *session_name = active_delete_session_name;
    }

    emit delete_loading_changed(true);
    emit status_message(QString("Deleting session '%1'...").arg(session.display_name));
    return true;
}

void ars_tracker_backend::handle_session_delete_response(group_status   status,
                                                         const QString& shell_output,
                                                         int32_t        shell_ret)
{
    if (delete_loading == false)
    {
        return;
    }

    QString deleted_session_id   = active_delete_session_id;
    QString deleted_session_name = active_delete_session_name;
    QString final_message;
    bool    refresh_requested = false;

    delete_loading = false;
    emit delete_loading_changed(false);

    if (status == STATUS_COMPLETE)
    {
        if (shell_ret == 0)
        {
            for (int i = 0; i < latest_sessions.length(); ++i)
            {
                if (latest_sessions.at(i).id == deleted_session_id)
                {
                    latest_sessions.removeAt(i);
                    break;
                }
            }

            final_message =
                QString("Deleted session '%1'. Refreshing sessions...").arg(deleted_session_name);
            refresh_requested = true;
        } else
        {
            final_message =
                QString("Delete session command failed, shell ret: %1").arg(QString::number(shell_ret));
        }
    } else if (status == STATUS_TIMEOUT)
    {
        final_message = QString("Delete session request timed out.");
    } else if (status == STATUS_CANCELLED)
    {
        final_message = QString("Delete session request cancelled.");
    } else if (status == STATUS_PROCESSOR_TRANSPORT_ERROR)
    {
        final_message = QString("Delete session failed due to transport error.");
    } else if (status == STATUS_TRANSPORT_DISCONNECTED)
    {
        final_message = QString("Delete session failed: transport disconnected.");
    } else
    {
        final_message =
            shell_output.isEmpty() ? QString("Delete session request failed.") : shell_output;
    }

    active_delete_session_id.clear();
    active_delete_session_name.clear();

    emit status_message(final_message);

    if (refresh_requested == true)
    {
        emit request_session_list_refresh_after_delete();
    }
}

bool ars_tracker_backend::resolve_session(const QString&         session_id,
                                          ars_tracker_session_t* session) const
{
    for (const ars_tracker_session_t& existing_session : latest_sessions)
    {
        if (existing_session.id == session_id)
        {
            if (session != nullptr)
            {
                *session = existing_session;
            }

            return true;
        }
    }

    return false;
}

QString ars_tracker_backend::resolve_destination_path(const QString& raw_destination_path) const
{
    QString trimmed_path = raw_destination_path.trimmed();

    if (trimmed_path.isEmpty())
    {
        return QString();
    }

    QFileInfo destination_info(trimmed_path);

    if (destination_info.isAbsolute())
    {
        return QDir::cleanPath(trimmed_path);
    }

    // ArsTracker exports treat manual relative paths as relative to the AuTerm executable,
    // not the process working directory.
    QDir application_dir(QCoreApplication::applicationDirPath());
    return QDir::cleanPath(application_dir.filePath(trimmed_path));
}

QString ars_tracker_backend::build_tracker_export_name(QString* error_message) const
{
    QString serial_value = latest_tracker_info.serial_number.value.trimmed();

    if (serial_value.isEmpty())
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Tracker serial number is not loaded.");
        }

        return QString();
    }

    int third_dot_index = -1;
    int search_from     = 0;

    for (int i = 0; i < 3; ++i)
    {
        third_dot_index = serial_value.indexOf('.', search_from);

        if (third_dot_index < 0)
        {
            break;
        }

        search_from = third_dot_index + 1;
    }

    if (third_dot_index < 0 || third_dot_index + 1 >= serial_value.length())
    {
        if (error_message != nullptr)
        {
            *error_message =
                QString("Tracker serial number has unexpected format: %1").arg(serial_value);
        }

        return QString();
    }

    QString serial_suffix = serial_value.mid(third_dot_index + 1).trimmed();

    if (serial_suffix.isEmpty())
    {
        if (error_message != nullptr)
        {
            *error_message =
                QString("Tracker serial number has no unique suffix: %1").arg(serial_value);
        }

        return QString();
    }

    QString type_value = latest_tracker_info.tracker_type.value.trimmed();

    if (type_value.isEmpty())
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Tracker type is not loaded.");
        }

        return QString();
    }

    QString type_suffix = type_value.left(1).trimmed().toUpper();

    // Export folder names must end with the raw tracker side suffix. Reject unknown values
    // instead of guessing, so files are not written into an ambiguous tracker directory.
    if (type_suffix != "R" && type_suffix != "L")
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Tracker type has unexpected format: %1").arg(type_value);
        }

        return QString();
    }

    return serial_suffix + type_suffix;
}

QString ars_tracker_backend::build_session_export_root_path(const QString&              destination,
                                                            const ars_tracker_session_t& session,
                                                            QString*                    error_message) const
{
    QString session_name = QFileInfo(session.display_name.trimmed()).fileName();

    if (session_name.isEmpty())
    {
        session_name = QFileInfo(session.id.trimmed()).fileName();
    }

    if (session_name.isEmpty())
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Selected session has no usable export directory name.");
        }

        return QString();
    }

    QString tracker_name = build_tracker_export_name(error_message);

    if (tracker_name.isEmpty())
    {
        return QString();
    }

    return QDir::cleanPath(QDir(QDir(destination).filePath(session_name)).filePath(tracker_name));
}

QString ars_tracker_backend::build_remote_file_path(const QString& remote_root,
                                                    const QString& filename) const
{
    if (remote_root.isEmpty())
    {
        return filename;
    }

    QString fixed_root = remote_root;

    while (fixed_root.endsWith('/'))
    {
        fixed_root.chop(1);
    }

    return "/NAND:/" + fixed_root + "/" + filename;
}

QString ars_tracker_backend::build_local_final_file_path(const QString& destination,
                                                         const QString& filename) const
{
    return QDir(destination).filePath(filename);
}

QString ars_tracker_backend::build_local_temp_file_path(const QString& destination,
                                                        const QString& filename) const
{
    return QDir(destination).filePath(QString(".%1.part").arg(filename));
}

bool ars_tracker_backend::ensure_download_temp_file(ars_tracker_download_item_t* item,
                                                    QString* error_message) const
{
    QFileInfo temp_info(item->local_temp_file);

    if (temp_info.exists())
    {
        return true;
    }

    QFileInfo final_info(item->local_file);

    if (final_info.exists() == false)
    {
        return true;
    }

    log_debug() << "ArsTracker export moving existing local file to partial download path for"
                << item->remote_file << "from" << item->local_file << "to" << item->local_temp_file;

    if (QFile::rename(item->local_file, item->local_temp_file))
    {
        return true;
    }

    if (QFile::copy(item->local_file, item->local_temp_file) && QFile::remove(item->local_file))
    {
        log_warning() << "ArsTracker export used copy/remove fallback to prepare partial file for"
                      << item->remote_file;
        return true;
    }

    if (error_message != nullptr)
    {
        *error_message =
            QString("Could not prepare partial file for resume: %1").arg(item->local_temp_file);
    }

    return false;
}

QString ars_tracker_backend::choose_export_hash_type(const QList<hash_checksum_t>& supported_hashes,
                                                     QString* error_message) const
{
    static const QStringList preferred_hashes =
        QStringList() << "sha256" << "sha384" << "sha512" << "sha1" << "md5" << "crc32";

    QStringList advertised_hashes;
    for (const hash_checksum_t& candidate : supported_hashes)
    {
        advertised_hashes.append(candidate.name);
    }
    log_debug() << "ArsTracker export advertised hash/checksum algorithms:" << advertised_hashes;

    for (const QString& preferred_hash : preferred_hashes)
    {
        for (const hash_checksum_t& candidate : supported_hashes)
        {
            QString normalized_candidate = normalize_hash_name(candidate.name);

            if (preferred_hash == "crc32" && is_crc32_hash_name(candidate.name))
            {
                log_debug() << "ArsTracker export selected CRC32 verification path using"
                            << candidate.name;
                return candidate.name;
            }

            QCryptographicHash::Algorithm algorithm;

            if (normalized_candidate == preferred_hash &&
                local_hash_algorithm_from_name(candidate.name, &algorithm))
            {
                Q_UNUSED(algorithm);
                log_debug() << "ArsTracker export selected QCryptographicHash verification path using"
                            << candidate.name;
                return candidate.name;
            }
        }
    }

    if (error_message != nullptr)
    {
        *error_message =
            QString("Tracker does not advertise a mutually supported hash/checksum for local verification.");
    }

    return QString();
}

bool ars_tracker_backend::compute_local_file_hash(const QString& file_path, const QString& hash_name,
                                                  QByteArray* result, QString* error_message) const
{
    if (is_crc32_hash_name(hash_name))
    {
        QFile local_file(file_path);

        if (local_file.open(QFile::ReadOnly) == false)
        {
            if (error_message != nullptr)
            {
                *error_message =
                    QString("Could not open existing local file for CRC32 verification: %1")
                        .arg(file_path);
            }

            return false;
        }

        log_debug() << "ArsTracker export verifying local file with IEEE CRC32:" << file_path;

        uint32_t    crc = 0xFFFFFFFFU;
        QByteArray  buffer;
        const qint64 chunk_size = 64 * 1024;

        while ((buffer = local_file.read(chunk_size)).isEmpty() == false)
        {
            crc = crc32_ieee_update(crc, buffer.constData(), buffer.size());
        }

        if (local_file.error() != QFileDevice::NoError)
        {
            if (error_message != nullptr)
            {
                *error_message =
                    QString("Could not read existing local file for CRC32 verification: %1")
                        .arg(file_path);
            }

            return false;
        }

        crc ^= 0xFFFFFFFFU;

        if (result != nullptr)
        {
            QByteArray crc_bytes;
            crc_bytes.append(char((crc >> 24U) & 0xffU));
            crc_bytes.append(char((crc >> 16U) & 0xffU));
            crc_bytes.append(char((crc >> 8U) & 0xffU));
            crc_bytes.append(char(crc & 0xffU));
            *result = crc_bytes;
        }

        return true;
    }

    QCryptographicHash::Algorithm algorithm;

    if (local_hash_algorithm_from_name(hash_name, &algorithm) == false)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Local hash algorithm is not supported: %1").arg(hash_name);
        }

        return false;
    }

    QFile local_file(file_path);

    if (local_file.open(QFile::ReadOnly) == false)
    {
        if (error_message != nullptr)
        {
            *error_message =
                QString("Could not open existing local file for verification: %1").arg(file_path);
        }

        return false;
    }

    log_debug() << "ArsTracker export verifying local file with QCryptographicHash:" << file_path
                << "algorithm" << hash_name;

    QCryptographicHash hasher(algorithm);

    if (hasher.addData(&local_file) == false)
    {
        if (error_message != nullptr)
        {
            *error_message =
                QString("Could not hash existing local file for verification: %1").arg(file_path);
        }

        return false;
    }

    if (result != nullptr)
    {
        *result = hasher.result();
    }

    return true;
}

void ars_tracker_backend::enqueue_fixed_files()
{
    for (const QString& file_name : ars_tracker_fixed_files)
    {
        ars_tracker_download_item_t item;
        item.remote_file     = build_remote_file_path(active_session_remote_root, file_name);
        item.local_file      = build_local_final_file_path(active_destination_path, file_name);
        item.local_temp_file = build_local_temp_file_path(active_destination_path, file_name);
        item.bytes_completed = 0;
        item.total_bytes     = 0;
        item.retry_count     = 0;
        item.category        = ARS_TRACKER_FILE_FIXED;
        item.status          = ARS_TRACKER_STATUS_PENDING;
        item.remote_file_size = 0;
        item.remote_file_hash.clear();

        download_queue.append(item);
    }
}

void ars_tracker_backend::enqueue_next_sensor_candidate()
{
    if (sensors_enumeration_done == true)
    {
        return;
    }

    QString sensor_filename = QString("sensors_%1.bin").arg(QString::number(next_sensor_index));

    ars_tracker_download_item_t item;
    item.remote_file     = build_remote_file_path(active_session_remote_root, sensor_filename);
    item.local_file      = build_local_final_file_path(active_destination_path, sensor_filename);
    item.local_temp_file = build_local_temp_file_path(active_destination_path, sensor_filename);
    item.bytes_completed = 0;
    item.total_bytes     = 0;
    item.retry_count     = 0;
    item.category        = ARS_TRACKER_FILE_SENSOR;
    item.status          = ARS_TRACKER_STATUS_PENDING;
    item.remote_file_size = 0;
    item.remote_file_hash.clear();

    ++next_sensor_index;
    download_queue.append(item);
}

bool ars_tracker_backend::begin_session_export(const QString& session_id,
                                               const QString& destination_path,
                                               QString*       error_message)
{
    QString resolved_destination_path = resolve_destination_path(destination_path);

    if (tracker_info_loading == true)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Tracker info refresh is already in progress.");
        }

        return false;
    }

    if (delete_loading == true)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("A session delete is already in progress.");
        }

        return false;
    }

    if (loading == true)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("A session list request is already in progress.");
        }

        return false;
    }

    if (export_loading == true)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("A session export is already in progress.");
        }

        return false;
    }

    if (session_id.isEmpty())
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Select a session first.");
        }

        return false;
    }

    ars_tracker_session_t session;

    if (resolve_session(session_id, &session) == false)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Selected session is no longer available.");
        }

        return false;
    }

    QString export_root_path =
        build_session_export_root_path(resolved_destination_path, session, error_message);

    if (export_root_path.isEmpty())
    {
        return false;
    }

    if (QDir().mkpath(export_root_path) == false)
    {
        if (error_message != nullptr)
        {
            *error_message =
                QString("Could not create export directory: %1").arg(export_root_path);
        }

        return false;
    }

    QFileInfo export_root_info(export_root_path);

    if (!export_root_info.exists() || !export_root_info.isDir())
    {
        if (error_message != nullptr)
        {
            *error_message =
                QString("Export directory is not usable: %1").arg(export_root_path);
        }

        return false;
    }

    if (export_root_info.isWritable() == false)
    {
        if (error_message != nullptr)
        {
            *error_message =
                QString("Export directory is not writable: %1").arg(export_root_path);
        }

        return false;
    }

    reset_export_state();

    export_loading             = true;
    active_session_id          = session.id;
    active_session_remote_root = session.remote_path_or_name;
    active_destination_path    = export_root_path;

    enqueue_fixed_files();
    enqueue_next_sensor_candidate();

    emit export_loading_changed(true);
    publish_export_file_rows();
    publish_progress_text();
    emit status_message(
        QString("Preparing session export for '%1'...").arg(session.display_name));
    emit request_file_hash_support();
    return true;
}

bool ars_tracker_backend::is_not_found_error(group_status   status,
                                             const QString& error_message) const
{
    if (status == STATUS_COMPLETE)
    {
        return false;
    }

    QString lowered = error_message.toLower();

    return lowered.contains("does not exist") || lowered.contains("not found") ||
           lowered.contains("no such file") || lowered.contains("no such file/entry") ||
           lowered.contains("enoent");
}

bool ars_tracker_backend::is_empty_file_error(group_status   status,
                                              const QString& error_message) const
{
    if (status == STATUS_COMPLETE)
    {
        return false;
    }

    QString lowered = error_message.toLower();

    return lowered.contains("file_empty") || lowered.contains("file is empty") ||
           lowered.contains("file empty") || lowered.contains("empty with no contents");
}

bool ars_tracker_backend::finalize_downloaded_file(ars_tracker_download_item_t* item,
                                                   QString*                     error_message)
{
    QFile temp_file(item->local_temp_file);

    if (!temp_file.exists())
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Downloaded temp file is missing.");
        }

        return false;
    }

    QFile::remove(item->local_file);

    if (QFile::rename(item->local_temp_file, item->local_file))
    {
        return true;
    }

    if (QFile::copy(item->local_temp_file, item->local_file) == false)
    {
        if (error_message != nullptr)
        {
            *error_message = QString("Could not finalise %1 in destination folder.")
                                 .arg(QFileInfo(item->local_file).fileName());
        }

        return false;
    }

    QFile::remove(item->local_temp_file);
    return true;
}

QString ars_tracker_backend::to_status_text(ars_tracker_download_status_t status) const
{
    switch (status)
    {
    case ARS_TRACKER_STATUS_PENDING:
        return "Pending";
    case ARS_TRACKER_STATUS_CHECKING_EXISTING:
        return "Checking existing";
    case ARS_TRACKER_STATUS_ALREADY_PRESENT:
        return "Already present";
    case ARS_TRACKER_STATUS_DOWNLOADING:
        return "Downloading";
    case ARS_TRACKER_STATUS_DOWNLOADED:
        return "Downloaded";
    case ARS_TRACKER_STATUS_MISSING:
        return "Skipped";
    case ARS_TRACKER_STATUS_SENSORS_END:
        return "Sensors sequence complete";
    case ARS_TRACKER_STATUS_FAILED:
        return "Failed";
    case ARS_TRACKER_STATUS_CANCELLED:
        return "Cancelled";
    }

    return "Unknown";
}

void ars_tracker_backend::publish_export_file_rows()
{
    QStringList rows;

    for (const ars_tracker_download_item_t& item : download_queue)
    {
        QString row = QString("%1 - %2").arg(QFileInfo(item.remote_file).fileName(),
                                             to_status_text(item.status));

        if (item.error_text.isEmpty() == false)
        {
            row.append(QString(" (%1)").arg(item.error_text));
        }

        rows.append(row);
    }

    emit export_file_list_changed(rows);
}

void ars_tracker_backend::publish_progress_text(const QString& current_file)
{
    uint32_t finished_count = 0;

    for (const ars_tracker_download_item_t& item : download_queue)
    {
        if (item.status == ARS_TRACKER_STATUS_ALREADY_PRESENT ||
            item.status == ARS_TRACKER_STATUS_DOWNLOADED ||
            item.status == ARS_TRACKER_STATUS_MISSING ||
            item.status == ARS_TRACKER_STATUS_SENSORS_END ||
            item.status == ARS_TRACKER_STATUS_FAILED || item.status == ARS_TRACKER_STATUS_CANCELLED)
        {
            ++finished_count;
        }
    }

    QString progress =
        QString("Files finished: %1/%2")
            .arg(QString::number(finished_count), QString::number(download_queue.length()));

    const ars_tracker_download_item_t* current_item = nullptr;
    if (current_download_index >= 0 && current_download_index < download_queue.length())
    {
        current_item = &download_queue.at(current_download_index);
    }

    QString display_file = current_file;
    if (display_file.isEmpty() && current_item != nullptr)
    {
        display_file = current_item->remote_file;
    }

    if (display_file.isEmpty() == false)
    {
        progress.append(QString(" | Current: %1").arg(QFileInfo(display_file).fileName()));

        if (current_item != nullptr)
        {
            if (current_item->status == ARS_TRACKER_STATUS_CHECKING_EXISTING)
            {
                progress.append(" (checking)");
            }
            else if (current_item->total_bytes > 0)
            {
                qint64 bytes_completed =
                    qBound<qint64>(0, current_item->bytes_completed, current_item->total_bytes);
                int percent = int((bytes_completed * 100) / current_item->total_bytes);
                progress.append(
                    QString(" (%1%%) %2 / %3")
                        .arg(QString::number(percent),
                             format_export_byte_count(bytes_completed),
                             format_export_byte_count(current_item->total_bytes)));
            }
            else if (current_item->bytes_completed > 0)
            {
                progress.append(QString(" %1")
                                    .arg(format_export_byte_count(current_item->bytes_completed)));
            }
        }
    }

    emit export_progress_changed(progress);
}

void ars_tracker_backend::handle_export_hash_support_result(
    group_status status, const QString& error_message,
    const QList<hash_checksum_t>& supported_hashes)
{
    if (export_loading == false)
    {
        return;
    }

    if (status == STATUS_CANCELLED || export_cancel_requested == true)
    {
        finish_export(false, true, "Session export cancelled.");
        return;
    }

    if (status != STATUS_COMPLETE)
    {
        QString message = error_message.isEmpty() ?
                              QString("Could not query tracker file hash support.") :
                              error_message;
        export_failed = true;
        finish_export(false, false, message);
        return;
    }

    QString hash_error;
    QString selected_hash = choose_export_hash_type(supported_hashes, &hash_error);

    if (selected_hash.isEmpty())
    {
        export_hash_name.clear();
        export_hash_ready = true;
        log_debug() << "ArsTracker export found no mutually supported verification algorithm;"
                    << "falling back to download without pre-verification." << hash_error;
        emit status_message(
            QString("No shared local verification algorithm; downloading files without pre-checks."));
        schedule_next_download_or_finish("hash support completed without shared algorithm");
        return;
    }

    export_hash_name = selected_hash;
    export_hash_ready = true;

    emit status_message(QString("Using %1 to verify existing local files before download.")
                            .arg(export_hash_name));
    schedule_next_download_or_finish("hash support completed");
}

void ars_tracker_backend::handle_file_metadata_result(group_status status, const QString& error_message,
                                                      const QByteArray& remote_hash,
                                                      uint32_t          remote_size)
{
    if (export_loading == false || current_download_index < 0 ||
        current_download_index >= download_queue.length())
    {
        return;
    }

    ars_tracker_download_item_t& item = download_queue[current_download_index];

    if (item.status != ARS_TRACKER_STATUS_CHECKING_EXISTING)
    {
        log_debug() << "ArsTracker export metadata callback ignored in state:"
                    << item.remote_file << "index" << current_download_index
                    << "status" << int(item.status)
                    << "callback status" << int(status);
        return;
    }

    log_debug() << "ArsTracker export metadata completed:" << item.remote_file
                << "index" << current_download_index
                << "status" << int(status)
                << "remote size" << remote_size;

    if (status == STATUS_CANCELLED || export_cancel_requested == true)
    {
        item.status     = ARS_TRACKER_STATUS_CANCELLED;
        item.error_text = "Cancelled";
        publish_export_file_rows();
        finish_export(false, true, "Session export cancelled.");
        return;
    }

    if (status == STATUS_COMPLETE)
    {
        item.remote_file_size = remote_size;
        item.remote_file_hash = remote_hash;

        if (remote_size == 0)
        {
            QFile::remove(item.local_temp_file);

            if (item.category == ARS_TRACKER_FILE_SENSOR)
            {
                sensors_enumeration_done = true;
                download_queue.removeAt(current_download_index);
                current_download_index = -1;
                publish_export_file_rows();
                publish_progress_text();
                schedule_next_download_or_finish("sensor enumeration completed at empty file");
                return;
            }

            item.status          = ARS_TRACKER_STATUS_MISSING;
            item.error_text      = "Remote file is empty";
            item.bytes_completed = 0;
            item.total_bytes     = 0;
            publish_export_file_rows();
            publish_progress_text();
            emit status_message(
                QString("Skipping %1, remote file is empty.")
                    .arg(QFileInfo(item.remote_file).fileName()));
            schedule_next_download_or_finish("remote file empty");
            return;
        }

        QFileInfo partial_info(item.local_temp_file);
        QFileInfo local_info(item.local_file);

        if (partial_info.exists())
        {
            schedule_current_file_download("partial temp file exists after metadata");
            return;
        }

        if (local_info.exists() == false)
        {
            schedule_current_file_download("local file missing after metadata");
            return;
        }

        if (export_hash_name.isEmpty() == false && local_info.size() == qint64(remote_size))
        {
            QByteArray local_hash;
            QString    local_hash_error;

            if (compute_local_file_hash(item.local_file, export_hash_name, &local_hash,
                                        &local_hash_error) == false)
            {
                emit status_message(
                    QString("Could not verify existing %1, resuming download from current file size.")
                        .arg(QFileInfo(item.remote_file).fileName()));
            }
            else if (local_hash == remote_hash)
            {
                item.status          = ARS_TRACKER_STATUS_ALREADY_PRESENT;
                item.bytes_completed = remote_size;
                item.total_bytes     = remote_size;
                item.error_text =
                    QString("Already present (%1, size match)").arg(export_hash_name.toUpper());

                if (item.category == ARS_TRACKER_FILE_SENSOR)
                {
                    enqueue_next_sensor_candidate();
                }

                publish_export_file_rows();
                publish_progress_text();
                emit status_message(
                    QString("Skipping %1, identical local copy already present.")
                        .arg(QFileInfo(item.remote_file).fileName()));
                schedule_next_download_or_finish("local file already present");
                return;
            }
        }

        QString prepare_error;
        if (ensure_download_temp_file(&item, &prepare_error) == false)
        {
            item.status     = ARS_TRACKER_STATUS_FAILED;
            item.error_text = prepare_error;
            export_failed   = true;
            publish_export_file_rows();
            finish_export(false, false, prepare_error);
            return;
        }

        schedule_current_file_download("metadata verified");
        return;
    }

    if (is_not_found_error(status, error_message))
    {
        QFile::remove(item.local_temp_file);

        if (item.category == ARS_TRACKER_FILE_SENSOR)
        {
            sensors_enumeration_done = true;
            download_queue.removeAt(current_download_index);
            current_download_index = -1;
            publish_export_file_rows();
            publish_progress_text();
            schedule_next_download_or_finish("sensor enumeration completed");
            return;
        }

        item.status     = ARS_TRACKER_STATUS_MISSING;
        item.error_text = "Remote file missing";
        item.bytes_completed = 0;
        item.total_bytes = 0;
        publish_export_file_rows();
        publish_progress_text();
        emit status_message(
            QString("Skipping %1, remote file is missing.")
                .arg(QFileInfo(item.remote_file).fileName()));
        schedule_next_download_or_finish("remote file missing");
        return;
    }

    if (is_empty_file_error(status, error_message))
    {
        QFile::remove(item.local_temp_file);

        if (item.category == ARS_TRACKER_FILE_SENSOR)
        {
            sensors_enumeration_done = true;
            download_queue.removeAt(current_download_index);
            current_download_index = -1;
            publish_export_file_rows();
            publish_progress_text();
            schedule_next_download_or_finish("sensor enumeration completed at empty file");
            return;
        }

        item.status          = ARS_TRACKER_STATUS_MISSING;
        item.error_text      = "Remote file is empty";
        item.bytes_completed = 0;
        item.total_bytes     = 0;
        publish_export_file_rows();
        publish_progress_text();
        emit status_message(
            QString("Skipping %1, remote file is empty.")
                .arg(QFileInfo(item.remote_file).fileName()));
        schedule_next_download_or_finish("remote file empty");
        return;
    }

    if (status == STATUS_TRANSPORT_DISCONNECTED)
    {
        item.status     = ARS_TRACKER_STATUS_FAILED;
        item.error_text = "Transport disconnected while checking remote file";
    }
    else if (status == STATUS_PROCESSOR_TRANSPORT_ERROR)
    {
        item.status     = ARS_TRACKER_STATUS_FAILED;
        item.error_text = "Transport send failed while checking remote file";
    }
    else if (status == STATUS_TIMEOUT)
    {
        item.status     = ARS_TRACKER_STATUS_FAILED;
        item.error_text = "Remote file check timed out";
    }
    else
    {
        item.status     = ARS_TRACKER_STATUS_FAILED;
        item.error_text = error_message.isEmpty() ? "Remote file check failed" : error_message;
    }

    export_failed = true;
    publish_export_file_rows();
    finish_export(false, false,
                  QString("Session export failed while checking %1.")
                      .arg(QFileInfo(item.remote_file).fileName()));
}

void ars_tracker_backend::schedule_current_file_download(const QString& reason)
{
    if (export_loading == false || current_download_index < 0 ||
        current_download_index >= download_queue.length())
    {
        log_debug() << "ArsTracker export download schedule ignored:" << reason
                    << "index" << current_download_index
                    << "queue length" << download_queue.length()
                    << "loading" << export_loading;
        return;
    }

    const uint32_t sequence = ++export_transition_sequence;
    const int      index    = current_download_index;
    const QString  remote_file = download_queue.at(index).remote_file;
    const ars_tracker_download_status_t status = download_queue.at(index).status;

    log_debug() << "ArsTracker export download scheduled:" << remote_file
                << "index" << index << "status" << int(status)
                << "sequence" << sequence << "reason" << reason;

    QTimer::singleShot(0, this, [this, sequence, index, remote_file, reason]() {
        if (export_loading == false)
        {
            log_debug() << "ArsTracker export scheduled download dropped after export stopped:"
                        << remote_file << "index" << index << "reason" << reason;
            return;
        }

        if (sequence != export_transition_sequence)
        {
            log_debug() << "ArsTracker export scheduled download dropped as stale:"
                        << remote_file << "index" << index << "sequence" << sequence
                        << "current sequence" << export_transition_sequence;
            return;
        }

        if (current_download_index != index || index < 0 || index >= download_queue.length())
        {
            log_debug() << "ArsTracker export scheduled download dropped after index changed:"
                        << remote_file << "scheduled index" << index
                        << "current index" << current_download_index
                        << "queue length" << download_queue.length();
            return;
        }

        ars_tracker_download_item_t& item = download_queue[index];

        if (item.remote_file != remote_file)
        {
            log_debug() << "ArsTracker export scheduled download dropped after file changed:"
                        << remote_file << "current file" << item.remote_file
                        << "index" << index;
            return;
        }

        if (item.status != ARS_TRACKER_STATUS_CHECKING_EXISTING &&
            item.status != ARS_TRACKER_STATUS_DOWNLOADING)
        {
            log_debug() << "ArsTracker export scheduled download dropped in state:"
                        << remote_file << "index" << index << "status" << int(item.status);
            return;
        }

        start_file_download(&item);
    });
}

void ars_tracker_backend::schedule_next_download_or_finish(const QString& reason)
{
    const uint32_t sequence = ++export_transition_sequence;
    const int      index    = current_download_index;
    const QString  remote_file =
        (index >= 0 && index < download_queue.length()) ? download_queue.at(index).remote_file :
                                                          QString();

    log_debug() << "ArsTracker export next file scheduled:" << remote_file
                << "index" << index << "sequence" << sequence
                << "reason" << reason;

    QTimer::singleShot(0, this, [this, sequence, index, remote_file, reason]() {
        if (export_loading == false)
        {
            log_debug() << "ArsTracker export scheduled next file dropped after export stopped:"
                        << remote_file << "index" << index << "reason" << reason;
            return;
        }

        if (sequence != export_transition_sequence)
        {
            log_debug() << "ArsTracker export scheduled next file dropped as stale:"
                        << remote_file << "index" << index << "sequence" << sequence
                        << "current sequence" << export_transition_sequence;
            return;
        }

        log_debug() << "ArsTracker export next file running:" << remote_file
                    << "index" << index << "sequence" << sequence
                    << "reason" << reason;

        request_next_download_or_finish();
    });
}

void ars_tracker_backend::start_file_download(ars_tracker_download_item_t* item)
{
    if (item == nullptr)
    {
        return;
    }

    QFileInfo partial_info(item->local_temp_file);
    bool      already_downloading = (item->status == ARS_TRACKER_STATUS_DOWNLOADING);

    item->status = ARS_TRACKER_STATUS_DOWNLOADING;
    item->error_text.clear();
    item->bytes_completed = partial_info.exists() ? partial_info.size() : 0;
    item->total_bytes     = item->remote_file_size;

    publish_export_file_rows();
    publish_progress_text(item->remote_file);

    if (already_downloading == false)
    {
        if (partial_info.exists() && partial_info.size() > 0)
        {
            emit status_message(
                QString("Resuming %1 from offset %2...")
                    .arg(QFileInfo(item->remote_file).fileName(), QString::number(partial_info.size())));
        }
        else
        {
            emit status_message(
                QString("Downloading %1 from offset 0...")
                    .arg(QFileInfo(item->remote_file).fileName()));
        }
    }

    log_debug() << "ArsTracker export download started:" << item->remote_file
                << "index" << current_download_index
                << "offset" << item->bytes_completed
                << "total" << item->total_bytes;

    emit request_file_download(item->remote_file, item->local_temp_file);
}

void ars_tracker_backend::request_next_download_or_finish()
{
    if (export_loading == false)
    {
        return;
    }

    if (export_cancel_requested == true)
    {
        finish_export(false, true, "Session export cancelled.");
        return;
    }

    if (export_hash_ready == false)
    {
        return;
    }

    for (int i = 0; i < download_queue.length(); ++i)
    {
        if (download_queue.at(i).status == ARS_TRACKER_STATUS_PENDING)
        {
            current_download_index            = i;
            ars_tracker_download_item_t& item = download_queue[i];
            QFileInfo partial_info(item.local_temp_file);

            item.status          = ARS_TRACKER_STATUS_CHECKING_EXISTING;
            item.error_text.clear();
            item.bytes_completed = partial_info.exists() ? partial_info.size() : 0;
            item.total_bytes     = 0;

            publish_export_file_rows();
            publish_progress_text(item.remote_file);
            emit status_message(
                QString("Checking %1...").arg(QFileInfo(item.remote_file).fileName()));

            log_debug() << "ArsTracker export metadata requested:" << item.remote_file
                        << "index" << current_download_index
                        << "hash" << export_hash_name
                        << "partial bytes" << item.bytes_completed;

            emit request_file_metadata(item.remote_file, export_hash_name);
            return;
        }
    }

    if (export_failed == true)
    {
        finish_export(false, false, "Session export failed.");
    } else
    {
        finish_export(true, false, "Session export completed.");
    }
}

void ars_tracker_backend::finish_export(bool success, bool cancelled, const QString& message)
{
    if (export_loading == false)
    {
        return;
    }

    export_loading = false;
    emit export_loading_changed(false);
    publish_export_file_rows();
    publish_progress_text();
    emit status_message(message);
    emit export_finished(success, cancelled, message);
}

void ars_tracker_backend::handle_file_download_progress(uint8_t percent)
{
    Q_UNUSED(percent);

    if (export_loading == false || current_download_index < 0 ||
        current_download_index >= download_queue.length())
    {
        return;
    }

    ars_tracker_download_item_t& item = download_queue[current_download_index];
    QFileInfo partial_info(item.local_temp_file);

    if (partial_info.exists())
    {
        item.bytes_completed = partial_info.size();
    }

    item.total_bytes = item.remote_file_size;
    publish_progress_text(item.remote_file);
}

void ars_tracker_backend::handle_file_download_result(group_status   status,
                                                      const QString& error_message)
{
    if (export_loading == false || current_download_index < 0 ||
        current_download_index >= download_queue.length())
    {
        return;
    }

    ars_tracker_download_item_t& item = download_queue[current_download_index];

    if (item.status != ARS_TRACKER_STATUS_DOWNLOADING)
    {
        log_debug() << "ArsTracker export download callback ignored in state:"
                    << item.remote_file << "index" << current_download_index
                    << "status" << int(item.status)
                    << "callback status" << int(status);
        return;
    }

    log_debug() << "ArsTracker export download completed:" << item.remote_file
                << "index" << current_download_index
                << "status" << int(status);

    if (status == STATUS_COMPLETE)
    {
        QFileInfo partial_info(item.local_temp_file);

        if (partial_info.exists() == false)
        {
            item.status     = ARS_TRACKER_STATUS_FAILED;
            item.error_text = "Downloaded file is missing on disk";
            export_failed   = true;
            publish_export_file_rows();
            finish_export(false, false,
                          QString("Session export failed while finalizing %1.")
                              .arg(QFileInfo(item.remote_file).fileName()));
            return;
        }

        qint64 completed_size = partial_info.size();
        item.bytes_completed  = completed_size;
        item.total_bytes      = item.remote_file_size;
        publish_progress_text(item.remote_file);

        if (completed_size > qint64(item.remote_file_size))
        {
            // A partial file larger than the verified remote size cannot be resumed safely.
            QFile::remove(item.local_temp_file);
            item.status     = ARS_TRACKER_STATUS_FAILED;
            item.error_text = QString("Downloaded file grew beyond expected size (%1 > %2)")
                                  .arg(QString::number(completed_size),
                                       QString::number(item.remote_file_size));
            export_failed   = true;
            publish_export_file_rows();
            finish_export(false, false,
                          QString("Session export failed while validating %1.")
                              .arg(QFileInfo(item.remote_file).fileName()));
            return;
        }

        if (completed_size < qint64(item.remote_file_size))
        {
            log_warning() << "ArsTracker export download cycle ended before file was complete for"
                          << item.remote_file << "local size" << completed_size
                          << "expected" << item.remote_file_size << "- resuming";
            item.status = ARS_TRACKER_STATUS_DOWNLOADING;
            item.error_text.clear();
            publish_export_file_rows();
            schedule_current_file_download("download ended before expected size");
            return;
        }

        QString finalise_error;

        if (finalize_downloaded_file(&item, &finalise_error) == false)
        {
            item.status     = ARS_TRACKER_STATUS_FAILED;
            item.error_text = finalise_error;
            export_failed   = true;
            publish_export_file_rows();
            finish_export(false, false, finalise_error);
            return;
        }

        item.status          = ARS_TRACKER_STATUS_DOWNLOADED;
        item.bytes_completed = completed_size;
        item.total_bytes     = completed_size;
        item.error_text.clear();

        if (item.category == ARS_TRACKER_FILE_SENSOR)
        {
            enqueue_next_sensor_candidate();
        }

        publish_export_file_rows();
        publish_progress_text();
        schedule_next_download_or_finish("download completed");
        return;
    }

    if (status == STATUS_CANCELLED || export_cancel_requested == true)
    {
        item.status     = ARS_TRACKER_STATUS_CANCELLED;
        item.error_text = "Cancelled";
        publish_export_file_rows();
        finish_export(false, true, "Session export cancelled.");
        return;
    }

    if (is_not_found_error(status, error_message))
    {
        QFile::remove(item.local_temp_file);

        if (item.category == ARS_TRACKER_FILE_SENSOR)
        {
            sensors_enumeration_done = true;
            download_queue.removeAt(current_download_index);
            current_download_index = -1;
            publish_export_file_rows();
            publish_progress_text();
            schedule_next_download_or_finish("sensor missing during download");
            return;
        }

        item.status     = ARS_TRACKER_STATUS_MISSING;
        item.error_text = "Remote file missing";
        item.bytes_completed = 0;
        item.total_bytes = 0;
        publish_export_file_rows();
        publish_progress_text();
        emit status_message(
            QString("Skipping %1, remote file is missing.")
                .arg(QFileInfo(item.remote_file).fileName()));
        schedule_next_download_or_finish("remote file missing during download");
        return;
    }

    if (is_empty_file_error(status, error_message))
    {
        QFile::remove(item.local_temp_file);

        if (item.category == ARS_TRACKER_FILE_SENSOR)
        {
            sensors_enumeration_done = true;
            download_queue.removeAt(current_download_index);
            current_download_index = -1;
            publish_export_file_rows();
            publish_progress_text();
            schedule_next_download_or_finish("sensor empty during download");
            return;
        }

        item.status          = ARS_TRACKER_STATUS_MISSING;
        item.error_text      = "Remote file is empty";
        item.bytes_completed = 0;
        item.total_bytes     = 0;
        publish_export_file_rows();
        publish_progress_text();
        emit status_message(
            QString("Skipping %1, remote file is empty.")
                .arg(QFileInfo(item.remote_file).fileName()));
        schedule_next_download_or_finish("remote file empty during download");
        return;
    }

    if (status == STATUS_TRANSPORT_DISCONNECTED)
    {
        item.status     = ARS_TRACKER_STATUS_FAILED;
        item.error_text = "Transport disconnected";
    } else if (status == STATUS_PROCESSOR_TRANSPORT_ERROR)
    {
        item.status     = ARS_TRACKER_STATUS_FAILED;
        item.error_text = "Transport send failed";
    } else if (status == STATUS_TIMEOUT)
    {
        item.status     = ARS_TRACKER_STATUS_FAILED;
        item.error_text = "Transfer timed out";
    } else
    {
        item.status     = ARS_TRACKER_STATUS_FAILED;
        item.error_text = error_message.isEmpty() ? "Transfer failed" : error_message;
    }

    export_failed = true;
    publish_export_file_rows();
    finish_export(false, false,
                  QString("Session export failed while downloading %1.")
                      .arg(QFileInfo(item.remote_file).fileName()));
}

bool ars_tracker_backend::export_in_progress() const
{
    return export_loading;
}

#ifndef SKIPPLUGIN_LOGGER
void ars_tracker_backend::set_logger(debug_logger* object)
{
    logger = object;
}
#endif

void ars_tracker_backend::cancel_all()
{
    if (tracker_info_loading == true)
    {
        emit status_message("Cancelling tracker info refresh...");
        emit request_cancel_tracker_info_shell_command();
        return;
    }

    if (export_loading == false)
    {
        emit status_message("No ArsTracker operation is currently running.");
        return;
    }

    export_cancel_requested = true;
    emit status_message("Cancelling session export...");
    emit request_cancel_file_download();
}

