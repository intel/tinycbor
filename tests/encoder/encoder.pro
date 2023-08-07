SOURCES += tst_encoder.cpp

CONFIG += testcase parallel_test c++11
QT = core testlib

DEFINES += QT_NO_FOREACH QT_NO_AS_CONST

INCLUDEPATH += ../../src
msvc: POST_TARGETDEPS = ../../lib/tinycbor.lib
else: POST_TARGETDEPS += ../../lib/libtinycbor.a
LIBS += $$POST_TARGETDEPS
