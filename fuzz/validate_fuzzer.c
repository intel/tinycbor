/****************************************************************************
**
** Copyright (C) 2026 Topogamer0
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

/*
 * Fuzzes cbor_value_validate() with every combination of validation flags.
 *
 * The first 4 bytes of the input are the CborValidationFlags to use; the rest
 * is the CBOR payload.
 */

#include "cbor.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef NDEBUG
#  error "The fuzzers must be built without NDEBUG: TinyCBOR's assertions check the API usage"
#endif

/* Abort on a result that contradicts the documented behavior */
#define CHECK(cond)     do { if (!(cond)) abort(); } while (0)

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    CborParser parser;
    CborValue it;
    CborValue copy;
    CborError err;
    CborError basic_err;
    uint32_t flags;

    if (size < sizeof(flags))
        return 0;
    memcpy(&flags, data, sizeof(flags));
    data += sizeof(flags);
    size -= sizeof(flags);

    if (cbor_parser_init(data, size, 0, &parser, &it) != CborNoError)
        return 0;

    err = cbor_value_validate(&it, flags);
    basic_err = cbor_value_validate_basic(&it);

    /* "If flags is CborValidateBasic, the result should be the same as
     * cbor_value_validate_basic()". That one advances over a tag without
     * looking at the tagged item, so only compare for other types. The two
     * also count nesting differently towards CBOR_PARSER_MAX_RECURSIONS. */
    if (flags == CborValidateBasic && !cbor_value_is_tag(&it) &&
            err != CborErrorNestingTooDeep && basic_err != CborErrorNestingTooDeep)
        CHECK((err == CborNoError) == (basic_err == CborNoError));

    /* "If no error was found, it returns CborNoError and the application can
     * iterate over the items with certainty that no errors will appear
     * during parsing" */
    if (err == CborNoError) {
        CHECK(basic_err == CborNoError);
        /* cbor_value_advance() over a tag stops at the tagged item */
        copy = it;
        CHECK(cbor_value_skip_tag(&copy) == CborNoError);
        CHECK(cbor_value_advance(&copy) == CborNoError);
        if (flags & CborValidateCompleteData)
            CHECK(cbor_value_get_next_byte(&copy) == data + size);
    }
    return 0;
}
