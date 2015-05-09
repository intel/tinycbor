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

class tst_Encoder : public QObject
{
    Q_OBJECT
private slots:
    void fixed_data();
    void fixed();
    void strings_data();
    void strings() { fixed(); }
    void tags_data();
    void tags();
};

template <size_t N> QByteArray raw(const char (&data)[N])
{
    return QByteArray::fromRawData(data, N - 1);
}

struct SimpleType { uint8_t type; };
Q_DECLARE_METATYPE(SimpleType)

struct Float16Standin { uint16_t val; };
Q_DECLARE_METATYPE(Float16Standin)

struct Tag { CborTag tag; QVariant tagged; };
Q_DECLARE_METATYPE(Tag)

CborError encodeVariant(CborEncoder *encoder, const QVariant &v)
{
    int type = v.userType();
    switch (type) {
    case QVariant::Int:
    case QVariant::LongLong:
        return cbor_encode_int(encoder, v.toLongLong());

    case QVariant::UInt:
    case QVariant::ULongLong:
        return cbor_encode_uint(encoder, v.toULongLong());

    case QVariant::Bool:
        return cbor_encode_boolean(encoder, v.toBool());

    case QVariant::Invalid:
        return cbor_encode_undefined(encoder);

    case QMetaType::VoidStar:
        return cbor_encode_null(encoder);

    case QVariant::Double:
        return cbor_encode_double(encoder, (const double*)v.constData());

    case QMetaType::Float:
        return cbor_encode_float(encoder, (const float*)v.constData());

    case QVariant::String: {
        QByteArray string = v.toString().toUtf8();
        return cbor_encode_text_string(encoder, string.constData(), string.length());
    }

    case QVariant::ByteArray: {
        QByteArray string = v.toByteArray();
        return cbor_encode_byte_string(encoder, string.constData(), string.length());
    }

    default:
        if (type == qMetaTypeId<SimpleType>())
            return cbor_encode_simple_value(encoder, v.value<SimpleType>().type);
        if (type == qMetaTypeId<Float16Standin>())
            return cbor_encode_half_float(encoder, v.constData());
        if (type == qMetaTypeId<Tag>()) {
            CborError err = cbor_encode_tag(encoder, v.value<Tag>().tag);
            if (err)
                return err;
            return encodeVariant(encoder, v.value<Tag>().tagged);
        }
    }
    return CborErrorUnknownType;
}

bool compareFailed;
void compare(const QVariant &input, const QByteArray &output)
{
    QByteArray buffer(output.length(), Qt::Uninitialized);
    CborEncoder encoder;
    cbor_encoder_init(&encoder, buffer.data(), buffer.length(), 0);
    QCOMPARE(int(encodeVariant(&encoder, input)), int(CborNoError));
    QCOMPARE(buffer, output);
}

void addColumns()
{
    QTest::addColumn<QByteArray>("output");
    QTest::addColumn<QVariant>("input");
}

void addFixedData()
{
    // unsigned integers
    QTest::newRow("0U") << raw("\x00") << QVariant(0U);
    QTest::newRow("1U") << raw("\x01") << QVariant(1U);
    QTest::newRow("10U") << raw("\x0a") << QVariant(10U);
    QTest::newRow("23U") << raw("\x17") << QVariant(23U);
    QTest::newRow("24U") << raw("\x18\x18") << QVariant(24U);
    QTest::newRow("255U") << raw("\x18\xff") << QVariant(255U);
    QTest::newRow("256U") << raw("\x19\x01\x00") << QVariant(256U);
    QTest::newRow("65535U") << raw("\x19\xff\xff") << QVariant(65535U);
    QTest::newRow("65536U") << raw("\x1a\0\1\x00\x00") << QVariant(65536U);
    QTest::newRow("4294967295U") << raw("\x1a\xff\xff\xff\xff") << QVariant(4294967295U);
    QTest::newRow("4294967296U") << raw("\x1b\0\0\0\1\0\0\0\0") << QVariant(Q_UINT64_C(4294967296));
    QTest::newRow("UINT64_MAX") << raw("\x1b" "\xff\xff\xff\xff" "\xff\xff\xff\xff")
                                << QVariant(std::numeric_limits<quint64>::max());

    // signed integers containing positive numbers
    QTest::newRow("0") << raw("\x00") << QVariant(0);
    QTest::newRow("1") << raw("\x01") << QVariant(1);
    QTest::newRow("10") << raw("\x0a") << QVariant(10);
    QTest::newRow("23") << raw("\x17") << QVariant(23);
    QTest::newRow("24") << raw("\x18\x18") << QVariant(24);
    QTest::newRow("255") << raw("\x18\xff") << QVariant(255);
    QTest::newRow("256") << raw("\x19\x01\x00") << QVariant(256);
    QTest::newRow("65535") << raw("\x19\xff\xff") << QVariant(65535);
    QTest::newRow("65536") << raw("\x1a\0\1\x00\x00") << QVariant(65536);
    QTest::newRow("4294967295") << raw("\x1a\xff\xff\xff\xff") << QVariant(Q_INT64_C(4294967295));
    QTest::newRow("4294967296") << raw("\x1b\0\0\0\1\0\0\0\0") << QVariant(Q_INT64_C(4294967296));

    // negative integers
    QTest::newRow("-1") << raw("\x20") << QVariant(-1);
    QTest::newRow("-2") << raw("\x21") << QVariant(-2);
    QTest::newRow("-24") << raw("\x37") << QVariant(-24);
    QTest::newRow("-25") << raw("\x38\x18") << QVariant(-25);
    QTest::newRow("-UINT8_MAX") << raw("\x38\xff") << QVariant(-256);
    QTest::newRow("-UINT8_MAX-1") << raw("\x39\x01\x00") << QVariant(-257);
    QTest::newRow("-UINT16_MAX") << raw("\x39\xff\xff") << QVariant(-65536);
    QTest::newRow("-UINT16_MAX-1") << raw("\x3a\0\1\x00\x00") << QVariant(-65537);
    QTest::newRow("-UINT32_MAX") << raw("\x3a\xff\xff\xff\xff") << QVariant(Q_INT64_C(-4294967296));
    QTest::newRow("-UINT32_MAX-1") << raw("\x3b\0\0\0\1\0\0\0\0") << QVariant(Q_INT64_C(-4294967297));
//    QTest::newRow("-UINT64_MAX") << raw("\x3b" "\xff\xff\xff\xff" "\xff\xff\xff\xff")
//                                 << QVariant::fromValue(BigNegative{std::numeric_limits<quint64>::max()});

    QTest::newRow("simple0") << raw("\xe0") << QVariant::fromValue(SimpleType{0});
    QTest::newRow("simple19") << raw("\xf3") << QVariant::fromValue(SimpleType{19});
    QTest::newRow("false") << raw("\xf4") << QVariant(false);
    QTest::newRow("true") << raw("\xf5") << QVariant(true);
    QTest::newRow("null") << raw("\xf6") << QVariant::fromValue<void *>(nullptr);
    QTest::newRow("undefined") << raw("\xf7") << QVariant();
    QTest::newRow("simple32") << raw("\xf8\x20") << QVariant::fromValue(SimpleType{32});
    QTest::newRow("simple255") << raw("\xf8\xff") << QVariant::fromValue(SimpleType{255});

    // floating point
    QTest::newRow("0f16") << raw("\xf9\0\0") << QVariant::fromValue(Float16Standin{0x0000});

    QTest::newRow("0.f") << raw("\xfa\0\0\0\0") << QVariant::fromValue(0.f);
    QTest::newRow("0.")  << raw("\xfb\0\0\0\0\0\0\0\0") << QVariant(0.);
    QTest::newRow("-1.f") << raw("\xfa\xbf\x80\0\0") << QVariant::fromValue(-1.f);
    QTest::newRow("-1.") << raw("\xfb\xbf\xf0\0\0\0\0\0\0") << QVariant(-1.);
    QTest::newRow("16777215.f") << raw("\xfa\x4b\x7f\xff\xff") << QVariant::fromValue(16777215.f);
    QTest::newRow("16777215.") << raw("\xfb\x41\x6f\xff\xff\xe0\0\0\0") << QVariant::fromValue(16777215.);
    QTest::newRow("-16777215.f") << raw("\xfa\xcb\x7f\xff\xff") << QVariant(-16777215.f);
    QTest::newRow("-16777215.") << raw("\xfb\xc1\x6f\xff\xff\xe0\0\0\0") << QVariant::fromValue(-16777215.);

    QTest::newRow("qnan_f") << raw("\xfa\xff\xc0\0\0") << QVariant::fromValue<float>(qQNaN());
    QTest::newRow("qnan") << raw("\xfb\xff\xf8\0\0\0\0\0\0") << QVariant(qQNaN());
    QTest::newRow("snan_f") << raw("\xfa\x7f\xc0\0\0") << QVariant::fromValue<float>(qSNaN());
    QTest::newRow("snan") << raw("\xfb\x7f\xf8\0\0\0\0\0\0") << QVariant(qSNaN());
    QTest::newRow("-inf_f") << raw("\xfa\xff\x80\0\0") << QVariant::fromValue<float>(-qInf());
    QTest::newRow("-inf") << raw("\xfb\xff\xf0\0\0\0\0\0\0") << QVariant(-qInf());
    QTest::newRow("+inf_f") << raw("\xfa\x7f\x80\0\0") << QVariant::fromValue<float>(qInf());
    QTest::newRow("+inf") << raw("\xfb\x7f\xf0\0\0\0\0\0\0") << QVariant(qInf());
}

void addStringsData()
{
    // byte strings
    QTest::newRow("emptybytestring") << raw("\x40") << QVariant(QByteArray(""));
    QTest::newRow("bytestring1") << raw("\x41 ") << QVariant(QByteArray(" "));
    QTest::newRow("bytestring1-nul") << raw("\x41\0") << QVariant(QByteArray("", 1));
    QTest::newRow("bytestring5") << raw("\x45Hello") << QVariant(QByteArray("Hello"));
    QTest::newRow("bytestring24") << raw("\x58\x18""123456789012345678901234")
                                  << QVariant(QByteArray("123456789012345678901234"));
    QTest::newRow("bytestring256") << raw("\x59\1\0") + QByteArray(256, '3')
                                   << QVariant(QByteArray(256, '3'));

    // text strings
    QTest::newRow("emptytextstring") << raw("\x60") << QVariant("");
    QTest::newRow("textstring1") << raw("\x61 ") << QVariant(" ");
    QTest::newRow("textstring1-nul") << raw("\x61\0") << QVariant(QString::fromLatin1("", 1));
    QTest::newRow("textstring5") << raw("\x65Hello") << QVariant("Hello");
    QTest::newRow("textstring24") << raw("\x78\x18""123456789012345678901234")
                                  << QVariant("123456789012345678901234");
    QTest::newRow("textstring256") << raw("\x79\1\0") + QByteArray(256, '3')
                                   << QVariant(QString(256, '3'));
}

void tst_Encoder::fixed_data()
{
    addColumns();
    addFixedData();
}

void tst_Encoder::fixed()
{
    QFETCH(QVariant, input);
    QFETCH(QByteArray, output);
    compare(input, output);
}

void tst_Encoder::strings_data()
{
    addColumns();
    addStringsData();
}

void tst_Encoder::tags_data()
{
    addColumns();
    addFixedData();
    addStringsData();
}

void tst_Encoder::tags()
{
    QFETCH(QVariant, input);
    QFETCH(QByteArray, output);

    compare(QVariant::fromValue(Tag{1, input}), "\xc1" + output);
    if (compareFailed) return;

    compare(QVariant::fromValue(Tag{24, input}), "\xd8\x18" + output);
    if (compareFailed) return;

    compare(QVariant::fromValue(Tag{255, input}), "\xd8\xff" + output);
    if (compareFailed) return;

    compare(QVariant::fromValue(Tag{256, input}), raw("\xd9\1\0") + output);
    if (compareFailed) return;

    compare(QVariant::fromValue(Tag{CborSignatureTag, input}), raw("\xd9\xd9\xf7") + output);
    if (compareFailed) return;

    compare(QVariant::fromValue(Tag{65535, input}), raw("\xd9\xff\xff") + output);
    if (compareFailed) return;

    compare(QVariant::fromValue(Tag{65536, input}), raw("\xda\0\1\0\0") + output);
    if (compareFailed) return;

    compare(QVariant::fromValue(Tag{UINT32_MAX, input}), raw("\xda\xff\xff\xff\xff") + output);
    if (compareFailed) return;

    compare(QVariant::fromValue(Tag{UINT32_MAX + Q_UINT64_C(1), input}), raw("\xdb\0\0\0\1\0\0\0\0") + output);
    if (compareFailed) return;

    compare(QVariant::fromValue(Tag{UINT64_MAX, input}), raw("\xdb\xff\xff\xff\xff\xff\xff\xff\xff") + output);
    if (compareFailed) return;

    // nested tags
    compare(QVariant::fromValue(Tag{1, QVariant::fromValue(Tag{1, input})}), "\xc1\xc1" + output);
}

QTEST_MAIN(tst_Encoder)
#include "tst_encoder.moc"
