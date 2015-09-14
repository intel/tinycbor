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

#define _BSD_SOURCE 1
#define _GNU_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "cbor.h"
#include "cborjson.h"
#include "compilersupport_p.h"

#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CborError value_to_json(FILE *out, CborValue *it, int flags, CborType type);

static CborError dump_bytestring_base16(char **result, CborValue *it)
{
    static const char characters[] = "0123456789abcdef";
    size_t n = 0;
    uint8_t *buffer;
    CborError err = cbor_value_calculate_string_length(it, &n);
    if (err)
        return err;

    // a Base16 (hex) output is twice as big as our buffer
    buffer = (uint8_t *)malloc(n * 2 + 1);
    *result = (char *)buffer;

    // let cbor_value_copy_byte_string know we have an extra byte for the terminating NUL
    ++n;
    err = cbor_value_copy_byte_string(it, buffer + n - 1, &n, it);
    assert(err == CborNoError);

    for (size_t i = 0; i < n; ++i) {
        uint8_t byte = buffer[n + i];
        buffer[2*i]     = characters[byte >> 4];
        buffer[2*i + 1] = characters[byte & 0xf];
    }
    return CborNoError;
}

static CborError generic_dump_base64(char **result, CborValue *it, const char alphabet[65])
{
    size_t n = 0;
    uint8_t *buffer, *out, *in;
    CborError err = cbor_value_calculate_string_length(it, &n);
    if (err)
        return err;

    // a Base64 output (untruncated) has 4 bytes for every 3 in the input
    size_t len = (n + 5) / 3 * 4;
    out = buffer = (uint8_t *)malloc(len + 1);
    *result = (char *)buffer;

    // we read our byte string at the tail end of the buffer
    // so we can do an in-place conversion while iterating forwards
    in = buffer + len - n;

    // let cbor_value_copy_byte_string know we have an extra byte for the terminating NUL
    ++n;
    err = cbor_value_copy_byte_string(it, in, &n, it);
    assert(err == CborNoError);

    uint_least32_t val;
    for ( ; n >= 3; n -= 3, in += 3) {
        // read 3 bytes x 8 bits = 24 bits
        val = (in[0] << 16) | (in[1] << 8) | in[2];

        // write 4 chars x 6 bits = 24 bits
        *out++ = alphabet[(val >> 18) & 0x3f];
        *out++ = alphabet[(val >> 12) & 0x3f];
        *out++ = alphabet[(val >> 6) & 0x3f];
        *out++ = alphabet[val & 0x3f];
    }

    // maybe 1 or 2 bytes left
    if (n) {
        val = in[0] << 16;

        // the 65th character in the alphabet is our filler: either '=' or '\0'
        out[4] = '\0';
        out[3] = alphabet[64];
        if (n == 2) {
            // read 2 bytes x 8 bits = 16 bits
            val |= (in[1] << 8);

            // write the third char in 3 chars x 6 bits = 18 bits
            out[2] = alphabet[(val >> 6) & 0x3f];
        } else {
            out[2] = alphabet[64];  // filler
        }
        out[1] = alphabet[(val >> 12) & 0x3f];
        out[0] = alphabet[(val >> 18) & 0x3f];
    } else {
        out[0] = '\0';
    }

    return CborNoError;
}

static CborError dump_bytestring_base64(char **result, CborValue *it)
{
    static const char alphabet[] = "ABCDEFGH" "IJKLMNOP" "QRSTUVWX" "YZabcdef"
                                   "ghijklmn" "opqrstuv" "wxyz0123" "456789+/" "=";
    return generic_dump_base64(result, it, alphabet);
}

static CborError dump_bytestring_base64url(char **result, CborValue *it)
{
    static const char alphabet[] = "ABCDEFGH" "IJKLMNOP" "QRSTUVWX" "YZabcdef"
                                   "ghijklmn" "opqrstuv" "wxyz0123" "456789-_";
    return generic_dump_base64(result, it, alphabet);
}

static CborError find_tagged_type(CborValue *it, CborTag *tag, CborType *type)
{
    CborError err = CborNoError;
    *type = cbor_value_get_type(it);
    while (*type == CborTagType) {
        cbor_value_get_tag(it, tag);    // can't fail
        err = cbor_value_advance_fixed(it);
        if (err)
            return err;

        *type = cbor_value_get_type(it);
    }
    return err;
}

static CborError tagged_value_to_json(FILE *out, CborValue *it, int flags)
{
    CborTag tag;
    cbor_value_get_tag(it, &tag);       // can't fail
    CborError err = cbor_value_advance_fixed(it);
    if (err)
        return err;

    if (flags & CborConvertTagsToObjects) {
        if (fprintf(out, "{\"tag%" PRIu64 "\":", tag) < 0)
            return CborErrorIO;

        err = value_to_json(out, it, flags, cbor_value_get_type(it));
        if (err)
            return err;
        if (fputc('}', out) < 0)
            return CborErrorIO;
        return CborNoError;
    }

    CborType type;
    err = find_tagged_type(it, &tag, &type);
    if (err)
        return err;

    // special handling of byte strings?
    if (type == CborByteStringType && (flags & CborConvertByteStringsToBase64Url) == 0 &&
            (tag == CborNegativeBignumTag || tag == CborExpectedBase16Tag || tag == CborExpectedBase64Tag)) {
        char *str;
        char *pre = "";

        if (tag == CborNegativeBignumTag) {
            pre = "~";
            err = dump_bytestring_base64url(&str, it);
        } else if (tag == CborExpectedBase64Tag) {
            err = dump_bytestring_base64(&str, it);
        } else { // tag == CborExpectedBase16Tag
            err = dump_bytestring_base16(&str, it);
        }
        if (err)
            return err;
        err = fprintf(out, "\"%s%s\"", pre, str) < 0 ? CborErrorIO : CborNoError;
        free(str);
        return err;
    }

    // no special handling
    return value_to_json(out, it, flags, type);
}

static CborError stringify_map_key(char **key, CborValue *it, int flags, CborType type)
{
    (void)flags;    // unused
    (void)type;     // unused
    size_t size;

    FILE *memstream = open_memstream(key, &size);
    if (memstream == NULL)
        return CborErrorOutOfMemory;        // could also be EMFILE, but it's unlikely
    CborError err = cbor_value_to_pretty_advance(memstream, it);

    if (unlikely(fclose(memstream) < 0 || *key == NULL))
        return CborErrorInternalError;
    return err;
}

static CborError array_to_json(FILE *out, CborValue *it, int flags)
{
    const char *comma = "";
    while (!cbor_value_at_end(it)) {
        if (fprintf(out, "%s", comma) < 0)
            return CborErrorIO;
        comma = ",";

        CborError err = value_to_json(out, it, flags, cbor_value_get_type(it));
        if (err)
            return err;
    }
    return CborNoError;
}

static CborError map_to_json(FILE *out, CborValue *it, int flags)
{
    const char *comma = "";
    CborError err;
    while (!cbor_value_at_end(it)) {
        char *key;
        if (fprintf(out, "%s", comma) < 0)
            return CborErrorIO;
        comma = ",";

        CborType keyType = cbor_value_get_type(it);
        if (likely(keyType == CborTextStringType)) {
            size_t n = 0;
            err = cbor_value_dup_text_string(it, &key, &n, it);
        } else if (flags & CborConvertStringifyMapKeys) {
            err = stringify_map_key(&key, it, flags, keyType);
        } else {
            return CborErrorJsonObjectKeyNotString;
        }
        if (err)
            return err;

        // first, print the key
        if (fprintf(out, "\"%s\":", key) < 0)
            return CborErrorIO;

        // then, print the value
        err = value_to_json(out, it, flags, cbor_value_get_type(it));

        free(key);
        if (err)
            return err;
    }
    return CborNoError;
}

static CborError value_to_json(FILE *out, CborValue *it, int flags, CborType type)
{
    CborError err;
    switch (type) {
    case CborArrayType:
    case CborMapType: {
        // recursive type
        CborValue recursed;
        err = cbor_value_enter_container(it, &recursed);
        if (err) {
            it->ptr = recursed.ptr;
            return err;       // parse error
        }
        if (fputc(type == CborArrayType ? '[' : '{', out) < 0)
            return CborErrorIO;

        err = (type == CborArrayType) ?
                  array_to_json(out, &recursed, flags) :
                  map_to_json(out, &recursed, flags);
        if (err) {
            it->ptr = recursed.ptr;
            return err;       // parse error
        }

        if (fputc(type == CborArrayType ? ']' : '}', out) < 0)
            return CborErrorIO;
        err = cbor_value_leave_container(it, &recursed);
        if (err)
            return err;       // parse error

        return CborNoError;
    }

    case CborIntegerType: {
        double num;     // JS numbers are IEEE double precision
        uint64_t val;
        cbor_value_get_raw_integer(it, &val);    // can't fail
        num = val;

        if (cbor_value_is_negative_integer(it)) {
            num = -num - 1;                     // convert to negative
        }
        if (fprintf(out, "%.0f", num) < 0)  // this number has no fraction, so no decimal points please
            return CborErrorIO;
        break;
    }

    case CborByteStringType:
    case CborTextStringType: {
        char *str;
        if (type == CborByteStringType) {
            err = dump_bytestring_base64url(&str, it);
        } else {
            size_t n = 0;
            err = cbor_value_dup_text_string(it, &str, &n, it);
        }
        if (err)
            return err;
        err = (fprintf(out, "\"%s\"", str) < 0) ? CborErrorIO : CborNoError;
        free(str);
        return err;
    }

    case CborTagType:
        return tagged_value_to_json(out, it, flags);

    case CborSimpleType: {
        uint8_t simple_type;
        cbor_value_get_simple_type(it, &simple_type);  // can't fail
        if (fprintf(out, "\"simple(%" PRIu8 ")\"", simple_type) < 0)
            return CborErrorIO;
        break;
    }

    case CborNullType:
        if (fprintf(out, "null") < 0)
            return CborErrorIO;
        break;

    case CborUndefinedType:
        if (fprintf(out, "\"undefined\"") < 0)
            return CborErrorIO;
        break;

    case CborBooleanType: {
        bool val;
        cbor_value_get_boolean(it, &val);       // can't fail
        if (fprintf(out, val ? "true" : "false") < 0)
            return CborErrorIO;
        break;
    }

    case CborDoubleType: {
        double val;
        if (false) {
            float f;
    case CborFloatType:
            cbor_value_get_float(it, &f);
            val = f;
        } else {
            cbor_value_get_double(it, &val);
        }

        if (isinf(val) || isnan(val)) {
            if (fprintf(out, "null") < 0)
                return CborErrorIO;
        } else {
            uint64_t ival = (uint64_t)fabs(val);
            int r;
            if ((double)ival == fabs(val)) {
                // print as integer so we get the full precision
                r = fprintf(out, "%s%" PRIu64, val < 0 ? "-" : "", ival);
            } else {
                // this number is definitely not a 64-bit integer
                r = fprintf(out, "%." DBL_DECIMAL_DIG_STR "g", val);
            }
            if (r < 0)
                return CborErrorIO;
        }
        break;
    }

    case CborHalfFloatType:
        return CborErrorUnsupportedType;

    case CborInvalidType:
        return CborErrorUnknownType;
    }

    return cbor_value_advance_fixed(it);
}

CborError cbor_value_to_json_advance(FILE *out, CborValue *value, int flags)
{
    return value_to_json(out, value, flags, cbor_value_get_type(value));
}
