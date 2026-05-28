TEMPLATE = subdirs

include(AuTerm-includes.pri)

SUBDIRS += \
    AuTerm

SUBDIRS += src/ars_tracker_reports/ars_reporter

!contains(DEFINES, SKIPPLUGINS) {
    ars_tracker_alga_qt.subdir = src/ars_tracker_alga_qt
    SUBDIRS += ars_tracker_alga_qt

    !contains(DEFINES, SKIPPLUGIN_MCUMGR) {
        mcumgr.subdir = plugins/mcumgr
        mcumgr.depends += ars_tracker_alga_qt
        SUBDIRS += mcumgr

        AuTerm.depends += mcumgr
    }

    !contains(DEFINES, SKIPPLUGIN_LOGGER) {
        SUBDIRS += \
            plugins/logger

        AuTerm.depends += plugins/logger
    }

    !contains(DEFINES, SKIPPLUGINS_TRANSPORT) {
        !contains(DEFINES, SKIPPLUGIN_TRANSPORT_ECHO) {
            SUBDIRS += \
                plugins/echo_transport

            AuTerm.depends += plugins/echo_transport
        }

        !contains(DEFINES, SKIPPLUGIN_TRANSPORT_NUS) {
            SUBDIRS += \
                plugins/nus_transport

            AuTerm.depends += plugins/nus_transport
        }
    }
}
RESOURCES += \
    AuTermImages.qrc
