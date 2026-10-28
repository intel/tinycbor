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
 * Fuzzes the conversion of CBOR to JSON with cbor_value_to_json_advance().
 *
 * The first 4 bytes of the input are a configuration word whose low bits are
 * the CborToJsonFlags to use; the rest is the CBOR payload, which is
 * converted as a sequence of items. Where available (glibc), the output
 * stream can be made to fail after a number of bytes, to exercise the error
 * handling.
 */

#define _GNU_SOURCE 1   /* for fopencookie() */

#include "cbor.h"
#include "cborjson.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef NDEBUG
#  error "The fuzzers must be built without NDEBUG: TinyCBOR's assertions check the API usage"
#endif

enum FuzzConfig {
    JsonFlagsMask       = CborConvertAddMetadata | CborConvertTagsToObjects |
                          CborConvertByteStringsToBase64Url | CborConvertStringifyMapKeys,

    /* if not zero, writing fails after this many bytes (bits 16 to 31) */
    ConfigFailShift     = 16
};

#ifdef __GLIBC__
static ssize_t failing_write(void *cookie, const char *buf, size_t size)
{
    size_t *bytes_until_failure = (size_t *)cookie;
    (void)buf;
    if (size > *bytes_until_failure) {
        *bytes_until_failure = 0;
        return -1;
    }
    *bytes_until_failure -= size;
    return (ssize_t)size;
}

static FILE *open_failing_stream(size_t *bytes_until_failure)
{
    cookie_io_functions_t functions = { NULL, failing_write, NULL, NULL };
    FILE *f = fopencookie(bytes_until_failure, "w", functions);
    if (f)
        setvbuf(f, NULL, _IONBF, 0);    /* fail as soon as possible */
    return f;
}
#endif

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static FILE *devnull;
    FILE *out;
    CborParser parser;
    CborValue it;
    CborError err;
    uint32_t config;
    size_t bytes_until_failure;
    const uint8_t *ptr;
    const uint8_t *end;

    if (!devnull)
        devnull = fopen("/dev/null", "w");
    if (!devnull || size < sizeof(config))
        return 0;
    memcpy(&config, data, sizeof(config));
    data += sizeof(config);
    size -= sizeof(config);
    end = data + size;

    out = devnull;
    bytes_until_failure = config >> ConfigFailShift;
#ifdef __GLIBC__
    if (bytes_until_failure) {
        out = open_failing_stream(&bytes_until_failure);
        if (!out)
            return 0;
    }
#endif

    ptr = data;
    while (ptr < end) {
        err = cbor_parser_init(ptr, (size_t)(end - ptr), 0, &parser, &it);
        if (!err)
            err = cbor_value_to_json_advance(out, &it, (int)(config & JsonFlagsMask));
        if (err || cbor_value_get_next_byte(&it) <= ptr)
            break;
        ptr = cbor_value_get_next_byte(&it);
    }

    if (out != devnull)
        fclose(out);
    return 0;
}
