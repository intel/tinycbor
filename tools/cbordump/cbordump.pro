TEMPLATE = app
CONFIG += console
CONFIG -= app_bundle
CONFIG -= qt
DESTDIR = ../../bin

CBORDIR = $$PWD/../../src
INCLUDEPATH += $$CBORDIR
SOURCES += \
    cbordump.c \
    $$CBORDIR/cborerrorstrings.c \
    $$CBORDIR/cborparser.c \
    $$CBORDIR/cborpretty.c \
