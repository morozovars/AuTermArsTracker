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
#include <QApplication>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QThread>
#include <cstdlib>
#include <cstdio>
#if TARGET_OS_MAC
#include <QStyleFactory>
#endif
#if defined(Q_OS_WIN)
#include <windows.h>
#endif

#ifndef QT_NO_DEBUG
static QFile *debug_log_file = nullptr;
static QMutex debug_log_mutex;

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

    if (debug_log_file != nullptr && debug_log_file->isOpen())
    {
        debug_log_file->write(line_bytes);
        debug_log_file->flush();
    }

    FILE *stream = (type == QtDebugMsg || type == QtInfoMsg) ? stdout : stderr;
    std::fwrite(line_bytes.constData(), 1, size_t(line_bytes.size()), stream);
    std::fflush(stream);

#if defined(Q_OS_WIN)
    OutputDebugStringW(reinterpret_cast<const wchar_t *>(line.utf16()));
#endif

    if (type == QtFatalMsg)
    {
        abort();
    }
}

static void install_auterm_debug_logging()
{
    QString log_path =
        QFileInfo(QDir(QCoreApplication::applicationDirPath()).filePath("auterm_debug.txt"))
            .absoluteFilePath();

    std::fprintf(stderr, "AUTERM DEBUG LOG PATH: %s\n", qPrintable(log_path));
    std::fflush(stderr);

    debug_log_file = new QFile(log_path, qApp);

    if (debug_log_file->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text) == false)
    {
        std::fprintf(stderr, "Could not open AuTerm debug log file: %s\n",
                     qPrintable(log_path));
        delete debug_log_file;
        debug_log_file = nullptr;
    }
    else
    {
        QByteArray banner =
            QString("\n----- AuTerm debug log started %1 -----\n"
                    "AUTERM DEBUG LOG PATH: %2\n")
                .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"),
                     log_path)
                .toUtf8();
        debug_log_file->write(banner);
        debug_log_file->flush();
    }

    qInstallMessageHandler(auterm_debug_message_handler);
}

static void auterm_debug_startup_probe(const char *message, const char *file, int line,
                                       const char *function)
{
    QMessageLogContext context(file, line, function, "auterm.startup");
    auterm_debug_message_handler(QtDebugMsg, context, QString::fromLatin1(message));
}
#endif

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
#ifndef QT_NO_DEBUG
    install_auterm_debug_logging();
    std::fprintf(stderr, "AUTERM DEBUG HANDLER INSTALLED\n");
    std::fflush(stderr);
    auterm_debug_startup_probe("AUTERM DEBUG HANDLER TEST", __FILE__, __LINE__, Q_FUNC_INFO);
#else
    std::fprintf(stderr, "AUTERM DEBUG HANDLER DISABLED: QT_NO_DEBUG is defined\n");
    std::fflush(stderr);
#endif
#if TARGET_OS_MAC
    //Fix for Mac to stop bad styling
    QApplication::setStyle(QStyleFactory::create("Fusion"));
#endif
    AutMainWindow w;
    w.show();

    return a.exec();
}

/******************************************************************************/
// END OF FILE
/******************************************************************************/
