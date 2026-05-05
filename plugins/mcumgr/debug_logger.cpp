/******************************************************************************
** Copyright (C) 2023 Jamie M.
**
** Project: AuTerm
**
** Module:  debug_logger.cpp
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
#ifndef SKIPPLUGIN_LOGGER

#include "debug_logger.h"
#include <QMutexLocker>

debug_logger::debug_logger(QObject *parent)
    : QIODevice{parent}
{
    QIODevice::open(QIODevice::WriteOnly);
    plugin_active = false;
    logger_type = log_level_debug;
    logger_file = nullptr;
    logger_line = 0;
    logger_function = nullptr;

    QMessageLogger(__FILE__, __LINE__, Q_FUNC_INFO, "mcumgr")
        .debug()
        .noquote()
        << "AUTERM MCUMGR debug_logger constructed"
        << "parent=" << (parent != nullptr ? parent->metaObject()->className() : "null");
}

debug_logger::~debug_logger()
{
    QMutexLocker locker(&state_mutex);
    if (plugin_active == true)
    {
        logger_pointer = nullptr;
        plugin_active = false;
    }
}

qint64 debug_logger::readData(char *, qint64)
{
    return 0;
}

qint64 debug_logger::writeData(const char *data, qint64 len)
{
    QString message = QString::fromUtf8(data, int(len)).trimmed();

    if (message.isEmpty())
    {
        return len;
    }

    logger_options_snapshot_t snapshot = options_snapshot();
    bool plugin_is_active = false;
    {
        QMutexLocker locker(&state_mutex);
        plugin_is_active = plugin_active;
    }

    if (plugin_is_active == true)
    {
        emit logger_log(snapshot.type, snapshot.title, message);
    }

    QByteArray category = snapshot.title.toUtf8();
    QMessageLogger message_logger(snapshot.file, snapshot.line, snapshot.function,
                                  category.constData());

    switch (snapshot.type)
    {
    case log_level_error:
        message_logger.critical().noquote() << message;
        break;
    case log_level_warning:
        message_logger.warning().noquote() << message;
        break;
    case log_level_information:
        message_logger.info().noquote() << message;
        break;
    case log_level_debug:
    default:
        message_logger.debug().noquote() << message;
        break;
    }

    return len;
}

void debug_logger::find_logger_plugin(const QObject *main_window)
{
    plugin_data logger;

    connect(this, SIGNAL(find_plugin(QString,plugin_data*)), main_window, SLOT(find_plugin(QString,plugin_data*)));
    emit find_plugin("logger", &logger);
    disconnect(this, SIGNAL(find_plugin(QString,plugin_data*)), main_window, SLOT(find_plugin(QString,plugin_data*)));

    if (logger.found == false)
    {
        return;
    }

    {
        QMutexLocker locker(&state_mutex);
        logger_pointer = logger.object;
        plugin_active = true;
    }

    connect(this, SIGNAL(logger_log(log_level_types,QString,QString)), logger_pointer, SLOT(log_message(log_level_types,QString,QString)));
    connect(this, SIGNAL(logger_set_visible(bool)), logger_pointer, SLOT(set_enabled(bool)));
}

void debug_logger::set_options(QString title, log_level_types type, const char *file,
                               int line, const char *function)
{
    QMutexLocker locker(&state_mutex);
    logger_title = title;
    logger_type = type;
    logger_file = file;
    logger_line = line;
    logger_function = function;
}

debug_logger::logger_options_snapshot_t debug_logger::options_snapshot() const
{
    QMutexLocker locker(&state_mutex);
    logger_options_snapshot_t snapshot;
    snapshot.title = logger_title;
    snapshot.type = logger_type;
    snapshot.file = logger_file;
    snapshot.line = logger_line;
    snapshot.function = logger_function;
    return snapshot;
}

#endif
