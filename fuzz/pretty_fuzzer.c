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
 * Fuzzes the conversion of CBOR to diagnostic notation, both with
 * cbor_value_to_pretty_advance_flags() (writing to a FILE *) and with
 * cbor_value_to_pretty_stream() (writing through a callback).
 *
 * The first 4 bytes of the input are a configuration word whose low bits are
 * the CborPrettyFlags to use (see FuzzConfig); the rest is the CBOR payload,
 * which is converted as a sequence of items. The callback can be made to fail
 * after a number of calls, to exercise the error handling.
 */

#include "cbor.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef NDEBUG
#  error "The fuzzers must be built without NDEBUG: TinyCBOR's assertions check the API usage"
#endif

enum FuzzConfig {
    PrettyFlagsMask     = CborPrettyNumericEncodingIndicators | CborPrettyIndicateIndeterminateLength |
                          CborPrettyIndicateOverlongNumbers | CborPrettyShowStringFragments,

    /* if not zero, the callback fails on that call (bits 16 to 23) */
    ConfigFailShift     = 16,
    ConfigFailMask      = 0xff,

    /* use cbor_value_to_pretty_stream() instead of a FILE * */
    ConfigUseStream     = 0x80000000U
};

struct Output
{
    char buffer[256];
    size_t total;
    unsigned calls_until_failure;
};

static CborError stream_function(void *token, const char *fmt, ...)
{
    struct Output *output = (struct Output *)token;
    va_list ap;
    int n;

    if (output->calls_until_failure && --output->calls_until_failure == 0)
        return CborErrorIO;

    va_start(ap, fmt);
    n = vsnprintf(output->buffer, sizeof(output->buffer), fmt, ap);
    va_end(ap);
    if (n < 0)
        return CborErrorIO;
    output->total += (size_t)n;
    return CborNoError;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static FILE *out;
    struct Output output = { { 0 }, 0, 0 };
    CborParser parser;
    CborValue it;
    CborError err;
    uint32_t config;
    int flags;
    const uint8_t *ptr;
    const uint8_t *end;

    if (!out)
        out = fopen("/dev/null", "w");
    if (!out || size < sizeof(config))
        return 0;
    memcpy(&config, data, sizeof(config));
    data += sizeof(config);
    size -= sizeof(config);
    end = data + size;
    flags = (int)(config & PrettyFlagsMask);
    output.calls_until_failure = (config >> ConfigFailShift) & ConfigFailMask;

    ptr = data;
    while (ptr < end) {
        err = cbor_parser_init(ptr, (size_t)(end - ptr), 0, &parser, &it);
        if (!err) {
            if (config & ConfigUseStream)
                err = cbor_value_to_pretty_stream(stream_function, &output, &it, flags);
            else
                err = cbor_value_to_pretty_advance_flags(out, &it, flags);
        }
        if (err || cbor_value_get_next_byte(&it) <= ptr)
            break;
        ptr = cbor_value_get_next_byte(&it);
    }
    return 0;
}
