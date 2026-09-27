QT += core testlib
QT -= gui
CONFIG += console testcase c++17
DEFINES += SKIPPLUGIN_LOGGER
TEMPLATE = app
TARGET = ars_tracker_log_utils_tests

SOURCES += \
    ars_tracker_log_utils_tests.cpp \
    ../ars_tracker_log_utils.cpp \
    ../ars_tracker_parser.cpp \
    ../smp_response_error_decoder.cpp \
    ../smp_shell_response_parser.cpp

HEADERS += \
    ../ars_tracker_log_utils.h \
    ../ars_tracker_parser.h \
    ../smp_response_error_decoder.h \
    ../smp_shell_response_parser.h
