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

#ifndef CBOR_H
#define CBOR_H

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#else
#include <stdbool.h>
#endif

#ifndef CBOR_API
#  define CBOR_API
#endif
#ifndef CBOR_PRIVATE_API
#  define CBOR_PRIVATE_API
#endif
#ifndef CBOR_INLINE_API
#  if defined(__cplusplus) || (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L)
#    define CBOR_INLINE_API inline
#  else
#    define CBOR_INLINE_API static inline
#  endif
#endif

typedef enum CborType {
    CborIntegerType     = 0x00,
    CborByteStringType  = 0x40,
    CborTextStringType  = 0x60,
    CborArrayType       = 0x80,
    CborMapType         = 0xa0,
    CborTagType         = 0xc0,
    CborSimpleType      = 0xe0,
    CborBooleanType     = 0xf5,
    CborNullType        = 0xf6,
    CborUndefinedType   = 0xf7,
    CborHalfFloatType   = 0xf9,
    CborFloatType       = 0xfa,
    CborDoubleType      = 0xfb,

    CborInvalidType     = 0xff
} CborType;

typedef uint64_t CborTag;
typedef enum CborKnownTags {
    CborDateTimeStringTag          = 0,        /* RFC 3339 format: YYYY-MM-DD hh:mm:ss+zzzz */
    CborUnixTime_tTag              = 1,
    CborPositiveBignumTag          = 2,
    CborNegativeBignumTag          = 3,
    CborDecimalTag                 = 4,
    CborBigfloatTag                = 5,
    CborExpectedBase64urlTag       = 21,
    CborExpectedBase64Tag          = 22,
    CborExpectedBase16Tag          = 23,
    CborUriTag                     = 32,
    CborBase64urlTag               = 33,
    CborBase64Tag                  = 34,
    CborRegularExpressionTag       = 35,
    CborMimeMessageTag             = 36,       /* RFC 2045-2047 */
    CborSignatureTag               = 55799
} CborKnownTags;

/* Parser API */

typedef enum CborParserError {
    CborNoError = 0,

    /* errors in all modes */
    CborErrorUnknownError,
    CborErrorGarbageAtEnd,
    CborErrorUnexpectedEOF,
    CborErrorBreakMissingAtEOF,      /* special case of UnexpectedEOF */
    CborErrorUnexpectedBreak,
    CborErrorUnknownType,            /* can only heppen in major type 7 */
    CborErrorIllegalType,            /* type not allowed here */
    CborErrorIllegalNumber,
    CborErrorIllegalSimpleType,      /* types of value less than 32 encoded in two bytes */

    /* errors in strict mode parsing only */
    CborErrorUnknownSimpleType = 256,
    CborErrorUnknownTag,
    CborErrorInappropriateTagForType,
    CborErrorDuplicateObjectKeys,
    CborErrorInvalidUtf8TextString,

    /* internal implementation errors */
    CborErrorDataTooLarge = 1024,
    CborErrorInternalError = ~0U
} CborParserError;

enum CborParserIteratorFlags
{
    CborIteratorFlag_IntegerValueTooLarge   = 0x01,
    CborIteratorFlag_NegativeInteger        = 0x02,
    CborIteratorFlag_UnknownLength          = 0x04
};

struct CborParser
{
    const char *end;
    int flags;
    CborParserError error;
};
typedef struct CborParser CborParser;

struct CborValue
{
    CborParser *parser;
    const char *ptr;
    uint32_t remaining;
    uint16_t extra;
    uint8_t type;
    uint8_t flags;
};
typedef struct CborValue CborValue;

CBOR_API const char *cbor_parser_error_string(CborParserError error);
CBOR_API void cbor_parser_init(const char *buffer, size_t size, int flags, CborParser *parser, CborValue *it);
CBOR_INLINE_API CborParserError cbor_parser_get_error(CborParser *parser)
{ return parser->error; }
CBOR_INLINE_API bool cbor_parser_has_error(CborParser *parser)
{ return parser->error; }

CBOR_INLINE_API bool cbor_value_at_end(const CborValue *it)
{ return it->remaining == 0; }
CBOR_API bool cbor_value_advance_fixed(CborValue *it);
CBOR_API bool cbor_value_advance(CborValue *it);
CBOR_API bool cbor_value_is_container(const CborValue *it);
CBOR_API bool cbor_value_enter_container(const CborValue *it, CborValue *recursed);
CBOR_API bool cbor_value_leave_container(CborValue *it, const CborValue *recursed);

CBOR_PRIVATE_API uint64_t _cbor_value_decode_int64_internal(const CborValue *value);
CBOR_INLINE_API uint64_t _cbor_value_extract_int64_helper(const CborValue *value)
{
    return value->flags & CborIteratorFlag_IntegerValueTooLarge ?
                _cbor_value_decode_int64_internal(value) : value->extra;
}

CBOR_INLINE_API bool cbor_value_is_valid(const CborValue *value)
{ return value && value->type != CborInvalidType; }
CBOR_INLINE_API CborType cbor_value_get_type(const CborValue *value)
{ return value->type; }

/* Null & undefined type */
CBOR_INLINE_API bool cbor_type_is_null(const CborValue *value)
{ return value->type == CborNullType; }
CBOR_INLINE_API bool cbor_type_is_undefined(const CborValue *value)
{ return value->type == CborUndefinedType; }

/* Booleans */
CBOR_INLINE_API bool cbor_value_is_boolean(const CborValue *value)
{ return value->type == CborBooleanType; }
CBOR_INLINE_API bool cbor_value_get_boolean(const CborValue *value, bool *result)
{
    assert(cbor_value_is_boolean(value));
    *result = value->extra;
    return true;
}

/* Simple types */
CBOR_INLINE_API bool cbor_value_is_simple_type(const CborValue *value)
{ return value->type == CborSimpleType; }
CBOR_INLINE_API bool cbor_value_get_simple_type(const CborValue *value, uint8_t *result)
{
    assert(cbor_value_is_simple_type(value));
    *result = value->extra;
    return true;
}

/* Integers */
CBOR_INLINE_API bool cbor_value_is_integer(const CborValue *value)
{ return value->type == CborIntegerType; }
CBOR_INLINE_API bool cbor_value_is_unsigned_integer(const CborValue *value)
{ return cbor_value_is_integer(value) && (value->flags & CborIteratorFlag_NegativeInteger) == 0; }
CBOR_INLINE_API bool cbor_value_is_negative_integer(const CborValue *value)
{ return cbor_value_is_integer(value) && (value->flags & CborIteratorFlag_NegativeInteger); }

CBOR_INLINE_API bool cbor_value_get_uint64(const CborValue *value, uint64_t *result)
{
    assert(cbor_value_is_unsigned_integer(value));
    *result = _cbor_value_extract_int64_helper(value);
    return true;
}

CBOR_INLINE_API bool cbor_value_get_int64(const CborValue *value, int64_t *result)
{
    assert(cbor_value_is_integer(value));
    *result = (int64_t) _cbor_value_extract_int64_helper(value);
    return true;
}

CBOR_INLINE_API bool cbor_value_get_int(const CborValue *value, int *result)
{
    assert(cbor_value_is_integer(value));
    *result = (int) _cbor_value_extract_int64_helper(value);
    return true;
}

CBOR_API bool cbor_value_get_int64_checked(const CborValue *value, int64_t *result);
CBOR_API bool cbor_value_get_int_checked(const CborValue *value, int *result);

CBOR_INLINE_API bool cbor_value_is_length_known(const CborValue *value)
{ return (value->flags & CborIteratorFlag_UnknownLength) == 0; }

/* Tags */
CBOR_INLINE_API bool cbor_value_is_tag(const CborValue *value)
{ return value->type == CborTagType; }
CBOR_INLINE_API bool cbor_value_get_tag(const CborValue *value, CborTag *result)
{
    assert(cbor_value_is_tag(value));
    *result = _cbor_value_extract_int64_helper(value);
    return true;
}

/* Strings */
CBOR_INLINE_API bool cbor_value_is_byte_string(const CborValue *value)
{ return value->type == CborByteStringType; }
CBOR_INLINE_API bool cbor_value_is_text_string(const CborValue *value)
{ return value->type == CborTextStringType; }

CBOR_INLINE_API bool cbor_value_get_string_length(const CborValue *value, size_t *length)
{
    assert(cbor_value_is_byte_string(value) || cbor_value_is_text_string(value));
    if (!cbor_value_is_length_known(value))
        return false;
    uint64_t v = _cbor_value_extract_int64_helper(value);
    *length = v;
    if (*length != v)
        return false;
    return true;
}

CBOR_API bool cbor_value_calculate_string_length(const CborValue *value, size_t *length);
CBOR_API size_t cbor_value_copy_string(const CborValue *value, char *buffer,
                                     size_t buflen, CborValue *next);
CBOR_API bool cbor_value_dup_string(const CborValue *value, char **buffer,
                                      size_t *len, CborValue *next);

/* ### TBD: partial reading API */

CBOR_API int cbor_value_text_string_compare(const CborValue *value, const char *string);
CBOR_INLINE_API bool cbor_value_text_string_equals(const CborValue *value, const char *string)
{ return cbor_value_is_text_string(value) && cbor_value_text_string_compare(value, string) == 0; }

/* Maps and arrays */
CBOR_INLINE_API bool cbor_value_is_array(const CborValue *value)
{ return value->type == CborArrayType; }
CBOR_INLINE_API bool cbor_value_is_map(const CborValue *value)
{ return value->type == CborMapType; }

CBOR_INLINE_API bool cbor_value_get_array_length(const CborValue *value, size_t *length)
{
    assert(cbor_value_is_array(value));
    if (!cbor_value_is_length_known(value))
        return false;
    uint64_t v = _cbor_value_extract_int64_helper(value);
    *length = v;
    if (*length != v)
        return false;
    return true;
}

CBOR_INLINE_API bool cbor_value_get_map_length(const CborValue *value, size_t *length)
{
    assert(cbor_value_is_map(value));
    if (!cbor_value_is_length_known(value))
        return false;
    uint64_t v = _cbor_value_extract_int64_helper(value);
    *length = v;
    if (*length != v)
        return false;
    return true;
}

/* Floating point */
CBOR_API bool cbor_value_get_half_float(const CborValue *value, void *result);
CBOR_INLINE_API bool cbor_value_get_float(const CborValue *value, float *result)
{
    assert(value->type == CborFloatType);
    uint32_t data = _cbor_value_extract_int64_helper(value);
    memcpy(result, &data, sizeof(*result));
    return true;
}

CBOR_INLINE_API bool cbor_value_get_double(const CborValue *value, double *result)
{
    assert(value->type == CborDoubleType);
    uint64_t data = _cbor_value_extract_int64_helper(value);
    memcpy(result, &data, sizeof(*result));
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // CBOR_H

