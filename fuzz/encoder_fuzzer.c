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
 * Fuzzes the encoder API. The input is a program: after two configuration
 * bytes, each byte is a command (low nibble) with a small argument (high
 * nibble), optionally followed by operand bytes. See run_program() for the
 * command list.
 *
 * The program is run with an output buffer of the size given in the
 * configuration; if that buffer was too small, it is run again with the size
 * that cbor_encoder_get_extra_bytes_needed() asked for. It is also run with a
 * writer function. All runs must produce the same bytes, which are then
 * parsed back, validated, and compared to what the program encoded.
 */

#include "cbor.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef NDEBUG
#  error "The fuzzers must be built without NDEBUG: TinyCBOR's assertions check the API usage"
#endif

#define MAX_NESTING     256

/* Nested containers and chained tags must stay within the parser's limit
 * (CBOR_PARSER_MAX_RECURSIONS, 1024) for the result to be parsed back */
#define MAX_TAGS        512

/* Abort on a result that contradicts the documented behavior */
#define CHECK(cond)     do { if (!(cond)) abort(); } while (0)

enum Command {
    CmdUInt = 0,
    CmdNegativeInt,
    CmdInt,
    CmdSimpleValue,
    CmdBooleanNullUndefined,
    CmdTag,
    CmdTextString,
    CmdByteString,
    CmdHalfFloat,
    CmdFloat,
    CmdDouble,
    CmdFloatAsHalfFloat,
    CmdCreateArray,
    CmdCreateMap,
    CmdCloseContainer,
    CmdRaw
};

/* what the program encoded, to be compared with what is parsed back */
enum EventType {
    EventInteger,
    EventNegativeInteger,
    EventSimple,            /* includes booleans, null and undefined */
    EventTag,
    EventString,
    EventHalfFloat,
    EventFloatAsHalfFloat,  /* cbor_encode_float_as_half_float(): value is the float */
    EventFloat,
    EventDouble,
    EventArray,
    EventMap,
    EventEnd
};

struct Event
{
    enum EventType type;
    CborType string_type;
    bool indefinite;
    uint64_t value;         /* value, bit pattern, length or number of pairs */
    const uint8_t *data;    /* string contents */
};

struct Input
{
    const uint8_t *ptr;
    const uint8_t *end;
};

struct Level
{
    CborEncoder encoder;
    size_t length;          /* declared number of items (pairs for maps) */
    size_t count;           /* number of items written, tags excluded */
    bool map;
    bool tag_pending;
};

struct WriterOutput
{
    uint8_t *data;
    size_t size;
    int command;            /* the command being run, to check the append type */
};

struct Run
{
    struct Level levels[MAX_NESTING + 1];
    int depth;
    int tags;
    struct WriterOutput *writer;

    struct Event *events;
    size_t event_count;
    size_t event_capacity;
    bool record_events;

    CborError error;        /* first error other than CborErrorOutOfMemory */
    bool out_of_memory;
    bool used_raw;
};

static uint8_t get_byte(struct Input *in)
{
    return in->ptr < in->end ? *in->ptr++ : 0;
}

static uint64_t get_number(struct Input *in, unsigned bytes)
{
    uint64_t v = 0;
    while (bytes--)
        v = (v << 8) | get_byte(in);
    return v;
}

/* small immediates (< 8) or 1, 2, 4 or 8 bytes from the input */
static uint64_t get_value(struct Input *in, unsigned arg)
{
    if (arg < 8)
        return arg;
    return get_number(in, 1U << (arg & 3));
}

static size_t get_data(struct Input *in, unsigned arg, const uint8_t **data)
{
    size_t len = arg < 15 ? arg : (size_t)get_number(in, 2);
    size_t available = (size_t)(in->end - in->ptr);
    if (len > available)
        len = available;
    *data = in->ptr;
    in->ptr += len;
    return len;
}

static CborError writer_function(void *token, const void *data, size_t len, CborEncoderAppendType type)
{
    struct WriterOutput *out = (struct WriterOutput *)token;
    uint8_t *ptr;

    switch (type) {
    case CborEncoderAppendCborData:
        CHECK(out->command != CmdRaw);
        break;
    case CborEncoderAppendStringData:
        CHECK(out->command == CmdTextString || out->command == CmdByteString);
        break;
    case CborEncoderAppendRawData:
        CHECK(out->command == CmdRaw);
        break;
    default:
        abort();
    }

    if (len == 0)
        return CborNoError;
    ptr = (uint8_t *)realloc(out->data, out->size + len);
    if (!ptr)
        return CborErrorOutOfMemory;
    memcpy(ptr + out->size, data, len);
    out->data = ptr;
    out->size += len;
    return CborNoError;
}

static void record(struct Run *run, enum EventType type, uint64_t value)
{
    struct Event *ev;
    if (!run->record_events)
        return;
    if (run->event_count == run->event_capacity) {
        run->event_capacity = run->event_capacity ? 2 * run->event_capacity : 64;
        run->events = (struct Event *)realloc(run->events, run->event_capacity * sizeof(struct Event));
        CHECK(run->events);
    }
    ev = &run->events[run->event_count++];
    memset(ev, 0, sizeof(*ev));
    ev->type = type;
    ev->value = value;
}

/* returns false if the program must stop */
static bool check_error(struct Run *run, CborError err)
{
    if (err == CborErrorOutOfMemory) {
        /* documented as recoverable: the encoder keeps counting */
        run->out_of_memory = true;
        return true;
    }
    if (err) {
        run->error = err;
        return false;
    }
    return true;
}

static void item_added(struct Run *run)
{
    struct Level *level = &run->levels[run->depth];
    if (level->tag_pending)
        level->tag_pending = false;
    ++level->count;
}

static bool encode_null(struct Run *run)
{
    record(run, EventSimple, CborNullType & 0x1f);
    item_added(run);
    return check_error(run, cbor_encode_null(&run->levels[run->depth].encoder));
}

static bool close_container(struct Run *run)
{
    struct Level *level = &run->levels[run->depth];
    size_t needed = level->map ? 2 * level->length : level->length;
    CborError err;

    /* complete what we can, so the result is more often valid CBOR */
    if (level->tag_pending && !encode_null(run))
        return false;
    if (level->length == CborIndefiniteLength) {
        if (level->map && level->count % 2 && !encode_null(run))
            return false;
    } else if (level->count < needed && needed - level->count <= 16) {
        while (level->count < needed) {
            if (!encode_null(run))
                return false;
        }
    }

    record(run, EventEnd, 0);
    --run->depth;
    err = cbor_encoder_close_container(&run->levels[run->depth].encoder, &level->encoder);
    return check_error(run, err);
}

static bool create_container(struct Run *run, bool map, unsigned arg, struct Input *in)
{
    struct Level *parent = &run->levels[run->depth];
    struct Level *level;
    size_t length;
    CborError err;

    if (arg < 14)
        length = arg;
    else if (arg == 14)
        length = CborIndefiniteLength;
    else
        length = (size_t)get_number(in, 8);
    if (run->depth == MAX_NESTING)
        return true;
    level = &run->levels[run->depth + 1];
    memset(level, 0, sizeof(*level));
    level->length = length;
    level->map = map;

    if (map)
        err = cbor_encoder_create_map(&parent->encoder, &level->encoder, length);
    else
        err = cbor_encoder_create_array(&parent->encoder, &level->encoder, length);
    if (!check_error(run, err))
        return false;

    record(run, map ? EventMap : EventArray, length);
    if (run->record_events)
        run->events[run->event_count - 1].indefinite = length == CborIndefiniteLength;
    item_added(run);
    ++run->depth;
    return true;
}

/* Runs the whole program on the encoder in run->levels[0] */
static void run_program(struct Run *run, struct Input in)
{
    while (in.ptr < in.end) {
        struct Level *level = &run->levels[run->depth];
        CborEncoder *encoder = &level->encoder;
        uint8_t command = get_byte(&in);
        unsigned arg = command >> 4;
        const uint8_t *data;
        size_t len;
        uint64_t v;
        uint8_t u8;
        uint16_t u16;
        uint32_t u32;
        float f;
        CborError err = CborNoError;

        if (run->writer)
            run->writer->command = command & 0xf;

        switch (command & 0xf) {
        case CmdUInt:
            v = get_value(&in, arg);
            record(run, EventInteger, v);
            err = cbor_encode_uint(encoder, v);
            item_added(run);
            break;

        case CmdNegativeInt:
            /* zero encodes -2^64 */
            v = get_value(&in, arg);
            record(run, EventNegativeInteger, v - 1);
            err = cbor_encode_negative_int(encoder, v);
            item_added(run);
            break;

        case CmdInt:
            v = get_value(&in, arg);
            if (arg & 4)
                v = ~v;     /* make it negative */
            if ((int64_t)v < 0)
                record(run, EventNegativeInteger, ~v);
            else
                record(run, EventInteger, v);
            err = cbor_encode_int(encoder, (int64_t)v);
            item_added(run);
            break;

        case CmdSimpleValue:
            /* 25 to 31 are rejected with CborErrorIllegalSimpleType; 24 is
             * accepted but can't be encoded as well-formed CBOR */
            u8 = get_byte(&in);
            if (u8 == 24)
                break;
            record(run, EventSimple, u8);
            err = cbor_encode_simple_value(encoder, u8);
            item_added(run);
            break;

        case CmdBooleanNullUndefined:
            switch (arg & 3) {
            case 0:
                record(run, EventSimple, (CborBooleanType & 0x1f) - 1);
                err = cbor_encode_boolean(encoder, false);
                break;
            case 1:
                record(run, EventSimple, CborBooleanType & 0x1f);
                err = cbor_encode_boolean(encoder, true);
                break;
            case 2:
                record(run, EventSimple, CborNullType & 0x1f);
                err = cbor_encode_null(encoder);
                break;
            case 3:
                record(run, EventSimple, CborUndefinedType & 0x1f);
                err = cbor_encode_undefined(encoder);
                break;
            }
            item_added(run);
            break;

        case CmdTag:
            v = get_value(&in, arg);
            if (++run->tags > MAX_TAGS)
                break;
            record(run, EventTag, v);
            err = cbor_encode_tag(encoder, v);
            level->tag_pending = true;
            break;

        case CmdTextString:
        case CmdByteString:
            len = get_data(&in, arg, &data);
            record(run, EventString, len);
            if (run->record_events) {
                struct Event *ev = &run->events[run->event_count - 1];
                ev->data = data;
                ev->string_type = (command & 0xf) == CmdTextString ? CborTextStringType : CborByteStringType;
            }
            if ((command & 0xf) == CmdTextString)
                err = cbor_encode_text_string(encoder, (const char *)data, len);
            else
                err = cbor_encode_byte_string(encoder, data, len);
            item_added(run);
            break;

        case CmdHalfFloat:
            u16 = (uint16_t)get_number(&in, 2);
            record(run, EventHalfFloat, u16);
            err = cbor_encode_half_float(encoder, &u16);
            item_added(run);
            break;

        case CmdFloat:
            u32 = (uint32_t)get_number(&in, 4);
            memcpy(&f, &u32, sizeof(f));
            record(run, EventFloat, u32);
            err = cbor_encode_float(encoder, f);
            item_added(run);
            break;

        case CmdDouble: {
            double d;
            v = get_number(&in, 8);
            memcpy(&d, &v, sizeof(d));
            record(run, EventDouble, v);
            err = cbor_encode_double(encoder, d);
            item_added(run);
            break;
        }

        case CmdFloatAsHalfFloat:
            u32 = (uint32_t)get_number(&in, 4);
            memcpy(&f, &u32, sizeof(f));
            /* the value must be representable as half-precision */
            if (!isnan(f) && !isinf(f) && fabsf(f) > 65504.f)
                break;
            record(run, EventFloatAsHalfFloat, u32);
            err = cbor_encode_float_as_half_float(encoder, f);
            item_added(run);
            break;

        case CmdCreateArray:
        case CmdCreateMap:
            if (!create_container(run, (command & 0xf) == CmdCreateMap, arg, &in))
                return;
            continue;

        case CmdCloseContainer:
            if (run->depth && !close_container(run))
                return;
            continue;

        case CmdRaw:
            /* arbitrary bytes: the result may not be valid CBOR any more */
            len = get_data(&in, arg, &data);
            run->used_raw = true;
            err = cbor_encode_raw(encoder, data, len);
            break;
        }

        if (!check_error(run, err))
            return;
    }

    if (run->writer)
        run->writer->command = CmdCloseContainer;
    while (run->depth) {
        if (!close_container(run))
            return;
    }
    if (run->levels[0].tag_pending)
        encode_null(run);
}

static double half_to_double(uint16_t half)
{
    int exponent = (half >> 10) & 0x1f;
    int mantissa = half & 0x3ff;
    double d;
    if (exponent == 0)
        d = ldexp(mantissa, -24);
    else if (exponent == 0x1f)
        d = mantissa ? NAN : INFINITY;
    else
        d = ldexp(mantissa + 1024, exponent - 25);
    return half & 0x8000 ? -d : d;
}

/* f was in range, so the result must be within half a unit in the last place */
static void check_half_conversion(float f, uint16_t half)
{
    double d = half_to_double(half);
    if (isnan(f)) {
        CHECK(isnan(d));
    } else if (isinf(f)) {
        CHECK(d == f);
    } else {
        int exponent = f == 0 ? -14 : ilogb(f);
        CHECK(fabs(d - f) <= ldexp(1, (exponent < -14 ? -14 : exponent) - 11));
        CHECK(d == 0 || !signbit(d) == !signbit(f));
    }
}

static size_t parse_back(CborValue *it, const struct Event *events, size_t count, size_t i, int nesting);

static size_t parse_back_one(CborValue *it, const struct Event *events, size_t count, size_t i, int nesting)
{
    const struct Event *ev;
    CborValue recursed;
    uint64_t v;
    uint16_t u16;
    float f;
    double d;
    uint8_t *buffer;
    size_t len;
    bool b;

    CHECK(i < count);
    ev = &events[i++];
    switch (cbor_value_get_type(it)) {
    case CborIntegerType:
        CHECK(ev->type == (cbor_value_is_negative_integer(it) ? EventNegativeInteger : EventInteger));
        cbor_value_get_raw_integer(it, &v);
        CHECK(v == ev->value);
        break;

    case CborSimpleType: {
        uint8_t simple;
        CHECK(ev->type == EventSimple);
        cbor_value_get_simple_type(it, &simple);
        CHECK(simple == ev->value);
        break;
    }

    case CborBooleanType:
        CHECK(ev->type == EventSimple);
        cbor_value_get_boolean(it, &b);
        CHECK(ev->value == (CborBooleanType & 0x1f) - 1U + b);
        break;

    case CborNullType:
    case CborUndefinedType:
        CHECK(ev->type == EventSimple && ev->value == (cbor_value_get_type(it) & 0x1fU));
        break;

    case CborTagType:
        CHECK(ev->type == EventTag);
        cbor_value_get_tag(it, &v);
        CHECK(v == ev->value);
        break;

    case CborHalfFloatType:
        CHECK(ev->type == EventHalfFloat || ev->type == EventFloatAsHalfFloat);
        cbor_value_get_half_float(it, &u16);
        if (ev->type == EventHalfFloat) {
            CHECK(u16 == ev->value);
        } else {
            uint32_t u32 = (uint32_t)ev->value;
            memcpy(&f, &u32, sizeof(f));
            check_half_conversion(f, u16);
        }
        break;

    case CborFloatType:
        CHECK(ev->type == EventFloat);
        cbor_value_get_float(it, &f);
        memcpy(&v, &f, sizeof(f));
        CHECK((uint32_t)v == ev->value);
        break;

    case CborDoubleType:
        CHECK(ev->type == EventDouble);
        cbor_value_get_double(it, &d);
        memcpy(&v, &d, sizeof(d));
        CHECK(v == ev->value);
        break;

    case CborByteStringType:
    case CborTextStringType:
        CHECK(ev->type == EventString && ev->string_type == cbor_value_get_type(it));
        CHECK(cbor_value_is_length_known(it));
        if (cbor_value_is_text_string(it))
            CHECK(cbor_value_dup_text_string(it, (char **)&buffer, &len, it) == CborNoError);
        else
            CHECK(cbor_value_dup_byte_string(it, &buffer, &len, it) == CborNoError);
        CHECK(len == ev->value);
        CHECK(len == 0 || memcmp(buffer, ev->data, len) == 0);
        free(buffer);
        return i;

    case CborArrayType:
    case CborMapType:
        CHECK(ev->type == (cbor_value_is_map(it) ? EventMap : EventArray));
        CHECK(cbor_value_is_length_known(it) == !ev->indefinite);
        if (!ev->indefinite) {
            if (cbor_value_is_map(it))
                CHECK(cbor_value_get_map_length(it, &len) == CborNoError);
            else
                CHECK(cbor_value_get_array_length(it, &len) == CborNoError);
            CHECK(len == ev->value);
        }
        CHECK(nesting > 0);
        CHECK(cbor_value_enter_container(it, &recursed) == CborNoError);
        i = parse_back(&recursed, events, count, i, nesting - 1);
        CHECK(i < count && events[i].type == EventEnd);
        CHECK(cbor_value_leave_container(it, &recursed) == CborNoError);
        return i + 1;

    default:
        abort();
    }

    CHECK(cbor_value_advance_fixed(it) == CborNoError);
    return i;
}

static size_t parse_back(CborValue *it, const struct Event *events, size_t count, size_t i, int nesting)
{
    while (!cbor_value_at_end(it))
        i = parse_back_one(it, events, count, i, nesting);
    return i;
}

static void free_run(struct Run *run)
{
    free(run->events);
}

static void init_run(struct Run *run, bool record_events)
{
    memset(run, 0, sizeof(*run));
    run->record_events = record_events;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static struct Run run, writer_run;
    struct Input in;
    struct WriterOutput writer_output = { NULL, 0, -1 };
    uint8_t *buffer;
    size_t buffer_size;
    size_t used;
    const uint8_t *ptr;
    const uint8_t *end;

    if (size < 2)
        return 0;
    buffer_size = data[0] | (data[1] & 0x0f) << 8;
    in.ptr = data + 2;
    in.end = data + size;

    /* first run, with a buffer that is possibly too small */
    init_run(&run, true);
    buffer = (uint8_t *)malloc(buffer_size);
    CHECK(buffer || buffer_size == 0);
    cbor_encoder_init(&run.levels[0].encoder, buffer, buffer_size, 0);
    run_program(&run, in);

    if (run.error == CborNoError && run.out_of_memory) {
        /* "Returns how many more bytes the original buffer [...] needs to be
         * extended by so that no CborErrorOutOfMemory condition will happen" */
        size_t extra = cbor_encoder_get_extra_bytes_needed(&run.levels[0].encoder);
        CHECK(extra > 0);
        buffer_size += extra;
        free(buffer);
        free_run(&run);

        init_run(&run, true);
        buffer = (uint8_t *)malloc(buffer_size);
        CHECK(buffer);
        cbor_encoder_init(&run.levels[0].encoder, buffer, buffer_size, 0);
        run_program(&run, in);
        CHECK(!run.out_of_memory);
        CHECK(cbor_encoder_get_buffer_size(&run.levels[0].encoder, buffer) == buffer_size);
    }

    /* the same program with a writer function */
    init_run(&writer_run, false);
    writer_run.writer = &writer_output;
    cbor_encoder_init_writer(&writer_run.levels[0].encoder, writer_function, &writer_output);
    run_program(&writer_run, in);
    CHECK(writer_run.error == run.error);
    CHECK(!writer_run.out_of_memory);

    if (run.error == CborNoError) {
        used = cbor_encoder_get_buffer_size(&run.levels[0].encoder, buffer);
        CHECK(cbor_encoder_get_extra_bytes_needed(&run.levels[0].encoder) == 0);
        CHECK(writer_output.size == used);
        CHECK(used == 0 || memcmp(writer_output.data, buffer, used) == 0);
    }

    if (run.error == CborNoError && !run.used_raw) {
        /* parse it back: it's a sequence of items. The encoder always uses
         * the shortest encoding for integers and lengths; parse_back()
         * checks that indeterminate length was used where requested. */
        size_t i = 0;

        ptr = buffer;
        end = buffer + used;
        while (ptr < end) {
            CborParser parser;
            CborValue it;
            CborError err;
            CHECK(cbor_parser_init(ptr, (size_t)(end - ptr), 0, &parser, &it) == CborNoError);
            err = cbor_value_validate(&it, CborValidateShortestIntegrals);
            CHECK(err == CborNoError);
            i = parse_back(&it, run.events, run.event_count, i, MAX_NESTING);
            CHECK(cbor_value_get_next_byte(&it) > ptr);
            ptr = cbor_value_get_next_byte(&it);
        }
        CHECK(i == run.event_count);
    }

    free(writer_output.data);
    free(buffer);
    free_run(&run);
    free_run(&writer_run);
    return 0;
}
