/****************************************************************************
**
** Copyright (C) 2015 Intel Corporation
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

Q_DECLARE_METATYPE(CborError)

class tst_Parser : public QObject
{
    Q_OBJECT
private slots:
    void initParserEmpty();

    // parsing API
    void fixed_data();
    void fixed();
    void strings_data();
    void strings() { fixed(); }
    void tags_data();
    void tags() { fixed(); }
    void tagTags_data() { tags_data(); }
    void tagTags();
    void emptyContainers_data();
    void emptyContainers() { fixed(); }
    void arrays_data();
    void arrays();
    void undefLengthArrays_data() { arrays_data(); }
    void undefLengthArrays();
    void nestedArrays_data() { arrays_data(); }
    void nestedArrays();
    void maps_data();
    void maps();
    void undefLengthMaps_data() { maps_data(); }
    void undefLengthMaps();
    void nestedMaps_data() { maps_data(); }
    void nestedMaps();
    void mapMixed_data();
    void mapMixed();
    void mapsAndArrays_data() { arrays_data(); }
    void mapsAndArrays();

    // convenience API
    void stringLength_data();
    void stringLength();
    void stringCompare_data();
    void stringCompare();
};

char toHexUpper(unsigned n)
{
    return n > 10 ? n + 'A' : n + '0';
}

QString escaped(const QString &raw)
{
    QString result;
    result.reserve(raw.size() + raw.size() / 3);

    auto begin = reinterpret_cast<const ushort *>(raw.constData());
    auto end = begin + raw.length();
    for (const ushort *p = begin; p != end; ++p) {
        if (*p < 0x7f && *p >= 0x20 && *p != '\\' && *p != '"') {
            result += *p;
            continue;
        }

        // print as an escape sequence
        result += '\\';

        switch (*p) {
        case '"':
        case '\\':
            result += *p;
            break;
        case '\b':
            result += 'b';
            break;
        case '\f':
            result += 'f';
            break;
        case '\n':
            result += 'n';
            break;
        case '\r':
            result += 'r';
            break;
        case '\t':
            result += 't';
            break;
        default:
            result += 'u';
            result += toHexUpper(ushort(*p) >> 12);
            result += toHexUpper(ushort(*p) >> 8);
            result += toHexUpper(*p >> 4);
            result += toHexUpper(*p);
        }
    }

    return result;
}

CborError parseContainer(CborValue *it, QString *parsed, CborType containerType);
CborError parseOne(CborValue *it, QString *parsed)
{
    CborError err;
    CborType type = cbor_value_get_type(it);
    switch (type) {
    case CborArrayType:
    case CborMapType: {
        // recursive type
        CborValue recursed;
        Q_ASSERT(cbor_value_is_container(it));

        *parsed += type == CborArrayType ? '[' : '{';
        if (!cbor_value_is_length_known(it))
            *parsed += "_ ";

        err = cbor_value_enter_container(it, &recursed);
        if (err)
            return err;       // parse error
        err = parseContainer(&recursed, parsed, type);
        if (err)
            return err;       // parse error
        err = cbor_value_leave_container(it, &recursed);
        if (err)
            return err;       // parse error

        *parsed += type == CborArrayType ? ']' : '}';
        return CborNoError;
    }

    case CborIntegerType:
        if (cbor_value_is_unsigned_integer(it)) {
            uint64_t val;
            cbor_value_get_uint64(it, &val);
            *parsed += QString::number(val);
        } else {
            int64_t val;
            cbor_value_get_int64(it, &val);     // can't fail
            if (val < 0) {
                *parsed += QString::number(val);
            } else {
                // 65-bit negative
                *parsed += '-';
                *parsed += QString::number(uint64_t(-val) - 1);
            }
        }
        break;

    case CborByteStringType:
    case CborTextStringType: {
        size_t n;
        err = cbor_value_calculate_string_length(it, &n);
        if (err)
            return err;

        QByteArray data(n, Qt::Uninitialized);
        err = cbor_value_copy_string(it, data.data(), &n, it);
        if (err)
            return err;     // parse error

        if (type == CborByteStringType) {
            *parsed += "h'";
            *parsed += data.toHex();
            *parsed += "'";
        } else {
            *parsed += '"';
            *parsed += escaped(QString::fromUtf8(data, data.length()));
            *parsed += '"';
        }
        return CborNoError;
    }

    case CborTagType: {
        CborTag tag;
        cbor_value_get_tag(it, &tag);       // can't fail
        *parsed += QString::number(tag);
        *parsed += '(';
        err = cbor_value_advance_fixed(it);
        if (err)
            return err;
        err = parseOne(it, parsed);
        if (err)
            return err;
        *parsed += ')';
        return CborNoError;
    }

    case CborSimpleType: {
        uint8_t type;
        cbor_value_get_simple_type(it, &type);  // can't fail
        *parsed += QString("simple(%0)").arg(type);
        break;
    }

    case CborNullType:
        *parsed += "null";
        break;

    case CborUndefinedType:
        *parsed += "undefined";
        break;

    case CborBooleanType: {
        bool val;
        cbor_value_get_boolean(it, &val);       // can't fail
        *parsed += val ? "true" : "false";
        break;
    }

    case CborDoubleType: {
        double val;
        if (false) {
    case CborFloatType:
                float f;
                cbor_value_get_float(it, &f);
                val = f;
        } else {
            cbor_value_get_double(it, &val);
        }
        QString number = QString::number(val, 'g', std::numeric_limits<double>::max_digits10 + 2);
        *parsed += number;
        if (number != "inf" && number != "-inf" && number != "nan") {
            if (!number.contains('.'))
                *parsed += '.';
            if (type == CborFloatType)
                *parsed += 'f';
        }
        break;
    }
    case CborHalfFloatType: {
        uint16_t val;
        cbor_value_get_half_float(it, &val);
        *parsed += QString("__f16(0x%0)").arg(val, 4, 16, QLatin1Char('0'));
        break;
    }

    case CborInvalidType:
        *parsed += "invalid";
        return CborErrorUnknownType;
    }

    err = cbor_value_advance_fixed(it);
    if (err)
        return err;
    return CborNoError;
}

CborError parseContainer(CborValue *it, QString *parsed, CborType containerType)
{
    const char *comma = nullptr;
    while (!cbor_value_at_end(it)) {
        *parsed += comma;
        comma = ", ";

        CborError err = parseOne(it, parsed);
        if (err)
            return err;

        if (containerType == CborArrayType)
            continue;

        // map: that was the key, so get the value
        *parsed += ": ";
        err = parseOne(it, parsed);
        if (err)
            return err;
    }
    return CborNoError;
}

template <size_t N> QByteArray raw(const char (&data)[N])
{
    return QByteArray::fromRawData(data, N - 1);
}

void tst_Parser::initParserEmpty()
{
    CborParser parser;
    CborValue first;
    CborError err = cbor_parser_init("", 0, 0, &parser, &first);
    QCOMPARE(err, CborErrorUnexpectedEOF);
}

void addColumns()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<QString>("expected");
}

bool compareFailed = true;
void compareOne_real(const QByteArray &data, const QString &expected, int line)
{
    compareFailed = true;
    CborParser parser;
    CborValue first;
    CborError err = cbor_parser_init(data.constData(), data.length(), 0, &parser, &first);
    QVERIFY2(!err, QByteArray::number(line) + ": Got error \"" + cbor_error_string(err) + "\"");

    QString decoded;
    err = parseOne(&first, &decoded);
    QVERIFY2(!err, QByteArray::number(line) + ": Got error \"" + cbor_error_string(err) +
                   "\"; decoded stream:\n" + decoded.toLatin1());
    QCOMPARE(decoded, expected);

    // check that we consumed everything
    QCOMPARE((void*)first.ptr, (void*)data.constEnd());

    compareFailed = false;
}
#define compareOne(data, expected) compareOne_real(data, expected, __LINE__)

void addFixedData()
{
    // unsigned integers
    QTest::newRow("0") << raw("\x00") << "0";
    QTest::newRow("1") << raw("\x01") << "1";
    QTest::newRow("10") << raw("\x0a") << "10";
    QTest::newRow("23") << raw("\x17") << "23";
    QTest::newRow("24") << raw("\x18\x18") << "24";
    QTest::newRow("UINT8_MAX") << raw("\x18\xff") << "255";
    QTest::newRow("UINT8_MAX+1") << raw("\x19\x01\x00") << "256";
    QTest::newRow("UINT16_MAX") << raw("\x19\xff\xff") << "65535";
    QTest::newRow("UINT16_MAX+1") << raw("\x1a\0\1\x00\x00") << "65536";
    QTest::newRow("UINT32_MAX") << raw("\x1a\xff\xff\xff\xff") << "4294967295";
    QTest::newRow("UINT32_MAX+1") << raw("\x1b\0\0\0\1\0\0\0\0") << "4294967296";
    QTest::newRow("UINT64_MAX") << raw("\x1b" "\xff\xff\xff\xff" "\xff\xff\xff\xff")
                                << QString::number(std::numeric_limits<uint64_t>::max());

    // negative integers
    QTest::newRow("-1") << raw("\x20") << "-1";
    QTest::newRow("-2") << raw("\x21") << "-2";
    QTest::newRow("-24") << raw("\x37") << "-24";
    QTest::newRow("-25") << raw("\x38\x18") << "-25";
    QTest::newRow("-UINT8_MAX") << raw("\x38\xff") << "-256";
    QTest::newRow("-UINT8_MAX-1") << raw("\x39\x01\x00") << "-257";
    QTest::newRow("-UINT16_MAX") << raw("\x39\xff\xff") << "-65536";
    QTest::newRow("-UINT16_MAX-1") << raw("\x3a\0\1\x00\x00") << "-65537";
    QTest::newRow("-UINT32_MAX") << raw("\x3a\xff\xff\xff\xff") << "-4294967296";
    QTest::newRow("-UINT32_MAX-1") << raw("\x3b\0\0\0\1\0\0\0\0") << "-4294967297";
    QTest::newRow("-UINT64_MAX") << raw("\x3b" "\xff\xff\xff\xff" "\xff\xff\xff\xff")
                                 << '-' + QString::number(std::numeric_limits<uint64_t>::max());

    // overlongs
    QTest::newRow("0*1") << raw("\x18\x00") << "0";
    QTest::newRow("0*2") << raw("\x19\x00\x00") << "0";
    QTest::newRow("0*4") << raw("\x1a\0\0\0\0") << "0";
    QTest::newRow("0*8") << raw("\x1b\0\0\0\0\0\0\0\0") << "0";
    QTest::newRow("-1*1") << raw("\x38\x00") << "-1";
    QTest::newRow("-1*2") << raw("\x39\x00\x00") << "-1";
    QTest::newRow("-1*4") << raw("\x3a\0\0\0\0") << "-1";
    QTest::newRow("-1*8") << raw("\x3b\0\0\0\0\0\0\0\0") << "-1";

    QTest::newRow("simple0") << raw("\xe0") << "simple(0)";
    QTest::newRow("simple19") << raw("\xf3") << "simple(19)";
    QTest::newRow("false") << raw("\xf4") << "false";
    QTest::newRow("true") << raw("\xf5") << "true";
    QTest::newRow("null") << raw("\xf6") << "null";
    QTest::newRow("undefined") << raw("\xf7") << "undefined";
    QTest::newRow("simple32") << raw("\xf8\x20") << "simple(32)";
    QTest::newRow("simple255") << raw("\xf8\xff") << "simple(255)";

    // floating point
    QTest::newRow("0f16") << raw("\xf9\0\0") << "__f16(0x0000)";

    QTest::newRow("0.f") << raw("\xfa\0\0\0\0") << "0.f";
    QTest::newRow("0.")  << raw("\xfb\0\0\0\0\0\0\0\0") << "0.";
    QTest::newRow("-1.f") << raw("\xfa\xbf\x80\0\0") << "-1.f";
    QTest::newRow("-1.") << raw("\xfb\xbf\xf0\0\0\0\0\0\0") << "-1.";
    QTest::newRow("16777215.f") << raw("\xfa\x4b\x7f\xff\xff") << "16777215.f";
    QTest::newRow("16777215.") << raw("\xfb\x41\x6f\xff\xff\xe0\0\0\0") << "16777215.";
    QTest::newRow("-16777215.f") << raw("\xfa\xcb\x7f\xff\xff") << "-16777215.f";
    QTest::newRow("-16777215.") << raw("\xfb\xc1\x6f\xff\xff\xe0\0\0\0") << "-16777215.";

    QTest::newRow("qnan_f") << raw("\xfa\x7f\xc0\0\0") << "nan";
    QTest::newRow("qnan") << raw("\xfb\x7f\xf8\0\0\0\0\0\0") << "nan";
    QTest::newRow("qnan_f") << raw("\xfa\x7f\xc0\0\0") << "nan";
    QTest::newRow("snan") << raw("\xfb\x7f\xf8\0\0\0\0\0\0") << "nan";
    QTest::newRow("-inf_f") << raw("\xfa\xff\x80\0\0") << "-inf";
    QTest::newRow("-inf") << raw("\xfb\xff\xf0\0\0\0\0\0\0") << "-inf";
    QTest::newRow("+inf_f") << raw("\xfa\x7f\x80\0\0") << "inf";
    QTest::newRow("+inf") << raw("\xfb\x7f\xf0\0\0\0\0\0\0") << "inf";

}

void tst_Parser::fixed_data()
{
    addColumns();
    addFixedData();
}

void tst_Parser::fixed()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, expected);

    compareOne(data, expected);
}

void addStringsData()
{
    // byte strings
    QTest::newRow("emptybytestring") << raw("\x40") << "h''";
    QTest::newRow("bytestring1") << raw("\x41 ") << "h'20'";
    QTest::newRow("bytestring1-nul") << raw("\x41\0") << "h'00'";
    QTest::newRow("bytestring5") << raw("\x45Hello") << "h'48656c6c6f'";
    QTest::newRow("bytestring24") << raw("\x58\x18""123456789012345678901234")
                                  << "h'313233343536373839303132333435363738393031323334'";
    QTest::newRow("bytestring256") << raw("\x59\1\0") + QByteArray(256, '3')
                                   << "h'" + QString(256 * 2, '3') + '\'';

    // text strings
    QTest::newRow("emptytextstring") << raw("\x60") << "\"\"";
    QTest::newRow("textstring1") << raw("\x61 ") << "\" \"";
    QTest::newRow("textstring1-nul") << raw("\x61\0") << "\"\\u0000\"";
    QTest::newRow("textstring5") << raw("\x65Hello") << "\"Hello\"";
    QTest::newRow("textstring24") << raw("\x78\x18""123456789012345678901234")
                                  << "\"123456789012345678901234\"";
    QTest::newRow("textstring256") << raw("\x79\1\0") + QByteArray(256, '3')
                                   << '"' + QString(256, '3') + '"';

    // strings with overlong length
    QTest::newRow("emptybytestring*1") << raw("\x58\x00") << "h''";
    QTest::newRow("emptytextstring*1") << raw("\x78\x00") << "\"\"";
    QTest::newRow("emptybytestring*2") << raw("\x59\x00\x00") << "h''";
    QTest::newRow("emptytextstring*2") << raw("\x79\x00\x00") << "\"\"";
    QTest::newRow("emptybytestring*4") << raw("\x5a\0\0\0\0") << "h''";
    QTest::newRow("emptytextstring*4") << raw("\x7a\0\0\0\0") << "\"\"";
    QTest::newRow("emptybytestring*8") << raw("\x5b\0\0\0\0\0\0\0\0") << "h''";
    QTest::newRow("emptytextstring*8") << raw("\x7b\0\0\0\0\0\0\0\0") << "\"\"";
    QTest::newRow("bytestring5*1") << raw("\x58\x05Hello") << "h'48656c6c6f'";
    QTest::newRow("textstring5*1") << raw("\x78\x05Hello") << "\"Hello\"";
    QTest::newRow("bytestring5*2") << raw("\x59\0\5Hello") << "h'48656c6c6f'";
    QTest::newRow("textstring5*2") << raw("\x79\0\x05Hello") << "\"Hello\"";
    QTest::newRow("bytestring5*4") << raw("\x5a\0\0\0\5Hello") << "h'48656c6c6f'";
    QTest::newRow("textstring5*4") << raw("\x7a\0\0\0\x05Hello") << "\"Hello\"";
    QTest::newRow("bytestring5*8") << raw("\x5b\0\0\0\0\0\0\0\5Hello") << "h'48656c6c6f'";
    QTest::newRow("textstring5*8") << raw("\x7b\0\0\0\0\0\0\0\x05Hello") << "\"Hello\"";

    // strings with undefined length
    QTest::newRow("_emptybytestring") << raw("\x5f\xff") << "h''";
    QTest::newRow("_emptytextstring") << raw("\x7f\xff") << "\"\"";
    QTest::newRow("_emptybytestring2") << raw("\x5f\x40\xff") << "h''";
    QTest::newRow("_emptytextstring2") << raw("\x7f\x60\xff") << "\"\"";
    QTest::newRow("_emptybytestring3") << raw("\x5f\x40\x40\xff") << "h''";
    QTest::newRow("_emptytextstring3") << raw("\x7f\x60\x60\xff") << "\"\"";
    QTest::newRow("_bytestring5*2") << raw("\x5f\x43Hel\x42lo\xff") << "h'48656c6c6f'";
    QTest::newRow("_textstring5*2") << raw("\x7f\x63Hel\x62lo\xff") << "\"Hello\"";
    QTest::newRow("_bytestring5*5") << raw("\x5f\x41H\x41""e\x41l\x41l\x41o\xff") << "h'48656c6c6f'";
    QTest::newRow("_textstring5*5") << raw("\x7f\x61H\x61""e\x61l\x61l\x61o\xff") << "\"Hello\"";
    QTest::newRow("_bytestring5*6") << raw("\x5f\x41H\x41""e\x40\x41l\x41l\x41o\xff") << "h'48656c6c6f'";
    QTest::newRow("_textstring5*6") << raw("\x7f\x61H\x61""e\x61l\x60\x61l\x61o\xff") << "\"Hello\"";
}

void tst_Parser::strings_data()
{
    addColumns();
    addStringsData();
}

void addTagsData()
{
    // since parseOne() works recursively for tags, we can't test lone tags
    QTest::newRow("tag0") << raw("\xc0\x00") << "0(0)";
    QTest::newRow("tag1") << raw("\xc1\x00") << "1(0)";
    QTest::newRow("tag24") << raw("\xd8\x18\x00") << "24(0)";
    QTest::newRow("tag255") << raw("\xd8\xff\x00") << "255(0)";
    QTest::newRow("tag256") << raw("\xd9\1\0\x00") << "256(0)";
    QTest::newRow("tag65535") << raw("\xd9\xff\xff\x00") << "65535(0)";
    QTest::newRow("tag65536") << raw("\xda\0\1\0\0\x00") << "65536(0)";
    QTest::newRow("tagUINT32_MAX-1") << raw("\xda\xff\xff\xff\xff\x00") << "4294967295(0)";
    QTest::newRow("tagUINT32_MAX") << raw("\xdb\0\0\0\1\0\0\0\0\x00") << "4294967296(0)";
    QTest::newRow("tagUINT64_MAX") << raw("\xdb" "\xff\xff\xff\xff" "\xff\xff\xff\xff" "\x00")
                                << QString::number(std::numeric_limits<uint64_t>::max()) + "(0)";

    // overlong tags
    QTest::newRow("tag0*1") << raw("\xd8\0\x00") << "0(0)";
    QTest::newRow("tag0*2") << raw("\xd9\0\0\x00") << "0(0)";
    QTest::newRow("tag0*4") << raw("\xda\0\0\0\0\x00") << "0(0)";
    QTest::newRow("tag0*8") << raw("\xdb\0\0\0\0\0\0\0\0\x00") << "0(0)";

    // tag other things
    QTest::newRow("unixtime") << raw("\xc1\x1a\x55\x4b\xbf\xd3") << "1(1431027667)";
    QTest::newRow("rfc3339date") << raw("\xc0\x78\x19" "2015-05-07 12:41:07-07:00")
                                 << "0(\"2015-05-07 12:41:07-07:00\")";
    QTest::newRow("tag6+false") << raw("\xc6\xf4") << "6(false)";
    QTest::newRow("tag25+true") << raw("\xd8\x19\xf5") << "25(true)";
    QTest::newRow("tag256+null") << raw("\xd9\1\0\xf6") << "256(null)";
    QTest::newRow("tag65536+simple32") << raw("\xda\0\1\0\0\xf8\x20") << "65536(simple(32))";
    QTest::newRow("float+unixtime") << raw("\xc1\xfa\x4e\xaa\x97\x80") << "1(1431027712.f)";
    QTest::newRow("double+unixtime") << raw("\xc1\xfb" "\x41\xd5\x52\xef" "\xf4\xc7\xce\xfe")
                                     << "1(1431027667.122008801)";
}

void tst_Parser::tags_data()
{
    addColumns();
    addTagsData();
}

void tst_Parser::tagTags()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, expected);

    compareOne("\xd9\xd9\xf7" + data, "55799(" + expected + ')');
    if (!compareFailed)
        compareOne("\xd9\xd9\xf7" "\xd9\xd9\xf7" + data, "55799(55799(" + expected + "))");
}

void addEmptyContainersData()
{
    QTest::newRow("emptyarray") << raw("\x80") << "[]";
    QTest::newRow("emptymap") << raw("\xa0") << "{}";
    QTest::newRow("_emptyarray") << raw("\x9f\xff") << "[_ ]";
    QTest::newRow("_emptymap") << raw("\xbf\xff") << "{_ }";
}

void tst_Parser::emptyContainers_data()
{
    addColumns();
    addEmptyContainersData();
}

void tst_Parser::arrays_data()
{
    addColumns();
    addFixedData();
    addStringsData();
    addTagsData();
}

void tst_Parser::arrays()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, expected);

    compareOne("\x81" + data, '[' + expected + ']');
    if (compareFailed) return;

    compareOne("\x82" + data + data, '[' + expected + ", " + expected + ']');
    if (compareFailed) return;

    // overlong length
    compareOne("\x98\1" + data, '[' + expected + ']');
    if (compareFailed) return;
    compareOne(raw("\x99\0\1") + data, '[' + expected + ']');
    if (compareFailed) return;
    compareOne(raw("\x9a\0\0\0\1") + data, '[' + expected + ']');
    if (compareFailed) return;
    compareOne(raw("\x9b\0\0\0\0\0\0\0\1") + data, '[' + expected + ']');
    if (compareFailed) return;

    // medium-sized array: 32 elements (1 << 5)
    expected += ", ";
    for (int i = 0; i < 5; ++i) {
        data += data;
        expected += expected;
    }
    expected.chop(2);   // remove the last ", "
    compareOne("\x98\x20" + data, '[' + expected + ']');
    if (compareFailed) return;

    // large array: 256 elements (32 << 3)
    expected += ", ";
    for (int i = 0; i < 3; ++i) {
        data += data;
        expected += expected;
    }
    expected.chop(2);   // remove the last ", "
    compareOne(raw("\x99\1\0") + data, '[' + expected + ']');
    if (compareFailed) return;
}

void tst_Parser::undefLengthArrays()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, expected);

    compareOne("\x9f" + data + "\xff", "[_ " + expected + ']');
    if (compareFailed) return;

    compareOne("\x9f" + data + data + "\xff", "[_ " + expected + ", " + expected + ']');
}

void tst_Parser::nestedArrays()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, expected);

    compareOne("\x81\x81" + data, "[[" + expected + "]]");
    if (compareFailed) return;

    compareOne("\x81\x81\x81" + data, "[[[" + expected + "]]]");
    if (compareFailed) return;

    compareOne("\x81\x82" + data + data, "[[" + expected + ", " + expected + "]]");
    if (compareFailed) return;

    compareOne("\x82\x81" + data + data, "[[" + expected + "], " + expected + "]");
    if (compareFailed) return;

    compareOne("\x82\x81" + data + '\x81' + data, "[[" + expected + "], [" + expected + "]]");
    if (compareFailed) return;

    // undefined length
    compareOne("\x9f\x9f" + data + data + "\xff\xff", "[_ [_ " + expected + ", " + expected + "]]");
    if (compareFailed) return;

    compareOne("\x9f\x9f" + data + "\xff\x9f" + data + "\xff\xff", "[_ [_ " + expected + "], [_ " + expected + "]]");
    if (compareFailed) return;

    compareOne("\x9f\x9f" + data + data + "\xff\x9f" + data + "\xff\xff",
               "[_ [_ " + expected + ", " + expected + "], [_ " + expected + "]]");
    if (compareFailed) return;

    // mix them
    compareOne("\x81\x9f" + data + "\xff", "[[_ " + expected + "]]");
    if (compareFailed) return;

    compareOne("\x9f\x81" + data + "\xff", "[_ [" + expected + "]]");
}

void tst_Parser::maps_data()
{
    arrays_data();
}

void tst_Parser::maps()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, expected);

    // integer key
    compareOne("\xa1\1" + data, "{1: " + expected + '}');
    if (compareFailed) return;

    // string key
    compareOne("\xa1\x65" "Hello" + data, "{\"Hello\": " + expected + '}');
    if (compareFailed) return;

    // map to self
    compareOne("\xa1" + data + data, '{' + expected + ": " + expected + '}');
    if (compareFailed) return;

    // two integer keys
    compareOne("\xa2\1" + data + "\2" + data, "{1: " + expected + ", 2: " + expected + '}');
    if (compareFailed) return;

    // one integer and one string key
    compareOne("\xa2\1" + data + "\x65" "Hello" + data, "{1: " + expected + ", \"Hello\": " + expected + '}');
    if (compareFailed) return;
}

void tst_Parser::undefLengthMaps()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, expected);

    // integer key
    compareOne("\xbf\1" + data + '\xff', "{_ 1: " + expected + '}');
    if (compareFailed) return;

    compareOne("\xbf\1" + data + '\2' + data + '\xff', "{_ 1: " + expected + ", 2: " + expected + '}');
    if (compareFailed) return;

    compareOne("\xbf\1" + data + "\x65Hello" + data + '\xff', "{_ 1: " + expected + ", \"Hello\": " + expected + '}');
    if (compareFailed) return;

    compareOne("\xbf\x65Hello" + data + '\1' + data + '\xff', "{_ \"Hello\": " + expected + ", 1: " + expected + '}');
}

void tst_Parser::nestedMaps()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, expected);

    // nested maps as values
    compareOne("\xa1\1\xa1\2" + data, "{1: {2: " + expected + "}}");
    if (compareFailed) return;

    compareOne("\xa1\x65Hello\xa1\2" + data, "{\"Hello\": {2: " + expected + "}}");
    if (compareFailed) return;

    compareOne("\xa1\1\xa2\2" + data + '\x20' + data, "{1: {2: " + expected + ", -1: " + expected + "}}");
    if (compareFailed) return;

    compareOne("\xa2\1\xa1\2" + data + "\2\xa1\x20" + data, "{1: {2: " + expected + "}, 2: {-1: " + expected + "}}");
    if (compareFailed) return;

    // nested maps as keys
    compareOne("\xa1\xa1\xf4" + data + "\xf5", "{{false: " + expected + "}: true}");
    if (compareFailed) return;

    compareOne("\xa1\xa1" + data + data + "\xa1" + data + data,
               "{{" + expected + ": " + expected + "}: {" + expected + ": " + expected + "}}");
    if (compareFailed) return;

    // undefined length
    compareOne("\xbf\1\xbf\2" + data + "\xff\xff", "{_ 1: {_ 2: " + expected + "}}");
    if (compareFailed) return;

    compareOne("\xbf\1\xbf\2" + data + '\x20' + data + "\xff\xff", "{_ 1: {_ 2: " + expected + ", -1: " + expected + "}}");
    if (compareFailed) return;

    compareOne("\xbf\1\xbf\2" + data + "\xff\2\xbf\x20" + data + "\xff\xff",
               "{_ 1: {_ 2: " + expected + "}, 2: {_ -1: " + expected + "}}");
    if (compareFailed) return;

    compareOne("\xbf\xbf" + data + data + "\xff\xbf" + data + data + "\xff\xff",
               "{_ {_ " + expected + ": " + expected + "}: {_ " + expected + ": " + expected + "}}");
    if (compareFailed) return;

    // mix them
    compareOne("\xa1\1\xbf\2" + data + "\xff", "{1: {_ 2: " + expected + "}}");
    if (compareFailed) return;

    compareOne("\xbf\1\xa1\2" + data + "\xff", "{_ 1: {2: " + expected + "}}");
    if (compareFailed) return;
}

void addMapMixedData()
{
    // this is just the contents of the map, not including the map itself
    // we do it like that so we can reuse the same data for multiple runs
    QTest::newRow("map-0-24") << raw("\0\x18\x18") << "0: 24";
    QTest::newRow("map-0*1-24") << raw("\x18\0\x18\x18") << "0: 24";
    QTest::newRow("map-0*1-24*2") << raw("\x18\0\x19\0\x18") << "0: 24";
    QTest::newRow("map-0*4-24*2") << raw("\x1a\0\0\0\0\x19\0\x18") << "0: 24";
    QTest::newRow("map-24-0") << raw("\x18\x18\0") << "24: 0";
    QTest::newRow("map-24-0*1") << raw("\x18\x18\0") << "24: 0";
    QTest::newRow("map-255-65535") << raw("\x18\xff\x19\xff\xff") << "255: 65535";
}

void tst_Parser::mapMixed_data()
{
    addColumns();
    addMapMixedData();
}

void tst_Parser::mapMixed()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, expected);

    compareOne("\xa1" + data, '{' + expected + '}');
    if (compareFailed) return;

    compareOne("\xbf" + data + "\xff", "{_ " + expected + '}');
}

void tst_Parser::mapsAndArrays()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, expected);

    // arrays of maps
    compareOne("\x81\xa1\1" + data, "[{1: " + expected + "}]");
    if (compareFailed) return;

    compareOne("\x82\xa1\1" + data + "\xa1\2" + data, "[{1: " + expected + "}, {2: " + expected + "}]");
    if (compareFailed) return;

    compareOne("\x81\xa2\1" + data + "\2" + data, "[{1: " + expected + ", 2: " + expected + "}]");
    if (compareFailed) return;

    compareOne("\x9f\xa1\1" + data + "\xff", "[_ {1: " + expected + "}]");
    if (compareFailed) return;

    compareOne("\x81\xbf\1" + data + "\xff", "[{_ 1: " + expected + "}]");
    if (compareFailed) return;

    compareOne("\x9f\xbf\1" + data + "\xff\xff", "[_ {_ 1: " + expected + "}]");
    if (compareFailed) return;

    // maps of arrays
    compareOne("\xa1\1\x81" + data, "{1: [" + expected + "]}");
    if (compareFailed) return;

    compareOne("\xa1\1\x82" + data + data, "{1: [" + expected + ", " + expected + "]}");
    if (compareFailed) return;

    compareOne("\xa2\1\x81" + data + "\x65Hello\x81" + data, "{1: [" + expected + "], \"Hello\": [" + expected + "]}");
    if (compareFailed) return;

    compareOne("\xa1\1\x9f" + data + "\xff", "{1: [_ " + expected + "]}");
    if (compareFailed) return;

    compareOne("\xa1\1\x9f" + data + data + "\xff", "{1: [_ " + expected + ", " + expected + "]}");
    if (compareFailed) return;

    compareOne("\xbf\1\x81" + data + "\xff", "{_ 1: [" + expected + "]}");
    if (compareFailed) return;

    compareOne("\xbf\1\x9f" + data + "\xff\xff", "{_ 1: [_ " + expected + "]}");
    if (compareFailed) return;

    compareOne("\xbf\1\x9f" + data + data + "\xff\xff", "{_ 1: [_ " + expected + ", " + expected + "]}");
    if (compareFailed) return;

    // mixed with indeterminate length strings
    compareOne("\xbf\1\x9f" + data + "\xff\x65Hello\xbf" + data + "\x7f\xff\xff\xff",
               "{_ 1: [_ " + expected + "], \"Hello\": {_ " + expected + ": \"\"}}");
}

void tst_Parser::stringLength_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<int>("expected");

    QTest::newRow("emptybytestring") << raw("\x40") << 0;
    QTest::newRow("bytestring1") << raw("\x41 ") << 1;
    QTest::newRow("bytestring1-nul") << raw("\x41\0") << 1;
    QTest::newRow("bytestring5") << raw("\x45Hello") << 5;
    QTest::newRow("bytestring24") << raw("\x58\x18""123456789012345678901234") << 24;
    QTest::newRow("bytestring256") << raw("\x59\1\0") + QByteArray(256, '3') << 256;

    // text strings
    QTest::newRow("emptytextstring") << raw("\x60") << 0;
    QTest::newRow("textstring1") << raw("\x61 ") << 1;
    QTest::newRow("textstring1-nul") << raw("\x61\0") << 1;
    QTest::newRow("textstring5") << raw("\x65Hello") << 5;
    QTest::newRow("textstring24") << raw("\x78\x18""123456789012345678901234") << 24;
    QTest::newRow("textstring256") << raw("\x79\1\0") + QByteArray(256, '3') << 256;

    // strings with overlong length
    QTest::newRow("emptybytestring*1") << raw("\x58\x00") << 0;
    QTest::newRow("emptytextstring*1") << raw("\x78\x00") << 0;
    QTest::newRow("emptybytestring*2") << raw("\x59\x00\x00") << 0;
    QTest::newRow("emptytextstring*2") << raw("\x79\x00\x00") << 0;
    QTest::newRow("emptybytestring*4") << raw("\x5a\0\0\0\0") << 0;
    QTest::newRow("emptytextstring*4") << raw("\x7a\0\0\0\0") << 0;
    QTest::newRow("emptybytestring*8") << raw("\x5b\0\0\0\0\0\0\0\0") << 0;
    QTest::newRow("emptytextstring*8") << raw("\x7b\0\0\0\0\0\0\0\0") << 0;
    QTest::newRow("bytestring5*1") << raw("\x58\x05Hello") << 5;
    QTest::newRow("textstring5*1") << raw("\x78\x05Hello") << 5;
    QTest::newRow("bytestring5*2") << raw("\x59\0\5Hello") << 5;
    QTest::newRow("textstring5*2") << raw("\x79\0\x05Hello") << 5;
    QTest::newRow("bytestring5*4") << raw("\x5a\0\0\0\5Hello") << 5;
    QTest::newRow("textstring5*4") << raw("\x7a\0\0\0\x05Hello") << 5;
    QTest::newRow("bytestring5*8") << raw("\x5b\0\0\0\0\0\0\0\5Hello") << 5;
    QTest::newRow("textstring5*8") << raw("\x7b\0\0\0\0\0\0\0\x05Hello") << 5;

    // strings with undefined length
    QTest::newRow("_emptybytestring") << raw("\x5f\xff") << 0;
    QTest::newRow("_emptytextstring") << raw("\x7f\xff") << 0;
    QTest::newRow("_emptybytestring2") << raw("\x5f\x40\xff") << 0;
    QTest::newRow("_emptytextstring2") << raw("\x7f\x60\xff") << 0;
    QTest::newRow("_emptybytestring3") << raw("\x5f\x40\x40\xff") << 0;
    QTest::newRow("_emptytextstring3") << raw("\x7f\x60\x60\xff") << 0;
    QTest::newRow("_bytestring5*2") << raw("\x5f\x43Hel\x42lo\xff") << 5;
    QTest::newRow("_textstring5*2") << raw("\x7f\x63Hel\x62lo\xff") << 5;
    QTest::newRow("_bytestring5*5") << raw("\x5f\x41H\x41""e\x41l\x41l\x41o\xff") << 5;
    QTest::newRow("_textstring5*5") << raw("\x7f\x61H\x61""e\x61l\x61l\x61o\xff") << 5;
    QTest::newRow("_bytestring5*6") << raw("\x5f\x41H\x41""e\x40\x41l\x41l\x41o\xff") << 5;
    QTest::newRow("_textstring5*6") << raw("\x7f\x61H\x61""e\x61l\x60\x61l\x61o\xff") << 5;
}

void tst_Parser::stringLength()
{
    QFETCH(QByteArray, data);
    QFETCH(int, expected);

    CborParser parser;
    CborValue value;
    CborError err = cbor_parser_init(data.constData(), data.length(), 0, &parser, &value);
    QVERIFY2(!err, QByteArray("Got error \"") + cbor_error_string(err) + "\"");

    size_t result;
    err = cbor_value_calculate_string_length(&value, &result);
    QVERIFY2(!err, QByteArray("Got error \"") + cbor_error_string(err) + "\"");
    QCOMPARE(result, size_t(expected));
}

void tst_Parser::stringCompare_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<QString>("string");
    QTest::addColumn<bool>("expected");

    // compare empty to empty
    QTest::newRow("empty-empty") << raw("\x60") << QString() << true;
    QTest::newRow("_empty-empty") << raw("\x7f\xff") << QString() << true;
    QTest::newRow("_empty*1-empty") << raw("\x7f\x60\xff") << QString() << true;
    QTest::newRow("_empty*2-empty") << raw("\x7f\x60\x60\xff") << QString() << true;

    // compare empty to non-empty
    QTest::newRow("empty-nonempty") << raw("\x60") << "Hello" << false;
    QTest::newRow("_empty-nonempty") << raw("\x7f\xff") << "Hello" << false;
    QTest::newRow("_empty*1-nonempty") << raw("\x7f\x60\xff") << "Hello" << false;
    QTest::newRow("_empty*2-nonempty") << raw("\x7f\x60\x60\xff") << "Hello" << false;

    // compare same strings
    QTest::newRow("same-short-short") << raw("\x65Hello") << "Hello" << true;
    QTest::newRow("same-_short*1-short") << raw("\x7f\x65Hello\xff") << "Hello" << true;
    QTest::newRow("same-_short*2-short") << raw("\x7f\x63Hel\x62lo\xff") << "Hello" << true;
    QTest::newRow("same-_short*5-short") << raw("\x7f\x61H\x61""e\x61l\x61l\x61o\xff") << "Hello" << true;
    QTest::newRow("same-_short*8-short") << raw("\x7f\x61H\x60\x61""e\x60\x61l\x61l\x60\x61o\xff") << "Hello" << true;
    QTest::newRow("same-long-long") << raw("\x78\x2aGood morning, good afternoon and goodnight")
                                    << "Good morning, good afternoon and goodnight" << true;
    QTest::newRow("same-_long*1-long") << raw("\x7f\x78\x2aGood morning, good afternoon and goodnight\xff")
                                       << "Good morning, good afternoon and goodnight" << true;
    QTest::newRow("same-_long*2-long") << raw("\x7f\x78\x1cGood morning, good afternoon\x6e and goodnight\xff")
                                       << "Good morning, good afternoon and goodnight" << true;

    // compare different strings (same length)
    QTest::newRow("diff-same-length-short-short") << raw("\x65Hello") << "World" << false;
    QTest::newRow("diff-same-length-_short*1-short") << raw("\x7f\x65Hello\xff") << "World" << false;
    QTest::newRow("diff-same-length-_short*2-short") << raw("\x7f\x63Hel\x62lo\xff") << "World" << false;
    QTest::newRow("diff-same-length-_short*5-short") << raw("\x7f\x61H\x61""e\x61l\x61l\x61o\xff") << "World" << false;
    QTest::newRow("diff-same-length-_short*8-short") << raw("\x7f\x61H\x60\x61""e\x60\x61l\x61l\x60\x61o\xff") << "World" << false;
    QTest::newRow("diff-same-length-long-long") << raw("\x78\x2aGood morning, good afternoon and goodnight")
                                                << "Good morning, good afternoon and goodnight, world" << false;
    QTest::newRow("diff-same-length-_long*1-long") << raw("\x7f\x78\x2aGood morning, good afternoon and goodnight\xff")
                                                   << "Good morning, good afternoon and goodnight, world" << false;
    QTest::newRow("diff-same-length-_long*2-long") << raw("\x7f\x78\x1cGood morning, good afternoon\x6e and goodnight\xff")
                                                   << "Good morning, good afternoon and goodnight, world" << false;

    // compare different strings (different length)
    QTest::newRow("diff-diff-length-short-short") << raw("\x65Hello") << "Hello World" << false;
    QTest::newRow("diff-diff-length-_short*1-short") << raw("\x7f\x65Hello\xff") << "Hello World" << false;
    QTest::newRow("diff-diff-length-_short*2-short") << raw("\x7f\x63Hel\x62lo\xff") << "Hello World" << false;
    QTest::newRow("diff-diff-length-_short*5-short") << raw("\x7f\x61H\x61""e\x61l\x61l\x61o\xff") << "Hello World" << false;
    QTest::newRow("diff-diff-length-_short*8-short") << raw("\x7f\x61H\x60\x61""e\x60\x61l\x61l\x60\x61o\xff") << "Hello World" << false;
    QTest::newRow("diff-diff-length-long-long") << raw("\x78\x2aGood morning, good afternoon and goodnight")
                                                << "Good morning, good afternoon and goodnight World" << false;
    QTest::newRow("diff-diff-length-_long*1-long") << raw("\x7f\x78\x2aGood morning, good afternoon and goodnight\xff")
                                                   << "Good morning, good afternoon and goodnight World" << false;
    QTest::newRow("diff-diff-length-_long*2-long") << raw("\x7f\x78\x1cGood morning, good afternoon\x6e and goodnight\xff")
                                                   << "Good morning, good afternoon and goodnight World" << false;

    // compare against non-strings
    QTest::newRow("unsigned") << raw("\0") << "0" << false;
    QTest::newRow("negative") << raw("\x20") << "-1" << false;
    QTest::newRow("emptybytestring") << raw("\x40") << "" << false;
    QTest::newRow("_emptybytestring") << raw("\x5f\xff") << "" << false;
    QTest::newRow("shortbytestring") << raw("\x45Hello") << "Hello" << false;
    QTest::newRow("longbytestring") << raw("\x58\x2aGood morning, good afternoon and goodnight")
                                    << "Good morning, good afternoon and goodnight" << false;
    QTest::newRow("emptyarray") << raw("\x80") << "" << false;
    QTest::newRow("emptymap") << raw("\xa0") << "" << false;
    QTest::newRow("array") << raw("\x81\x65Hello") << "Hello" << false;
    QTest::newRow("map") << raw("\xa1\x65Hello\x65World") << "Hello World" << false;
    QTest::newRow("false") << raw("\xf4") << "false" << false;
    QTest::newRow("true") << raw("\xf5") << "true" << false;
    QTest::newRow("null") << raw("\xf6") << "null" << false;
}

void compareOneString(const QByteArray &data, const QString &string, bool expected, int line)
{
    compareFailed = true;

    CborParser parser;
    CborValue value;
    CborError err = cbor_parser_init(data.constData(), data.length(), 0, &parser, &value);
    QVERIFY2(!err, QByteArray::number(line) + ": Got error \"" + cbor_error_string(err) + "\"");

    bool result;
    err = cbor_value_text_string_equals(&value, string.toUtf8().constData(), &result);
    QVERIFY2(!err, QByteArray::number(line) + ": Got error \"" + cbor_error_string(err) + "\"");
    QCOMPARE(result, expected);

    compareFailed = false;
}
#define compareOneString(data, string, expected) compareOneString(data, string, expected, __LINE__)

void tst_Parser::stringCompare()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, string);
    QFETCH(bool, expected);

    compareOneString(data, string, expected);
    if (compareFailed) return;

    // tag it
    compareOneString("\xc1" + data, string, expected);
    if (compareFailed) return;

    compareOneString("\xc1\xc2" + data, string, expected);
}

QTEST_MAIN(tst_Parser)
#include "tst_parser.moc"
