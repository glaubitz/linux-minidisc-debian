TEMPLATE=app
CONFIG  -= qt
CONFIG  += console link_pkgconfig link_prl
PKGCONFIG += glib-2.0 id3tag libusb-1.0
INCLUDEPATH += ../libnetmd
SOURCES += netmdcli.c

include(../libnetmd/use_libnetmd.prl)

unix:!macx {
	target.path = /usr/bin
	INSTALLS += target
}

mac:INCLUDEPATH += /opt/local/include
