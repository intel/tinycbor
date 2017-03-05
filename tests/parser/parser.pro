SOURCES += tst_parser.cpp ../../src/cborparser.c ../../src/cborparser_dup_string.c
HEADERS += alloc.h

CONFIG += testcase parallel_test c++11
QT = core testlib
DEFINES += CBOR_PARSER_MAX_RECURSIONS=16
DEFINES += CBOR_ALLOC_INCLUDE=\\\"alloc.h\\\"
DEFINES += CBOR_ALLOC=my_alloc

INCLUDEPATH += ../../src
msvc: POST_TARGETDEPS = ../../lib/tinycbor.lib
else: POST_TARGETDEPS += ../../lib/libtinycbor.a
LIBS += $$POST_TARGETDEPS
