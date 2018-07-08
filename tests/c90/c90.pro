CONFIG += testcase parallel_test
CONFIG -= qt
gcc: QMAKE_CFLAGS += -std=c90 -pedantic-errors -Wall -Wextra -Werror

SOURCES += tst_c90.c
INCLUDEPATH += ../../src
