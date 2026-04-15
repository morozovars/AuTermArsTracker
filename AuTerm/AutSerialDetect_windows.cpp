/******************************************************************************
** Copyright (C) 2024 Jamie M.
**
** Project: AuTerm
**
** Module: AutSerialDetect_windows.cpp
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
/* Include Files */
/******************************************************************************/
#include "AutSerialDetect_windows.h"

#include <QAbstractEventDispatcher>
#include <QSerialPortInfo>
#include <QWidget>

// clang-format off
#include <windows.h>
#include <initguid.h>
#include <dbt.h>
#include <devguid.h>
// clang-format on

/******************************************************************************/
/* Local Functions or Private Members */
/******************************************************************************/
AutSerialDetect::AutSerialDetect(QObject* parent) : AutSerialDetect_base{ parent }
{
    QAbstractEventDispatcher::instance()->installNativeEventFilter(this);
}

AutSerialDetect::~AutSerialDetect()
{
    QAbstractEventDispatcher::instance()->removeNativeEventFilter(this);
    stop();
}

void AutSerialDetect::start(QString port)
{
    auto* parentWidget = qobject_cast<QWidget*>(this->parent());
    if (parentWidget == nullptr)
    {
        return;
    }

    HWND                            main_window = reinterpret_cast<HWND>(parentWidget->winId());
    DEV_BROADCAST_DEVICEINTERFACE_A dev_search;

    memset(&dev_search, 0, sizeof(dev_search));
    dev_search.dbcc_size       = sizeof(dev_search);
    dev_search.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    dev_search.dbcc_classguid  = GUID_DEVCLASS_PORTS;

    access = RegisterDeviceNotificationA(main_window, &dev_search, DEVICE_NOTIFY_WINDOW_HANDLE);

    if (access == nullptr)
    {
        return;
    }

    watch_port = port;
    port_set   = true;
}

void AutSerialDetect::stop()
{
    if (!port_set)
    {
        return;
    }

    if (access != nullptr)
    {
        UnregisterDeviceNotification(reinterpret_cast<HDEVNOTIFY>(access));
        access = nullptr;
    }

    port_set = false;
    watch_port.clear();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
bool AutSerialDetect::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
#else
bool AutSerialDetect::nativeEventFilter(const QByteArray& eventType, void* message, long* result)
#endif
{
    Q_UNUSED(eventType);
    Q_UNUSED(result);

    MSG* msg = static_cast<MSG*>(message);
    if (msg == nullptr)
    {
        return false;
    }

    const uint   type  = msg->message;
    const WPARAM param = msg->wParam;

    if (type == WM_DEVICECHANGE && param == DBT_DEVICEARRIVAL)
    {
        auto* hdr = reinterpret_cast<DEV_BROADCAST_HDR*>(msg->lParam);

        if (hdr != nullptr && hdr->dbch_devicetype == DBT_DEVTYP_PORT)
        {
            for (const QSerialPortInfo& info : QSerialPortInfo::availablePorts())
            {
                if (info.portName() == watch_port)
                {
                    emit port_reconnected(watch_port);
                    stop();
                    break;
                }
            }
        }
    }

    return false;
}

/******************************************************************************/
/* END OF FILE */
/******************************************************************************/
