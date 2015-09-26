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

#define _POSIX_C_SOURCE 200809L
#include "cbor.h"

#include <cJSON.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

uint8_t *buffer;
size_t buffersize;
bool usingMetaData = false;

size_t get_cjson_size_limited(cJSON *container)
{
    // cJSON_GetArraySize is O(n), so don't go too far
    unsigned s = 0;
    cJSON *item;
    for (item = container->child; item; item = item->next) {
        if (++s > 255)
            return CborIndefiniteLength;
    }
    return s;
}

CborError decode_json(cJSON *json, CborEncoder *encoder)
{
    CborEncoder container;
    CborError err;
    cJSON *item;

    switch (json->type) {
    case cJSON_False:
    case cJSON_True:
        return cbor_encode_boolean(encoder, json->type == cJSON_True);

    case cJSON_NULL:
        return cbor_encode_null(encoder);

    case cJSON_Number:
        if ((double)json->valueint == json->valuedouble)
            return cbor_encode_int(encoder, json->valueint);
encode_double:
        // the only exception that JSON is larger: floating point numbers
        container = *encoder;   // save the state
        err = cbor_encode_floating_point(encoder, CborDoubleType, &json->valuedouble);

        if (err == CborErrorOutOfMemory) {
            buffersize += 1024;
            uint8_t *newbuffer = realloc(buffer, buffersize);
            if (newbuffer == NULL)
                return err;

            *encoder = container;   // restore state
            encoder->ptr = newbuffer + (container.ptr - buffer);
            encoder->end = newbuffer + buffersize;
            buffer = newbuffer;
            goto encode_double;
        }
        return err;

    case cJSON_String:
        return cbor_encode_text_stringz(encoder, json->valuestring);

    default:
        return CborErrorUnknownType;

    case cJSON_Array:
        err = cbor_encoder_create_array(encoder, &container, get_cjson_size_limited(json));
        if (err)
            return err;
        for (item = json->child; item; item = item->next) {
            err = decode_json(item, &container);
            if (err)
                return err;
        }
        return cbor_encoder_close_container_checked(encoder, &container);

    case cJSON_Object:
        err = cbor_encoder_create_map(encoder, &container, get_cjson_size_limited(json));
        if (err)
            return err;

        for (item = json->child ; item; item = item->next) {
            err = cbor_encode_text_stringz(&container, item->string);
            if (err)
                return err;
            err = decode_json(item, &container);
            if (err)
                return err;
        }

        return cbor_encoder_close_container_checked(encoder, &container);
    }
}

int main(int argc, char **argv)
{
    int c;
    while ((c = getopt(argc, argv, "M")) != -1) {
        switch (c) {
        case 'M':
            usingMetaData = true;
            break;

        case '?':
            fprintf(stderr, "Unknown option -%c.\n", optopt);
            // fall through
        case 'h':
            puts("Usage: json2cbor [OPTION]... [FILE]...\n"
                 "Reads JSON content from FILE and convert to CBOR.\n"
                 "\n"
                 "Options:\n"
                 " -M       Interpret metadata added by cbordump tool\n"
                 "");
            return c == '?' ? EXIT_FAILURE : EXIT_SUCCESS;
        }
    }

    FILE *in;
    const char *fname = argv[optind];
    if (fname && strcmp(fname, "-") != 0) {
        in = fopen(fname, "r");
        if (!in) {
            perror("open");
            return EXIT_FAILURE;
        }
    } else {
        in = stdin;
        fname = "-";
    }

    /* 1. read the file */
    off_t fsize;
    if (fseeko(in, 0, SEEK_END) == 0 && (fsize = ftello(in)) >= 0) {
        buffersize = fsize + 1;
        buffer = malloc(buffersize);
        if (buffer == NULL) {
            perror("malloc");
            return EXIT_FAILURE;
        }

        rewind(in);
        fsize = fread(buffer, 1, fsize, in);
        buffer[fsize] = '\0';
    } else {
        const unsigned chunk = 16384;
        buffersize = 0;
        buffer = NULL;
        do {    // it the hard way
            buffer = realloc(buffer, buffersize + chunk);
            if (buffer == NULL)
                perror("malloc");

            buffersize += fread(buffer + buffersize, 1, chunk, in);
        } while (!feof(in) && !ferror(in));
        buffer[buffersize] = '\0';
    }

    if (ferror(in)) {
        perror("read");
        return EXIT_FAILURE;
    }
    if (in != stdin)
        fclose(in);

    /* 2. parse as JSON */
    cJSON *doc = cJSON_ParseWithOpts((char *)buffer, NULL, true);
    if (doc == NULL) {
        fprintf(stderr, "json2cbor: %s: could not parse.\n", fname);
        return EXIT_FAILURE;
    }

    /* 3. encode as CBOR */
    // We're going to reuse the buffer, as CBOR is usually shorter than the equivalent JSON
    CborEncoder encoder;
    cbor_encoder_init(&encoder, buffer, buffersize, 0);
    CborError err = decode_json(doc, &encoder);

    cJSON_Delete(doc);

    if (err) {
        fprintf(stderr, "json2cbor: %s: error encoding to CBOR: %s\n", fname,
                cbor_error_string(err));
        return EXIT_FAILURE;
    }

    fwrite(buffer, 1, encoder.ptr - buffer, stdout);
    free(buffer);
    return EXIT_SUCCESS;
}
