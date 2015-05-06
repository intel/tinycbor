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
#include <string.h>

/**
 * \typedef CborValue
 * This type contains one value parsed from the CBOR stream.
 *
 * To get the actual type, use cbor_value_get_type(). Then extract the value
 * using one of the corresponding functions: cbor_value_get_boolean(), cbor_value_get_int64(),
 * cbor_value_get_int(), cbor_value_copy_string(), cbor_value_get_array(), cbor_value_get_map(),
 * cbor_value_get_double(), cbor_value_get_float().
 *
 * In C++ and C11 modes, you can additionally use the cbor_value_get_integer()
 * and cbor_value_get_floating_point() generic functions.
 *
 * \omit
 * Implementation details: the CborValue contains these fields:
 * \list
 *   \li ptr: pointer to the actual data
 *   \li flags: flags from the decoder
 *   \li extra: partially decoded integer value
 *   \li remaining: remaining items in this collection after this item or UINT32_MAX if length is unknown
 * \endlist
 * \endomit
 */

static bool makeError(CborParser *parser, CborParserError error, uint64_t addend)
{
    (void)addend;
    parser->error = error;
    return false;
}
#ifdef CBOR_PARSER_NO_DETAILED_ERROR
// This way, the compiler should eliminate all error settings by dead code elimination
#  define makeError(parser, err, addend)    makeError(parser, (err) * 0 + CborErrorInternalError, addend)
#endif

static inline uint16_t get16(const char *ptr)
{
    uint16_t result;
    memcpy(&result, ptr, sizeof(result));
    return be16toh(result);
}

static inline uint32_t get32(const char *ptr)
{
    uint32_t result;
    memcpy(&result, ptr, sizeof(result));
    return be32toh(result);
}

static inline uint64_t get64(const char *ptr)
{
    uint64_t result;
    memcpy(&result, ptr, sizeof(result));
    return be64toh(result);
}

static void preparse_value(CborValue *it)
{
    // are we at the end?
    CborParser *parser = it->parser;
    if (it->ptr == parser->end)
        goto error_eof;

    uint8_t descriptor = *it->ptr;
    it->type = descriptor & MajorTypeMask;
    it->flags = 0;
    descriptor &= SmallValueMask;
    it->extra = descriptor;

    switch ((CborMajorTypes)(it->type >> MajorTypeShift)) {
    case NegativeIntegerType:
        it->flags |= CborIteratorFlag_NegativeInteger;
        // fall through
    case UnsignedIntegerType:
    case TagType:
        break;

    case SimpleTypesType:
        switch (descriptor) {
        case FalseValue:
            it->extra = false;
            // fall through
        case TrueValue:
        case NullValue:
        case UndefinedValue:
        case HalfPrecisionFloat:
        case SinglePrecisionFloat:
        case DoublePrecisionFloat:
            it->type = descriptor;
            break;

        case SimpleTypeInNextByte:
            if (it->ptr + 1 > parser->end)
                goto error_eof;
            it->extra = it->ptr[1];
#ifndef CBOR_PARSER_NO_STRICT_CHECKS
            if (it->extra < 32) {
                makeError(parser, CborErrorIllegalSimpleType, it->extra);
                goto error;
            }
#endif
        case 28:
        case 29:
        case 30:
            makeError(parser, CborErrorUnknownType, *it->ptr);
            goto error;

        case Break:
            makeError(parser, CborErrorUnexpectedBreak, 0);
            goto error;
        }
        break;

    case ByteStringType:
    case TextStringType:
    case ArrayType:
    case MapType:
        if (descriptor == IndefiniteLength)
            it->flags |= CborIteratorFlag_UnknownLength;
        break;
    }

    // try to decode up to 16 bits
    if (descriptor < Value8Bit)
        return;
    if (unlikely(descriptor > Value64Bit))
        goto illegal_number_error;

    size_t bytesNeeded = 1 << (descriptor - Value8Bit);
    if (it->ptr + 1 + bytesNeeded > parser->end)
        goto error_eof;
    if (descriptor == Value8Bit) {
        it->extra = it->ptr[1];
        return;
    }
    if (descriptor == Value16Bit) {
        it->extra = get16(it->ptr + 1);
        return;
    }
    // Value32Bit or Value64Bit
    it->flags |= CborIteratorFlag_IntegerValueTooLarge;
    return;

illegal_number_error:
    makeError(parser, CborErrorIllegalNumber, it->ptr[1]);
    goto error;

error_eof:
    makeError(parser, CborErrorUnexpectedEOF, 0);
error:
    it->type = CborInvalidType;
    return;
}

uint64_t _cbor_value_decode_int64_internal(const CborValue *value)
{
    assert(value->flags & CborIteratorFlag_IntegerValueTooLarge);
    if ((*value->ptr & SmallValueMask) == Value32Bit)
        return get32(value->ptr + 1);

    assert((*value->ptr & SmallValueMask) == Value64Bit);
    return get64(value->ptr + 1);
}

/**
 * Initializes the CBOR parser for parsing \a size bytes beginning at \a
 * buffer. Parsing will use flags set in \a flags. The iterator to the first
 * element is returned in \a it.
 */
void cbor_parser_init(const char *buffer, size_t size, int flags, CborParser *parser, CborValue *it)
{
    memset(parser, 0, sizeof(*parser));
    parser->end = buffer + size;
    parser->error = CborNoError;
    parser->flags = flags;
    it->parser = parser;
    it->ptr = buffer;
    it->remaining = 1;      // there's one type altogether, usually an array or map
    preparse_value(it);
}

static bool is_fixed_type(uint8_t type)
{
    return type == CborIntegerType || type == CborTagType || type == CborSimpleType;
}

static bool advance_internal(CborValue *it)
{
    unsigned size = 1 << (*it->ptr - Value8Bit);
    if (it->ptr + size > it->parser->end)
        return makeError(it->parser, CborErrorUnexpectedEOF, 0);

    it->ptr += size;
    if (it->remaining == UINT32_MAX && *it->ptr == (char)BreakByte) {
        // end of map or array
        // ### FIXME: was it a map or array?
        it->remaining = 0;
        return true;
    }

    if (it->remaining != UINT32_MAX)
        --it->remaining;
    preparse_value(it);
    return true;
}

bool cbor_value_advance_fixed(CborValue *it)
{
    assert(is_fixed_type(it->type));
    return it->remaining && advance_internal(it);
}

bool cbor_value_is_recursive(const CborValue *it)
{
    return it->type == CborArrayType || it->type == CborMapType;
}

bool cbor_value_begin_recurse(const CborValue *it, CborValue *recursed)
{
    assert(cbor_value_is_recursive(it));
    *recursed = *it;
    if (it->flags & CborIteratorFlag_UnknownLength) {
        recursed->remaining = UINT32_MAX;
    } else {
        uint64_t len = _cbor_value_extract_int64_helper(it);
        recursed->remaining = len;
        if (recursed->remaining != len || len == UINT32_MAX)
            return makeError(it->parser, CborErrorDataTooLarge, len);
    }
    if (!advance_internal(recursed))
        return false;
    return true;
}
