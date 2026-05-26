include(../../AuTerm-includes.pri)

TEMPLATE = lib
CONFIG += staticlib
CONFIG += c++17

QT += core
QT -= gui widgets

TARGET = ars_tracker_alga

ARS_TRACKER_ALGA_ROOT = $$PWD/../ars_tracker_alga

INCLUDEPATH += \
    $$ARS_TRACKER_ALGA_ROOT \
    $$ARS_TRACKER_ALGA_ROOT/sources/soccer_insole

DEPENDPATH += \
    $$ARS_TRACKER_ALGA_ROOT \
    $$ARS_TRACKER_ALGA_ROOT/sources/soccer_insole

SOURCES += \
    $$ARS_TRACKER_ALGA_ROOT/sources/soccer_insole/PostProcessing.cpp

HEADERS += \
    $$ARS_TRACKER_ALGA_ROOT/Algorithms.h \
    $$ARS_TRACKER_ALGA_ROOT/Biomech.h \
    $$ARS_TRACKER_ALGA_ROOT/FootballStat.h \
    $$ARS_TRACKER_ALGA_ROOT/sources/soccer_insole/PostProcessing.h

CONFIG(release, debug|release) {
    DESTDIR = $$PWD/../../release
} else {
    DESTDIR = $$PWD/../../debug
}
