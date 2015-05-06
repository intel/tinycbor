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

static bool makeError(CborParser *parser, CborParserError error, uint64_t addend)
{
    (void)addend;
    parser->error = error;
    return false;
}
#ifdef CBOR_PARSER_NO_DETAILED_ERROR
// This way, the compiler should eliminate all error settings by dead code elimination
#  define makeError(parser, err, addend)    makeError(parser, (err) * 0 + CborErrorUnknownError, addend)
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

static inline bool extract_length(CborParser *parser, const char **ptr, size_t *len)
{
    uint8_t additional_information = **ptr & SmallValueMask;
    if (additional_information < Value8Bit) {
        *len = additional_information;
        ++(*ptr);
        return true;
    }
    if (unlikely(additional_information > Value64Bit))
        return makeError(parser, CborErrorIllegalNumber, additional_information);

    size_t bytesNeeded = 1 << (additional_information - Value8Bit);
    if (unlikely(*ptr + 1 + bytesNeeded > parser->end)) {
        return makeError(parser, CborErrorUnexpectedEOF, 0);
    } else if (bytesNeeded == 1) {
        *len = (*ptr)[1];
    } else if (bytesNeeded == 2) {
        *len = get16(*ptr + 1);
    } else if (bytesNeeded == 4) {
        *len = get32(*ptr + 1);
    } else {
        uint64_t v = get64(*ptr + 1);
        *len = v;
        if (v != *len)
            return makeError(parser, CborErrorDataTooLarge, 0);
    }
    *ptr += bytesNeeded;
    return true;
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

    size_t bytesNeeded = descriptor < Value8Bit ? 0 : (1 << (descriptor - Value8Bit));
    if (it->ptr + 1 + bytesNeeded > parser->end)
        goto error_eof;

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
            it->type = *it->ptr;
            break;

        case SimpleTypeInNextByte:
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

static bool advance_internal(CborValue *it)
{
    unsigned size = 1 << (*it->ptr - Value8Bit);
    if (it->ptr + size > it->parser->end)
        return makeError(it->parser, CborErrorUnexpectedEOF, 0);

    it->ptr += size;
    if (it->remaining == UINT32_MAX && *it->ptr == (char)BreakByte) {
        // end of map or array
        it->remaining = 0;
        return true;
    }

    if (it->remaining != UINT32_MAX)
        --it->remaining;
    preparse_value(it);
    return true;
}

static bool is_fixed_type(uint8_t type)
{
    return type != CborTextStringType && type != CborByteStringType && type != CborArrayType &&
           type != CborMapType;
}

/**
 * Advances the CBOR value \a it by one fixed-size position. Fixed-size types
 * are: integers, tags, simple types (including boolean, null and undefined
 * values) and floating point types. This function returns true if the
 * advancing succeeded, false on a decoding error or if there are no more
 * items.
 *
 * \sa cbor_value_at_end(), cbor_value_advance(), cbor_value_begin_recurse(), cbor_value_end_recurse()
 */
bool cbor_value_advance_fixed(CborValue *it)
{
    assert(it->type != CborInvalidType);
    assert(!is_fixed_type(it->type));
    return it->remaining && advance_internal(it);
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
 * This function returns true if it advanced by one element, false if this was
 * the last element or a decoding error happened.
 *
 * \sa cbor_value_at_end(), cbor_value_advance_fixed(), cbor_value_begin_recurse(), cbor_value_end_recurse()
 */
bool cbor_value_advance(CborValue *it)
{
    assert(it->type != CborInvalidType);
    if (!it->remaining)
        return false;
    if (is_fixed_type(it->type))
        return cbor_value_advance_fixed(it);

    if (cbor_value_is_container(it)) {
        // map or array
        CborValue recursed;
        if (!cbor_value_enter_container(it, &recursed))
            return false;
        while (!cbor_value_at_end(&recursed))
            if (!cbor_value_advance(&recursed))
                return false;
        if (!cbor_value_leave_container(it, &recursed))
            return false;
    } else {
        // string
        if (!cbor_value_copy_string(it, NULL, 0, it))
            return false;
    }
    return true;
}

/**
 * Returns true if the \a it value is a container and requires recursion in
 * order to decode (maps and arrays), false otherwise.
 */
bool cbor_value_is_container(const CborValue *it)
{
    return it->type == CborArrayType || it->type == CborMapType;
}

/**
 * Creates a CborValue iterator pointing to the first element of the container
 * represented by \a it and saves it in \a recursed. The \a it container object
 * needs to be kept and passed again to cbor_value_leave_container() in order
 * to continue iterating past this container.
 *
 * \sa cbor_value_is_container(), cbor_value_leave_container(), cbor_value_advance()
 */
bool cbor_value_enter_container(const CborValue *it, CborValue *recursed)
{
    assert(cbor_value_is_container(it));
    *recursed = *it;
    if (it->flags & CborIteratorFlag_UnknownLength) {
        recursed->remaining = UINT32_MAX;
    } else {
        uint64_t len = _cbor_value_extract_int64_helper(it);
        recursed->remaining = len;
        if (recursed->remaining != len || len == UINT32_MAX)
            return makeError(it->parser, CborErrorDataTooLarge, len);
    }
    return advance_internal(recursed);
}

/**
 * Updates \a it to point to the next element after the container. The \a
 * recursed object needs to point to the last element of the container.
 *
 * \sa cbor_value_enter_container(), cbor_value_at_end()
 */
bool cbor_value_leave_container(CborValue *it, const CborValue *recursed)
{
    assert(cbor_value_is_container(it));
    assert(cbor_value_at_end(recursed));
    it->ptr = recursed->ptr;
    return advance_internal(it);
}

static bool copy_chunk(CborParser *parser, uint8_t expectedType, char *dst, const char **src, size_t *chunkLen)
{
    // is this the right type?
    if ((**src & MajorTypeMask) != expectedType)
        return makeError(parser, CborErrorIllegalType, **src);

    if (!extract_length(parser, src, chunkLen))
        return false;       // error condition already set

    if (dst)
        memcpy(dst, *src, *chunkLen);

    *src += *chunkLen;
    return true;
}

/**
 * Calculates the length of the string in \a value and stores the result in \a
 * len. This function is different from cbor_value_get_string_length() in that
 * it calculates the length even for strings sent in chunks. For that reason,
 * this function may not run in constant time (it will run in O(n) time on the
 * number of chunks).
 *
 * This function returns true if the length was successfully calculated or false
 * if there was a decoding error.
 *
 * \note On 32-bit platforms, this function will return false and set condition
 * of \ref CborErrorDataTooLarge if the stream indicates a length that is too
 * big to fit in 32-bit.
 *
 * \sa cbor_value_get_string_length(), cbor_value_copy_string(), cbor_value_is_length_known()
 */
bool cbor_value_calculate_string_length(const CborValue *value, size_t *len)
{
    if (cbor_value_get_string_length(value, len))
        return true;

    // chunked string, iterate to calculate full size
    size_t total = 0;
    const char *ptr = value->ptr + 1;
    while (true) {
        if (ptr == value->parser->end)
            return makeError(value->parser, CborErrorUnexpectedEOF, 0);

        if (*ptr == (char)BreakByte) {
            ++ptr;
            break;
        }

        size_t chunkLen;
        if (!copy_chunk(value->parser, value->type, NULL, &ptr, &chunkLen))
            return false;

        if (!add_check_overflow(total, chunkLen, &total))
            return makeError(value->parser, CborErrorDataTooLarge, 0);
    }

    *len = total;
    return true;
}

/**
 * Allocates memory for the string pointed by \a value and copies it into this
 * buffer. The pointer to the buffer is stored in \a buffer and the number of
 * bytes copied is stored in \a len (those variables must not be NULL).
 *
 * This function returns false if a decoding error occurred or if we ran out of
 * memory. OOM situations are indicated by setting both \c{*buffer} to \c NULL.
 * If the caller needs to recover from an OOM condition, it should initialize
 * the variable to a non-NULL value (it does not have to be a valid pointer).
 *
 * On success, this function returns true and \c{*buffer} will contain a valid
 * pointer that must be freed by calling \c{free()}. This is the case even for
 * zero-length strings.
 *
 * The \a next pointer, if not null, will be updated to point to the next item
 * after this string.
 *
 * \note This function does not perform UTF-8 validation on the incoming text
 * string.
 *
 * \sa cbor_value_copy_string()
 */
bool cbor_value_dup_string(const CborValue *value, char **buffer, size_t *len, CborValue *next)
{
    assert(buffer);
    assert(len);
    if (!cbor_value_calculate_string_length(value, len))
        return false;

    *buffer = malloc(*len + 1);
    if (!*buffer) {
        // out of memory
        return false;
    }
    size_t copied = cbor_value_copy_string(value, *buffer, *len + 1, next);
    if (copied == SIZE_MAX) {
        free(*buffer);
        return false;
    }
    assert(copied == *len);
    return true;
}

/**
 * Copies the string pointed by \a value into the buffer provided at \a buffer
 * of \a buflen bytes. If \a buffer is a NULL pointer, this function will not
 * copy anything and will only update the \a next value.
 *
 * This function returns \c SIZE_MAX if a decoding error occurred or if the
 * buffer was not large enough. If you need to calculate the length of the
 * string in order to preallocate a buffer, use
 * cbor_value_calculate_string_length().
 *
 * On success, this function returns the number of bytes copied. If the buffer
 * is large enough, this function will insert a null byte after the last copied
 * byte, to facilitate manipulation of text strings. That byte is not included
 * in the returned value.
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
size_t cbor_value_copy_string(const CborValue *value, char *buffer,
                              size_t buflen, CborValue *next)
{
    assert(cbor_value_is_byte_string(value) || cbor_value_is_text_string(value));

    size_t total;
    const char *ptr = value->ptr;
    if (cbor_value_is_length_known(value)) {
        // easy case: fixed length
        if (!extract_length(value->parser, &ptr, &total))
            return SIZE_MAX;
        if (buffer) {
            if (buflen < total)
                return SIZE_MAX;
            memcpy(buffer, ptr, total);
            ptr += total;
        }
    } else {
        // chunked
        ++ptr;
        while (true) {
            if (ptr == value->parser->end)
                return makeError(value->parser, CborErrorUnexpectedEOF, 0);

            if (*ptr == (char)BreakByte) {
                ++ptr;
                break;
            }

            // is this the right type?
            if ((*ptr & MajorTypeMask) != value->type)
                return makeError(value->parser, CborErrorIllegalType, *ptr);

            size_t chunkLen;
            if (!extract_length(value->parser, &ptr, &chunkLen))
                return false;       // error condition already set

            size_t newTotal;
            if (unlikely(!add_check_overflow(total, chunkLen, &newTotal)))
                return makeError(value->parser, CborErrorDataTooLarge, 0);

            if (buffer) {
                if (buflen < newTotal)
                    return SIZE_MAX;
                memcpy(buffer + total, ptr, chunkLen);
            }
            ptr += chunkLen;
            total = newTotal;
        }
    }

    // is there enough room for the ending NUL byte?
    if (buffer && buflen > total)
        buffer[total] = '\0';

    if (next) {
        *next = *value;
        next->ptr = ptr;
        if (!next->remaining) {
            next->type = CborInvalidType;
        } else {
            if (next->remaining != UINT32_MAX)
                --next->remaining;
            preparse_value(next);
        }
    }
    return total;
}

bool cbor_value_get_half_float(const CborValue *value, void *result)
{
    if (value->type != CborHalfFloatType)
        return false;

    // size has been computed already
    uint16_t v = get16(value->ptr + 1);
    memcpy(result, &v, sizeof(v));
    return true;
}
