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
 * Fuzzes the parser API: the input is decoded as a sequence of CBOR items and
 * every item is visited, including those nested inside arrays, maps and tags,
 * extracting each value with the corresponding cbor_value_get_* function.
 *
 * The first 4 bytes of the input are a configuration word (see FuzzConfig);
 * the rest is the CBOR payload.
 */

#include "cbor.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef NDEBUG
#  error "The fuzzers must be built without NDEBUG: TinyCBOR's assertions check the API usage"
#endif

/* Bounds the harness' own recursion. The library has its own limit
 * (CBOR_PARSER_MAX_RECURSIONS) for the functions that recurse. */
#define MAX_NESTING             1024

/* Cross-checks whose cost is proportional to the size of the item (instead
 * of being constant per item) are limited, so the total time stays linear in
 * the size of the input */
#define MAX_EXPENSIVE_CHECKS    16

enum FuzzConfig {
    /* use cbor_parser_init_reader() instead of cbor_parser_init() */
    ConfigUseReader     = 0x01,

    /* reader only: make the first item visible one byte at a time, calling
     * cbor_value_reparse() after each CborErrorUnexpectedEOF */
    ConfigIncremental   = 0x02,

    /* reader only: skip containers with cbor_value_advance() and read
     * strings with cbor_value_copy_*_string() instead of chunk by chunk */
    ConfigReaderAdvance = 0x04
};

/* Abort on a result that is inconsistent with another API's result: if
 * this triggers, the library disagrees with itself. */
#define CHECK(cond)     do { if (!(cond)) abort(); } while (0)

/* keeps the decoded values from being optimized out */
static volatile uint64_t sink;

struct ReaderInput
{
    const uint8_t *data;
    size_t size;            /* how much of the data is visible to the parser */
    size_t consumed;
};

struct Context
{
    bool reader;
    bool reader_advance;
    int expensive_checks;
    uint8_t *buffer;        /* for cbor_value_copy_*_string() in reader mode */
    size_t buffer_size;
};

static bool reader_can_read_bytes(void *token, size_t len)
{
    struct ReaderInput *input = (struct ReaderInput *)token;
    return input->size - input->consumed >= len;
}

static void *reader_read_bytes(void *token, void *dst, size_t offset, size_t len)
{
    struct ReaderInput *input = (struct ReaderInput *)token;
    size_t available = input->size - input->consumed;

    /* the library must have checked with can_read_bytes() first */
    CHECK(offset <= available && len <= available - offset);
    return memcpy(dst, input->data + input->consumed + offset, len);
}

static void reader_advance_bytes(void *token, size_t len)
{
    struct ReaderInput *input = (struct ReaderInput *)token;
    CHECK(len <= input->size - input->consumed);
    input->consumed += len;
}

static CborError reader_transfer_string(void *token, const void **userptr, size_t offset, size_t len)
{
    struct ReaderInput *input = (struct ReaderInput *)token;
    size_t available = input->size - input->consumed;
    if (offset > available || len > available - offset)
        return CborErrorUnexpectedEOF;
    input->consumed += offset;
    *userptr = input->data + input->consumed;
    input->consumed += len;
    return CborNoError;
}

static const struct CborParserOperations reader_ops = {
    reader_can_read_bytes,
    reader_read_bytes,
    reader_advance_bytes,
    reader_transfer_string
};

static bool expensive_check(struct Context *ctx)
{
    if (ctx->expensive_checks == 0)
        return false;
    --ctx->expensive_checks;
    return true;
}

static uint64_t checksum(const void *ptr, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)ptr;
    uint64_t sum = 0;
    while (len--)
        sum = sum * 31 + *bytes++;
    return sum;
}

static CborError get_string_chunk(CborValue *it, const void **ptr, size_t *len)
{
    if (cbor_value_is_text_string(it))
        return cbor_value_get_text_string_chunk(it, (const char **)ptr, len, it);
    return cbor_value_get_byte_string_chunk(it, (const uint8_t **)ptr, len, it);
}

static CborError copy_string(const CborValue *it, void *buffer, size_t *len, CborValue *next)
{
    if (cbor_value_is_text_string(it))
        return cbor_value_copy_text_string(it, (char *)buffer, len, next);
    return cbor_value_copy_byte_string(it, (uint8_t *)buffer, len, next);
}

static CborError parse_value(struct Context *ctx, CborValue *it, int nesting);

static void parse_integer(const CborValue *it)
{
    uint64_t raw, u64;
    int64_t i64, i64_unchecked;
    int i, i_unchecked;

    cbor_value_get_raw_integer(it, &raw);
    sink += raw;
    if (cbor_value_is_unsigned_integer(it)) {
        cbor_value_get_uint64(it, &u64);
        CHECK(u64 == raw);
    } else {
        CHECK(cbor_value_is_negative_integer(it));
    }

    /* compare with the unchecked getters where the value fits */
    if (cbor_value_get_int64_checked(it, &i64) == CborNoError) {
        cbor_value_get_int64(it, &i64_unchecked);
        CHECK(i64 == i64_unchecked);
        CHECK(cbor_value_is_negative_integer(it) ? i64 < 0 : i64 >= 0);
    }
    if (cbor_value_get_int_checked(it, &i) == CborNoError) {
        cbor_value_get_int(it, &i_unchecked);
        CHECK(i == i_unchecked);
    }
}

static void parse_floating_point(const CborValue *it)
{
    float f;
    double d;
    uint16_t half;

    switch (cbor_value_get_type(it)) {
    case CborHalfFloatType:
        cbor_value_get_half_float(it, &half);
        cbor_value_get_half_float_as_float(it, &f);
        sink += half;
        /* NaN in half-precision is NaN in single-precision */
        CHECK(((half & 0x7c00) == 0x7c00 && (half & 0x3ff)) == (f != f));
        break;
    case CborFloatType:
        cbor_value_get_float(it, &f);
        memcpy(&half, &f, sizeof(half));
        sink += half;
        break;
    case CborDoubleType:
        cbor_value_get_double(it, &d);
        memcpy(&half, &d, sizeof(half));
        sink += half;
        break;
    default:
        break;
    }
}

/* Reads a string with every method and cross-checks them. On success, *it
 * points to the next item. */
static CborError parse_string(CborValue *it)
{
    CborError err;
    CborValue chunks = *it;
    CborValue next;
    size_t len, total, n;
    uint8_t *buffer;
    uint8_t *copy;
    const void *ptr;
    bool equals;
    bool text = cbor_value_is_text_string(it);

    if (cbor_value_is_length_known(it)) {
        err = cbor_value_get_string_length(it, &len);
        if (err)
            return err;
    }

    err = cbor_value_calculate_string_length(it, &len);
    if (err)
        return err;

    if (text)
        err = cbor_value_dup_text_string(it, (char **)&buffer, &n, &next);
    else
        err = cbor_value_dup_byte_string(it, &buffer, &n, &next);
    if (err)
        return err;
    CHECK(n == len);
    sink += checksum(buffer, len);

    /* copy into a buffer without room for the NUL and into one that is one
     * byte too small */
    copy = (uint8_t *)malloc(len ? len : 1);
    CHECK(copy);
    n = len;
    CHECK(copy_string(it, copy, &n, NULL) == CborNoError);
    CHECK(n == len && memcmp(copy, buffer, len) == 0);
    if (len) {
        n = len - 1;
        CHECK(copy_string(it, copy, &n, NULL) == CborErrorOutOfMemory);
    }
    free(copy);

    /* iterate over the chunks and compare them to the dup'ed string */
    err = cbor_value_begin_string_iteration(&chunks);
    total = 0;
    while (!err) {
        err = get_string_chunk(&chunks, &ptr, &n);
        if (err == CborErrorNoMoreStringChunks) {
            err = cbor_value_finish_string_iteration(&chunks);
            break;
        }
        if (!err) {
            CHECK(n <= len - total);
            CHECK(memcmp(buffer + total, ptr, n) == 0);
            total += n;
        }
    }
    CHECK(err == CborNoError);
    CHECK(total == len);
    CHECK(cbor_value_get_next_byte(&chunks) == cbor_value_get_next_byte(&next));

    if (text && memchr(buffer, '\0', len) == NULL) {
        CHECK(cbor_value_text_string_equals(it, (const char *)buffer, &equals) == CborNoError);
        CHECK(equals);
        if (len) {
            buffer[len - 1] = '\0';
            CHECK(cbor_value_text_string_equals(it, (const char *)buffer, &equals) == CborNoError);
            CHECK(!equals);
        }
    }

    free(buffer);
    *it = next;
    return CborNoError;
}

/* Reads a string when using the reader API: the source can't be rewound, so
 * the string must be read exactly once. */
static CborError parse_string_reader(struct Context *ctx, CborValue *it)
{
    CborError err;
    const void *ptr;
    size_t n;
    size_t len = 0;
    size_t total = 0;
    bool length_known = cbor_value_is_length_known(it);

    if (length_known) {
        err = cbor_value_get_string_length(it, &len);
        if (err)
            return err;
    }

    if (ctx->reader_advance) {
        /* the buffer is as large as the whole input, so it's big enough */
        n = ctx->buffer_size;
        err = copy_string(it, ctx->buffer, &n, it);
        if (err)
            return err;
        CHECK(!length_known || n == len);
        sink += checksum(ctx->buffer, n);
        return CborNoError;
    }

    err = cbor_value_begin_string_iteration(it);
    while (!err) {
        err = get_string_chunk(it, &ptr, &n);
        if (err == CborErrorNoMoreStringChunks) {
            CHECK(!length_known || total == len);
            return cbor_value_finish_string_iteration(it);
        }
        if (!err) {
            sink += checksum(ptr, n);
            total += n;
        }
    }
    return err;
}

static CborError parse_container(struct Context *ctx, CborValue *it, int nesting)
{
    CborError err;
    CborValue recursed;
    CborValue element;
    size_t len;

    if (nesting == 0)
        return CborErrorNestingTooDeep;

    if (cbor_value_is_length_known(it)) {
        if (cbor_value_is_array(it))
            err = cbor_value_get_array_length(it, &len);
        else
            err = cbor_value_get_map_length(it, &len);
        if (err)
            return err;
    }

    if (ctx->reader_advance)
        return cbor_value_advance(it);

    if (!ctx->reader && cbor_value_is_map(it) && expensive_check(ctx)) {
        /* the result is irrelevant, we only need the search to run */
        cbor_value_map_find_value(it, "a", &element);
    }

    err = cbor_value_enter_container(it, &recursed);
    if (err)
        return err;
    while (!cbor_value_at_end(&recursed)) {
        err = parse_value(ctx, &recursed, nesting - 1);
        if (err)
            return err;
    }
    return cbor_value_leave_container(it, &recursed);
}

/* Decodes the item at *it and advances past it */
static CborError parse_value(struct Context *ctx, CborValue *it, int nesting)
{
    CborValue skipped;
    CborTag tag;
    uint8_t simple;
    bool boolean;

    switch (cbor_value_get_type(it)) {
    case CborIntegerType:
        parse_integer(it);
        return cbor_value_advance_fixed(it);

    case CborByteStringType:
    case CborTextStringType:
        return ctx->reader ? parse_string_reader(ctx, it) : parse_string(it);

    case CborArrayType:
    case CborMapType:
        return parse_container(ctx, it, nesting);

    case CborTagType:
        cbor_value_get_tag(it, &tag);
        sink += tag;
        if (!ctx->reader && expensive_check(ctx)) {
            skipped = *it;
            if (cbor_value_skip_tag(&skipped) == CborNoError)
                CHECK(!cbor_value_is_tag(&skipped));
            /* this skips the tags too */
            cbor_value_text_string_equals(it, "", &boolean);
        }
        /* the tagged item is visited by the caller as the next item */
        return cbor_value_advance_fixed(it);

    case CborSimpleType:
        cbor_value_get_simple_type(it, &simple);
        sink += simple;
        return cbor_value_advance_fixed(it);

    case CborBooleanType:
        cbor_value_get_boolean(it, &boolean);
        sink += boolean;
        return cbor_value_advance_fixed(it);

    case CborNullType:
    case CborUndefinedType:
        return cbor_value_advance_fixed(it);

    case CborHalfFloatType:
    case CborFloatType:
    case CborDoubleType:
        parse_floating_point(it);
        return cbor_value_advance_fixed(it);

    case CborInvalidType:
    default:
        /* preparsing never produces an invalid iterator without an error */
        abort();
    }
}

static void parse_reader(struct Context *ctx, const uint8_t *data, size_t size, uint32_t config)
{
    struct ReaderInput input = { data, size, 0 };
    CborParser parser;
    CborValue it;
    CborError err;

    ctx->buffer_size = size + 1;
    ctx->buffer = (uint8_t *)malloc(ctx->buffer_size);
    CHECK(ctx->buffer);

    if (config & ConfigIncremental)
        input.size = 0;

    while (input.consumed < size) {
        size_t consumed = input.consumed;
        memset(&it, 0, sizeof(it));
        err = cbor_parser_init_reader(&reader_ops, &parser, &it, &input);
        while (err == CborErrorUnexpectedEOF && input.size < size) {
            /* make one more byte available and try again */
            CHECK(input.consumed == consumed);
            ++input.size;
            err = cbor_value_reparse(&it);
        }
        input.size = size;

        while (!err && !cbor_value_at_end(&it))
            err = parse_value(ctx, &it, MAX_NESTING);
        if (err || input.consumed == consumed)
            break;
    }
    free(ctx->buffer);
}

static void parse_buffer(struct Context *ctx, const uint8_t *data, size_t size)
{
    const uint8_t *ptr = data;
    const uint8_t *end = data + size;
    CborParser parser;
    CborValue it;
    CborValue advanced;
    CborError err;
    CborError advanced_err;

    /* the input is a sequence of items (RFC 8742) */
    while (ptr < end) {
        err = cbor_parser_init(ptr, (size_t)(end - ptr), 0, &parser, &it);
        while (!err && !cbor_value_at_end(&it)) {
            /* reference result to compare with the item-by-item traversal */
            advanced = it;
            advanced_err = cbor_value_advance(&advanced);
            err = parse_value(ctx, &it, MAX_NESTING);
            if (!err && !advanced_err)
                CHECK(cbor_value_get_next_byte(&it) == cbor_value_get_next_byte(&advanced));
        }
        if (err)
            break;
        CHECK(cbor_value_get_next_byte(&it) > ptr && cbor_value_get_next_byte(&it) <= end);
        ptr = cbor_value_get_next_byte(&it);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct Context ctx;
    uint32_t config;

    if (size < sizeof(config))
        return 0;
    memcpy(&config, data, sizeof(config));
    data += sizeof(config);
    size -= sizeof(config);

    memset(&ctx, 0, sizeof(ctx));
    ctx.expensive_checks = MAX_EXPENSIVE_CHECKS;
    if (config & ConfigUseReader) {
        ctx.reader = true;
        ctx.reader_advance = (config & ConfigReaderAdvance) != 0;
        parse_reader(&ctx, data, size, config);
    } else {
        parse_buffer(&ctx, data, size);
    }
    return 0;
}
