/******************************************************************************
** Copyright (C) 2023-2024 Jamie M.
**
** Project: AuTerm
**
** Module:  smp_group_shell_mgmt.cpp
**
** Notes:
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

/******************************************************************************/
// Include Files
/******************************************************************************/
#include "smp_group_shell_mgmt.h"
#include "smp_shell_response_parser.h"

/******************************************************************************/
// Enum typedefs
/******************************************************************************/
enum modes : uint8_t {
    MODE_IDLE = 0,
    MODE_EXECUTE,
};

enum shell_mgmt_commands : uint8_t {
    COMMAND_EXECUTE = 0,
};

/******************************************************************************/
// Constants
/******************************************************************************/
static const QStringList smp_error_defines = QStringList() <<
    //Error index starts from 2 (no error and unknown error are common and handled in the base code)
    "COMMAND_TOO_LONG" <<
    "EMPTY_COMMAND";

static const QStringList smp_error_values = QStringList() <<
    //Error index starts from 2 (no error and unknown error are common and handled in the base code)
    "The provided command to execute is too long" <<
    "No command to execute was provided";

/******************************************************************************/
// Local Functions or Private Members
/******************************************************************************/
smp_group_shell_mgmt::smp_group_shell_mgmt(smp_processor *parent) : smp_group(parent, "SHELL", SMP_GROUP_ID_SHELL, error_lookup, error_define_lookup)
{
    mode = MODE_IDLE;
}

void smp_group_shell_mgmt::receive_ok(uint8_t version, uint8_t op, uint16_t group, uint8_t command, QByteArray data)
{
    Q_UNUSED(op);
    //    qDebug() << "Got ok: " << version << ", " << op << ", " << group << ", "  << command << ", " << data;

    if (mode == MODE_IDLE)
    {
        log_error() << "Unexpected response, not busy";
        emit status(smp_user_data, STATUS_ERROR, "Unexpected response, shell mgmt not busy");
    }
    else if (group != SMP_GROUP_ID_SHELL)
    {
        log_error() << "Unexpected group " << group << ", not " << SMP_GROUP_ID_SHELL;
        emit status(smp_user_data, STATUS_ERROR, "Unexpected group, not shell mgmt");
    }
    else
    {
        uint8_t finished_mode = mode;
        mode = MODE_IDLE;

        if (version != smp_version)
        {
            //The target device does not support the SMP version being used, adjust for duration of transfer and raise a warning to the parent
            smp_version = version;
            emit version_error(version);
        }

        if (finished_mode == MODE_EXECUTE && command == COMMAND_EXECUTE)
        {
            //Response to execute
            const smp_shell_execute_response_t response =
                    smp_shell_response_parser::parse_execute_response(data);
            execute_ret_valid = response.valid && response.ret_valid;
            *return_ret = response.ret_valid ? response.ret : 0;

            if (response.valid)
            {
                emit status(smp_user_data, STATUS_COMPLETE, response.output);
            }
            else
            {
                emit status(smp_user_data, STATUS_ERROR,
                            QStringLiteral("Invalid shell management response"));
            }
        }
        else
        {
            log_error() << "Unsupported command received";
        }
    }
}

void smp_group_shell_mgmt::receive_error(uint8_t version, uint8_t op, uint16_t group, uint8_t command, smp_error_t error)
{
    Q_UNUSED(version);
    Q_UNUSED(op);
    Q_UNUSED(group);
    Q_UNUSED(error);

    bool cleanup = true;
    log_error() << "error :(";

    if (command == COMMAND_EXECUTE && mode == MODE_EXECUTE)
    {
        //TODO
        emit status(smp_user_data, status_error_return(error), smp_error::error_lookup_string(&error));
    }
    else
    {
        //Unexpected response operation for mode
        emit status(smp_user_data, STATUS_ERROR, QString("Unexpected error (Mode: %1, op: %2)").arg(mode_to_string(mode), command_to_string(command)));
    }

    if (cleanup == true)
    {
        mode = MODE_IDLE;
    }
}

void smp_group_shell_mgmt::cancel()
{
    if (mode != MODE_IDLE)
    {
        if (processor->is_busy())
        {
            processor->cancel();
        }
        else
        {
            mode = MODE_IDLE;
            emit status(smp_user_data, STATUS_CANCELLED, nullptr);
        }
    }
}

bool smp_group_shell_mgmt::start_execute(QStringList *arguments, int32_t *ret)
{
    smp_message *tmp_message = new smp_message();
    tmp_message->start_message(SMP_OP_WRITE, smp_version, SMP_GROUP_ID_SHELL, COMMAND_EXECUTE, 1);
    tmp_message->writer()->append("argv");
    tmp_message->writer()->startArray(arguments->length());

    uint8_t i = 0;
    while (i < arguments->length())
    {
        tmp_message->writer()->append(arguments->at(i));
        ++i;
    }

    tmp_message->writer()->endArray();
    tmp_message->end_message();

    return_ret = ret;
    *return_ret = 0;
    execute_ret_valid = false;
    mode = MODE_EXECUTE;

    //	    qDebug() << "len: " << message.length();

    if (check_message_before_send(tmp_message) == false)
    {
        return false;
    }

    return handle_transport_error(processor->send(tmp_message, smp_timeout, smp_retries, true));
}

bool smp_group_shell_mgmt::last_execute_ret_valid() const
{
    return execute_ret_valid;
}

QString smp_group_shell_mgmt::mode_to_string(uint8_t mode)
{
    switch (mode)
    {
    case MODE_IDLE:
        return "Idle";
    case MODE_EXECUTE:
        return "Executing";
    default:
        return "Invalid";
    }
}

QString smp_group_shell_mgmt::command_to_string(uint8_t command)
{
    switch (command)
    {
    case COMMAND_EXECUTE:
        return "Executing";
    default:
        return "Invalid";
    }
}

bool smp_group_shell_mgmt::error_lookup(int32_t rc, QString *error)
{
    rc -= smp_version_2_error_code_start;

    if (rc < smp_error_values.length())
    {
        *error = smp_error_values.at(rc);
        return true;
    }

    return false;
}

bool smp_group_shell_mgmt::error_define_lookup(int32_t rc, QString *error)
{
    rc -= smp_version_2_error_code_start;

    if (rc < smp_error_defines.length())
    {
        *error = smp_error_defines.at(rc);
        return true;
    }

    return false;
}

void smp_group_shell_mgmt::cleanup()
{
    mode = MODE_IDLE;
    return_ret = nullptr;
}

/******************************************************************************/
// END OF FILE
/******************************************************************************/
