/****************************************************************************
**
** Copyright (C) 2017 Intel Corporation
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
#ifndef __STDC_LIMIT_MACROS
#  define __STDC_LIMIT_MACROS 1
#endif

#include "cbor.h"
#include "cborinternal_p.h"
#include "compilersupport_p.h"

#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/**
 * \defgroup CborPretty Converting CBOR to text
 * \brief Group of functions used to convert CBOR to text form.
 *
 * This group contains two functions that are can be used to convert one
 * CborValue object to a text representation. This module attempts to follow
 * the recommendations from RFC 7049 section 6 "Diagnostic Notation", though it
 * has a few differences. They are noted below.
 *
 * TinyCBOR does not provide a way to convert from the text representation back
 * to encoded form. To produce a text form meant to be parsed, CborToJson is
 * recommended instead.
 *
 * Either of the functions in this section will attempt to convert exactly one
 * CborValue object to text. Those functions may return any error documented
 * for the functions for CborParsing. In addition, if the C standard library
 * stream functions return with error, the text conversion will return with
 * error CborErrorIO.
 *
 * These functions also perform UTF-8 validation in CBOR text strings. If they
 * encounter a sequence of bytes that not permitted in UTF-8, they will return
 * CborErrorInvalidUtf8TextString. That includes encoding of surrogate points
 * in UTF-8.
 *
 * \warning The output type produced by these functions is not guaranteed to
 * remain stable. A future update of TinyCBOR may produce different output for
 * the same input and parsers may be unable to handle them.
 *
 * \sa CborParsing, CborToJson, cbor_parser_init()
 */

/**
 * \addtogroup CborPretty
 * @{
 * <h2 class="groupheader">Text format</h2>
 *
 * As described in RFC 7049 section 6 "Diagnostic Notation", the format is
 * largely borrowed from JSON, but modified to suit CBOR's different data
 * types. TinyCBOR makes further modifications to distinguish different, but
 * similar values.
 *
 * CBOR values are currently encoded as follows:
 * \par Integrals (unsigned and negative)
 *      Base-10 (decimal) text representation of the value
 * \par Byte strings:
 *      <tt>"h'"</tt> followed by the Base16 (hex) representation of the binary data, followed by an ending quote (')
 * \par Text strings:
 *      C-style escaped string in quotes, with C11/C++11 escaping of Unicode codepoints above U+007F.
 * \par Tags:
 *      Tag value, with the tagged value in parentheses. No special encoding of the tagged value is performed.
 * \par Simple types:
 *      <tt>"simple(nn)"</tt> where \c nn is the simple value
 * \par Null:
 *      \c null
 * \par Undefined:
 *      \c undefined
 * \par Booleans:
 *      \c true or \c false
 * \par Floating point:
 *      If NaN or infinite, the actual words \c NaN or \c infinite.
 *      Otherwise, the decimal representation with as many digits as necessary to ensure no loss of information.
 *      By default, float values are suffixed by "f" and half-float values suffixed by "f16" (doubles have no suffix).
 *      If the CborPrettyNumericEncodingIndicators flag is active, the values instead are encoded following the
 *      Section 6 recommended encoding indicators: float values are suffixed with "_2" and half-float with "_1".
 *      A dot is always present.
 * \par Arrays:
 *      Comma-separated list of elements, enclosed in square brackets ("[" and "]").
 *     If the array length is indeterminate, an underscore ("_") appears immediately after the opening bracket.
 * \par Maps:
 *      Comma-separated list of key-value pairs, with the key and value separated
 *      by a colon (":"), enclosed in curly braces ("{" and "}").
 *      If the map length is indeterminate, an underscore ("_") appears immediately after the opening brace.
 */

/**
 * \enum CborPrettyFlags
 * The CborPrettyFlags enum contains flags that control the conversion of CBOR to text format.
 *
 * \value CborPrettyNumericEncodingIndicators   Use numeric encoding indicators instead of textual for float and half-float.
 * \value CborPrettyTextualEncodingIndicators   Use textual encoding indicators for float ("f") and half-float ("f16").
 * \value CborPrettyShowStringFragments         If the byte or text string is transmitted in chunks, show each individually.
 * \value CborPrettyMergeStringFragment         Merge all chunked byte or text strings and display them in a single entry.
 * \value CborPrettyDefaultFlags       Default conversion flags.
 */

static CborError hexDump(FILE *out, const void *ptr, size_t n)
{
    const uint8_t *buffer = (const uint8_t *)ptr;
    while (n--) {
        int r = fprintf(out, "%02" PRIx8, *buffer++);
        if (r < 0)
            return CborErrorIO;
    }
    return CborNoError;
}

/* This function decodes buffer as UTF-8 and prints as escaped UTF-16.
 * On UTF-8 decoding error, it returns CborErrorInvalidUtf8TextString */
static CborError utf8EscapedDump(FILE *out, const void *ptr, size_t n)
{
    const char *buffer = (const char *)ptr;
    uint32_t uc;
    while (n--) {
        uc = (uint8_t)*buffer++;
        if (uc < 0x80) {
            /* single-byte UTF-8 */
            if (uc < 0x7f && uc >= 0x20 && uc != '\\' && uc != '"') {
                if (fprintf(out, "%c", (char)uc) < 0)
                    return CborErrorIO;
                continue;
            }

            /* print as an escape sequence */
            char escaped = (char)uc;
            switch (uc) {
            case '"':
            case '\\':
                break;
            case '\b':
                escaped = 'b';
                break;
            case '\f':
                escaped = 'f';
                break;
            case '\n':
                escaped = 'n';
                break;
            case '\r':
                escaped = 'r';
                break;
            case '\t':
                escaped = 't';
                break;
            default:
                goto print_utf16;
            }
            if (fprintf(out, "\\%c", escaped) < 0)
                return CborErrorIO;
            continue;
        }

        /* multi-byte UTF-8, decode it */
        unsigned charsNeeded;
        uint32_t min_uc;
        if (unlikely(uc <= 0xC1))
            return CborErrorInvalidUtf8TextString;
        if (uc < 0xE0) {
            /* two-byte UTF-8 */
            charsNeeded = 2;
            min_uc = 0x80;
            uc &= 0x1f;
        } else if (uc < 0xF0) {
            /* three-byte UTF-8 */
            charsNeeded = 3;
            min_uc = 0x800;
            uc &= 0x0f;
        } else if (uc < 0xF5) {
            /* four-byte UTF-8 */
            charsNeeded = 4;
            min_uc = 0x10000;
            uc &= 0x07;
        } else {
            return CborErrorInvalidUtf8TextString;
        }

        if (n < charsNeeded - 1)
            return CborErrorInvalidUtf8TextString;
        n -= charsNeeded - 1;

        /* first continuation character */
        uint8_t b = (uint8_t)*buffer++;
        if ((b & 0xc0) != 0x80)
            return CborErrorInvalidUtf8TextString;
        uc <<= 6;
        uc |= b & 0x3f;

        if (charsNeeded > 2) {
            /* second continuation character */
            b = (uint8_t)*buffer++;
            if ((b & 0xc0) != 0x80)
                return CborErrorInvalidUtf8TextString;
            uc <<= 6;
            uc |= b & 0x3f;

            if (charsNeeded > 3) {
                /* third continuation character */
                b = (uint8_t)*buffer++;
                if ((b & 0xc0) != 0x80)
                    return CborErrorInvalidUtf8TextString;
                uc <<= 6;
                uc |= b & 0x3f;
            }
        }

        /* overlong sequence? surrogate pair? out or range? */
        if (uc < min_uc || uc - 0xd800U < 2048U || uc > 0x10ffff)
            return CborErrorInvalidUtf8TextString;

        /* now print the sequence */
        if (charsNeeded > 3) {
            /* needs surrogate pairs */
            if (fprintf(out, "\\u%04" PRIX32 "\\u%04" PRIX32,
                        (uc >> 10) + 0xd7c0,    /* high surrogate */
                        (uc % 0x0400) + 0xdc00) < 0)
                return CborErrorIO;
        } else {
print_utf16:
            /* no surrogate pair needed */
            if (fprintf(out, "\\u%04" PRIX32, uc) < 0)
                return CborErrorIO;
        }
    }
    return CborNoError;
}

static CborError value_to_pretty(FILE *out, CborValue *it, int flags);
static CborError container_to_pretty(FILE *out, CborValue *it, CborType containerType, int flags)
{
    const char *comma = "";
    while (!cbor_value_at_end(it)) {
        if (fprintf(out, "%s", comma) < 0)
            return CborErrorIO;
        comma = ", ";

        CborError err = value_to_pretty(out, it, flags);
        if (err)
            return err;

        if (containerType == CborArrayType)
            continue;

        /* map: that was the key, so get the value */
        if (fprintf(out, ": ") < 0)
            return CborErrorIO;
        err = value_to_pretty(out, it, flags);
        if (err)
            return err;
    }
    return CborNoError;
}

static CborError value_to_pretty(FILE *out, CborValue *it, int flags)
{
    CborError err;
    CborType type = cbor_value_get_type(it);
    switch (type) {
    case CborArrayType:
    case CborMapType: {
        /* recursive type */
        CborValue recursed;

        if (fprintf(out, type == CborArrayType ? "[" : "{") < 0)
            return CborErrorIO;
        if (!cbor_value_is_length_known(it)) {
            if (fprintf(out, "_ ") < 0)
                return CborErrorIO;
        }

        err = cbor_value_enter_container(it, &recursed);
        if (err) {
            it->ptr = recursed.ptr;
            return err;       /* parse error */
        }
        err = container_to_pretty(out, &recursed, type, flags);
        if (err) {
            it->ptr = recursed.ptr;
            return err;       /* parse error */
        }
        err = cbor_value_leave_container(it, &recursed);
        if (err)
            return err;       /* parse error */

        if (fprintf(out, type == CborArrayType ? "]" : "}") < 0)
            return CborErrorIO;
        return CborNoError;
    }

    case CborIntegerType: {
        uint64_t val;
        cbor_value_get_raw_integer(it, &val);    /* can't fail */

        if (cbor_value_is_unsigned_integer(it)) {
            if (fprintf(out, "%" PRIu64, val) < 0)
                return CborErrorIO;
        } else {
            /* CBOR stores the negative number X as -1 - X
             * (that is, -1 is stored as 0, -2 as 1 and so forth) */
            if (++val) {                /* unsigned overflow may happen */
                if (fprintf(out, "-%" PRIu64, val) < 0)
                    return CborErrorIO;
            } else {
                /* overflown
                 *   0xffff`ffff`ffff`ffff + 1 =
                 * 0x1`0000`0000`0000`0000 = 18446744073709551616 (2^64) */
                if (fprintf(out, "-18446744073709551616") < 0)
                    return CborErrorIO;
            }
        }
        break;
    }

    case CborByteStringType:
    case CborTextStringType: {
        size_t n = 0;
        const void *ptr;
        bool showingFragments = (flags & CborPrettyShowStringFragments) && !cbor_value_is_length_known(it);
        const char *separator = "";
        char close = '\'';
        char open[3] = "h'";

        if (type == CborTextStringType) {
            close = open[0] = '"';
            open[1] = '\0';
        }

        if (fputs(showingFragments ? "(_ " : open, out) < 0)
            return CborErrorIO;

        while (1) {
            err = _cbor_value_get_string_chunk(it, &ptr, &n, it);
            if (err)
                return err;
            if (!ptr)
                break;

            if (showingFragments && fprintf(out, "%s%s", separator, open) < 0)
                return CborErrorIO;
            err = (type == CborByteStringType ? hexDump(out, ptr, n) : utf8EscapedDump(out, ptr, n));
            if (err)
                return err;
            if (showingFragments) {
                if (fputc(close, out) < 0)
                    return CborErrorIO;
                separator = ", ";
            }
        }

        if (fputc(showingFragments ? ')' : close, out) < 0)
            return CborErrorIO;
        return CborNoError;
    }

    case CborTagType: {
        CborTag tag;
        cbor_value_get_tag(it, &tag);       /* can't fail */
        if (fprintf(out, "%" PRIu64 "(", tag) < 0)
            return CborErrorIO;
        err = cbor_value_advance_fixed(it);
        if (err)
            return err;
        err = value_to_pretty(out, it, flags);
        if (err)
            return err;
        if (fprintf(out, ")") < 0)
            return CborErrorIO;
        return CborNoError;
    }

    case CborSimpleType: {
        uint8_t simple_type;
        cbor_value_get_simple_type(it, &simple_type);  /* can't fail */
        if (fprintf(out, "simple(%" PRIu8 ")", simple_type) < 0)
            return CborErrorIO;
        break;
    }

    case CborNullType:
        if (fprintf(out, "null") < 0)
            return CborErrorIO;
        break;

    case CborUndefinedType:
        if (fprintf(out, "undefined") < 0)
            return CborErrorIO;
        break;

    case CborBooleanType: {
        bool val;
        cbor_value_get_boolean(it, &val);       /* can't fail */
        if (fprintf(out, val ? "true" : "false") < 0)
            return CborErrorIO;
        break;
    }

    case CborDoubleType: {
        const char *suffix;
        double val;
        int r;
        if (false) {
            float f;
    case CborFloatType:
            cbor_value_get_float(it, &f);
            val = f;
            suffix = flags & CborPrettyNumericEncodingIndicators ? "_2" : "f";
        } else if (false) {
            uint16_t f16;
    case CborHalfFloatType:
            cbor_value_get_half_float(it, &f16);
            val = decode_half(f16);
            suffix = flags & CborPrettyNumericEncodingIndicators ? "_1" : "f16";
        } else {
            cbor_value_get_double(it, &val);
            suffix = "";
        }

        if ((flags & CborPrettyNumericEncodingIndicators) == 0) {
            r = fpclassify(val);
            if (r == FP_NAN || r == FP_INFINITE)
                suffix = "";
        }

        uint64_t ival = (uint64_t)fabs(val);
        if (ival == fabs(val)) {
            /* this double value fits in a 64-bit integer, so show it as such
             * (followed by a floating point suffix, to disambiguate) */
            r = fprintf(out, "%s%" PRIu64 ".%s", val < 0 ? "-" : "", ival, suffix);
        } else {
            /* this number is definitely not a 64-bit integer */
            r = fprintf(out, "%." DBL_DECIMAL_DIG_STR "g%s", val, suffix);
        }
        if (r < 0)
            return CborErrorIO;
        break;
    }

    case CborInvalidType:
        if (fprintf(out, "invalid") < 0)
            return CborErrorIO;
        return CborErrorUnknownType;
    }

    err = cbor_value_advance_fixed(it);
    return err;
}

/**
 * \fn CborError cbor_value_to_pretty(FILE *out, const CborValue *value)
 *
 * Converts the current CBOR type pointed by \a value to its textual
 * representation and writes it to the \a out stream. If an error occurs, this
 * function returns an error code similar to CborParsing.
 *
 * \sa cbor_value_to_pretty_advance(), cbor_value_to_json_advance()
 */

/**
 * Converts the current CBOR type pointed by \a value to its textual
 * representation and writes it to the \a out stream. If an error occurs, this
 * function returns an error code similar to CborParsing.
 *
 * If no error ocurred, this function advances \a value to the next element.
 * Often, concatenating the text representation of multiple elements can be
 * done by appending a comma to the output stream.
 *
 * \sa cbor_value_to_pretty(), cbor_value_to_json_advance()
 */
CborError cbor_value_to_pretty_advance(FILE *out, CborValue *value)
{
    return value_to_pretty(out, value, CborPrettyDefaultFlags);
}

/**
 * Converts the current CBOR type pointed by \a value to its textual
 * representation and writes it to the \a out stream. If an error occurs, this
 * function returns an error code similar to CborParsing.
 *
 * The textual representation can be controlled by the \a flags parameter (see
 * CborPrettyFlags for more information).
 *
 * If no error ocurred, this function advances \a value to the next element.
 * Often, concatenating the text representation of multiple elements can be
 * done by appending a comma to the output stream.
 *
 * \sa cbor_value_to_pretty(), cbor_value_to_json_advance()
 */
CborError cbor_value_to_pretty_advance_flags(FILE *out, CborValue *value, int flags)
{
    return value_to_pretty(out, value, flags);
}

/** @} */
