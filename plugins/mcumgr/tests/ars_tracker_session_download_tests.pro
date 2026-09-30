QT += core testlib
CONFIG += console testcase c++17
DEFINES += SKIPPLUGIN_LOGGER
TEMPLATE = app
TARGET = ars_tracker_session_download_tests

INCLUDEPATH += ..

SOURCES += \
    ars_tracker_session_download_tests.cpp \
    ../ars_tracker_backend.cpp \
    ../ars_tracker_parser.cpp \
    ../ars_trackers_session_download_coordinator.cpp \
    ../crc16.cpp \
    ../smp_fs_status_response_parser.cpp

HEADERS += \
    ../ars_tracker_backend.h \
    ../ars_tracker_parser.h \
    ../ars_trackers_session_download_coordinator.h \
    ../crc16.h \
    ../smp_fs_status_response_parser.h
