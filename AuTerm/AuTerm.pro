# AuTerm Qt project qmake file

include(../AuTerm-includes.pri)

# Common build location
CONFIG(release, debug|release) {
    DESTDIR = ../release
} else {
    DESTDIR = ../debug
}

QT += core gui widgets serialport
INCLUDEPATH += ../src

!contains(DEFINES, SKIPONLINE) {
    QT += network
}

TARGET = AuTerm
TEMPLATE = app

win32:CONFIG(debug, debug|release) {
    QMAKE_LFLAGS_WINDOWS = -Wl,-subsystem,console
}

win32:CONFIG(release, debug|release) {
    CONFIG -= console
}

SOURCES += main.cpp\
    AutEscape.cpp \
    AutLogger.cpp \
    AutMainWindow.cpp \
    AutPlugin.cpp \
    AutPopup.cpp \
    AutScrollEdit.cpp \
    ../src/ars/workspace/ArsAppSettings.cpp \
    ../src/ars/workspace/ArsLocalWorkspace.cpp \
    ../src/ars/workspace/ArsPlayerRepository.cpp \
    ../src/ars/workspace/ArsPlayerPosition.cpp \
    ../src/ars/workspace/ArsSessionRepository.cpp \
    ../src/ars/workspace/ArsTargetsByPosition.cpp \
    ../src/ars/workspace/ArsTeamRepository.cpp \
    ../src/ars/workspace/ArsTrackerBindingRepository.cpp

HEADERS  += \
    AutEscape.h \
    AutLogger.h \
    AutMainWindow.h \
    AutPopup.h \
    AutScrollEdit.h \
    ../src/ars/workspace/ArsAppSettings.h \
    ../src/ars/workspace/ArsLocalWorkspace.h \
    ../src/ars/workspace/ArsPlayerRepository.h \
    ../src/ars/workspace/ArsPlayerPosition.h \
    ../src/ars/workspace/ArsSessionRepository.h \
    ../src/ars/workspace/ArsTargetsByPosition.h \
    ../src/ars/workspace/ArsTeamRepository.h \
    ../src/ars/workspace/ArsTrackerBindingRepository.h

FORMS    += \
    AutMainWindow.ui \
    AutPopup.ui

RESOURCES += \
    AuTermImages.qrc

# Automation form
!contains(DEFINES, SKIPAUTOMATIONFORM) {
    SOURCES += \
        AutAutomation.cpp
    HEADERS += \
        AutAutomation.h
    FORMS += \
        AutAutomation.ui
}

# Scripting form
!contains(DEFINES, SKIPSCRIPTINGFORM) {
    SOURCES += \
        AutCodeEditor.cpp \
        AutHighlighter.cpp \
        AutScripting.cpp

    HEADERS += \
        AutCodeEditor.h \
        AutHighlighter.h \
        AutScripting.h

    FORMS += \
    AutScripting.ui
}

# Error code form
!contains(DEFINES, SKIPERRORCODEFORM) {
    SOURCES += \
        AutErrorCode.cpp
    HEADERS += \
        AutErrorCode.h
    FORMS += \
        AutErrorCode.ui
}

# Serial detection object
!contains(DEFINES, SKIPSERIALDETECT) {
    HEADERS += \
        AutSerialDetect_base.h

    win32 {
        SOURCES += \
            AutSerialDetect_windows.cpp
        HEADERS += \
            AutSerialDetect_windows.h
        LIBS += -luser32
    } else {
        SOURCES += \
            AutSerialDetect_linux.cpp
        HEADERS += \
            AutSerialDetect_linux.h
        LIBS += -ludev
    }
}

# Windows application version information
win32:RC_FILE = version.rc

# Windows application icon
win32:RC_ICONS = images/Ars32.ico

# Mac application icon
ICON = MacAuTermIcon.icns

# Windows static Qt build support. This is intentionally scoped to the app
# target; the top-level subdirs project only orchestrates build order.
win32:contains(CONFIG, static) {
    QTPLUGIN += qwindows
    DEFINES += "AUTERM_IMPORT_WINDOWS_QPA_PLUGIN"

    contains(QT_CONFIG, style-windowsvista) {
        QTPLUGIN += qwindowsvistastyle
        DEFINES += "AUTERM_IMPORT_WINDOWS_VISTA_STYLE_PLUGIN"
    }

    !isEmpty(AUTERM_STATIC_QT_EXTRA_PLUGINS) {
        QTPLUGIN += $$AUTERM_STATIC_QT_EXTRA_PLUGINS
    }
}

# Common build location
CONFIG(release, debug|release) {
    DESTDIR = ../release
} else {
    DESTDIR = ../debug
}

# Plugins
!contains(DEFINES, SKIPPLUGINS) {
    HEADERS += AutPlugin.h

    contains(CONFIG, static) {
        CONFIG(release, debug|release) {
            ARS_TRACKER_ALGA_LIBDIR = ../release
            win32-g++|win32-clang-g++: ARS_TRACKER_ALGA_LIBFILE = ../release/libars_tracker_alga.a
            else:win32: ARS_TRACKER_ALGA_LIBFILE = ../release/ars_tracker_alga.lib
            else: ARS_TRACKER_ALGA_LIBFILE = ../release/libars_tracker_alga.a
        } else {
            ARS_TRACKER_ALGA_LIBDIR = ../debug
            win32-g++|win32-clang-g++: ARS_TRACKER_ALGA_LIBFILE = ../debug/libars_tracker_alga.a
            else:win32: ARS_TRACKER_ALGA_LIBFILE = ../debug/ars_tracker_alga.lib
            else: ARS_TRACKER_ALGA_LIBFILE = ../debug/libars_tracker_alga.a
        }

        QT += $$ADDITIONAL_MODULES

        !contains(DEFINES, SKIPPLUGIN_MCUMGR) {
            exists(../plugins/mcumgr) {
                DEFINES += "STATICPLUGIN_MCUMGR"

                win32: LIBS += -L$$DESTDIR -lplugin_mcumgr
                else: LIBS += -L$$DESTDIR -lplugin_mcumgr

                win32-g++: PRE_TARGETDEPS += $$DESTDIR/libplugin_mcumgr.a
                else:win32:!win32-g++: PRE_TARGETDEPS += $$DESTDIR/plugin_mcumgr.lib
                else: PRE_TARGETDEPS += $$DESTDIR/libplugin_mcumgr.a

                # plugin_mcumgr static objects reference PostProcessing symbols from ars_tracker_alga.
                # In static builds, AuTerm must link the alga static library explicitly.
                LIBS += $$ARS_TRACKER_ALGA_LIBFILE
                PRE_TARGETDEPS += $$ARS_TRACKER_ALGA_LIBFILE

                contains(DEFINES, PLUGIN_MCUMGR_TRANSPORT_BLUETOOTH) {
                    macx: QMAKE_INFO_PLIST = Info.plist
                }
            }
        }

        !contains(DEFINES, SKIPPLUGIN_LOGGER) {
            exists(../plugins/logger) {
                DEFINES += "STATICPLUGIN_LOGGER"

                win32: LIBS += -L$$DESTDIR -lplugin_logger
                else: LIBS += -L$$DESTDIR -lplugin_logger

                win32-g++: PRE_TARGETDEPS += $$DESTDIR/libplugin_logger.a
                else:win32:!win32-g++: PRE_TARGETDEPS += $$DESTDIR/plugin_logger.lib
                else: PRE_TARGETDEPS += $$DESTDIR/libplugin_logger.a
            }
        }

        !contains(DEFINES, SKIPPLUGINS_TRANSPORT) {
            !contains(DEFINES, SKIPPLUGIN_TRANSPORT_ECHO) {
                exists(../plugins/nus_transport) {
                    DEFINES += "STATICPLUGIN_TRANSPORT_ECHO"

                    win32: LIBS += -L$$DESTDIR -lplugin_echo_transport
                    else: LIBS += -L$$DESTDIR -lplugin_echo_transport

                    win32-g++: PRE_TARGETDEPS += $$DESTDIR/libplugin_echo_transport.a
                    else:win32:!win32-g++: PRE_TARGETDEPS += $$DESTDIR/plugin_echo_transport.lib
                    else: PRE_TARGETDEPS += $$DESTDIR/libplugin_echo_transport.a
                }
            }

            !contains(DEFINES, SKIPPLUGIN_TRANSPORT_NUS) {
                exists(../plugins/nus_transport) {
                    DEFINES += "STATICPLUGIN_TRANSPORT_NUS"

                    win32: LIBS += -L$$DESTDIR -lplugin_nus_transport
                    else: LIBS += -L$$DESTDIR -lplugin_nus_transport

                    win32-g++: PRE_TARGETDEPS += $$DESTDIR/libplugin_nus_transport.a
                    else:win32:!win32-g++: PRE_TARGETDEPS += $$DESTDIR/plugin_nus_transport.lib
                    else: PRE_TARGETDEPS += $$DESTDIR/libplugin_nus_transport.a
                }
            }
        }
    }
}
