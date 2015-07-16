SOURCES += $$PWD/cborparser.c $$PWD/cborencoder.c $$PWD/cborerrorstrings.c $$PWD/cborpretty.c
QMAKE_CFLAGS *= $$QMAKE_CFLAGS_SPLIT_SECTIONS
QMAKE_LFLAGS *= $$QMAKE_LFLAGS_GCSECTIONS
INCLUDEPATH += $$PWD
