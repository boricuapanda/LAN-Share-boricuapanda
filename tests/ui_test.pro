QT += core gui network testlib widgets
CONFIG += console c++17 warn_on
TARGET = ui_test
TEMPLATE = app

INCLUDEPATH += ../src

SOURCES += ui_test.cpp \
    ../src/settings.cpp \
    ../src/util.cpp \
    ../src/log.cpp \
    ../src/ui/mainwindow.cpp \
    ../src/ui/receiverselectordialog.cpp \
    ../src/ui/aboutdialog.cpp \
    ../src/ui/settingsdialog.cpp \
    ../src/ui/logviewerdialog.cpp \
    ../src/ui/tlstrustdialog.cpp \
    ../src/ui/uitheme.cpp \
    ../src/ui/transferprogresswidget.cpp \
    ../src/transfer/devicebroadcaster.cpp \
    ../src/transfer/receiver.cpp \
    ../src/transfer/sender.cpp \
    ../src/transfer/transfer.cpp \
    ../src/transfer/transferjournal.cpp \
    ../src/transfer/tlshelper.cpp \
    ../src/transfer/transferserver.cpp \
    ../src/model/device.cpp \
    ../src/model/devicelistmodel.cpp \
    ../src/model/transferinfo.cpp \
    ../src/model/transferfailure.cpp \
    ../src/model/transfertablemodel.cpp

HEADERS += ../src/settings.h \
    ../src/util.h \
    ../src/log.h \
    ../src/ui/mainwindow.h \
    ../src/ui/receiverselectordialog.h \
    ../src/ui/aboutdialog.h \
    ../src/ui/settingsdialog.h \
    ../src/ui/logviewerdialog.h \
    ../src/ui/tlstrustdialog.h \
    ../src/ui/uitheme.h \
    ../src/ui/transferprogresswidget.h \
    ../src/transfer/devicebroadcaster.h \
    ../src/transfer/receiver.h \
    ../src/transfer/sender.h \
    ../src/transfer/transfer.h \
    ../src/transfer/transferjournal.h \
    ../src/transfer/tlshelper.h \
    ../src/transfer/transferserver.h \
    ../src/model/device.h \
    ../src/model/devicelistmodel.h \
    ../src/model/transferinfo.h \
    ../src/model/transferfailure.h \
    ../src/model/transfertablemodel.h

FORMS += ../src/ui/mainwindow.ui \
    ../src/ui/receiverselectordialog.ui \
    ../src/ui/aboutdialog.ui \
    ../src/ui/settingsdialog.ui

RESOURCES += ../src/res.qrc
