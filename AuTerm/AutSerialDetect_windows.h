/******************************************************************************
** Copyright (C) 2024 Jamie M.
**
** Project: AuTerm
**
** Module: AutSerialDetect_windows.h
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
#ifndef AUTSERIALDETECT_WINDOWS_H
#define AUTSERIALDETECT_WINDOWS_H

/******************************************************************************/
/* Include Files */
/******************************************************************************/
#include "AutSerialDetect_base.h"
#include <QObject>
#include <QAbstractNativeEventFilter>

/******************************************************************************/
/* Class definitions */
/******************************************************************************/
class AutSerialDetect : public AutSerialDetect_base, public QAbstractNativeEventFilter
{
    Q_OBJECT

public:
    explicit AutSerialDetect(QObject* parent = nullptr);
    ~AutSerialDetect() override;

    void start(QString port) override;
    void stop() override;

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;
#else
    bool nativeEventFilter(const QByteArray& eventType, void* message, long* result) override;
#endif

private:
    void* access = nullptr;
};

#endif // AUTSERIALDETECT_WINDOWS_H

/******************************************************************************/
/* END OF FILE */
/******************************************************************************/
