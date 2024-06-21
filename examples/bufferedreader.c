/* vim: set sw=4 ts=4 et tw=78: */

/**
 * \brief       An example of a buffered CBOR file reader using low-level
 *              POSIX file I/O as might be implemented in a microcontroller
 *              RTOS.
 *
 * \author      Stuart Longland <stuartl@vrt.com.au>
 *
 * \copyright   tinycbor project contributors
 *
 * \file        bufferedwriter.c
 */

/* Includes for POSIX low-level file I/O */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

/* Sanity check routine */
#include <assert.h>

/* Pull in "standard" integer types */
#include <stdint.h>

/* Pull in definitions for printf and errno */
#include <stdio.h>
#include <string.h>
#include <errno.h>

/* For example usage */
#include <stdlib.h>

#include "../src/cbor.h"

/**
 * Context for the file reader.  This stores the file descriptor, a pointer to
 * the read buffer, and context pointers.  The assumption here is that the
 * CBOR document being read is less than 64KiB (65536 bytes) in size.
 */
struct filereader
{
    /**
     * Read buffer.  This must be allocated by the caller, and sized
     * appropriately since the buffer must be big enough to accommodate
     * entire string chunks embedded in the CBOR document.
     */
    uint8_t* buffer;

    /**
     * File descriptor, returned by the `open` system call.
     */
    int fd;

    /**
     * Size of the file in bytes.
     */
    uint16_t file_sz;

    /**
     * Size of the read buffer in bytes.
     */
    uint16_t buffer_sz;

    /**
     * Read position within the file.  This basically describes where
     * `buffer[0]` came from in the source file.
     */
    uint16_t pos;

    /**
     * Number of bytes stored in the buffer presently.
     */
    uint16_t used_sz;

    /**
     * Block size.  When reading from the file, we round up to whole multiples
     * of this block size to improve I/O efficiency.
     */
    uint16_t block_sz;
};

/* Implementation routines */

/**
 * Return the nearest (earlier) position that is on a block boundary.
 */
static uint16_t filereader_get_block_pos(
        const struct filereader * const context,
        uint16_t pos
) {
    return context->block_sz * (pos / context->block_sz);
}

/**
 * Retrieve a pointer to the region defined by \a pos and \a sz.
 *
 * \retval  NULL    The region is not contained in the buffer.
 */
static uint8_t *filereader_get_ptr(
        struct filereader *context, uint16_t pos, uint16_t sz
) {
    /* Sanity check, disallow `sz` bigger than buffer content */
    if (sz > context->used_sz) {
        return NULL;
    }

    /* Is `pos` off the start of the buffer? */
    if (pos < context->pos) {
        return NULL;
    }

    /* Is `pos + sz` off the end of the buffer? */
    if ((pos + sz) > (context->pos + context->used_sz)) {
        return NULL;
    }

    /* We should be good */
    return &(context->buffer[pos - context->pos]);
}

/**
 * Copy the data from the requested position in the file to the buffer.
 */
static int filereader_read(
        struct filereader *context, uint16_t pos, uint16_t sz, uint8_t *wptr
) {
    /* Seek to the required file position */
    off_t seek_res = lseek(context->fd, pos, SEEK_SET);
    if (seek_res != pos) {
        /* We failed */
        return -errno;
    }

    /* Perform the read */
    ssize_t read_res = read(context->fd, wptr, sz);
    if (read_res != sz) {
        /* Truncated read */
        return -errno;
    }

    return sz;
}

/**
 * Prepend \arg sz bytes from the file to the buffer, shifting the
 * read window back \arg sz bytes.
 *
 * \param   context Reader context
 * \param   sz      Number of bytes to read
 *
 * \retval  ≥0      Number of bytes read into the buffer.
 * \retval  <0      Read failure, value is `errno` negated.
 */
static int filereader_prepend_buffer(struct filereader *context, uint16_t sz) {
    /* Compute read position */
    const uint16_t pos = context->pos - sz;

    /* Shuffle existing data forward by sz bytes */
    memmove(
            &(context->buffer[sz]), context->buffer,
            context->buffer_sz - sz
    );

    /* Copy the data in */
    int read_res = filereader_read(context, pos, sz, context->buffer);
    if (read_res >= 0) {
        context->pos = pos;
        if ((context->used_sz + sz) < context->buffer_sz) {
            context->used_sz += sz;
        }
    }

    return read_res;
}

/**
 * Append \arg sz bytes from the file to the buffer, shifting the
 * read window forward \arg sz bytes.
 *
 * \param   context Reader context
 * \param   sz      Number of bytes to read
 *
 * \retval  ≥0      Number of bytes read into the buffer.
 * \retval  <0      Read failure, value is `errno` negated.
 */
static int filereader_append_buffer(struct filereader *context, uint16_t sz) {
    /* Compute read position */
    const uint16_t pos = context->pos + context->used_sz;

    /* Is there room? */
    if ((context->buffer_sz - context->used_sz) < sz) {
        /* Shuffle existing data forward by sz bytes */
        memmove(
                context->buffer, &(context->buffer[sz]),
                context->buffer_sz - sz
        );
        context->pos += sz;
        context->used_sz -= sz;
    }

    /* Copy the data in */
    int read_res = filereader_read(
            context, pos, sz,
            &(context->buffer[context->used_sz])
    );
    if (read_res >= 0) {
        context->used_sz += sz;
    }

    return read_res;
}

/**
 * Read data from the file and place it in the buffer, shuffling
 * existing data around as required.
 *
 * \param   context Reader context
 * \param   pos     Position in the file to start reading
 * \param   sz      Number of bytes to read
 *
 * \retval  ≥0      Number of bytes read into the buffer.
 * \retval  <0      Read failure, value is `errno` negated.
 */
static int filereader_load_buffer(
        struct filereader *context, uint16_t pos, uint16_t sz
) {
    /* Compute the end position (not-inclusive) */
    uint16_t end = pos + sz;

    /* Is this in the buffer already? */
    if (
            (pos < context->pos)
            || (end > (context->pos + context->used_sz))
    ) {
        /* Make a note of the current buffer state */
        uint16_t buffer_end = context->pos + context->used_sz;
        uint16_t buffer_rem = context->buffer_sz - context->used_sz;

        /* Our buffer write position */
        uint8_t* wptr = context->buffer;

        /*
         * Dumb approach for now, replace the entire buffer.  Round
         * the start and end points to block boundaries for efficiency.
         */
        pos = filereader_get_block_pos(context, pos);
        end = filereader_get_block_pos(context, end + (context->block_sz - 1));

        /* Clamp the end position to the file size */
        if (end > context->file_sz) {
            end = context->file_sz;
        }

        /* Compute new rounded size, then clamp to buffer size */
        sz = end - pos;
        if (sz > context->buffer_sz) {
            sz = context->buffer_sz;
        }

        /* Can we re-use existing data? */
        if (
                (pos >= context->pos)
                && (pos < buffer_end)
                && (end > buffer_end)
        ) {
            return filereader_append_buffer(context, end - buffer_end);
        } else if (
                (pos < context->pos)
                && (end >= context->pos)
                && (end <= buffer_end)
        ) {
            return filereader_prepend_buffer(context, context->pos - pos);
        } else {
            /* Nope, read the lot in */
            const uint16_t file_rem = context->file_sz - pos;
            if (file_rem < context->buffer_sz) {
                sz = file_rem;
            } else {
                sz = context->buffer_sz;
            }

            int read_res = filereader_read(
                    context, pos, sz,
                    context->buffer
            );
            if (read_res >= 0) {
                context->pos = pos;
                context->used_sz = sz;
            }
            return read_res;
        }
    } else {
        /* Nothing to do, we have the required data already */
        return 0;
    }
}

/**
 * Try to read the data into the buffer, then return a pointer to it.
 *
 * \retval  NULL    The region could not be loaded into the buffer.
 */
static uint8_t *filereader_fetch_ptr(
        struct filereader *context, uint16_t pos, uint16_t sz
) {
    /* Ensure the data we need is present */
    if (filereader_load_buffer(context, pos, sz) < 0) {
        /* We failed */
        return NULL;
    } else {
        return filereader_get_ptr(context, pos, sz);
    }
}

/**
 * Fetch the reader context from the CborValue
 */
static struct filereader *filereader_get_context(const CborValue * const value) {
    return (struct filereader*)(value->parser->data.ctx);
}

/**
 * Fetch the CborValue read position
 */
static uint16_t filereader_get_pos(const CborValue * const value) {
    return (uint16_t)(uintptr_t)(value->source.token);
}

/**
 * Set the CborValue read position
 */
static void filereader_set_pos(CborValue * const value, uint16_t new_pos) {
    value->source.token = (void*)(uintptr_t)new_pos;
}

/**
 * Return `true` if there is at least \a len bytes that can be read from
 * the file at this moment in time.
 */
static bool filereader_impl_can_read_bytes(
        const struct CborValue *value,
        size_t len
) {
    const struct filereader *context = filereader_get_context(value);
    const uint16_t pos = filereader_get_pos(value);

    return ((size_t)pos + len) <= context->file_sz;
}

/**
 * Read the bytes from the buffer without advancing the read pointer.
 */
static void* filereader_impl_read_bytes(
        const struct CborValue *value,
        void* dst, size_t offset, size_t len
) {
    struct filereader *context = filereader_get_context(value);

    /* Determine read position factoring in offset */
    const uint16_t pos = filereader_get_pos(value) + offset;

    /* Fetch the data from the file */
    const uint8_t* ptr = filereader_fetch_ptr(context, pos, (uint16_t)len);
    if (ptr != NULL) {
        return memcpy(dst, ptr, len);
    } else {
        /* We could not read the data */
        return NULL;
    }
}

/**
 * Advance the pointer by the requested amount.
 */
static void filereader_impl_advance_bytes(struct CborValue *value, size_t len) {
    filereader_set_pos(value, filereader_get_pos(value) + (uint16_t)len);
}

/**
 * Retrieve a pointer to the string defined by the given offset and length.
 */
CborError filereader_impl_transfer_string(
        struct CborValue *value,
        const void **userptr, size_t offset, size_t len
) {
    struct filereader *context = filereader_get_context(value);

    /* Determine read position factoring in offset */
    const uint16_t pos = filereader_get_pos(value) + offset;

    /* Fetch the data from the file */
    const uint8_t* ptr = filereader_fetch_ptr(context, pos, (uint16_t)len);
    if (ptr != NULL) {
        /* All good, advance the cursor past the data and return the pointer */
        filereader_set_pos(value, pos + len);
        *userptr = (void*)ptr;
        return CborNoError;
    } else {
        /* We could not read the data */
        return CborErrorIO;
    }
}

/**
 * Implementation of the CBOR File Reader operations.
 */
static const struct CborParserOperations filereader_ops = {
	.can_read_bytes	= filereader_impl_can_read_bytes,
	.read_bytes = filereader_impl_read_bytes,
	.advance_bytes = filereader_impl_advance_bytes,
	.transfer_string = filereader_impl_transfer_string
};

/**
 * Open a CBOR file for reading.
 *
 * \param[inout]    parser          CBOR parser object to initialise.
 * \param[inout]    value           Root CBOR cursor object to initialise.
 *
 * \param[inout]    context         The file reader context.  This must exist
 *                                  for the duration the file is open.
 *
 * \param[inout]    buffer          Read buffer allocated by the caller where
 *                                  the read data will be stored.
 *
 * \param[in]       buffer_sz       Size of the read buffer.
 *
 * \param[in]       path            The path to the file being read.
 *
 * \param[in]       flags           `open` flags.  `O_RDONLY` is logic-ORed
 *                                  with this value, but the user may provide
 *                                  other options here.
 *
 * \param[in]       block_sz        Size of read blocks.  Where possible,
 *                                  reads will be rounded up and aligned with
 *                                  blocks of this size for efficiency.  Set
 *                                  to 0 to default to `buffer_sz / 2`.
 *
 * \retval          CborErrorIO     The `open` call failed for some reason,
 *                                  see the POSIX standard `errno` variable
 *                                  for why.
 *
 * \retval          CborErrorDataTooLarge   The CBOR document is too big to be
 *                                          handled by this reader.
 *
 * \retval          CborNoError     CBOR encoder initialised successfully.
 */
CborError filereader_open(
        CborParser * const parser,
        CborValue * const value,
        struct filereader * const context,
        uint8_t *buffer,
        uint16_t buffer_sz,
        const char* path,
        int flags,
        uint16_t block_sz
)
{
    CborError error = CborNoError;
    struct stat path_stat;

    /* Determine the file size */
    if (stat(path, &path_stat) < 0) {
        /* stat fails */
        error = CborErrorIO;
    } else {
        context->fd = open(path, O_RDONLY | flags);
        if (context->fd < 0) {
            /* Open fails */
            error = CborErrorIO;
        } else {
            /* Sanity check document size */
            if (path_stat.st_size > UINT16_MAX) {
                error = CborErrorDataTooLarge;
            } else {
                /* Initialise structure */
                context->pos = 0;
                context->used_sz = 0;
                context->buffer = buffer;
                context->buffer_sz = buffer_sz;
                context->file_sz = (uint16_t)path_stat.st_size;

                if (block_sz == 0) {
                    block_sz = buffer_sz / 2;
                }
                context->block_sz = block_sz;

                /* Fill the initial buffer */
                if (filereader_load_buffer(context, 0, buffer_sz) >= 0) {
                    /* Initialise the CBOR parser */
                    error = cbor_parser_init_reader(
                            &filereader_ops, parser, value, (void*)context
                    );
                }
            }

            if (error != CborNoError) {
                /* Close the file, if we can */
                assert(close(context->fd) == 0);
                context->fd = -1;
            }
        }
    }

    return error;
}

/**
 * Close the file reader.
 *
 * \param[inout]    context         File reader context to close.
 *
 * \retval          CborErrorIO     The `close` call failed for some
 *                                  reason, see the POSIX standard
 *                                  `errno` variable for why.
 *
 * \retval          CborNoError     File closed, `fd` should be set to -1.
 */
CborError filereader_close(struct filereader * const context)
{
    CborError error = CborNoError;

    /* Try to close the file */
    if (close(context->fd) < 0) {
        /* Close fails! */
        error = CborErrorIO;
    } else {
        context->fd = -1;
    }

    return error;
}

/* --- Example usage of the above reader --- */

/**
 * Indent the output text to the level specified.  Taken from `simplereader.c`
 */
static void indent(int nestingLevel)
{
    while (nestingLevel--)
        printf("  ");
}

/**
 * Dump the raw bytes given.  Taken from `simplereader.c`
 */
static void dumpbytes(const uint8_t *buf, size_t len)
{
    while (len--)
        printf("%02X ", *buf++);
}

/**
 * Recursively dump the CBOR data structure.  Taken from `simplereader.c`
 */
static CborError dumprecursive(CborValue *it, int nestingLevel)
{
    while (!cbor_value_at_end(it)) {
        CborError err;
        CborType type = cbor_value_get_type(it);

        indent(nestingLevel);
        switch (type) {
        case CborArrayType:
        case CborMapType: {
            // recursive type
            CborValue recursed;
            assert(cbor_value_is_container(it));
            puts(type == CborArrayType ? "Array[" : "Map[");
            err = cbor_value_enter_container(it, &recursed);
            if (err)
                return err;       // parse error
            err = dumprecursive(&recursed, nestingLevel + 1);
            if (err)
                return err;       // parse error
            err = cbor_value_leave_container(it, &recursed);
            if (err)
                return err;       // parse error
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
            uint8_t *buf;
            size_t n;
            err = cbor_value_dup_byte_string(it, &buf, &n, it);
            if (err)
                return err;     // parse error
            dumpbytes(buf, n);
            printf("\n");
            free(buf);
            continue;
        }

        case CborTextStringType: {
            char *buf;
            size_t n;
            err = cbor_value_dup_text_string(it, &buf, &n, it);
            if (err)
                return err;     // parse error
            puts(buf);
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
            break;

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

        err = cbor_value_advance_fixed(it);
        if (err)
            return err;
    }
    return CborNoError;
}

/**
 * Print the error encountered.  If the error is `CborErrorIO`, also check
 * the global `errno` variable and print the resultant error seen.
 *
 * \param[in]       error       CBORError constant
 */
void print_err(CborError error)
{
    if (error == CborErrorIO) {
        printf("IO: %s\n", strerror(errno));
    } else {
        printf("%s\n", cbor_error_string(error));
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf(
                "Usage: %s <filename> [buffer_sz [block_sz]]\n",
                argv[0]
        );
        return 1;
    } else {
        struct filereader context;
        CborParser parser;
        CborValue value;
        CborError error;

        uint16_t buffer_sz = 64;
        uint16_t block_sz = 0;

        if (argc > 2) {
            /* buffer_sz given */
            char *endptr = NULL;
            unsigned long long_buffer_sz = strtoul(argv[2], &endptr, 0);

            if (!endptr || *endptr) {
                printf("Invalid buffer size %s\n", argv[2]);
                return 1;
            }

            if (long_buffer_sz > UINT16_MAX) {
                printf("Buffer size (%lu bytes) too big\n", long_buffer_sz);
                return 1;
            }

            buffer_sz = (uint16_t)long_buffer_sz;

            if (argc > 3) {
                /* block_sz given */
                char *endptr = NULL;
                unsigned long long_block_sz = strtoul(argv[3], &endptr, 0);

                if (!endptr || *endptr) {
                    printf("Invalid block size %s\n", argv[3]);
                    return 1;
                }

                if (long_block_sz > buffer_sz) {
                    printf("Block size (%lu bytes) too big\n", long_block_sz);
                    return 1;
                }

                block_sz = (uint16_t)long_block_sz;
            }
        }

        /* Allocate the buffer on the stack */
        uint8_t buffer[buffer_sz];

        /* Open the file for writing, create if needed */
        error = filereader_open(
                &parser,                                /* CBOR context */
                &value,                                 /* CBOR cursor */
                &context,                               /* Reader context */
                buffer, buffer_sz,                      /* Reader buffer & size */
                argv[1],                                /* File name */
                0,                                      /* Open flags */
                block_sz                                /* Block size */
        );

        if (error != CborNoError) {
            printf("Failed to open %s for reading: ", argv[1]);
            print_err(error);
        } else {
            error = dumprecursive(&value, 0);
            if (error != CborNoError) {
                printf("Failed to read file: ");
                print_err(error);
            }

            error = filereader_close(&context);
            if (error != CborNoError) {
                printf("Failed to close file: ");
                print_err(error);
            }
        }

        if (error != CborNoError) {
            return 2;
        } else {
            return 0;
        }
    }
}
