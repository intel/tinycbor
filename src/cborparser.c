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

#if defined(__GNUC__) && !defined(__INTEL_COMPILER) && !defined(__clang__) && \
    (__GNUC__ * 100 + __GNUC_MINOR__ >= 404)
#  pragma GCC optimize("-ffunction-sections")
#endif

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
 *   \li extra: partially decoded integer value (0, 1 or 2 bytes)
 *   \li remaining: remaining items in this collection after this item or UINT32_MAX if length is unknown
 * \endlist
 * \endomit
 */

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

static inline CborError extract_number(const CborParser *parser, const char **ptr, uint64_t *len)
{
    uint8_t additional_information = **ptr & SmallValueMask;
    ++*ptr;
    if (additional_information < Value8Bit) {
        *len = additional_information;
        return CborNoError;
    }
    if (unlikely(additional_information > Value64Bit))
        return CborErrorIllegalNumber;

    size_t bytesNeeded = 1 << (additional_information - Value8Bit);
    if (unlikely(*ptr + bytesNeeded > parser->end)) {
        return CborErrorUnexpectedEOF;
    } else if (bytesNeeded == 1) {
        *len = (uint8_t)(*ptr)[0];
    } else if (bytesNeeded == 2) {
        *len = get16(*ptr);
    } else if (bytesNeeded == 4) {
        *len = get32(*ptr);
    } else {
        *len = get64(*ptr);
    }
    *ptr += bytesNeeded;
    return CborNoError;
}

static inline CborError extract_length(const CborParser *parser, const char **ptr, size_t *len)
{
    uint64_t v;
    CborError err = extract_number(parser, ptr, &v);
    if (err)
        return err;

    *len = v;
    if (v != *len)
        return CborErrorDataTooLarge;
    return CborNoError;
}

static bool is_fixed_type(uint8_t type)
{
    return type != CborTextStringType && type != CborByteStringType && type != CborArrayType &&
           type != CborMapType;
}

static CborError preparse_value(CborValue *it)
{
    const CborParser *parser = it->parser;

    // are we at the end?
    if (it->ptr == parser->end)
        return CborErrorUnexpectedEOF;

    uint8_t descriptor = *it->ptr;
    uint8_t type = descriptor & MajorTypeMask;
    it->flags = 0;
    it->type = CborInvalidType;
    it->extra = (descriptor &= SmallValueMask);

    if (descriptor == IndefiniteLength && !is_fixed_type(type)) {
        // special case
        it->flags |= CborIteratorFlag_UnknownLength;
        it->type = type;
        return CborNoError;
    }

    size_t bytesNeeded = descriptor < Value8Bit ? 0 : (1 << (descriptor - Value8Bit));
    if (it->ptr + 1 + bytesNeeded > parser->end)
        return CborErrorUnexpectedEOF;

    switch ((CborMajorTypes)(type >> MajorTypeShift)) {
    case NegativeIntegerType:
        it->flags |= CborIteratorFlag_NegativeInteger;
        type = CborIntegerType;
        // fall through
    case UnsignedIntegerType:
    case ByteStringType:
    case TextStringType:
    case ArrayType:
    case MapType:
    case TagType:
        break;

    case SimpleTypesType:
        switch (descriptor) {
        case FalseValue:
            it->extra = false;
            type = CborBooleanType;
            break;

        case TrueValue:
        case NullValue:
        case UndefinedValue:
        case HalfPrecisionFloat:
        case SinglePrecisionFloat:
        case DoublePrecisionFloat:
            type = *it->ptr;
            break;

        case SimpleTypeInNextByte:
#ifndef CBOR_PARSER_NO_STRICT_CHECKS
            if ((unsigned char)it->ptr[1] < 32)
                return CborErrorIllegalSimpleType;
#endif
            break;

        case 28:
        case 29:
        case 30:
            return CborErrorUnknownType;

        case Break:
            return CborErrorUnexpectedBreak;
        }
        break;
    }

    if (unlikely(descriptor > Value64Bit))
        return CborErrorIllegalNumber;

    // no further errors possible
    it->type = type;

    // try to decode up to 16 bits
    if (descriptor < Value8Bit)
        return CborNoError;

    if (descriptor == Value8Bit)
        it->extra = (uint8_t)it->ptr[1];
    else if (descriptor == Value16Bit)
        it->extra = get16(it->ptr + 1);
    else
        it->flags |= CborIteratorFlag_IntegerValueTooLarge;     // Value32Bit or Value64Bit
    return CborNoError;
}

static CborError preparse_next_value(CborValue *it)
{
    it->type = CborInvalidType;
    if (it->remaining != UINT32_MAX) {
        if (!--it->remaining) {
            return it->ptr == it->parser->end ? CborNoError : CborErrorGarbageAtEnd;
        }
    }
    return preparse_value(it);
}

static CborError advance_internal(CborValue *it)
{
    uint64_t length;
    CborError err = extract_number(it->parser, &it->ptr, &length);
    assert(err == CborNoError);

    if (!is_fixed_type(it->type)) {
        assert(length == (size_t)length);
        it->ptr += length;
    }

    if (it->remaining == UINT32_MAX && *it->ptr == (char)BreakByte) {
        // end of map or array
        it->remaining = 0;
        return CborNoError;
    }

    return preparse_next_value(it);
}

/** \internal
 *
 * Decodes the CBOR integer value when it is larger than the 16 bits available
 * in value->extra. This function requires that value->flags have the
 * CborIteratorFlag_IntegerValueTooLarge flag set.
 *
 * This function is also used to extract single- and double-precision floating
 * point values (SinglePrecisionFloat == Value32Bit and DoublePrecisionFloat ==
 * Value64Bit).
 */
uint64_t _cbor_value_decode_int64_internal(const CborValue *value)
{
    assert(value->flags & CborIteratorFlag_IntegerValueTooLarge ||
           value->type == CborFloatType || value->type == CborDoubleType);
    if ((*value->ptr & SmallValueMask) == Value32Bit)
        return get32(value->ptr + 1);

    assert((*value->ptr & SmallValueMask) == Value64Bit);
    return get64(value->ptr + 1);
}

/**
 * Initializes the CBOR parser for parsing \a size bytes beginning at \a
 * buffer. Parsing will use flags set in \a flags. The iterator to the first
 * element is returned in \a it.
 *
 * The \a parser structure needs to remain valid throughout the decoding
 * process. It is not thread-safe to share one CborParser among multiple
 * threads iterating at the same time, but the object can be copied so multiple
 * threads can iterate.
 *
 * ### Write how to determine the end pointer
 * ### Write how to do limited-buffer windowed decoding
 */
CborError cbor_parser_init(const char *buffer, size_t size, int flags, CborParser *parser, CborValue *it)
{
    memset(parser, 0, sizeof(*parser));
    parser->end = buffer + size;
    parser->flags = flags;
    it->parser = parser;
    it->ptr = buffer;
    it->remaining = 1;      // there's one type altogether, usually an array or map
    return preparse_value(it);
}

/**
 * Advances the CBOR value \a it by one fixed-size position. Fixed-size types
 * are: integers, tags, simple types (including boolean, null and undefined
 * values) and floating point types.
 *
 * \sa cbor_value_at_end(), cbor_value_advance(), cbor_value_begin_recurse(), cbor_value_end_recurse()
 */
CborError cbor_value_advance_fixed(CborValue *it)
{
    assert(it->type != CborInvalidType);
    assert(is_fixed_type(it->type));
    if (!it->remaining)
        return CborErrorAdvancePastEOF;
    return advance_internal(it);
}

/**
 * Advances the CBOR value \a it by one element, skipping over containers.
 * Unlike cbor_value_advance_fixed(), this function can be called on a CBOR
 * value of any type. However, if the type is a container (map or array) or a
 * string with a chunked payload, this function will not run in constant time
 * and will recurse into itself (it will run on O(n) time for the number of
 * elements or chunks and will use O(n) memory for the number of nested
 * containers).
 *
 * \sa cbor_value_at_end(), cbor_value_advance_fixed(), cbor_value_begin_recurse(), cbor_value_end_recurse()
 */
CborError cbor_value_advance(CborValue *it)
{
    assert(it->type != CborInvalidType);
    if (!it->remaining)
        return CborErrorAdvancePastEOF;
    if (is_fixed_type(it->type))
        return advance_internal(it);

    if (!cbor_value_is_container(it))
        return cbor_value_copy_string(it, NULL, 0, it);

    // map or array
    CborError err;
    CborValue recursed;
    err = cbor_value_enter_container(it, &recursed);
    if (err)
        return err;
    while (!cbor_value_at_end(&recursed)) {
        err = cbor_value_advance(&recursed);
        if (err)
            return err;
    }
    return cbor_value_leave_container(it, &recursed);
}

/**
 * \fn bool cbor_value_is_container(const CborValue *it)
 *
 * Returns true if the \a it value is a container and requires recursion in
 * order to decode (maps and arrays), false otherwise.
 */

/**
 * Creates a CborValue iterator pointing to the first element of the container
 * represented by \a it and saves it in \a recursed. The \a it container object
 * needs to be kept and passed again to cbor_value_leave_container() in order
 * to continue iterating past this container.
 *
 * \sa cbor_value_is_container(), cbor_value_leave_container(), cbor_value_advance()
 */
CborError cbor_value_enter_container(const CborValue *it, CborValue *recursed)
{
    assert(cbor_value_is_container(it));
    *recursed = *it;
    if (it->flags & CborIteratorFlag_UnknownLength) {
        recursed->remaining = UINT32_MAX;
    } else {
        uint64_t len = _cbor_value_extract_int64_helper(it);
        recursed->remaining = len;
        if (recursed->remaining != len || len == UINT32_MAX)
            return CborErrorDataTooLarge;
    }
    return advance_internal(recursed);
}

/**
 * Updates \a it to point to the next element after the container. The \a
 * recursed object needs to point to the last element of the container.
 *
 * \sa cbor_value_enter_container(), cbor_value_at_end()
 */
CborError cbor_value_leave_container(CborValue *it, const CborValue *recursed)
{
    assert(cbor_value_is_container(it));
    assert(cbor_value_at_end(recursed));
    it->ptr = recursed->ptr;
    return advance_internal(it);
}

/**
 * Calculates the length of the string in \a value and stores the result in \a
 * len. This function is different from cbor_value_get_string_length() in that
 * it calculates the length even for strings sent in chunks. For that reason,
 * this function may not run in constant time (it will run in O(n) time on the
 * number of chunks).
 *
 * \note On 32-bit platforms, this function will return error condition of \ref
 * CborErrorDataTooLarge if the stream indicates a length that is too big to
 * fit in 32-bit.
 *
 * \sa cbor_value_get_string_length(), cbor_value_copy_string(), cbor_value_is_length_known()
 */
CborError cbor_value_calculate_string_length(const CborValue *value, size_t *len)
{
    return cbor_value_copy_string(value, NULL, len, NULL);
}

/**
 * Allocates memory for the string pointed by \a value and copies it into this
 * buffer. The pointer to the buffer is stored in \a buffer and the number of
 * bytes copied is stored in \a len (those variables must not be NULL).
 *
 * If \c malloc returns a NULL pointer, this function will return error
 * condition \ref CborErrorOutOfMemory.
 *
 * On success, \c{*buffer} will contain a valid pointer that must be freed by
 * calling \c{free()}. This is the case even for zero-length strings.
 *
 * The \a next pointer, if not null, will be updated to point to the next item
 * after this string. If \a value points to the last item, then \a next will be
 * invalid.
 *
 * \note This function does not perform UTF-8 validation on the incoming text
 * string.
 *
 * \sa cbor_value_copy_string()
 */
CborError cbor_value_dup_string(const CborValue *value, char **buffer, size_t *buflen, CborValue *next)
{
    assert(buffer);
    assert(buflen);
    CborError err = cbor_value_calculate_string_length(value, buflen);
    if (err)
        return err;

    ++*buflen;
    *buffer = malloc(*buflen);
    if (!*buffer) {
        // out of memory
        return CborErrorOutOfMemory;
    }
    err = cbor_value_copy_string(value, *buffer, buflen, next);
    if (err) {
        free(*buffer);
        return err;
    }
    return CborNoError;
}

/**
 * Copies the string pointed by \a value into the buffer provided at \a buffer
 * of \a buflen bytes. If \a buffer is a NULL pointer, this function will not
 * copy anything and will only update the \a next value.
 *
 * If the provided buffer length was too small, this function returns an error
 * condition of \ref CborErrorOutOfMemory. If you need to calculate the length
 * of the string in order to preallocate a buffer, use
 * cbor_value_calculate_string_length().
 *
 * On success, this function sets the number of bytes copied to \c{*buflen}. If
 * the buffer is large enough, this function will insert a null byte after the
 * last copied byte, to facilitate manipulation of text strings. That byte is
 * not included in the returned value of \c{*buflen}.
 *
 * The \a next pointer, if not null, will be updated to point to the next item
 * after this string. If \a value points to the last item, then \a next will be
 * invalid.
 *
 * \note This function does not perform UTF-8 validation on the incoming text
 * string.
 *
 * \sa cbor_value_dup_string(), cbor_value_get_string_length(), cbor_value_calculate_string_length()
 */
CborError cbor_value_copy_string(const CborValue *value, char *buffer,
                                 size_t *buflen, CborValue *next)
{
    assert(cbor_value_is_byte_string(value) || cbor_value_is_text_string(value));

    size_t total;
    CborError err;
    const char *ptr = value->ptr;
    if (cbor_value_is_length_known(value)) {
        // easy case: fixed length
        err = extract_length(value->parser, &ptr, &total);
        if (err)
            return err;
        if (buffer) {
            if (*buflen < total)
                return CborErrorOutOfMemory;
            memcpy(buffer, ptr, total);
            ptr += total;
        }
    } else {
        // chunked
        ++ptr;
        total = 0;
        while (true) {
            size_t chunkLen;
            size_t newTotal;

            if (ptr == value->parser->end)
                return CborErrorUnexpectedEOF;

            if (*ptr == (char)BreakByte) {
                ++ptr;
                break;
            }

            // is this the right type?
            if ((*ptr & MajorTypeMask) != value->type)
                return CborErrorIllegalType;

            err = extract_length(value->parser, &ptr, &chunkLen);
            if (err)
                return err;

            if (unlikely(!add_check_overflow(total, chunkLen, &newTotal)))
                return CborErrorDataTooLarge;

            if (buffer) {
                if (*buflen < newTotal)
                    return CborErrorOutOfMemory;
                memcpy(buffer + total, ptr, chunkLen);
            }
            ptr += chunkLen;
            total = newTotal;
        }
    }

    // is there enough room for the ending NUL byte?
    if (buffer && *buflen > total)
        buffer[total] = '\0';
    *buflen = total;

    if (next) {
        *next = *value;
        next->ptr = ptr;
        err = preparse_next_value(next);
        if (err)
            return err;
    }
    return CborNoError;
}

CborError cbor_value_get_half_float(const CborValue *value, void *result)
{
    assert(value->type == CborHalfFloatType);

    // size has been computed already
    uint16_t v = get16(value->ptr + 1);
    memcpy(result, &v, sizeof(v));
    return CborNoError;
}
