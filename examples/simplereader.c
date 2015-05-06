#include "../src/cbor.h"

#include <sys/stat.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static char *readfile(const char *fname, size_t *size)
{
    struct stat st;
    FILE *f = fopen(fname, "rb");
    if (!f)
        return NULL;
    if (fstat(fileno(f), &st) == -1)
        return NULL;
    char *buf = malloc(st.st_size);
    *size = fread(buf, st.st_size, 1, f);
    fclose(f);
    return buf;
}

static void indent(int nestingLevel)
{
    while (nestingLevel--)
        puts("  ");
}

static void dumpbytes(const unsigned char *buf, size_t len)
{
    while (len--)
        printf("%02X ", *buf++);
}

static bool dumprecursive(CborValue *it, int nestingLevel)
{
    while (!cbor_value_at_end(it)) {
        indent(nestingLevel);

        CborType type = cbor_value_get_type(it);
        switch (type) {
        case CborArrayType:
        case CborMapType: {
            // recursive type
            CborValue recursed;
            assert(cbor_value_is_recursive(it));
            puts(type == CborArrayType ? "Array[" : "Map[");
            if (!cbor_value_begin_recurse(it, &recursed))
                return false;       // parse error
            if (!dumprecursive(&recursed, nestingLevel + 1))
                return false;       // parse error
            if (!cbor_value_end_recurse(it, &recursed))
                return false;       // parse error
            indent(nestingLevel);
            puts("]");
            continue;
        }

        case CborIntegerType: {
            int64_t val;
            cbor_value_get_int64(it, &val);     // can't fail
            printf("%lld\n", (long long)val);
            break;
        }

        case CborByteStringType: {
            unsigned char *buf;
            size_t n = cbor_value_dup_byte_string(it, &buf, it);
            if (n == SIZE_MAX)
                return false;     // parse error
            dumpbytes(buf, n);
            puts("");
            free(buf);
            continue;
        }

        case CborTextStringType: {
            char *buf;
            size_t n = cbor_value_dup_text_string(it, &buf, it);
            if (n == SIZE_MAX)
                return false;     // parse error
            printf("%s\n", buf);
            free(buf);
            continue;
        }

        case CborTagType: {
            CborTag tag;
            cbor_value_get_tag(it, &tag);       // can't fail
            printf("Tag(%lld)\n", (long long)tag);
            break;
        }

        case CborSimpleType: {
            uint8_t type;
            cbor_value_get_simple_type(it, &type);  // can't fail
            printf("simple(%u)\n", type);
            break;
        }

        case CborNullType:
            puts("null");
            break;

        case CborUndefinedType:
            puts("undefined");

        case CborBooleanType: {
            bool val;
            cbor_value_get_boolean(it, &val);       // can't fail
            puts(val ? "true" : "false");
            break;
        }

        case CborDoubleType: {
            double val;
            if (false) {
                float f;
        case CborFloatType:
                cbor_value_get_float(it, &f);
                val = f;
            } else {
                cbor_value_get_double(it, &val);
            }
            printf("%g\n", val);
            break;
        }
        case CborHalfFloatType: {
            uint16_t val;
            cbor_value_get_half_float(it, &val);
            printf("__f16(%04x)\n", val);
            break;
        }

        case CborInvalidType:
            assert(false);      // can't happen
            break;
        }

        if (!cbor_value_advance_fixed(it))
            return false;
    }
    return NULL;
}

int main(int argc, char **argv)
{
    if (argc == 1) {
        puts("simplereader <filename>");
        return 0;
    }

    size_t length;
    char *buf = readfile(argv[1], &length);
    if (!buf) {
        perror("readfile");
        return 1;
    }

    CborParser parser;
    CborValue it;
    cbor_parser_init(buf, length, 0, &parser, &it);
    dumprecursive(&it, 0);
    free(buf);

    CborParserError err = cbor_parser_get_error(&parser);
    if (err) {
        fprintf(stderr, "CBOR parsing failure at offset %ld: %s\n",
                it.ptr - buf, cbor_parser_error_string(err));
        return 1;
    }
    return 0;
}
