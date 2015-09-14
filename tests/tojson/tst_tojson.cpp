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
#include "cborjson.h"

class tst_ToJson : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();

    void fixed_data();
    void fixed();
    void textstrings_data();
    void textstrings() { fixed(); }
    void nonjson_data();
    void nonjson() { fixed(); }
    void bytestrings_data();
    void bytestrings() { fixed(); }
    void emptyContainers_data();
    void emptyContainers() { fixed(); }
    void arrays_data();
    void arrays();
    void nestedArrays_data() { arrays_data(); }
    void nestedArrays();
    void maps_data() { arrays_data(); }
    void maps();
    void nestedMaps_data() { maps_data(); }
    void nestedMaps();
    void nonStringKeyMaps_data();
    void nonStringKeyMaps();

    void tagsToObjects_data();
    void tagsToObjects();
    void taggedByteStringsToBase16_data();
    void taggedByteStringsToBase16();
    void taggedByteStringsToBase64_data() { taggedByteStringsToBase16_data(); }
    void taggedByteStringsToBase64();
    void taggedByteStringsToBigNum_data()  { taggedByteStringsToBase16_data(); }
    void taggedByteStringsToBigNum();
    void otherTags_data();
    void otherTags();
};

template <size_t N> QByteArray raw(const char (&data)[N])
{
    return QByteArray::fromRawData(data, N - 1);
}

void addColumns()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<QString>("expected");
}

void addFixedData()
{
    // unsigned integers
    QTest::newRow("0") << raw("\x00") << "0";
    QTest::newRow("1") << raw("\x01") << "1";
    QTest::newRow("2^53-1") << raw("\x1b\0\x1f\xff\xff""\xff\xff\xff\xff") << "9007199254740991";
    QTest::newRow("2^64-epsilon") << raw("\x1b\xff\xff\xff\xff""\xff\xff\xf8\x00") << "18446744073709549568";

    // negative integers
    QTest::newRow("-1") << raw("\x20") << "-1";
    QTest::newRow("-2") << raw("\x21") << "-2";
    QTest::newRow("-2^53+1") << raw("\x3b\0\x1f\xff\xff""\xff\xff\xff\xfe") << "-9007199254740991";
    QTest::newRow("-2^64+epsilon") << raw("\x3b\xff\xff\xff\xff""\xff\xff\xf8\x00") << "-18446744073709549568";

    QTest::newRow("false") << raw("\xf4") << "false";
    QTest::newRow("true") << raw("\xf5") << "true";
    QTest::newRow("null") << raw("\xf6") << "null";

    QTest::newRow("0.f") << raw("\xfa\0\0\0\0") << "0";
    QTest::newRow("0.")  << raw("\xfb\0\0\0\0\0\0\0\0") << "0";
    QTest::newRow("-1.f") << raw("\xfa\xbf\x80\0\0") << "-1";
    QTest::newRow("-1.") << raw("\xfb\xbf\xf0\0\0\0\0\0\0") << "-1";
    QTest::newRow("16777215.f") << raw("\xfa\x4b\x7f\xff\xff") << "16777215";
    QTest::newRow("16777215.") << raw("\xfb\x41\x6f\xff\xff\xe0\0\0\0") << "16777215";
    QTest::newRow("-16777215.f") << raw("\xfa\xcb\x7f\xff\xff") << "-16777215";
    QTest::newRow("-16777215.") << raw("\xfb\xc1\x6f\xff\xff\xe0\0\0\0") << "-16777215";

    QTest::newRow("0.5f") << raw("\xfa\x3f\0\0\0") << "0.5";
    QTest::newRow("0.5") << raw("\xfb\x3f\xe0\0\0\0\0\0\0") << "0.5";
    QTest::newRow("2.f^24-1") << raw("\xfa\x4b\x7f\xff\xff") << "16777215";
    QTest::newRow("2.^53-1") << raw("\xfb\x43\x3f\xff\xff""\xff\xff\xff\xff") << "9007199254740991";
    QTest::newRow("2.f^64-epsilon") << raw("\xfa\x5f\x7f\xff\xff") << "18446742974197923840";
    QTest::newRow("2.^64-epsilon") << raw("\xfb\x43\xef\xff\xff""\xff\xff\xff\xff") << "18446744073709549568";
    QTest::newRow("2.f^64") << raw("\xfa\x5f\x80\0\0") << "1.8446744073709552e+19";
    QTest::newRow("2.^64") << raw("\xfb\x43\xf0\0\0\0\0\0\0") << "1.8446744073709552e+19";

    // infinities and NaN are not supported in JSON, they convert to null
    QTest::newRow("qnan_f") << raw("\xfa\x7f\xc0\0\0") << "null";
    QTest::newRow("qnan") << raw("\xfb\x7f\xf8\0\0\0\0\0\0") << "null";
    QTest::newRow("snan_f") << raw("\xfa\x7f\xc0\0\0") << "null";
    QTest::newRow("snan") << raw("\xfb\x7f\xf8\0\0\0\0\0\0") << "null";
    QTest::newRow("-inf_f") << raw("\xfa\xff\x80\0\0") << "null";
    QTest::newRow("-inf") << raw("\xfb\xff\xf0\0\0\0\0\0\0") << "null";
    QTest::newRow("+inf_f") << raw("\xfa\x7f\x80\0\0") << "null";
    QTest::newRow("+inf") << raw("\xfb\x7f\xf0\0\0\0\0\0\0") << "null";
}

void addTextStringsData()
{
    QTest::newRow("emptytextstring") << raw("\x60") << "\"\"";
    QTest::newRow("textstring1") << raw("\x61 ") << "\" \"";
    QTest::newRow("textstring5") << raw("\x65Hello") << "\"Hello\"";
    QTest::newRow("textstring24") << raw("\x78\x18""123456789012345678901234")
                                  << "\"123456789012345678901234\"";
    QTest::newRow("textstring256") << raw("\x79\1\0") + QByteArray(256, '3')
                                   << '"' + QString(256, '3') + '"';

    // strings with undefined length
    QTest::newRow("_emptytextstring") << raw("\x7f\xff") << "\"\"";
    QTest::newRow("_emptytextstring2") << raw("\x7f\x60\xff") << "\"\"";
    QTest::newRow("_emptytextstring3") << raw("\x7f\x60\x60\xff") << "\"\"";
    QTest::newRow("_textstring5*2") << raw("\x7f\x63Hel\x62lo\xff") << "\"Hello\"";
    QTest::newRow("_textstring5*5") << raw("\x7f\x61H\x61""e\x61l\x61l\x61o\xff") << "\"Hello\"";
    QTest::newRow("_textstring5*6") << raw("\x7f\x61H\x61""e\x61l\x60\x61l\x61o\xff") << "\"Hello\"";
}

void addNonJsonData()
{
    QTest::newRow("undefined") << raw("\xf7") << "\"undefined\"";
    QTest::newRow("simple0") << raw("\xe0") << "\"simple(0)\"";
    QTest::newRow("simple19") << raw("\xf3") << "\"simple(19)\"";
    QTest::newRow("simple32") << raw("\xf8\x20") << "\"simple(32)\"";
    QTest::newRow("simple255") << raw("\xf8\xff") << "\"simple(255)\"";
}

void addByteStringsData()
{
    QTest::newRow("emptybytestring") << raw("\x40") << "\"\"";
    QTest::newRow("bytestring1") << raw("\x41 ") << "\"IA\"";
    QTest::newRow("bytestring1-nul") << raw("\x41\0") << "\"AA\"";
    QTest::newRow("bytestring2") << raw("\x42Hi") << "\"SGk\"";
    QTest::newRow("bytestring3") << raw("\x43Hey") << "\"SGV5\"";
    QTest::newRow("bytestring4") << raw("\x44Hola") << "\"SG9sYQ\"";
    QTest::newRow("bytestring5") << raw("\x45Hello") << "\"SGVsbG8\"";
    QTest::newRow("bytestring24") << raw("\x58\x18""123456789012345678901234")
                                  << "\"MTIzNDU2Nzg5MDEyMzQ1Njc4OTAxMjM0\"";

    // strings with undefined length
    QTest::newRow("_emptybytestring") << raw("\x5f\xff") << "\"\"";
    QTest::newRow("_emptybytestring2") << raw("\x5f\x40\xff") << "\"\"";
    QTest::newRow("_emptybytestring3") << raw("\x5f\x40\x40\xff") << "\"\"";
    QTest::newRow("_bytestring5*2") << raw("\x5f\x43Hel\x42lo\xff") << "\"SGVsbG8\"";
    QTest::newRow("_bytestring5*5") << raw("\x5f\x41H\x41""e\x41l\x41l\x41o\xff") << "\"SGVsbG8\"";
    QTest::newRow("_bytestring5*6") << raw("\x5f\x41H\x41""e\x40\x41l\x41l\x41o\xff") << "\"SGVsbG8\"";
}

void addEmptyContainersData()
{
    QTest::newRow("emptyarray") << raw("\x80") << "[]";
    QTest::newRow("emptymap") << raw("\xa0") << "{}";
    QTest::newRow("_emptyarray") << raw("\x9f\xff") << "[]";
    QTest::newRow("_emptymap") << raw("\xbf\xff") << "{}";
}

CborError parseOne(CborValue *it, QString *parsed, int flags)
{
    char *buffer;
    size_t size;

    FILE *f = open_memstream(&buffer, &size);
    CborError err = cbor_value_to_json_advance(f, it, flags);
    fclose(f);

    *parsed = QString::fromLatin1(buffer);
    free(buffer);
    return err;
}

bool compareFailed = true;
void compareOne_real(const QByteArray &data, const QString &expected, int flags, int line)
{
    compareFailed = true;
    CborParser parser;
    CborValue first;
    CborError err = cbor_parser_init(reinterpret_cast<const quint8 *>(data.constData()), data.length(), 0, &parser, &first);
    QVERIFY2(!err, QByteArray::number(line) + ": Got error \"" + cbor_error_string(err) + "\"");

    QString decoded;
    err = parseOne(&first, &decoded, flags);
    QVERIFY2(!err, QByteArray::number(line) + ": Got error \"" + cbor_error_string(err) +
                   "\"; decoded stream:\n" + decoded.toLatin1());
    QCOMPARE(decoded, expected);

    // check that we consumed everything
    QCOMPARE((void*)first.ptr, (void*)data.constEnd());

    compareFailed = false;
}
#define compareOne(data, expected, flags) \
    compareOne_real(data, expected, flags, __LINE__); \
    if (compareFailed) return

void tst_ToJson::initTestCase()
{
    setlocale(LC_ALL, "C");
}

void tst_ToJson::fixed_data()
{
    addColumns();
    addFixedData();
}

void tst_ToJson::fixed()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, expected);

    compareOne(data, expected, 0);
}

void tst_ToJson::textstrings_data()
{
    addColumns();
    addTextStringsData();
}

void tst_ToJson::nonjson_data()
{
    addColumns();
    addNonJsonData();
}

void tst_ToJson::bytestrings_data()
{
    addColumns();
    addByteStringsData();
}

void tst_ToJson::emptyContainers_data()
{
    addColumns();
    addEmptyContainersData();
}

void tst_ToJson::arrays_data()
{
    addColumns();
    addFixedData();
    addTextStringsData();
    addNonJsonData();
    addByteStringsData();
}

void tst_ToJson::arrays()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, expected);

    compareOne("\x81" + data, '[' + expected + ']', 0);
    compareOne("\x82" + data + data, '[' + expected + ',' + expected + ']', 0);
}

void tst_ToJson::nestedArrays()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, expected);

    compareOne("\x81\x81" + data, "[[" + expected + "]]", 0);
    compareOne("\x81\x81\x81" + data, "[[[" + expected + "]]]", 0);
    compareOne("\x81\x82" + data + data, "[[" + expected + ',' + expected + "]]", 0);
    compareOne("\x82\x81" + data + data, "[[" + expected + "]," + expected + "]", 0);
    compareOne("\x82\x81" + data + '\x81' + data, "[[" + expected + "],[" + expected + "]]", 0);
}

void tst_ToJson::maps()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, expected);

    compareOne("\xa1\x65" "Hello" + data, "{\"Hello\":" + expected + '}', 0);
}

void tst_ToJson::nestedMaps()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, expected);

    compareOne("\xa1\x65Hello\xa1\x65World" + data, "{\"Hello\":{\"World\":" + expected + "}}", 0);
//    compareOne("\xa1\x63""foo\xa1\63""bar" + data + "\63""baz\xa1\x64quux" + data,
//               "{\"foo\":{\"bar\":" + expected + "},\"baz\":{\"quux\":" + expected + "}", 0);
}

void tst_ToJson::nonStringKeyMaps_data()
{
    addColumns();
    addFixedData();
    addNonJsonData();
    addByteStringsData();
}

void tst_ToJson::nonStringKeyMaps()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, expected);

    if (!expected.startsWith('"'))
        expected = '"' + expected + '"';
    data = "\xa1" + data + "\1";
    compareOne(data, "{" + expected + ":1}", CborConvertStringifyMapKeys);

    // and verify that they fail if we use CborConvertRequireMapStringKeys
    CborParser parser;
    CborValue first;
    QString decoded;
    cbor_parser_init(reinterpret_cast<const quint8 *>(data.constData()), data.length(), 0, &parser, &first);
    CborError err = parseOne(&first, &decoded, CborConvertRequireMapStringKeys);
    QCOMPARE(err, CborErrorJsonObjectKeyNotString);
}

void tst_ToJson::tagsToObjects_data()
{
    addColumns();
    QTest::newRow("0(0)") << raw("\xc0\0") << "{\"tag0\":0}";
    QTest::newRow("0(-1)") << raw("\xc0\x20") << "{\"tag0\":-1}";
    QTest::newRow("0(\"hello\")") << raw("\xc0\x65hello") << "{\"tag0\":\"hello\"}";
    QTest::newRow("22(h'48656c6c6f')") << raw("\xd6\x45Hello") << "{\"tag22\":\"SGVsbG8\"}";
    QTest::newRow("0([1,2,3])") << raw("\xc0\x83\1\2\3") << "{\"tag0\":[1,2,3]}";
    QTest::newRow("0({\"z\":true})") << raw("\xc0\xa1\x61z\xf5") << "{\"tag0\":{\"z\":true}}";

    // large tags
    QTest::newRow("55799(0)") << raw("\xd9\xd9\xf7\0") << "{\"tag55799\":0}";
    QTest::newRow("4294967295") << raw("\xda\xff\xff\xff\xff\0") << "{\"tag4294967295\":0}";
    QTest::newRow("18446744073709551615(0)") << raw("\xdb\xff\xff\xff\xff""\xff\xff\xff\xff\0")
                                             << "{\"tag18446744073709551615\":0}";

    // nested tags
    QTest::newRow("0(1(2))") << raw("\xc0\xc1\2") << "{\"tag0\":{\"tag1\":2}}";
    QTest::newRow("0({\"z\":1(2)})") << raw("\xc0\xa1\x61z\xc1\2") << "{\"tag0\":{\"z\":{\"tag1\":2}}}";
}

void tst_ToJson::tagsToObjects()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, expected);

    compareOne(data, expected, CborConvertTagsToObjects);
}

void tst_ToJson::taggedByteStringsToBase16_data()
{
    QTest::addColumn<QByteArray>("data");
    QTest::addColumn<QString>("base64url");
    QTest::addColumn<QString>("base64");
    QTest::addColumn<QString>("base16");

    QTest::newRow("emptybytestring") << raw("\x40") << "" << "" << "";
    QTest::newRow("bytestring1") << raw("\x41 ") << "IA" << "IA==" << "20";
    QTest::newRow("bytestring1-nul") << raw("\x41\0") << "AA" << "AA==" << "00";
    QTest::newRow("bytestring1-ff") << raw("\x41\xff") << "_w" << "/w==" << "ff";
    QTest::newRow("bytestring2") << raw("\x42Hi") << "SGk" << "SGk=" << "4869";
    QTest::newRow("bytestring3") << raw("\x43Hey") << "SGV5" << "SGV5" << "486579";
    QTest::newRow("bytestring4") << raw("\x44Hola") << "SG9sYQ" << "SG9sYQ==" << "486f6c61";
    QTest::newRow("bytestring5") << raw("\x45Hello") << "SGVsbG8" << "SGVsbG8=" << "48656c6c6f";
    QTest::newRow("bytestring24") << raw("\x58\x18""123456789012345678901234")
                                  << "MTIzNDU2Nzg5MDEyMzQ1Njc4OTAxMjM0"
                                  << "MTIzNDU2Nzg5MDEyMzQ1Njc4OTAxMjM0"
                                  << "313233343536373839303132333435363738393031323334";

    // strings with undefined length
    QTest::newRow("_emptybytestring") << raw("\x5f\xff") << "" << "" << "";
    QTest::newRow("_emptybytestring2") << raw("\x5f\x40\xff") << "" << "" << "";
    QTest::newRow("_emptybytestring3") << raw("\x5f\x40\x40\xff") << "" << "" << "";
    QTest::newRow("_bytestring5*2") << raw("\x5f\x43Hel\x42lo\xff") << "SGVsbG8" << "SGVsbG8=" << "48656c6c6f";
    QTest::newRow("_bytestring5*5") << raw("\x5f\x41H\x41""e\x41l\x41l\x41o\xff")
                                    << "SGVsbG8" << "SGVsbG8=" << "48656c6c6f";
    QTest::newRow("_bytestring5*6") << raw("\x5f\x41H\x41""e\x40\x41l\x41l\x41o\xff")
                                    << "SGVsbG8" << "SGVsbG8=" << "48656c6c6f";
}

void tst_ToJson::taggedByteStringsToBase16()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, base16);

    compareOne('\xd7' + data, '"' + base16 + '"', 0);
}

void tst_ToJson::taggedByteStringsToBase64()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, base64);

    compareOne('\xd6' + data, '"' + base64 + '"', 0);
}

void tst_ToJson::taggedByteStringsToBigNum()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, base64url);

    compareOne('\xc3' + data, "\"~" + base64url + '"', 0);
}

void tst_ToJson::otherTags_data()
{
    addColumns();
    addFixedData();
    addTextStringsData();
    addNonJsonData();
    addByteStringsData();
    addEmptyContainersData();
}

void tst_ToJson::otherTags()
{
    QFETCH(QByteArray, data);
    QFETCH(QString, expected);

    // other tags produce no change in output
    compareOne("\xc0" + data, expected, 0);
    compareOne("\xc1" + data, expected, 0);
    compareOne("\xc2" + data, expected, 0);
    compareOne("\xc4" + data, expected, 0);
    compareOne("\xc5" + data, expected, 0);
    compareOne("\xd8\x20" + data, expected, 0);
    compareOne("\xd8\x21" + data, expected, 0);
    compareOne("\xd8\x22" + data, expected, 0);
    compareOne("\xd8\x23" + data, expected, 0);
    compareOne("\xd8\x24" + data, expected, 0);
    compareOne("\xd9\xd9\xf7" + data, expected, 0);
}

QTEST_MAIN(tst_ToJson)
#include "tst_tojson.moc"
