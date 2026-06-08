include(../../AuTerm-includes.pri)

QT += gui widgets serialport $$ADDITIONAL_MODULES

TEMPLATE = lib

CONFIG += plugin
CONFIG += c++17

include(../../src/ars_tracker_reports/ars_reporter/ars_reporter_sources.pri)

ARS_TRACKER_ALGA_ROOT = $$PWD/../../src/ars_tracker_alga

INCLUDEPATH    += ../../AuTerm \
    ../../src \
    $$ARS_TRACKER_ALGA_ROOT
DEPENDPATH += $$ARS_TRACKER_ALGA_ROOT
TARGET          = $$qtLibraryTarget(plugin_mcumgr)

!exists($$ARS_TRACKER_ALGA_ROOT/Algorithms.h) {
    error("Missing ars_tracker_alga sources at $$ARS_TRACKER_ALGA_ROOT. Initialize/update submodule src/ars_tracker_alga before building plugin_mcumgr.")
}

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    ../../AuTerm/AutScrollEdit.cpp \
    ars_tracker/ars_processed_str_parser.cpp \
    ars_tracker/ars_session_postprocessor.cpp \
    ars_tracker/ars_session_duration_scanner.cpp \
    ars_tracker/ars_session_info_json.cpp \
    ars_tracker/ars_session_assignment_dialog.cpp \
    ars_tracker/ars_session_processing_loader.cpp \
    ars_tracker/ars_report_session_data_builder.cpp \
    ars_tracker/ars_tracker_assign_pair_dialog.cpp \
    ars_tracker/ars_team_edit_dialog.cpp \
    ars_tracker/ars_team_targets_dialog.cpp \
    ars_tracker/ars_tracker_player_edit_dialog.cpp \
    ars_tracker/ars_tracker_players_tab.cpp \
    ars_tracker/ars_tracker_team_tab.cpp \
    ars_trackers_session_download_coordinator.cpp \
    ars_tracker_bulk_fw_update_dialog.cpp \
    ars_tracker_bulk_fw_update_worker.cpp \
    ars_trackers_ui_state.cpp \
    ars_tracker_sessions_tab.cpp \
    ../../AuTerm/AutEscape.cpp \
    ars_tracker_backend.cpp \
    ars_tracker_utils.cpp \
    ars_tracker_parser.cpp \
    crc16.cpp \
    debug_logger.cpp \
    error_lookup.cpp \
    plugin_mcumgr.cpp \
    smp_error.cpp \
    smp_group_enum_mgmt.cpp \
    smp_group_fs_mgmt.cpp \
    smp_group_os_mgmt.cpp \
    smp_group_settings_mgmt.cpp \
    smp_group_shell_mgmt.cpp \
    smp_group_stat_mgmt.cpp \
    smp_group_zephyr_mgmt.cpp \
    smp_json.cpp \
    smp_message.cpp \
    smp_processor.cpp \
    smp_uart_auterm.cpp \
    smp_group_img_mgmt.cpp \
    ../../src/ars/workspace/ArsLocalWorkspace.cpp \
    ../../src/ars/workspace/ArsPlayerRepository.cpp \
    ../../src/ars/workspace/ArsAppSettings.cpp \
    ../../src/ars/workspace/ArsPlayerPosition.cpp \
    ../../src/ars/workspace/ArsTargetsByPosition.cpp \
    ../../src/ars/workspace/ArsSessionPlayerBindingResolver.cpp \
    ../../src/ars/workspace/ArsSessionRepository.cpp \
    ../../src/ars/workspace/ArsTeamRepository.cpp \
    ../../src/ars/workspace/ArsTrackerBindingRepository.cpp

HEADERS += \
    ../../AuTerm/AutPlugin.h \
    ../../AuTerm/AutScrollEdit.h \
    ars_tracker/ars_processed_str_parser.h \
    ars_tracker/ars_session_postprocessor.h \
    ars_tracker/ars_session_duration_scanner.h \
    ars_tracker/ars_session_info.h \
    ars_tracker/ars_session_info_json.h \
    ars_tracker/ars_session_assignment_dialog.h \
    ars_tracker/ars_session_processing_loader.h \
    ars_tracker/ars_report_session_data_builder.h \
    ars_tracker/ars_tracker_assign_pair_dialog.h \
    ars_tracker/ars_team_edit_dialog.h \
    ars_tracker/ars_team_targets_dialog.h \
    ars_tracker/ars_tracker_player_edit_dialog.h \
    ars_tracker/ars_tracker_players_tab.h \
    ars_tracker/ars_tracker_team_tab.h \
    ars_trackers_session_download_coordinator.h \
    ars_tracker_bulk_fw_update_dialog.h \
    ars_tracker_bulk_fw_update_models.h \
    ars_tracker_bulk_fw_update_worker.h \
    ars_trackers_ui_state.h \
    ars_tracker_sessions_tab.h \
    ../../AuTerm/AutEscape.h \
    ars_tracker_backend.h \
    ars_tracker_utils.h \
    ars_tracker_parser.h \
    crc16.h \
    debug_logger.h \
    error_lookup.h \
    plugin_mcumgr.h \
    smp_error.h \
    smp_group_array.h \
    smp_group_enum_mgmt.h \
    smp_group_fs_mgmt.h \
    smp_group_os_mgmt.h \
    smp_group_settings_mgmt.h \
    smp_group_shell_mgmt.h \
    smp_group_stat_mgmt.h \
    smp_group_zephyr_mgmt.h \
    smp_json.h \
    smp_message.h \
    smp_processor.h \
    smp_transport.h \
    smp_uart_auterm.h \
    smp_group.h \
    smp_group_img_mgmt.h \
    ../../src/ars/workspace/ArsAppSettings.h \
    ../../src/ars/workspace/ArsPlayerPosition.h \
    ../../src/ars/workspace/ArsTargetsByPosition.h \
    ../../src/ars/workspace/ArsPlayer.h \
    ../../src/ars/workspace/ArsPlayerRepository.h \
    ../../src/ars/workspace/ArsSessionPlayerBindingResolver.h \
    ../../src/ars/workspace/ArsTrackerBinding.h \
    ../../src/ars/workspace/ArsTrackerBindingRepository.h

DISTFILES += plugin_mcumgr.json

# Default rules for deployment.
unix {
    target.path = $$[QT_INSTALL_PLUGINS]/plugin_mcumgr
}
!isEmpty(target.path): INSTALLS += target

CONFIG += install_ok  # Do not cargo-cult this!

# Common build location
CONFIG(release, debug|release) {
    DESTDIR = ../../release
    ARS_TRACKER_ALGA_LIBDIR = ../../release
    win32-g++|win32-clang-g++: ARS_TRACKER_ALGA_LIBFILE = ../../release/libars_tracker_alga.a
    else:win32: ARS_TRACKER_ALGA_LIBFILE = ../../release/ars_tracker_alga.lib
    else: ARS_TRACKER_ALGA_LIBFILE = ../../release/libars_tracker_alga.a
} else {
    DESTDIR = ../../debug
    ARS_TRACKER_ALGA_LIBDIR = ../../debug
    win32-g++|win32-clang-g++: ARS_TRACKER_ALGA_LIBFILE = ../../debug/libars_tracker_alga.a
    else:win32: ARS_TRACKER_ALGA_LIBFILE = ../../debug/ars_tracker_alga.lib
    else: ARS_TRACKER_ALGA_LIBFILE = ../../debug/libars_tracker_alga.a


    # The following form is only used for creating the GUI in Qt Creator, it is
    # not used by any part of the code in a normal build, therefore only build
    # this in debug mode.
    SOURCES += \
        form.cpp

    HEADERS += \
        form.h

    FORMS += \
        form.ui
}

win32-g++|win32-clang-g++ {
    # Force ld.lld to pull the PostProcessing object from ars_tracker_alga even when archive
    # extraction is sensitive to the current Windows/llvm-mingw static-lib setup.
    LIBS += -Wl,--whole-archive $$ARS_TRACKER_ALGA_LIBFILE -Wl,--no-whole-archive
} else {
    LIBS += $$ARS_TRACKER_ALGA_LIBFILE
}
PRE_TARGETDEPS += $$ARS_TRACKER_ALGA_LIBFILE

# Do not prefix with lib for non-static builds
!contains(CONFIG, static) {
    CONFIG += no_plugin_name_prefix
}

# This is a GUI build
DEFINES += GUI_PRESENT

FORMS += \
    error_lookup.ui

contains(DEFINES, PLUGIN_MCUMGR_TRANSPORT_BLUETOOTH) {
    SOURCES += \
        bluetooth_setup.cpp \
        smp_bluetooth.cpp

    HEADERS += \
        bluetooth_setup.h \
        smp_bluetooth.h

    FORMS += \
        bluetooth_setup.ui
}

contains(DEFINES, PLUGIN_MCUMGR_TRANSPORT_UDP) {
    SOURCES += \
        udp_setup.cpp \
        smp_udp.cpp

    HEADERS += \
        udp_setup.h \
        smp_udp.h

    FORMS += \
        udp_setup.ui
}

contains(DEFINES, PLUGIN_MCUMGR_TRANSPORT_LORAWAN) {
    SOURCES += \
        lorawan_setup.cpp \
        smp_lorawan.cpp

    HEADERS += \
        lorawan_setup.h \
        smp_lorawan.h

    FORMS += \
    lorawan_setup.ui
}
