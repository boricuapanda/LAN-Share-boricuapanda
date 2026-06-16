QT += core network testlib widgets
CONFIG += console c++17 warn_on
TARGET = transfer_test
TEMPLATE = app

INCLUDEPATH += ../src

SOURCES += transfer_test.cpp \
    ../src/transfer/transfer.cpp \
    ../src/transfer/sender.cpp \
    ../src/transfer/receiver.cpp \
    ../src/transfer/tlshelper.cpp \
    ../src/transfer/transferjournal.cpp \
    ../src/transfer/transferserver.cpp \
    ../src/transfer/devicebroadcaster.cpp \
    ../src/model/transferinfo.cpp \
    ../src/model/transferfailure.cpp \
    ../src/model/device.cpp \
    ../src/model/devicelistmodel.cpp \
    ../src/settings.cpp \
    ../src/util.cpp \
    ../src/log.cpp

HEADERS += ../src/transfer/transfer.h \
    ../src/transfer/sender.h \
    ../src/transfer/receiver.h \
    ../src/transfer/tlshelper.h \
    ../src/transfer/transferjournal.h \
    ../src/transfer/transferserver.h \
    ../src/transfer/devicebroadcaster.h \
    ../src/model/transferinfo.h \
    ../src/model/transferfailure.h \
    ../src/model/device.h \
    ../src/model/devicelistmodel.h \
    ../src/settings.h \
    ../src/util.h \
    ../src/log.h
