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

#define _BSD_SOURCE
#include "cbor.h"
#include "cborconstants_p.h"
#include "compilersupport_p.h"

#include <assert.h>
#include <endian.h>
#include <stdlib.h>
#include <string.h>

void cbor_encoder_init(CborEncoder *encoder, char *buffer, size_t size, int flags)
{
    encoder->ptr = buffer;
    encoder->end = buffer + size;
    encoder->flags = flags;
}

static inline void put16(char *where, uint16_t v)
{
    v = htobe16(v);
    memcpy(where, &v, sizeof(v));
}

static inline void put32(char *where, uint32_t v)
{
    v = htobe32(v);
    memcpy(where, &v, sizeof(v));
}

static inline void put64(char *where, uint64_t v)
{
    v = htobe64(v);
    memcpy(where, &v, sizeof(v));
}

static inline CborError append_to_buffer(CborEncoder *encoder, const char *data, size_t len)
{
    if (encoder->ptr + len > encoder->end)
        return CborErrorOutOfMemory;
    memcpy(encoder->ptr, data, len);
    encoder->ptr += len;
    return CborNoError;
}

static inline CborError encode_number(CborEncoder *encoder, uint64_t ui, uint8_t shiftedMajorType)
{
    /* Little-endian would have been so much more convenient here:
     * We could just write at the beginning of buf but append_to_buffer
     * only the necessary bytes.
     * Since it has to be big endian, do it the other way around:
     * write from the end. */
    char buf[1 + sizeof(ui)];
    char *const bufend = buf + sizeof(buf);
    char *bufstart = bufend - 1;
    put64(buf + 1, ui);     // we probably have a bunch of zeros in the beginning

    if (ui < Value8Bit) {
        *bufstart += shiftedMajorType;
    } else {
        unsigned more = 0;
        if (ui > UINT8_MAX)
            ++more;
        if (ui > UINT16_MAX)
            ++more;
        if (ui != (uint32_t)ui)
            ++more;
        bufstart -= 1 << more;
        *bufstart = shiftedMajorType + Value8Bit + more;
    }

    return append_to_buffer(encoder, bufstart, bufend - bufstart);
}

CborError cbor_encode_uint(CborEncoder *encoder, uint64_t value)
{
    return encode_number(encoder, value, UnsignedIntegerType << MajorTypeShift);
}

CborError cbor_encode_int(CborEncoder *encoder, int64_t value)
{
    // adapted from code in RFC 7049 appendix C (pseudocode)
    uint64_t ui = value >> 63;              // extend sign to whole length
    uint8_t majorType = ui & 0x20;          // extract major type
    ui ^= value;                            // complement negatives
    return encode_number(encoder, ui, majorType);
}

CborError cbor_encode_simple_value(CborEncoder *encoder, uint8_t value)
{
#ifndef CBOR_ENCODER_NO_CHECK_USER
    // check if this is a valid simple type
    if (value >= HalfPrecisionFloat && value <= Break)
        return CborErrorIllegalSimpleType;
#endif
    return encode_number(encoder, value, SimpleTypesType << MajorTypeShift);
}

CborError cbor_encode_half_float(CborEncoder *encoder, const void *value)
{
    char buf[1 + sizeof(uint16_t)] = { CborHalfFloatType };
    put16(buf + 1, *(const uint16_t*)value);
    return append_to_buffer(encoder, buf, sizeof(buf));
}

CborError cbor_encode_float(CborEncoder *encoder, const float *value)
{
    char buf[1 + sizeof(uint32_t)] = { CborFloatType };
    put32(buf + 1, *(const uint32_t*)value);
    return append_to_buffer(encoder, buf, sizeof(buf));
}

CborError cbor_encode_double(CborEncoder *encoder, const double *value)
{
    char buf[1 + sizeof(uint64_t)] = { CborDoubleType };
    put64(buf + 1, *(const uint64_t*)value);
    return append_to_buffer(encoder, buf, sizeof(buf));
}

CborError cbor_encode_tag(CborEncoder *encoder, CborTag tag)
{
    return encode_number(encoder, tag, TagType << MajorTypeShift);
}

static CborError encode_string(CborEncoder *encoder, size_t length, uint8_t shiftedMajorType, const char *string)
{
    CborError err = encode_number(encoder, length, shiftedMajorType);
    if (err)
        return err;
    return append_to_buffer(encoder, string, length);
}

CborError cbor_encode_byte_string(CborEncoder *encoder, const char *string, size_t length)
{
    return encode_string(encoder, length, ByteStringType << MajorTypeShift, string);
}

CborError cbor_encode_text_string(CborEncoder *encoder, const char *string, size_t length)
{
    return encode_string(encoder, length, TextStringType << MajorTypeShift, string);
}

static CborError create_container(CborEncoder *encoder, size_t length, uint8_t shiftedMajorType, CborEncoder *container)
{
    CborError err = encode_number(encoder, length, shiftedMajorType);
    if (err)
        return err;
    *container = *encoder;
    return CborNoError;
}

CborError cbor_encoder_create_array(CborEncoder *encoder, CborEncoder *arrayEncoder, size_t length)
{
    return create_container(encoder, length, ArrayType << MajorTypeShift, arrayEncoder);
}

CborError cbor_encoder_create_map(CborEncoder *encoder, CborEncoder *mapEncoder, size_t length)
{
    return create_container(encoder, length, MapType << MajorTypeShift, mapEncoder);
}

CborError cbor_encoder_close_container(CborEncoder *encoder, const CborEncoder *containerEncoder)
{
    *encoder = *containerEncoder;
    return CborNoError;
}
