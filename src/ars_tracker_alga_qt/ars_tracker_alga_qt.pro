include(../../AuTerm-includes.pri)

TEMPLATE = lib
CONFIG += staticlib
CONFIG += c++17

win32-g++|win32-clang-g++ {
    QMAKE_LIB = ar -rcs
    # Keep a fresh archive index for ld.lld when this static lib is linked from qmake plugin targets.
    QMAKE_POST_LINK += $$escape_expand(\n\t)ranlib $(DESTDIR_TARGET)
}

QT += core
QT -= gui widgets

TARGET = ars_tracker_alga

ARS_TRACKER_ALGA_ROOT = $$PWD/../ars_tracker_alga

!exists($$ARS_TRACKER_ALGA_ROOT/Algorithms.h) {
    error("Missing ars_tracker_alga sources at $$ARS_TRACKER_ALGA_ROOT. Initialize/update submodule src/ars_tracker_alga before building ars_tracker_alga_qt.")
}

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
    DESTDIR = ../../release
} else {
    DESTDIR = ../../debug
}
