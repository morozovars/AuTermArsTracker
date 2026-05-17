/******************************************************************************
** Copyright (C) 2015-2017 Laird
** Copyright (C) 2024 Jamie M.
**
** Project: AuTerm
**
** Module: main.cpp
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
#include "AutMainWindow.h"
#include "ars/workspace/ArsLocalWorkspace.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>
#include <QProcessEnvironment>
#include <QHash>
#include <QVector>
#include <algorithm>
#if defined(QT_STATIC) && defined(Q_OS_WIN)
#include <QtPlugin>
#endif
#include <cstdlib>
#include <cstdio>
#if TARGET_OS_MAC
#include <QStyleFactory>
#endif

#if defined(QT_STATIC) && defined(Q_OS_WIN) && defined(AUTERM_IMPORT_WINDOWS_QPA_PLUGIN)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif

#if defined(QT_STATIC) && defined(Q_OS_WIN) && defined(AUTERM_IMPORT_WINDOWS_VISTA_STYLE_PLUGIN)
Q_IMPORT_PLUGIN(QWindowsVistaStylePlugin)
#endif

static QFile *debug_log_file = nullptr;
static QMutex debug_log_mutex;
static bool g_log_flush_every_line = false;
static bool g_log_stats_enabled = false;
static bool g_log_top_sources_enabled = false;
static qint64 g_log_stats_window_start_ms = 0;
static qint64 g_log_top_sources_window_start_ms = 0;
static quint64 g_log_stats_messages = 0;
static quint64 g_log_stats_bytes = 0;
static quint64 g_log_stats_flushes = 0;
struct auterm_log_source_stats_t
{
    quint64 messages = 0;
    quint64 bytes = 0;
};
static QHash<QString, auterm_log_source_stats_t> g_log_source_stats;

static QString debug_log_source_key(const QMessageLogContext &context)
{
    const QString category = (context.category != nullptr) ? QString::fromLatin1(context.category) :
                                                          QString("default");
    const QString file_name = (context.file != nullptr) ?
                                  QFileInfo(QString::fromUtf8(context.file)).fileName() :
                                  QString("unknown");
    const QString function_name = (context.function != nullptr) ?
                                      QString::fromUtf8(context.function) :
                                      QString("unknown");
    return QString("%1:%2 | %3 | %4").arg(file_name).arg(context.line).arg(function_name, category);
}

static QString debug_level_name(QtMsgType type)
{
    switch (type)
    {
    case QtDebugMsg:
        return "DEBUG";
    case QtInfoMsg:
        return "INFO";
    case QtWarningMsg:
        return "WARNING";
    case QtCriticalMsg:
        return "ERROR";
    case QtFatalMsg:
        return "FATAL";
    }

    return "UNKNOWN";
}

static QString debug_log_module(const QMessageLogContext &context)
{
    if (context.category != nullptr && QString::fromLatin1(context.category) != "default")
    {
        return QString::fromLatin1(context.category);
    }

    if (context.file != nullptr)
    {
        return QFileInfo(QString::fromUtf8(context.file)).baseName();
    }

    return "default";
}

static QString debug_log_source(const QMessageLogContext &context)
{
    QString source;

    if (context.file != nullptr)
    {
        source = QString("%1:%2").arg(QFileInfo(QString::fromUtf8(context.file)).fileName())
                                  .arg(context.line);
    }

    if (context.function != nullptr)
    {
        if (source.isEmpty() == false)
        {
            source.append(" ");
        }

        source.append(QString::fromUtf8(context.function));
    }

    return source;
}

static void auterm_debug_message_handler(QtMsgType type, const QMessageLogContext &context,
                                         const QString &message)
{
    QMutexLocker locker(&debug_log_mutex);

    QString thread_name = QThread::currentThread() != nullptr ? QThread::currentThread()->objectName() :
                                                               QString();
    QString thread_text =
        QString("tid=0x%1").arg(QString::number(reinterpret_cast<quintptr>(
                                                    QThread::currentThreadId()),
                                                16));

    if (thread_name.isEmpty() == false)
    {
        thread_text.append(QString(" name=%1").arg(thread_name));
    }

    QString source = debug_log_source(context);
    QString line =
        QString("[%1] [%2] [%3] [%4] %5")
            .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"),
                 debug_level_name(type), debug_log_module(context), thread_text, message);

    if (source.isEmpty() == false)
    {
        line.append(QString(" (%1)").arg(source));
    }

    line.append('\n');

    QByteArray line_bytes = line.toUtf8();
    const bool is_warning_or_worse =
        (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg);
    bool did_flush = false;

    if (debug_log_file != nullptr && debug_log_file->isOpen())
    {
        debug_log_file->write(line_bytes);
        if (g_log_flush_every_line || is_warning_or_worse)
        {
            debug_log_file->flush();
            did_flush = true;
        }
    }

    ++g_log_stats_messages;
    g_log_stats_bytes += quint64(line_bytes.size());
    if (did_flush)
    {
        ++g_log_stats_flushes;
    }
    if (g_log_top_sources_enabled)
    {
        const QString source_key = debug_log_source_key(context);
        auterm_log_source_stats_t &entry = g_log_source_stats[source_key];
        ++entry.messages;
        entry.bytes += quint64(line_bytes.size());
    }

    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    if (g_log_stats_window_start_ms == 0)
    {
        g_log_stats_window_start_ms = now_ms;
    }
    if (g_log_top_sources_window_start_ms == 0)
    {
        g_log_top_sources_window_start_ms = now_ms;
    }
    if (g_log_stats_enabled && (now_ms - g_log_stats_window_start_ms) >= 5000)
    {
        const qint64 elapsed_ms = qMax<qint64>(1, now_ms - g_log_stats_window_start_ms);
        const double msgs_per_sec = (1000.0 * double(g_log_stats_messages)) / double(elapsed_ms);
        const double bytes_per_sec = (1000.0 * double(g_log_stats_bytes)) / double(elapsed_ms);
        const double flushes_per_sec = (1000.0 * double(g_log_stats_flushes)) / double(elapsed_ms);
        const QString stats_line =
            QString("[AUTERM_LOG_STATS] window_ms=%1 msgs_per_sec=%2 bytes_per_sec=%3 flushes_per_sec=%4\n")
                .arg(elapsed_ms)
                .arg(QString::number(msgs_per_sec, 'f', 1))
                .arg(QString::number(bytes_per_sec, 'f', 1))
                .arg(QString::number(flushes_per_sec, 'f', 1));
        const QByteArray stats_bytes = stats_line.toUtf8();
        if (debug_log_file != nullptr && debug_log_file->isOpen())
        {
            debug_log_file->write(stats_bytes);
            debug_log_file->flush();
        }
#if defined(QT_DEBUG)
        std::fwrite(stats_bytes.constData(), 1, size_t(stats_bytes.size()), stderr);
        std::fflush(stderr);
#endif
        g_log_stats_window_start_ms = now_ms;
        g_log_stats_messages = 0;
        g_log_stats_bytes = 0;
        g_log_stats_flushes = 0;
    }
    if (g_log_top_sources_enabled && (now_ms - g_log_top_sources_window_start_ms) >= 5000)
    {
        const qint64 elapsed_ms = qMax<qint64>(1, now_ms - g_log_top_sources_window_start_ms);
        QVector<QPair<QString, auterm_log_source_stats_t>> ranked;
        ranked.reserve(g_log_source_stats.size());
        for (auto it = g_log_source_stats.cbegin(); it != g_log_source_stats.cend(); ++it)
        {
            ranked.push_back(qMakePair(it.key(), it.value()));
        }
        std::sort(ranked.begin(), ranked.end(), [](const auto &lhs, const auto &rhs) {
            return lhs.second.messages > rhs.second.messages;
        });

        QString top_block = QString("[AUTERM_LOG_TOP_SOURCES] window_ms=%1\n").arg(elapsed_ms);
        const int limit = qMin(10, ranked.size());
        for (int i = 0; i < limit; ++i)
        {
            const double msgs_per_sec =
                (1000.0 * double(ranked[i].second.messages)) / double(elapsed_ms);
            const double bytes_per_sec =
                (1000.0 * double(ranked[i].second.bytes)) / double(elapsed_ms);
            top_block.append(
                QString("  %1) %2 | msgs_per_sec=%3 | bytes_per_sec=%4\n")
                    .arg(i + 1)
                    .arg(ranked[i].first)
                    .arg(QString::number(msgs_per_sec, 'f', 1))
                    .arg(QString::number(bytes_per_sec, 'f', 1)));
        }
        const QByteArray top_bytes = top_block.toUtf8();
        if (debug_log_file != nullptr && debug_log_file->isOpen())
        {
            debug_log_file->write(top_bytes);
            debug_log_file->flush();
        }
#if defined(QT_DEBUG)
        std::fwrite(top_bytes.constData(), 1, size_t(top_bytes.size()), stderr);
        std::fflush(stderr);
#endif
        g_log_source_stats.clear();
        g_log_top_sources_window_start_ms = now_ms;
    }

#if defined(QT_DEBUG)
    FILE *stream = (type == QtDebugMsg || type == QtInfoMsg) ? stdout : stderr;
    std::fwrite(line_bytes.constData(), 1, size_t(line_bytes.size()), stream);
    std::fflush(stream);
#endif

    if (type == QtFatalMsg)
    {
        abort();
    }
}

static void installApplicationMessageHandler()
{
    g_log_flush_every_line = qEnvironmentVariableIntValue("AUTERM_LOG_FLUSH_EVERY_LINE") == 1;
    g_log_stats_enabled = qEnvironmentVariableIntValue("AUTERM_LOG_STATS") == 1;
    g_log_top_sources_enabled = qEnvironmentVariableIntValue("AUTERM_LOG_TOP_SOURCES") == 1;
    QString log_path =
        QFileInfo(QDir(QCoreApplication::applicationDirPath()).filePath("auterm_debug.txt"))
            .absoluteFilePath();

    debug_log_file = new QFile(log_path, qApp);

    if (debug_log_file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text) == false)
    {
#if defined(QT_DEBUG)
        std::fprintf(stderr, "Could not open AuTerm debug log file: %s\n",
                     qPrintable(log_path));
        std::fflush(stderr);
#endif
        delete debug_log_file;
        debug_log_file = nullptr;
    }
    else
    {
        QByteArray banner =
            QString("\n===== AuTerm started %1 =====\n"
                    "Log file: %2\n")
                .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"),
                     log_path)
                .toUtf8();
        debug_log_file->write(banner);
        debug_log_file->flush();
    }

    qInstallMessageHandler(auterm_debug_message_handler);

#if defined(QT_DEBUG)
    if (debug_log_file != nullptr && debug_log_file->isOpen())
    {
        std::fprintf(stderr, "AuTerm debug log file: %s\n", qPrintable(log_path));
        std::fflush(stderr);
    }
#endif
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    installApplicationMessageHandler();
    qInfo().noquote() << "Application message handler installed";

    ArsLocalWorkspace localWorkspace;
    if (localWorkspace.initialize() == false)
    {
        qWarning() << "ArsLocalWorkspace initialization failed; application will continue running.";
    }

#if TARGET_OS_MAC
    //Fix for Mac to stop bad styling
    QApplication::setStyle(QStyleFactory::create("Fusion"));
#endif
    AutMainWindow main_window;
    main_window.showMaximized();
    return a.exec();
}

/******************************************************************************/
// END OF FILE
/******************************************************************************/
