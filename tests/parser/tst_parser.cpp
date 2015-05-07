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
    void fixed_data();
    void fixed();
    void strings_data();
    void strings();
    void tags_data();
    void tags();
    void tagTags_data() { tags_data(); }
    void tagTags();
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

CborError parse(CborValue *it, QString *parsed);
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
        err = parse(&recursed, parsed);
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

CborError parse(CborValue *it, QString *parsed)
{
    const char *comma = ", ";
    while (!cbor_value_at_end(it)) {
        CborError err = parseOne(it, parsed);
        if (err)
            return err;
        *parsed += comma;
        comma = nullptr;
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

void tst_Parser::fixed_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<QString>("expected");

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

void compareOne(const QByteArray &data, const QString &expected)
{
    CborParser parser;
    CborValue first;
    CborError err = cbor_parser_init(data.constData(), data.length(), 0, &parser, &first);
    QVERIFY2(!err, QByteArray("Got error \"") + cbor_error_string(err) + "\"");

    QString decoded;
    err = parseOne(&first, &decoded);
    QVERIFY2(!err, QByteArray("Got error \"") + cbor_error_string(err) +
                   "\"; decoded stream:\n" + decoded.toLatin1());
    QCOMPARE(decoded, expected);
}

void tst_Parser::fixed()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, expected);

    compareOne(data, expected);
}

void tst_Parser::strings_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<QString>("expected");

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

void tst_Parser::strings()
{
    fixed();
}

void tst_Parser::tags_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<QString>("expected");

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

void tst_Parser::tags()
{
    fixed();
}

void tst_Parser::tagTags()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, expected);

    compareOne("\xd9\xd9\xf7" + data, "55799(" + expected + ')');
    compareOne("\xd9\xd9\xf7" "\xd9\xd9\xf7" + data, "55799(55799(" + expected + "))");
}

QTEST_MAIN(tst_Parser)
#include "tst_parser.moc"
