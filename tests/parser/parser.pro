SOURCES += tst_parser.cpp

CONFIG += testcase parallel_test c++11
QT = core testlib
DEFINES += CBOR_PARSER_MAX_RECURSIONS=16

include(../../src/src.pri)
