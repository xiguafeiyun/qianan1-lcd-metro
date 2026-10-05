QT       += core gui widgets

CONFIG   += c++17
TEMPLATE  = app
TARGET    = metro-lcd

DEFINES  += QT_DEPRECATED_WARNINGS

INCLUDEPATH += src

SOURCES += \
    src/main.cpp \
    src/TrainScreen.cpp

HEADERS += \
    src/TrainScreen.h
