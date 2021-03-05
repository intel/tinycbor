/****************************************************************************
**
** Copyright (C) 2018 Intel Corporation
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
#define _DEFAULT_SOURCE 1
#define _GNU_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#ifndef __STDC_LIMIT_MACROS
#  define __STDC_LIMIT_MACROS 1
#endif

#include "cbor.h"
#include "cborjson.h"
#include "cborinternal_p.h"
#include "compilersupport_p.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/**
 * \defgroup CborToJson Converting CBOR to JSON
 * \brief Group of functions used to convert CBOR to JSON.
 *
 * This group contains two functions that can be used to convert a \ref
 * CborValue object to an equivalent JSON representation. This module attempts
 * to follow the recommendations from RFC 7049 section 4.1 "Converting from
 * CBOR to JSON", though it has a few differences. They are noted below.
 *
 * These functions produce a "minified" JSON output, with no spacing,
 * indentation or line breaks. If those are necessary, they need to be applied
 * in a post-processing phase.
 *
 * Note that JSON cannot support all CBOR types with fidelity, so the
 * conversion is usually lossy. For that reason, TinyCBOR supports adding a set
 * of metadata JSON values that can be used by a JSON-to-CBOR converter to
 * restore the original data types.
 *
 * The TinyCBOR library does not provide a way to convert from JSON
 * representation back to encoded form. However, it provides a tool called
 * \c json2cbor which can be used for that purpose. That tool supports the
 * metadata format that these functions may produce.
 *
 * Either of the functions in this section will attempt to convert exactly one
 * CborValue object to JSON. Those functions may return any error documented
 * for the functions for CborParsing. In addition, if the C standard library
 * stream functions return with error, the text conversion will return with
 * error CborErrorIO.
 *
 * These functions also perform UTF-8 validation in CBOR text strings. If they
 * encounter a sequence of bytes that is not permitted in UTF-8, they will return
 * CborErrorInvalidUtf8TextString. That includes encoding of surrogate points
 * in UTF-8.
 *
 * \warning The metadata produced by these functions is not guaranteed to
 * remain stable. A future update of TinyCBOR may produce different output for
 * the same input and parsers may be unable to handle it.
 *
 * \sa CborParsing, CborPretty, cbor_parser_init()
 */

/**
 * \addtogroup CborToJson
 * @{
 * <h2 class="groupheader">Conversion limitations</h2>
 *
 * When converting from CBOR to JSON, there may be information loss. This
 * section lists the possible scenarios.
 *
 * \par Number precision:
 * ALL JSON numbers, due to its JavaScript heritage, are IEEE 754
 * double-precision floating point. This means JSON is not capable of
 * representing all integers numbers outside the range [-(2<sup>53</sup>)+1,
 * 2<sup>53</sup>-1] and is not capable of representing NaN or infinite. If the
 * CBOR data contains a number outside the valid range, the conversion will
 * lose precision. If the input was NaN or infinite, the result of the
 * conversion will be the JSON null value. In addition, the distinction between
 * half-, single- and double-precision is lost.
 *
 * \par
 * If enabled, the original value and original type are stored in the metadata.
 *
 * \par Non-native types:
 * CBOR's type system is richer than JSON's, which means some data values
 * cannot be represented when converted to JSON. The conversion silently turns
 * them into strings: CBOR simple types become "simple(nn)" where \c nn is the
 * simple type's value, with the exception of CBOR undefined, which becomes
 * "undefined", while CBOR byte strings are converted to an Base16, Base64, or
 * Base64url encoding
 *
 * \par
 * If enabled, the original type is stored in the metadata.
 *
 * \par Presence of tags:
 * JSON has no support for tagged values, so by default tags are dropped when
 * converting to JSON. However, if the CborConvertObeyByteStringTags option is
 * active (default), then certain known tags are honored and are used to format
 * the conversion of the tagged byte string to JSON.
 *
 * \par
 * If the CborConvertTagsToObjects option is active, then the tag and the
 * tagged value are converted to a JSON object. Otherwise, if enabled, the
 * last (innermost) tag is stored in the metadata.
 *
 * \par Non-string keys in maps:
 * JSON requires all Object keys to be strings, while CBOR does not. By
 * default, if a non-string key is found, the conversion fails with error
 * CborErrorJsonObjectKeyNotString. If the CborConvertStringifyMapKeys option
 * is active, then the conversion attempts to create a string representation
 * using CborPretty. Note that the \c json2cbor tool is not able to parse this
 * back to the original form.
 *
 * \par Duplicate keys in maps:
 * Neither JSON nor CBOR allow duplicated keys, but current TinyCBOR does not
 * validate that this is the case. If there are duplicated keys in the input,
 * they will be repeated in the output, which many JSON tools may flag as
 * invalid. In addition to that, if the CborConvertStringifyMapKeys option is
 * active, it is possible that a non-string key in a CBOR map will be converted
 * to a string form that is identical to another key.
 *
 * \par
 * When metadata support is active, the conversion will add extra key-value
 * pairs to the JSON output so it can store the metadata. It is possible that
 * the keys for the metadata clash with existing keys in the JSON map.
 */

#define IF_NO_ERROR(e, proc) do { if(!e) { e = proc; } } while(0)

enum ConversionStatusFlags {
    TypeWasNotNative            = 0x100,    /* anything but strings, boolean, null, arrays and maps */
    TypeWasTagged               = 0x200,
    NumberPrecisionWasLost      = 0x400,
    NumberWasNaN                = 0x800,
    NumberWasInfinite           = 0x1000,
    NumberWasNegative           = 0x2000,   /* only used with NumberWasInifite or NumberWasTooBig */

    FinalTypeMask               = 0xff
};

typedef struct ConversionStatus {
    CborTag lastTag;
    uint64_t originalNumber;
    int flags;
} ConversionStatus;

static CborError value_to_json(CborStreamFunction stream, void *token, CborValue *it, int flags, CborType type, ConversionStatus *status);

static CborError dump_bytestring_base16(char **result, CborValue *it)
{
    static const char characters[] = "0123456789abcdef";
    size_t i;
    size_t n = 0;
    uint8_t *buffer;
    CborError err = cbor_value_calculate_string_length(it, &n);
    if (err)
        return err;

    /* a Base16 (hex) output is twice as big as our buffer */
    buffer = (uint8_t *)malloc(n * 2 + 1);
    if (buffer == NULL)
        /* out of memory */
        return CborErrorOutOfMemory;

    *result = (char *)buffer;

    /* let cbor_value_copy_byte_string know we have an extra byte for the terminating NUL */
    ++n;
    err = cbor_value_copy_byte_string(it, buffer + n - 1, &n, it);
    cbor_assert(err == CborNoError);

    for (i = 0; i < n; ++i) {
        uint8_t byte = buffer[n + i];
        buffer[2*i]     = characters[byte >> 4];
        buffer[2*i + 1] = characters[byte & 0xf];
    }
    return CborNoError;
}

static CborError generic_dump_base64(char **result, CborValue *it, const char alphabet[65])
{
    size_t n = 0, i;
    uint8_t *buffer, *out, *in;
    CborError err = cbor_value_calculate_string_length(it, &n);
    if (err)
        return err;

    /* a Base64 output (untruncated) has 4 bytes for every 3 in the input */
    size_t len = (n + 5) / 3 * 4;
    buffer = (uint8_t *)malloc(len + 1);
    if (buffer == NULL)
        /* out of memory */
        return CborErrorOutOfMemory;

    out = buffer;
    *result = (char *)buffer;

    /* we read our byte string at the tail end of the buffer
     * so we can do an in-place conversion while iterating forwards */
    in = buffer + len - n;

    /* let cbor_value_copy_byte_string know we have an extra byte for the terminating NUL */
    ++n;
    err = cbor_value_copy_byte_string(it, in, &n, it);
    cbor_assert(err == CborNoError);

    uint_least32_t val = 0;
    for (i = 0; n - i >= 3; i += 3) {
        /* read 3 bytes x 8 bits = 24 bits */
        if (false) {
#ifdef __GNUC__
        } else if (i) {
            __builtin_memcpy(&val, in + i - 1, sizeof(val));
            val = cbor_ntohl(val);
#endif
        } else {
            val = (in[i] << 16) | (in[i + 1] << 8) | in[i + 2];
        }

        /* write 4 chars x 6 bits = 24 bits */
        *out++ = alphabet[(val >> 18) & 0x3f];
        *out++ = alphabet[(val >> 12) & 0x3f];
        *out++ = alphabet[(val >> 6) & 0x3f];
        *out++ = alphabet[val & 0x3f];
    }

    /* maybe 1 or 2 bytes left */
    if (n - i) {
        /* we can read in[i + 1] even if it's past the end of the string because
         * we know (by construction) that it's a NUL byte */
#ifdef __GNUC__
        uint16_t val16;
        __builtin_memcpy(&val16, in + i, sizeof(val16));
        val = cbor_ntohs(val16);
#else
        val = (in[i] << 8) | in[i + 1];
#endif
        val <<= 8;

        /* the 65th character in the alphabet is our filler: either '=' or '\0' */
        out[4] = '\0';
        out[3] = alphabet[64];
        if (n - i == 2) {
            /* write the third char in 3 chars x 6 bits = 18 bits */
            out[2] = alphabet[(val >> 6) & 0x3f];
        } else {
            out[2] = alphabet[64];  /* filler */
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

static CborError add_value_metadata(CborStreamFunction stream, void *out, CborType type, const ConversionStatus *status)
{
    int flags = status->flags;
    if (flags & TypeWasTagged) {
        /* extract the tagged type, which may be JSON native */
        type = flags & FinalTypeMask;
        flags &= ~(FinalTypeMask | TypeWasTagged);

        if (stream(out, "\"tag\":\"%" PRIu64 "\"%s", status->lastTag,
                    flags & ~TypeWasTagged ? "," : ""))
            return CborErrorIO;
    }

    if (!flags)
        return CborNoError;

    /* print at least the type */
    if (stream(out, "\"t\":%d", type))
        return CborErrorIO;

    if (flags & NumberWasNaN)
        if (stream(out, ",\"v\":\"nan\""))
            return CborErrorIO;
    if (flags & NumberWasInfinite)
        if (stream(out, ",\"v\":\"%sinf\"", flags & NumberWasNegative ? "-" : ""))
            return CborErrorIO;
    if (flags & NumberPrecisionWasLost)
        if (stream(out, ",\"v\":\"%c%" PRIx64 "\"", flags & NumberWasNegative ? '-' : '+',
                    status->originalNumber))
            return CborErrorIO;
    if (type == CborSimpleType)
        if (stream(out, ",\"v\":%d", (int)status->originalNumber))
            return CborErrorIO;
    return CborNoError;
}

static CborError find_tagged_type(CborValue *it, CborTag *tag, CborType *type)
{
    CborError err = CborNoError;
    *type = cbor_value_get_type(it);
    while (*type == CborTagType) {
        cbor_value_get_tag(it, tag);    /* can't fail */
        err = cbor_value_advance_fixed(it);
        if (err)
            return err;

        *type = cbor_value_get_type(it);
    }
    return err;
}

static CborError tagged_value_to_json(CborStreamFunction stream, void *out, CborValue *it, int flags, ConversionStatus *status)
{
    CborTag tag;
    CborError err;

    if (flags & CborConvertTagsToObjects) {
        cbor_value_get_tag(it, &tag);       /* can't fail */
        err = cbor_value_advance_fixed(it);
        if (err)
            return err;

        if (stream(out, "{\"tag%" PRIu64 "\":", tag))
            return CborErrorIO;

        CborType type = cbor_value_get_type(it);
        err = value_to_json(stream, out, it, flags, type, status);
        if (err)
            return err;
        if (flags & CborConvertAddMetadata && status->flags) {
            if (stream(out, ",\"tag%" PRIu64 "$cbor\":{", tag) ||
                    add_value_metadata(stream, out, type, status) != CborNoError ||
                    stream(out, "%c", '}'))
                return CborErrorIO;
        }
        if (stream(out, "%c", '}'))
            return CborErrorIO;
        status->flags = TypeWasNotNative | CborTagType;
        return CborNoError;
    }

    CborType type;
    err = find_tagged_type(it, &status->lastTag, &type);
    if (err)
        return err;
    tag = status->lastTag;

    /* special handling of byte strings? */
    if (type == CborByteStringType && (flags & CborConvertByteStringsToBase64Url) == 0 &&
            (tag == CborNegativeBignumTag || tag == CborExpectedBase16Tag || tag == CborExpectedBase64Tag)) {
        char *str;
        const char *pre = "";

        if (tag == CborNegativeBignumTag) {
            pre = "~";
            err = dump_bytestring_base64url(&str, it);
        } else if (tag == CborExpectedBase64Tag) {
            err = dump_bytestring_base64(&str, it);
        } else { /* tag == CborExpectedBase16Tag */
            err = dump_bytestring_base16(&str, it);
        }
        if (err)
            return err;
        err = stream(out, "\"%s%s\"", pre, str);
        free(str);
        status->flags = TypeWasNotNative | TypeWasTagged | CborByteStringType;
        return err;
    }

    /* no special handling */
    err = value_to_json(stream, out, it, flags, type, status);
    status->flags |= TypeWasTagged | type;
    return err;
}

static CborError array_to_json(CborStreamFunction stream, void *out, CborValue *it, int flags, ConversionStatus *status)
{
    const char *comma = "";
    while (!cbor_value_at_end(it)) {
        if (stream(out, "%s", comma))
            return CborErrorIO;
        comma = ",";

        CborError err = value_to_json(stream, out, it, flags, cbor_value_get_type(it), status);
        if (err)
            return err;
    }
    return CborNoError;
}

static CborError put_string_to_stream(CborStreamFunction stream, void* out, CborValue *it)
{
    char *string = NULL;
    size_t n = 0;
    CborError err = cbor_value_dup_text_string(it, &string, &n, it);
    if (err)
        return err;

    err = stream(out, "%s", string);
    free(string);

    return err;
}

static CborError map_to_json(CborStreamFunction stream, void *out, CborValue *it, int flags, ConversionStatus *status)
{
    const char *comma = "";
    CborError err = CborNoError;
    while (!cbor_value_at_end(it)) {
        /* Remember the iterator position for re-read the key value */
        CborValue it_key = *it;

        if (stream(out, "%s", comma))
            return CborErrorIO;
        comma = ",";

        /* first, print the key */
        CborType keyType = cbor_value_get_type(it);
        if (likely(keyType == CborTextStringType)) {
            IF_NO_ERROR(err, stream(out, "\""));
            IF_NO_ERROR(err, put_string_to_stream(stream, out, it) );
            IF_NO_ERROR(err, stream(out, "\":"));

            if (err)
                return CborErrorIO;
        } else if (flags & CborConvertStringifyMapKeys) {
            IF_NO_ERROR(err, stream(out, "\""));
            IF_NO_ERROR(err, cbor_value_to_pretty_stream(stream, out, it, CborPrettyDefaultFlags));
            IF_NO_ERROR(err, stream(out, "\":"));
            if (err)
                return err;
        } else {
            return CborErrorJsonObjectKeyNotString;
        }

        /* then, print the value */
        CborType valueType = cbor_value_get_type(it);
        err = value_to_json(stream, out, it, flags, valueType, status);

        /* finally, print any metadata we may have */
        if (flags & CborConvertAddMetadata) {
            if (!err && keyType != CborTextStringType) {
                /* Might be read the key again in the next if-clause, keep this. */
                CborValue tmpval = it_key;
                IF_NO_ERROR(err, stream(out, ",\""));
                IF_NO_ERROR(err, cbor_value_to_pretty_stream(stream, out, &tmpval, CborPrettyDefaultFlags));
                IF_NO_ERROR(err, stream(out, "$keycbordump\":true"));

                if(err)
                    err = CborErrorIO;
            }
            if (!err && status->flags) {
                IF_NO_ERROR(err, stream(out, ",\""));
                if (keyType == CborTextStringType) {
                    IF_NO_ERROR(err, put_string_to_stream(stream, out, &it_key));
                } else {
                    IF_NO_ERROR(err, cbor_value_to_pretty_stream(stream, out, &it_key, CborPrettyDefaultFlags));
                }
                IF_NO_ERROR(err, stream(out, "$cbor\":{"));
                IF_NO_ERROR(err, add_value_metadata(stream, out, valueType, status));
                IF_NO_ERROR(err, stream(out, "%c", '}'));

                if(err)
                    err = CborErrorIO;
            }
        }

        if (err)
            return err;
    }
    return CborNoError;
}

static CborError value_to_json(CborStreamFunction stream, void *out, CborValue *it, int flags, CborType type, ConversionStatus *status)
{
    CborError err;
    status->flags = 0;

    switch (type) {
    case CborArrayType:
    case CborMapType: {
        /* recursive type */
        CborValue recursed;
        err = cbor_value_enter_container(it, &recursed);
        if (err) {
            it->ptr = recursed.ptr;
            return err;       /* parse error */
        }
        if (stream(out, "%c", (type == CborArrayType ? '[' : '{') ))
            return CborErrorIO;

        err = (type == CborArrayType) ?
                  array_to_json(stream, out, &recursed, flags, status) :
                  map_to_json(stream, out, &recursed, flags, status);
        if (err) {
            it->ptr = recursed.ptr;
            return err;       /* parse error */
        }

        if (stream(out, "%c", (type == CborArrayType ? ']' : '}') ))
            return CborErrorIO;
        err = cbor_value_leave_container(it, &recursed);
        if (err)
            return err;       /* parse error */

        status->flags = 0;    /* reset, there are never conversion errors for us */
        return CborNoError;
    }

    case CborIntegerType: {
        double num;     /* JS numbers are IEEE double precision */
        uint64_t val;
        cbor_value_get_raw_integer(it, &val);    /* can't fail */
        num = (double)val;

        if (cbor_value_is_negative_integer(it)) {
            num = -num - 1;                     /* convert to negative */
            if ((uint64_t)(-num - 1) != val) {
                status->flags = NumberPrecisionWasLost | NumberWasNegative;
                status->originalNumber = val;
            }
        } else {
            if ((uint64_t)num != val) {
                status->flags = NumberPrecisionWasLost;
                status->originalNumber = val;
            }
        }
        if (stream(out, "%.0f", num))  /* this number has no fraction, so no decimal points please */
            return CborErrorIO;
        break;
    }

    case CborByteStringType:
    case CborTextStringType: {
        char *str;
        if (type == CborByteStringType) {
            err = dump_bytestring_base64url(&str, it);
            status->flags = TypeWasNotNative;
        } else {
            size_t n = 0;
            err = cbor_value_dup_text_string(it, &str, &n, it);
        }
        if (err)
            return err;
        err = (stream(out, "\"%s\"", str));
        free(str);
        return err;
    }

    case CborTagType:
        return tagged_value_to_json(stream, out, it, flags, status);

    case CborSimpleType: {
        uint8_t simple_type;
        cbor_value_get_simple_type(it, &simple_type);  /* can't fail */
        status->flags = TypeWasNotNative;
        status->originalNumber = simple_type;
        if (stream(out, "\"simple(%" PRIu8 ")\"", simple_type))
            return CborErrorIO;
        break;
    }

    case CborNullType:
        if (stream(out, "null"))
            return CborErrorIO;
        break;

    case CborUndefinedType:
        status->flags = TypeWasNotNative;
        if (stream(out, "\"undefined\""))
            return CborErrorIO;
        break;

    case CborBooleanType: {
        bool val;
        cbor_value_get_boolean(it, &val);       /* can't fail */
        if (stream(out, val ? "true" : "false"))
            return CborErrorIO;
        break;
    }

#ifndef CBOR_NO_FLOATING_POINT
    case CborDoubleType: {
        double val;
        if (false) {
            float f;
    case CborFloatType:
            status->flags = TypeWasNotNative;
            cbor_value_get_float(it, &f);
            val = f;
        } else if (false) {
            uint16_t f16;
    case CborHalfFloatType:
#  ifndef CBOR_NO_HALF_FLOAT_TYPE
            status->flags = TypeWasNotNative;
            cbor_value_get_half_float(it, &f16);
            val = decode_half(f16);
#  else
            (void)f16;
            err = CborErrorUnsupportedType;
            break;
#  endif
        } else {
            cbor_value_get_double(it, &val);
        }

        int r = fpclassify(val);
        if (r == FP_NAN || r == FP_INFINITE) {
            if (stream(out, "null"))
                return CborErrorIO;
            status->flags |= r == FP_NAN ? NumberWasNaN :
                                           NumberWasInfinite | (val < 0 ? NumberWasNegative : 0);
        } else {
            uint64_t ival = (uint64_t)fabs(val);
            if ((double)ival == fabs(val)) {
                /* print as integer so we get the full precision */
                r = stream(out, "%s%" PRIu64, val < 0 ? "-" : "", ival);
                status->flags |= TypeWasNotNative;   /* mark this integer number as a double */
            } else {
                /* this number is definitely not a 64-bit integer */
                r = stream(out, "%." DBL_DECIMAL_DIG_STR "g", val);
            }
            if (r)
                return CborErrorIO;
        }
        break;
    }
#else
    case CborDoubleType:
    case CborFloatType:
    case CborHalfFloatType:
        err = CborErrorUnsupportedType;
        break;
#endif /* !CBOR_NO_FLOATING_POINT */

    case CborInvalidType:
        return CborErrorUnknownType;
    }

    return cbor_value_advance_fixed(it);
}

/**
 * \enum CborToJsonFlags
 * The CborToJsonFlags enum contains flags that control the conversion of CBOR to JSON.
 *
 * \value CborConvertAddMetadata        Adds metadata to facilitate restoration of the original CBOR data.
 * \value CborConvertTagsToObjects      Converts CBOR tags to JSON objects
 * \value CborConvertIgnoreTags         (default) Ignore CBOR tags, except for byte strings
 * \value CborConvertObeyByteStringTags (default) Honor formatting of CBOR byte strings if so tagged
 * \value CborConvertByteStringsToBase64Url Force the conversion of all CBOR byte strings to Base64url encoding, despite any tags
 * \value CborConvertRequireMapStringKeys (default) Require CBOR map keys to be strings, failing the conversion if they are not
 * \value CborConvertStringifyMapKeys   Convert non-string keys in CBOR maps to a string form
 * \value CborConvertDefaultFlags       Default conversion flags.
 */

/**
 * \fn CborError cbor_value_to_json(FILE *out, const CborValue *value, int flags)
 *
 * Converts the current CBOR type pointed to by \a value to JSON and writes that
 * to the \a out stream. If an error occurs, this function returns an error
 * code similar to CborParsing. The \a flags parameter indicates one or more of
 * the flags from CborToJsonFlags that control the conversion.
 *
 * \sa cbor_value_to_json_advance(), cbor_value_to_pretty()
 */

/**
 * Converts the current CBOR type pointed by \a value to JSON and writes that
 * to the stream by calling the \a streamFunction. If an error occurs,
 * this function returns an error code similar to \ref CborParsing.
 * The \a flags parameter indicates one or more of the flags from CborToJsonFlags
 * that control the conversion.
 *
 * If no error ocurred, this function advances \a value to the next element.
 *
 * The \a streamFunction function will be called with the \a token value as the
 * first parameter and a printf-style format string as the second, with a variable
 * number of further parameters.
 *
 * \sa cbor_value_to_pretty_stream(), cbor_value_to_json_advance()
 */

CborError cbor_value_to_json_stream(CborStreamFunction streamFunction, void *token, CborValue *value, int flags)
{
    ConversionStatus status;
    return value_to_json(streamFunction, token, value, flags, cbor_value_get_type(value), &status);
}

static CborError cborjson_fprintf(void *out, const char *fmt, ...)
{
    int n;

    va_list list;
    va_start(list, fmt);
    n = vfprintf((FILE *)out, fmt, list);
    va_end(list);

    return n < 0 ? CborErrorIO : CborNoError;
}

/**
 * Converts the current CBOR type pointed to by \a value to JSON and writes that
 * to the \a out stream. If an error occurs, this function returns an error
 * code similar to CborParsing. The \a flags parameter indicates one or more of
 * the flags from CborToJsonFlags that control the conversion.
 *
 * If no error ocurred, this function advances \a value to the next element.
 *
 * \sa cbor_value_to_json(), cbor_value_to_pretty_advance()
 */
CborError cbor_value_to_json_advance(FILE *out, CborValue *value, int flags)
{
    ConversionStatus status;
    return value_to_json(cborjson_fprintf, out, value, flags, cbor_value_get_type(value), &status);
}

/** @} */
