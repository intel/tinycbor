/****************************************************************************
**
** Copyright (C) 2026 Intel Corporation
**
** Permission is hereby granted, free of charge, to any person obtaining a copy
** of this software and associated documentation files (the "Software"), to deal
** in the Software without restriction, including without limitation the rights
** to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
** copies of the Software, and to permit persons to whom the Software is
** furnished to do so, subject to the following conditions:
**
** The above copyright notice and this permission notice shall be included in
** all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
** OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
** THE SOFTWARE.
**
****************************************************************************/

#include <QtTest>
#include "cbor.h"
#include <limits>
#include <stdarg.h>
#include <stdio.h>

#ifndef CBOR_PARSER_MAX_RECURSIONS
#  define CBOR_PARSER_MAX_RECURSIONS 1024
#endif

#ifndef QCOMPARE_EQ
// added for Qt 6.4
#  define QCOMPARE_EQ QCOMPARE
#endif

Q_DECLARE_METATYPE(CborError)

namespace QTest {
template<> char *toString<CborError>(const CborError &err)
{
    return qstrdup(cbor_error_string(err));
}
}

template <size_t N> static QByteArray raw(const char (&data)[N])
{
    return QByteArray::fromRawData(data, N - 1);
}

static CborError qstring_printf(void *out, const char *fmt, ...)
{
    auto str = static_cast<QString *>(out);
    va_list va;
    va_start(va, fmt);
    *str += QString::vasprintf(fmt, va);
    va_end(va);
    return CborNoError;
};

static CborError parseOne(CborValue *it, QString *parsed, int flags)
{
    parsed->clear();
    return cbor_value_to_pretty_stream(qstring_printf, parsed, it, flags);
}

// Convenience combinations of CborPrettyFlags used throughout this file.
enum {
    FlagsNone = 0,
    FlagsFragments = CborPrettyShowStringFragments,
    FlagsIndeterminate = CborPrettyIndicateIndeterminateLength,
    FlagsOverlong = CborPrettyIndicateOverlongNumbers,
    FlagsFragmentsIndeterminate = FlagsFragments | FlagsIndeterminate,
    FlagsIndeterminateOverlong = FlagsIndeterminate | FlagsOverlong,
    FlagsAll = FlagsFragments | FlagsIndeterminate | FlagsOverlong
};

class tst_Pretty : public QObject
{
    Q_OBJECT
private slots:
    void validInput_data();
    void validInput();
    void incompleteValidInput_data() { validInput_data(); }
    void incompleteValidInput();
    void invalidInput_data();
    void invalidInput();
    void recursionLimit_data();
    void recursionLimit();
};

void tst_Pretty::validInput_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<int>("flags");
    QTest::addColumn<QString>("expected");

    // integers: flags are irrelevant here (no overlong encoding used)
    QTest::newRow("0") << raw("\x00") << int(FlagsAll) << "0";
    QTest::newRow("1") << raw("\x01") << int(FlagsAll) << "1";
    QTest::newRow("23") << raw("\x17") << int(FlagsAll) << "23";
    QTest::newRow("24") << raw("\x18\x18") << int(FlagsAll) << "24";
    QTest::newRow("UINT8_MAX") << raw("\x18\xff") << int(FlagsAll) << "255";
    QTest::newRow("UINT16_MAX") << raw("\x19\xff\xff") << int(FlagsAll) << "65535";
    QTest::newRow("UINT32_MAX") << raw("\x1a\xff\xff\xff\xff") << int(FlagsAll) << "4294967295";
    QTest::newRow("UINT64_MAX") << raw("\x1b" "\xff\xff\xff\xff" "\xff\xff\xff\xff") << int(FlagsAll)
                                 << QString::number(std::numeric_limits<uint64_t>::max());
    QTest::newRow("-1") << raw("\x20") << int(FlagsAll) << "-1";
    QTest::newRow("-24") << raw("\x37") << int(FlagsAll) << "-24";
    QTest::newRow("-25") << raw("\x38\x18") << int(FlagsAll) << "-25";
    QTest::newRow("INT64_MIN") << raw("\x3b\x7f\xff\xff\xff""\xff\xff\xff\xff") << int(FlagsAll)
                                << QString::number(std::numeric_limits<int64_t>::min());
    QTest::newRow("INT64_MIN-1") << raw("\x3b\x80\x00\x00\x00""\x00\x00\x00\x00") << int(FlagsAll)
                                  << "-9223372036854775809";
    QTest::newRow("-UINT64_MAX-1") << raw("\x3b\xff\xff\xff\xff""\xff\xff\xff\xff") << int(FlagsAll)
                                    << "-18446744073709551616";

    // overlong integers - tests CborPrettyIndicateOverlongNumbers
    QTest::newRow("0*1-all") << raw("\x18\x00") << int(FlagsAll) << "0_0";
    QTest::newRow("0*1-none") << raw("\x18\x00") << int(FlagsNone) << "0";
    QTest::newRow("0*2-all") << raw("\x19\x00\x00") << int(FlagsAll) << "0_1";
    QTest::newRow("0*2-none") << raw("\x19\x00\x00") << int(FlagsNone) << "0";
    QTest::newRow("0*4-all") << raw("\x1a\0\0\0\0") << int(FlagsAll) << "0_2";
    QTest::newRow("0*4-none") << raw("\x1a\0\0\0\0") << int(FlagsNone) << "0";
    QTest::newRow("0*8-all") << raw("\x1b\0\0\0\0\0\0\0\0") << int(FlagsAll) << "0_3";
    QTest::newRow("0*8-none") << raw("\x1b\0\0\0\0\0\0\0\0") << int(FlagsNone) << "0";
    QTest::newRow("-1*1-all") << raw("\x38\x00") << int(FlagsAll) << "-1_0";
    QTest::newRow("-1*1-none") << raw("\x38\x00") << int(FlagsNone) << "-1";
    QTest::newRow("-1*2-all") << raw("\x39\x00\x00") << int(FlagsAll) << "-1_1";
    QTest::newRow("-1*2-none") << raw("\x39\x00\x00") << int(FlagsNone) << "-1";

    // simple types and booleans: flags are irrelevant
    QTest::newRow("simple0") << raw("\xe0") << int(FlagsAll) << "simple(0)";
    QTest::newRow("simple255") << raw("\xf8\xff") << int(FlagsAll) << "simple(255)";
    QTest::newRow("false") << raw("\xf4") << int(FlagsAll) << "false";
    QTest::newRow("true") << raw("\xf5") << int(FlagsAll) << "true";
    QTest::newRow("null") << raw("\xf6") << int(FlagsAll) << "null";
    QTest::newRow("undefined") << raw("\xf7") << int(FlagsAll) << "undefined";

    // floating point: flags are irrelevant (no overlong encoding for floats)
    QTest::newRow("0.f16") << raw("\xf9\0\0") << int(FlagsAll) << "0.f16";
    QTest::newRow("0.f") << raw("\xfa\0\0\0\0") << int(FlagsAll) << "0.f";
    QTest::newRow("0.") << raw("\xfb\0\0\0\0\0\0\0\0") << int(FlagsAll) << "0.";
    QTest::newRow("-1.f16") << raw("\xf9\xbc\x00") << int(FlagsAll) << "-1.f16";
    QTest::newRow("nan") << raw("\xfb\x7f\xf8\0\0\0\0\0\0") << int(FlagsAll) << "nan";
    QTest::newRow("+inf") << raw("\xfb\x7f\xf0\0\0\0\0\0\0") << int(FlagsAll) << "inf";

    // non-chunked byte/text strings, without overlong length: flags are irrelevant.
    QTest::newRow("emptybytestring") << raw("\x40") << int(FlagsAll) << "h''";
    QTest::newRow("bytestring5") << raw("\x45Hello") << int(FlagsAll) << "h'48656c6c6f'";
    QTest::newRow("emptytextstring") << raw("\x60") << int(FlagsAll) << "\"\"";
    QTest::newRow("textstring5") << raw("\x65Hello") << int(FlagsAll) << "\"Hello\"";
    QTest::newRow("textstring1-nul") << raw("\x61\0") << int(FlagsAll) << "\"\\u0000\"";

    // overlong string lengths
    QTest::newRow("emptybytestring*1-all") << raw("\x58\x00") << int(FlagsAll) << "h''_0";
    QTest::newRow("emptybytestring*1-none") << raw("\x58\x00") << int(FlagsNone) << "h''";
    QTest::newRow("emptytextstring*1-all") << raw("\x78\x00") << int(FlagsAll) << "\"\"_0";
    QTest::newRow("emptytextstring*1-none") << raw("\x78\x00") << int(FlagsNone) << "\"\"";
    QTest::newRow("bytestring5*2-all") << raw("\x59\0\5Hello") << int(FlagsAll) << "h'48656c6c6f'_1";
    QTest::newRow("bytestring5*2-none") << raw("\x59\0\5Hello") << int(FlagsNone) << "h'48656c6c6f'";

    // tags: no indeterminate length involved, but overlong tags are present
    QTest::newRow("tag0") << raw("\xc0\x00") << int(FlagsAll) << "0(0)";
    QTest::newRow("tag1") << raw("\xc1\x00") << int(FlagsAll) << "1(0)";
    QTest::newRow("tag256") << raw("\xd9\1\0\x00") << int(FlagsAll) << "256(0)";
    QTest::newRow("tag0*1-none") << raw("\xd8\0\x00") << int(FlagsNone) << "0(0)";
    QTest::newRow("tag0*1-all") << raw("\xd8\0\x00") << int(FlagsAll) << "0_0(0)";
    QTest::newRow("tag0*2-all") << raw("\xd9\0\0\x00") << int(FlagsAll) << "0_1(0)";
    QTest::newRow("tag0*3-all") << raw("\xda\0\0\0\0\x00") << int(FlagsAll) << "0_2(0)";
    QTest::newRow("tag0*4-all") << raw("\xdb\0\0\0\0\0\0\0\0\x00") << int(FlagsAll) << "0_3(0)";
    QTest::newRow("unixtime") << raw("\xc1\x1a\x55\x4b\xbf\xd3") << int(FlagsAll) << "1(1431027667)";

    // definite-length arrays and maps, with determinate length: flags are
    // irrelevant
    QTest::newRow("emptyarray") << raw("\x80") << int(FlagsAll) << "[]";
    QTest::newRow("emptymap") << raw("\xa0") << int(FlagsAll) << "{}";
    QTest::newRow("array123") << raw("\x83\1\2\3") << int(FlagsAll) << "[1, 2, 3]";
    QTest::newRow("map-0-24") << raw("\xa1\0\x18\x18") << int(FlagsAll) << "{0: 24}";
    QTest::newRow("nestedarray") << raw("\x81\x81\1") << int(FlagsAll) << "[[1]]";

    // overlong array/map lengths: only safe together with the overlong
    // flag; the indeterminate flag does not matter since these are
    // definite-length containers.
    QTest::newRow("array1*1-all") << raw("\x98\1\1") << int(FlagsAll) << "[_0 1]";
    QTest::newRow("array1*1-none") << raw("\x98\1\1") << int(FlagsNone) << "[1]";
    QTest::newRow("array1*1-overlongonly") << raw("\x98\1\1") << int(FlagsOverlong) << "[_0 1]";
    QTest::newRow("map-0*1-24-all") << raw("\xa1\x18\0\x18\x18") << int(FlagsAll) << "{0_0: 24}";
    QTest::newRow("map-0*1-24-none") << raw("\xa1\x18\0\x18\x18") << int(FlagsNone) << "{0: 24}";

    // indeterminate-length (chunked) strings
    QTest::newRow("_emptybytestring-frag-indet") << raw("\x5f\xff") << int(FlagsFragmentsIndeterminate) << "(_ )";
    QTest::newRow("_emptybytestring-indet") << raw("\x5f\xff") << int(FlagsIndeterminate) << "h''_";
    QTest::newRow("_emptybytestring-none") << raw("\x5f\xff") << int(FlagsNone) << "h''";
    QTest::newRow("_emptybytestring-overlong-only") << raw("\x5f\xff") << int(FlagsOverlong) << "h''";
    QTest::newRow("_emptytextstring-overlong-only") << raw("\x7f\xff") << int(FlagsOverlong) << "\"\"";
    QTest::newRow("_bytestring2-frag-indet") << raw("\x5f\x41 \x41z\xff") << int(FlagsFragmentsIndeterminate)
                                              << "(_ h'20', h'7a')";
    QTest::newRow("_bytestring2-indet") << raw("\x5f\x41 \x41z\xff") << int(FlagsIndeterminate) << "h'207a'";
    QTest::newRow("_bytestring2-none") << raw("\x5f\x41 \x41z\xff") << int(FlagsNone) << "h'207a'";
    QTest::newRow("_textstring2-frag-indet") << raw("\x7f\x61 \x61z\xff") << int(FlagsFragmentsIndeterminate)
                                              << "(_ \" \", \"z\")";
    QTest::newRow("_textstring2-indet") << raw("\x7f\x61 \x61z\xff") << int(FlagsIndeterminate) << "\" z\"";
    QTest::newRow("_textstring2-none") << raw("\x7f\x61 \x61z\xff") << int(FlagsNone) << "\" z\"";

    // chunks with overlong lengths
    QTest::newRow("_bytestring5x2-overlong-indet-frag")
            << raw("\x5f\x5b\0\0\0\0\0\0\0\3Hel\x5a\0\0\0\2lo\xff") << int(FlagsAll)
            << "(_ h'48656c'_3, h'6c6f'_2)";
    QTest::newRow("_bytestring5x2-overlong-indet")
            << raw("\x5f\x5b\0\0\0\0\0\0\0\3Hel\x5a\0\0\0\2lo\xff") << int(FlagsIndeterminateOverlong)
            << "h'48656c6c6f'_3";
    QTest::newRow("_bytestring5x2-overlong-only")
            << raw("\x5f\x5b\0\0\0\0\0\0\0\3Hel\x5a\0\0\0\2lo\xff") << int(FlagsOverlong)
            << "h'48656c6c6f'_3";

    // indeterminate-length arrays and maps
    QTest::newRow("_emptyarray-indet") << raw("\x9f\xff") << int(FlagsIndeterminate) << "[_ ]";
    QTest::newRow("_emptyarray-none") << raw("\x9f\xff") << int(FlagsNone) << "[]";
    QTest::newRow("_emptymap-indet") << raw("\xbf\xff") << int(FlagsIndeterminate) << "{_ }";
    QTest::newRow("_emptymap-none") << raw("\xbf\xff") << int(FlagsNone) << "{}";
    QTest::newRow("_array1-indet") << raw("\x9f\1\xff") << int(FlagsIndeterminate) << "[_ 1]";
    QTest::newRow("_array1-none") << raw("\x9f\1\xff") << int(FlagsNone) << "[1]";
    QTest::newRow("_array1-indet-overlong") << raw("\x9f\1\xff") << int(FlagsIndeterminateOverlong) << "[_ 1]";
    QTest::newRow("_map-0-24-indet") << raw("\xbf\0\x18\x18\xff") << int(FlagsIndeterminate) << "{_ 0: 24}";
    QTest::newRow("_map-0-24-none") << raw("\xbf\0\x18\x18\xff") << int(FlagsNone) << "{0: 24}";
    QTest::newRow("_nestedarray-indet") << raw("\x9f\x9f\1\xff\xff") << int(FlagsIndeterminate) << "[_ [_ 1]]";
    QTest::newRow("_arrayofmap-indet") << raw("\x9f\xa1\1\2\xff") << int(FlagsIndeterminate) << "[_ {1: 2}]";
    QTest::newRow("_emptyarray-overlong-only") << raw("\x9f\xff") << int(FlagsOverlong) << "[]";
    QTest::newRow("_emptymap-overlong-only") << raw("\xbf\xff") << int(FlagsOverlong) << "{}";
}

void tst_Pretty::validInput()
{
    QFETCH(QByteArray, data);
    QFETCH(int, flags);
    QFETCH(QString, expected);

    // make the first element definitely valid so cbor_parse_init() succeeds;
    // we want to test cbor_value_to_pretty_stream()
    data.prepend(0x81);   // 1-element array

    for (int len = 1; len < data.length() - 1; ++len) {
        CborParser parser;
        CborValue value;
        CborError err = cbor_parser_init(reinterpret_cast<const uint8_t *>(data.constData()), len, 0,
                                         &parser, &value);
        QVERIFY2(!err, cbor_error_string(err));

        QString decoded;
        err = parseOne(&value, &decoded, flags);
        if (err != CborErrorUnexpectedEOF)
            qDebug() << "Length is" << len;
        QCOMPARE(err, CborErrorUnexpectedEOF);
    }
}

void tst_Pretty::incompleteValidInput()
{
    QFETCH(QByteArray, data);
    QFETCH(int, flags);
    QFETCH(QString, expected);

    CborParser parser;
    CborValue value;
    CborError err = cbor_parser_init(reinterpret_cast<const uint8_t *>(data.constData()), data.size(), 0,
                                      &parser, &value);
    QVERIFY2(!err, cbor_error_string(err));

    QString decoded;
    err = parseOne(&value, &decoded, flags);
    QVERIFY2(!err, QByteArray("Got error \"") + cbor_error_string(err) + "\"; decoded stream:\n" +
                    decoded.toLatin1());
    QCOMPARE(decoded, expected);

}

void tst_Pretty::invalidInput_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<int>("flags");
    QTest::addColumn<CborError>("expectedError");

    auto addRow = [](const char *name, const QByteArray &data, CborError expectedError) {
        QTest::addRow("%s:none", name) << data << int(FlagsNone) << expectedError;
        QTest::addRow("%s:fragments", name) << data << int(FlagsFragments) << expectedError;
        QTest::addRow("%s:indeterminate", name) << data << int(FlagsIndeterminate) << expectedError;
        QTest::addRow("%s:overlong", name) << data << int(FlagsOverlong) << expectedError;
        QTest::addRow("%s:fragments+indeterminate", name) << data << int(FlagsFragmentsIndeterminate) << expectedError;
        QTest::addRow("%s:indeterminate+overlong", name) << data << int(FlagsIndeterminateOverlong) << expectedError;
        QTest::addRow("%s:all", name) << data << int(FlagsAll) << expectedError;
    };

    // illegal numbers are future extension points
    addRow("illegal-number-in-unsigned-1", raw("\x1c"), CborErrorIllegalNumber);
    addRow("illegal-number-in-unsigned-2", raw("\x1d"), CborErrorIllegalNumber);
    addRow("illegal-number-in-unsigned-3", raw("\x1e"), CborErrorIllegalNumber);
    addRow("illegal-number-in-unsigned-4", raw("\x1f"), CborErrorIllegalNumber);
    addRow("illegal-number-in-negative-1", raw("\x3c"), CborErrorIllegalNumber);
    addRow("illegal-number-in-negative-2", raw("\x3d"), CborErrorIllegalNumber);
    addRow("illegal-number-in-negative-3", raw("\x3e"), CborErrorIllegalNumber);
    addRow("illegal-number-in-negative-4", raw("\x3f"), CborErrorIllegalNumber);
    addRow("illegal-number-in-bytearray-length-1", raw("\x5c"), CborErrorIllegalNumber);
    addRow("illegal-number-in-bytearray-length-2", raw("\x5d"), CborErrorIllegalNumber);
    addRow("illegal-number-in-bytearray-length-3", raw("\x5e"), CborErrorIllegalNumber);
    addRow("illegal-number-in-string-length-1", raw("\x7c"), CborErrorIllegalNumber);
    addRow("illegal-number-in-string-length-2", raw("\x7d"), CborErrorIllegalNumber);
    addRow("illegal-number-in-string-length-3", raw("\x7e"), CborErrorIllegalNumber);
    addRow("illegal-number-in-array-length-1", raw("\x9c"), CborErrorIllegalNumber);
    addRow("illegal-number-in-array-length-2", raw("\x9d"), CborErrorIllegalNumber);
    addRow("illegal-number-in-array-length-3", raw("\x9e"), CborErrorIllegalNumber);
    addRow("illegal-number-in-map-length-1", raw("\xbc"), CborErrorIllegalNumber);
    addRow("illegal-number-in-map-length-2", raw("\xbd"), CborErrorIllegalNumber);
    addRow("illegal-number-in-map-length-3", raw("\xbe"), CborErrorIllegalNumber);
    addRow("illegal-number-in-tag-1", raw("\xdc"), CborErrorIllegalNumber);
    addRow("illegal-number-in-tag-2", raw("\xdd"), CborErrorIllegalNumber);
    addRow("illegal-number-in-tag-3", raw("\xde"), CborErrorIllegalNumber);
    addRow("illegal-number-in-tag-4", raw("\xdf"), CborErrorIllegalNumber);
    // major type 7 has future types
    addRow("future-type-28", raw("\xfc"), CborErrorUnknownType);
    addRow("future-type-29", raw("\xfd"), CborErrorUnknownType);
    addRow("future-type-30", raw("\xfe"), CborErrorUnknownType);
    addRow("unexpected-break", raw("\xff"), CborErrorUnexpectedBreak);
    // simple types 0-31 must be encoded in a single byte
    addRow("illegal-simple-0", raw("\xf8\0"), CborErrorIllegalSimpleType);
    addRow("illegal-simple-31", raw("\xf8\x1f"), CborErrorIllegalSimpleType);

    // Break in bad positions
    addRow("array-element-is-break", raw("\x81\xff"), CborErrorUnexpectedBreak);
    addRow("map-key-is-break", raw("\xa1\xff\0"), CborErrorUnexpectedBreak);
    addRow("map-value-is-break", raw("\xa1\0\xff"), CborErrorUnexpectedBreak);
    addRow("map-break-after-key", raw("\xbf\0\xff"), CborErrorUnexpectedBreak);
    addRow("map-break-after-second-key", raw("\xbf\x64xyzw\x04\x00\xff"), CborErrorUnexpectedBreak);
    addRow("map-break-after-value-tag", raw("\xbf\0\xc0\xff"), CborErrorUnexpectedBreak);
    addRow("map-break-after-value-tag2", raw("\xbf\0\xd8\x20\xff"), CborErrorUnexpectedBreak);
    addRow("tagged-break", raw("\xc0\xff"), CborErrorUnexpectedBreak);

    // check for pointer additions wrapping over the limit of the address space
    auto wraparoundError = [](uint64_t encodedSize) {
        return encodedSize > SIZE_MAX ? CborErrorDataTooLarge : CborErrorUnexpectedEOF;
    };
    constexpr uint64_t FourGB = UINT32_MAX + UINT64_C(1);
    // on 32-bit systems, this is a -1
    addRow("bytearray-wraparound1", raw("\x5a\xff\xff\xff\xff"), wraparoundError(UINT32_MAX));
    addRow("string-wraparound1", raw("\x7a\xff\xff\xff\xff"), wraparoundError(UINT32_MAX));
    // on 32-bit systems, a 4GB addition could be dropped
    addRow("bytearray-wraparound2", raw("\x5b\0\0\0\1\0\0\0\0"), wraparoundError(FourGB));
    addRow("string-wraparound2", raw("\x7b\0\0\0\1\0\0\0\0"), wraparoundError(FourGB));
    // on 64-bit systems, this could be a -1
    addRow("bytearray-wraparound3", raw("\x5b\xff\xff\xff\xff\xff\xff\xff\xff"),
           wraparoundError(UINT64_MAX));
    addRow("string-wraparound3", raw("\x7b\xff\xff\xff\xff\xff\xff\xff\xff"),
           wraparoundError(UINT64_MAX));

    // ditto on chunks
    addRow("bytearray-chunk-wraparound1", raw("\x5f\x5a\xff\xff\xff\xff"), wraparoundError(UINT32_MAX));
    addRow("string-chunk-wraparound1", raw("\x7f\x7a\xff\xff\xff\xff"), wraparoundError(UINT32_MAX));
    // on 32-bit systems, a 4GB addition could be dropped
    addRow("bytearray-chunk-wraparound2", raw("\x5f\x5b\0\0\0\1\0\0\0\0"), wraparoundError(FourGB));
    addRow("string-chunk-wraparound2", raw("\x7f\x7b\0\0\0\1\0\0\0\0"), wraparoundError(FourGB));
    // on 64-bit systems, this could be a -1
    addRow("bytearray-chunk-wraparound3", raw("\x5f\x5b\xff\xff\xff\xff\xff\xff\xff\xff"),
           wraparoundError(UINT64_MAX));
    addRow("string-chunk-wraparound3", raw("\x7f\x7b\xff\xff\xff\xff\xff\xff\xff\xff"),
           wraparoundError(UINT64_MAX));

    // bad types inside chunked strings
    addRow("string-chunk-unsigned", raw("\x7f\0\xff"), CborErrorIllegalType);
    addRow("string-chunk-negative", raw("\x7f\x20\xff"), CborErrorIllegalType);
    addRow("string-chunk-bytearray", raw("\x7f\x40\xff"), CborErrorIllegalType);
    addRow("string-chunk-array", raw("\x7f\x80\xff"), CborErrorIllegalType);
    addRow("string-chunk-map", raw("\x7f\xa0\xff"), CborErrorIllegalType);
    addRow("string-chunk-tag", raw("\x7f\xc0\xff"), CborErrorIllegalType);
    addRow("string-chunk-tagged-string", raw("\x7f\xc0\x60\xff"), CborErrorIllegalType);
    addRow("string-chunk-simple0", raw("\x7f\xe0\xff"), CborErrorIllegalType);
    addRow("string-chunk-false", raw("\x7f\xf4\xff"), CborErrorIllegalType);
    addRow("string-chunk-true", raw("\x7f\xf5\xff"), CborErrorIllegalType);
    addRow("string-chunk-null", raw("\x7f\xf6\xff"), CborErrorIllegalType);
    addRow("string-chunk-undefined", raw("\x7f\xf7\xff"), CborErrorIllegalType);
    addRow("bytearray-chunk-string", raw("\x5f\x60\xff"), CborErrorIllegalType);
    addRow("bytearray-chunk-tagged-bytearray", raw("\x7f\xc0\x40\xff"), CborErrorIllegalType);

    // Bad UTF-8
    addRow("invalid-utf8-bad-continuation-1char", raw("\x61\x80"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-bad-continuation-2chars-1", raw("\x62\xc2\xc0"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-bad-continuation-2chars-2", raw("\x62\xc3\xdf"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-bad-continuation-2chars-3", raw("\x62\xc7\xf0"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-bad-continuation-3chars-1", raw("\x63\xe0\xa0\xc0"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-bad-continuation-3chars-2", raw("\x63\xe0\xc0\xa0"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-bad-continuation-4chars-1", raw("\x64\xf0\x90\x80\xc0"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-bad-continuation-4chars-2", raw("\x64\xf0\x90\xc0\x80"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-bad-continuation-4chars-3", raw("\x64\xf0\xc0\x80\x80"), CborErrorInvalidUtf8TextString);
    // Too short UTF-8 sequences (in an array so there's a byte after that would make it valid UTF-8 if it were part of the string)
    addRow("invalid-utf8-too-short-2chars", raw("\x82\x61\xc2\x80"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-too-short-3chars-1", raw("\x82\x61\xe0\x80"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-too-short-3chars-2", raw("\x82\x62\xe0\xa0\x80"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-too-short-4chars-1", raw("\x82\x61\xf0\x80"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-too-short-4chars-2", raw("\x82\x62\xf0\x90\x80"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-too-short-4chars-3", raw("\x82\x63\xf0\x90\x80\x80"), CborErrorInvalidUtf8TextString);
    // UTF-16 surrogages encoded in UTF-8
    addRow("invalid-utf8-hi-surrogate", raw("\x63\xed\xa0\x80"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-lo-surrogate", raw("\x63\xed\xb0\x80"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-surrogate-pair", raw("\x66\xed\xa0\x80\xed\xb0\x80"), CborErrorInvalidUtf8TextString);
    // Non-Unicode UTF-8 sequences
    addRow("invalid-utf8-non-unicode-1", raw("\x64\xf4\x90\x80\x80"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-non-unicode-2", raw("\x65\xf8\x88\x80\x80\x80"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-non-unicode-3", raw("\x66\xfc\x84\x80\x80\x80\x80"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-non-unicode-4", raw("\x66\xfd\xbf\xbf\xbf\xbf\xbf"), CborErrorInvalidUtf8TextString);
    // invalid bytes in UTF-8
    addRow("invalid-utf8-fe", raw("\x61\xfe"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-ff", raw("\x61\xff"), CborErrorInvalidUtf8TextString);
    // Overlong sequences
    addRow("invalid-utf8-overlong-1-2", raw("\x62\xc1\x81"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-overlong-1-3", raw("\x63\xe0\x81\x81"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-overlong-1-4", raw("\x64\xf0\x80\x81\x81"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-overlong-1-5", raw("\x65\xf8\x80\x80\x81\x81"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-overlong-1-6", raw("\x66\xfc\x80\x80\x80\x81\x81"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-overlong-2-3", raw("\x63\xe0\x82\x80"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-overlong-2-4", raw("\x64\xf0\x80\x82\x80"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-overlong-2-5", raw("\x65\xf8\x80\x80\x82\x80"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-overlong-2-6", raw("\x66\xfc\x80\x80\x80\x82\x80"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-overlong-3-4", raw("\x64\xf0\x80\xa0\x80"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-overlong-3-5", raw("\x65\xf8\x80\x80\xa0\x80"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-overlong-3-6", raw("\x66\xfc\x80\x80\x80\xa0\x80"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-overlong-4-5", raw("\x65\xf8\x80\x84\x80\x80"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-overlong-4-6", raw("\x66\xfc\x80\x80\x84\x80\x80"), CborErrorInvalidUtf8TextString);
    // valid UTF-8 but split into CBOR chunks
    addRow("invalid-utf8-split-u0080", raw("\x7f\x61\xc2\x61\x80\xff"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-split-u00ff", raw("\x7f\x61\xc3\x61\xbf\xff"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-split-u0100", raw("\x7f\x61\xc4\x61\x80\xff"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-split-u07ff", raw("\x7f\x61\xdf\x61\xbf\xff"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-split-u0800", raw("\x7f\x61\xe0\x62\xa0\x80\xff"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-split-u1000", raw("\x7f\x62\xe1\x80\x61\x80\xff"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-split-uffff", raw("\x7f\x61\xef\x62\xbf\xbf\xff"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-split-U00010000", raw("\x7f\x61\xf0\x63\x90\x80\x80\xff"), CborErrorInvalidUtf8TextString);
    addRow("invalid-utf8-split-U0010ffff", raw("\x7f\x62\xf4\x8f\x62\xbf\xbf\xff"), CborErrorInvalidUtf8TextString);
}

void tst_Pretty::invalidInput()
{
    QFETCH(QByteArray, data);
    QFETCH(int, flags);
    QFETCH(CborError, expectedError);

    // make the first element definitely valid so cbor_parse_init() succeeds;
    // we want to test cbor_value_to_pretty_stream()
    data.prepend(0x81);   // 1-element array

    CborParser parser;
    CborValue value;
    CborError err = cbor_parser_init(reinterpret_cast<const uint8_t *>(data.constData()), data.size(), 0,
                                      &parser, &value);
    QVERIFY2(!err, cbor_error_string(err));

    QString decoded;
    err = parseOne(&value, &decoded, flags);
    QCOMPARE(err, expectedError);
}

void tst_Pretty::recursionLimit_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<int>("flags");

    static const int recursions = CBOR_PARSER_MAX_RECURSIONS + 2;

    QTest::newRow("array") << QByteArray(recursions, '\x81') + '\x20' << int(FlagsAll);
    QTest::newRow("_array") << QByteArray(recursions, '\x9f') + '\x20' + QByteArray(recursions, '\xff')
                             << int(FlagsAll);

    QByteArray data;
    for (int i = 0; i < recursions; ++i)
        data += "\xa1\x65Hello";
    data += '\2';
    QTest::newRow("map") << data << int(FlagsAll);

    data.clear();
    for (int i = 0; i < recursions; ++i)
        data += "\xbf\x65World";
    data += '\2';
    for (int i = 0; i < recursions; ++i)
        data += "\xff";
    QTest::newRow("_map") << data << int(FlagsAll);

    QTest::newRow("tag") << QByteArray(recursions, '\xc1') + '\x20' << int(FlagsAll);
}

void tst_Pretty::recursionLimit()
{
    QFETCH(QByteArray, data);
    QFETCH(int, flags);

    CborParser parser;
    CborValue value;
    CborError err = cbor_parser_init(reinterpret_cast<const uint8_t *>(data.constData()), data.size(), 0,
                                      &parser, &value);
    QVERIFY2(!err, cbor_error_string(err));

    QString decoded;
    err = parseOne(&value, &decoded, flags);
    QVERIFY2(!err, QByteArray("Got error \"") + cbor_error_string(err) + "\"; decoded stream:\n" +
                    decoded.toLatin1());
    QVERIFY2(decoded.contains(QLatin1String("<nesting too deep, recursion stopped>")),
             qPrintable(decoded));
}

QTEST_MAIN(tst_Pretty)
#include "tst_pretty.moc"
