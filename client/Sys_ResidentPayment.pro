QT       += core gui network
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    src/main.cpp \
    src/common/network/socket.cpp \
    src/ui/main/mainwindow.cpp \
    src/ui/main/widget.cpp \
    src/ui/dialogs/login/loginswitch.cpp \
    src/ui/dialogs/login/registerdialog.cpp \
    src/ui/dialogs/billsdialog.cpp \
    src/ui/dialogs/noticedialog.cpp \
    src/ui/dialogs/resultdialog.cpp \
    src/ui/dialogs/retrievedialog.cpp \
    src/ui/admin/admin_mainwindow.cpp \
    src/ui/admin/adminreportdialog.cpp \
    src/ui/admin/reportwidget.cpp \
    src/ui/widgets/bubblelabel.cpp \
    src/ui/widgets/clickablelabel.cpp \
    src/ui/widgets/dashboardcard.cpp \
    src/ui/widgets/feecardwidget.cpp \
    src/ui/widgets/historylistwidget.cpp \
    src/ui/widgets/imageslot.cpp \
    src/ui/widgets/leftbubble.cpp \
    src/ui/widgets/rightbubble.cpp \
    src/ui/widgets/reportitemwidget.cpp

HEADERS += \
    src/common/global.h \
    src/common/network/socket.h \
    src/ui/main/mainwindow.h \
    src/ui/main/widget.h \
    src/ui/dialogs/login/loginswitch.h \
    src/ui/dialogs/login/registerdialog.h \
    src/ui/dialogs/billsdialog.h \
    src/ui/dialogs/noticedialog.h \
    src/ui/dialogs/resultdialog.h \
    src/ui/dialogs/retrievedialog.h \
    src/ui/admin/admin_mainwindow.h \
    src/ui/admin/adminreportdialog.h \
    src/ui/admin/reportwidget.h \
    src/ui/widgets/bubblelabel.h \
    src/ui/widgets/clickablelabel.h \
    src/ui/widgets/dashboardcard.h \
    src/ui/widgets/feecardwidget.h \
    src/ui/widgets/historylistwidget.h \
    src/ui/widgets/imageslot.h \
    src/ui/widgets/leftbubble.h \
    src/ui/widgets/rightbubble.h \
    src/ui/widgets/reportitemwidget.h

FORMS += \
    src/ui/main/mainwindow.ui \
    src/ui/main/widget.ui \
    src/ui/dialogs/login/registerdialog.ui \
    src/ui/dialogs/billsdialog.ui \
    src/ui/dialogs/noticedialog.ui \
    src/ui/dialogs/resultdialog.ui \
    src/ui/dialogs/retrievedialog.ui \
    src/ui/admin/admin_mainwindow.ui \
    src/ui/admin/adminreportdialog.ui \
    src/ui/admin/reportwidget.ui \
    src/ui/widgets/reportitemwidget.ui \
    src/ui/widgets/feecardwidget.ui \
    src/ui/widgets/historylistwidget.ui \
    src/ui/widgets/leftbubble.ui \
    src/ui/widgets/rightbubble.ui

INCLUDEPATH += \
    $$PWD/src \
    $$PWD/src/common \
    $$PWD/src/ui \
    $$PWD/src/ui/widgets \
    $$PWD/src/ui/dialogs \
    $$PWD/src/ui/admin

TRANSLATIONS +=
CONFIG += lrelease
CONFIG += embed_translations

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    src/ui/resources/picture.qrc \
    src/ui/resources/resources.qrc

DISTFILES +=

