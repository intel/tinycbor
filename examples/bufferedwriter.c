/* vim: set sw=4 ts=4 et tw=78: */

/**
 * \brief       An example of a buffered CBOR file writer using low-level
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
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

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
 * File writer buffer size.  This should be tuned to balance memory usage and
 * performance.  Most interfaces, bigger writes are more efficient, but on a
 * small MCU, memory may be tight.
 *
 * We're using `uint8_t` to represent our buffer position, so this must be
 * strictly less than 256 bytes unless you change it in \ref filewriter (and
 * in \ref filewriter_writer_impl) below.
 */
#define FILEWRITER_BUFFER_SZ    (64)

/**
 * Context for the file writer.  This stores the file descriptor, the write
 * buffer, and a counter indicating our position within it.
 */
struct filewriter
{
    /**
     * Write buffer.  `tinycbor` writes will be initially buffered here, and
     * the buffer will automatically be flushed:
     * - when the buffer position counter reaches \ref FILEWRITER_BUFFER_SZ
     * - when the file is closed with \ref filewriter_close
     */
    uint8_t buffer[FILEWRITER_BUFFER_SZ];

    /**
     * File descriptor, returned by the `open` system call.
     */
    int fd;

    /**
     * Position within the buffer.  When less than \ref FILEWRITER_BUFFER_SZ
     * this indicates the position for new data.  If new data arrives and
     * this value is equal to \ref FILEWRITER_BUFFER_SZ, the buffer will be
     * flushed first before writing.
     */
    uint8_t pos;
};

/* Forward declaration, we'll cover this later */
static CborError filewriter_writer_impl(
        void* token, const void* data, size_t len, CborEncoderAppendType append
);

/**
 * Open a CBOR file for writing.
 *
 * \param[inout]    encoder         CBOR encoder object to initialise.
 *
 * \param[inout]    context         The file writer context.  This must exist
 *                                  for the duration the file is open.
 *
 * \param[in]       path            The path to the file being written.
 *
 * \param[in]       flags           `open` flags.  `O_WRONLY` is logic-ORed
 *                                  with this value, but the user may provide
 *                                  other options here.
 *
 * \param[in]       mode            Mode bits to set on the created file.
 *
 * \retval          CborErrorIO     The `open` call failed for some reason,
 *                                  see the POSIX standard `errno` variable
 *                                  for why.
 *
 * \retval          CborNoError     CBOR encoder initialised successfully.
 */
CborError filewriter_open(
        CborEncoder * const encoder,
        struct filewriter * const context,
        const char* path,
        int flags,
        mode_t mode
)
{
    CborError error = CborNoError;

    context->fd = open(path, O_WRONLY | flags, mode);
    if (context->fd < 0) {
        /* Open fails */
        error = CborErrorIO;
    } else {
        /* Initialise structure */
        context->pos = 0;

        /* Initialise the CBOR encoder */
        cbor_encoder_init_writer(encoder, filewriter_writer_impl, context);
    }

    return error;
}

/**
 * Explicitly flush the content of the buffer.  This is called automatically
 * when the file is closed or when we run out of buffer space.  Is a no-op if
 * there is nothing to flush.
 *
 * \param[inout]    context         File writer context to flush.
 *
 * \retval          CborErrorIO     The `write` call failed for some reason,
 *                                  see the POSIX standard `errno` variable
 *                                  for why.
 *
 * \retval          CborNoError     Buffer flushed, position reset.
 */
CborError filewriter_flush(struct filewriter * const context)
{
    CborError error = CborNoError;

	if (context->pos > 0) {
		if (write(context->fd, context->buffer, context->pos) < context->pos) {
			error = CborErrorIO;
        } else {
            /* Success */
            context->pos = 0;
        }
	}

	return error;
}

/**
 * Close the file writer, flushing any remaining data and finishing the write.
 * It is assumed that relevant CBOR containers have been closed first.
 *
 * \param[inout]    context         File writer context to flush.
 *
 * \retval          CborErrorIO     The `write` or `close` call failed for
 *                                  some reason, see the POSIX standard
 *                                  `errno` variable for why.  (CBOR file
 *                                  should be considered invalid in this
 *                                  case.)  *The file may still be open!*
 *
 * \retval          CborNoError     File closed, `fd` should be set to -1.
 */
CborError filewriter_close(struct filewriter * const context)
{
    CborError error = filewriter_flush(context);

    if (error == CborNoError) {
        int res = close(context->fd);
        if (res < 0) {
            /* Close failed */
            error = CborErrorIO;
        } else {
            /* Mark context as closed */
            context->fd = -1;
        }
    }

    return error;
}

/**
 * CBOR Writer implementation.  This function implements the necessary
 * interface expected by `tinycbor` to perform synchronous writes to a
 * file arbitrarily.  Flushing is automatically handled.
 */
static CborError filewriter_writer_impl(
        void* token, const void* data, size_t len, CborEncoderAppendType append
)
{
    struct filewriter* context = (struct filewriter*)token;
    const uint8_t* rptr = (const uint8_t*)data;
    CborError error = CborNoError;

    (void)append;	/* We don't use the `append` argument */

    while ((len > 0) && (error == CborNoError)) {
        /* How much space is left? */
        uint8_t rem = FILEWRITER_BUFFER_SZ - context->pos;

        /* Is there any space? */
        if (rem > 0) {
            /* Where is our write pointer at? */
            uint8_t* wptr = &(context->buffer[context->pos]);

            /* How much can we write? */
            uint8_t sz = rem;
            if (sz > len) {
                /* Clamp to amount of data available */
                sz = len;
            }

            /* Copy that into the buffer */
            memcpy(wptr, rptr, sz);
            context->pos += sz;
            rptr += sz;
            len -= sz;
            rem -= sz;
        }

        /* Are we full yet? */
        if (rem == 0) {
            error = filewriter_flush(context);
        }
    }

    return error;
}

/* --- Example usage of the above writer --- */

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

/* Forward declarations */
int exec_arg_array(CborEncoder * const encoder, size_t len, int argc, char **argv);
int exec_arg_map(CborEncoder * const encoder, size_t len, int argc, char **argv);

/**
 * Interpret the arguments given and execute one of the `tinycbor` routines.
 *
 * \param[inout]    encoder     CBOREncoder instance
 * \param[in]       argc        Number of command line arguments remaining
 * \param[in]       argv        Command line arguments' values
 *
 * \retval          ≤0          CBOR error occurred, stop here.
 * \retval          >0          Number of arguments consumed.
 */
int exec_arg(CborEncoder * const encoder, int argc, char **argv)
{
    if (argc > 1) {
        CborError error;
        int consumed;
        int len = strlen(argv[0]);

        printf("Command: %s (%d bytes)\n", argv[0], len);
        if (len == 1) {
            /* Single-character commands */
            switch (argv[0][0]) {
            case '{':   /* Begin unknown-length map */
                consumed = exec_arg_map(
                        encoder, CborIndefiniteLength,
                        argc - 1, argv + 1
                );
                if (consumed > 0) {
                    consumed++;
                }
                return consumed;
            case '[':   /* Begin unknown-length array */
                consumed = exec_arg_array(
                        encoder, CborIndefiniteLength,
                        argc - 1, argv + 1
                );
                if (consumed > 0) {
                    consumed++;
                }
                return consumed;

            case 'N':   /* Null */
            case 'n':
                error = cbor_encode_null(encoder);
                if (error != CborNoError) {
                    printf("Failed at null: ");
                    print_err(error);
                    return -1;
                }
                return 1;
            case 'U':   /* Undefined */
            case 'u':
                error = cbor_encode_undefined(encoder);
                if (error != CborNoError) {
                    printf("Failed at undefined: ");
                    print_err(error);
                    return -1;
                }
                return 1;
            case 'F':   /* False */
            case 'f':
                error = cbor_encode_boolean(encoder, false);
                if (error != CborNoError) {
                    printf("Failed at false: ");
                    print_err(error);
                    return -1;
                }
                return 1;
            case 'T':   /* True */
            case 't':
                error = cbor_encode_boolean(encoder, true);
                if (error != CborNoError) {
                    printf("Failed at true: ");
                    print_err(error);
                    return -1;
                }
                return 1;
            default:
                printf("Unknown single-character command: %s", argv[0]);
                return -1;
            }
        } else if (strncmp(argv[0], "map(", 4) == 0) {
            /* Fixed-size map */
            char *endptr = NULL;
            unsigned long maplen = strtoul(&(argv[0][4]), &endptr, 0);

            if (!endptr || (*endptr != ')')) {
                /* Not a valid length */
                printf("Invalid length for map: %s\n", argv[0]);
                return -1;
            } else {
                consumed = exec_arg_map(encoder, maplen, argc - 1, argv + 1);
                if (consumed > 0) {
                    consumed++;
                }
                return consumed;
            }
        } else if (strncmp(argv[0], "array(", 6) == 0) {
            /* Fixed-size array */
            char *endptr = NULL;
            unsigned long arraylen = strtoul(&(argv[0][4]), &endptr, 0);

            if (!endptr || (*endptr != ')')) {
                /* Not a valid length */
                printf("Invalid length for array: %s\n", argv[0]);
                return -1;
            } else {
                consumed = exec_arg_array(
                        encoder, arraylen, argc - 1, argv + 1
                );
                if (consumed > 0) {
                    consumed++;
                }
                return consumed;
            }
        } else if (argv[0][0] == 's') {
            /* Text string */
            error = cbor_encode_text_string(encoder, &(argv[0][1]), len - 1);
            if (error != CborNoError) {
                printf(
                        "Failed at text string (%s): ",
                        argv[0]
                );
                print_err(error);
                return -1;
            } else {
                return 1;
            }
        } else if (argv[0][0] == 'x') {
            /* Byte string, total length must be odd with 'x' prefix */
            if ((len % 2) == 0) {
                printf("Byte string must be an even number of hex digits.\n");
                return -1;
            }

            uint8_t bytes[len / 2];
            int i;
            for (i = 1; i < len; i++) {
                char nybble = argv[0][i];
                if ((nybble >= '0') && (nybble <= '9')) {
                    nybble -= '0';
                } else if ((nybble >= 'A') && (nybble <= 'F')) {
                    nybble -= 'A';
                    nybble += 10;
                } else if ((nybble >= 'a') && (nybble <= 'f')) {
                    nybble -= 'a';
                    nybble += 10;
                } else {
                    printf("Unsupported character '%c' in byte string at %d\n",
                            nybble, i);
                    return -1;
                }

                if ((i % 2) == 0) {
                    /* Even numbered: lower nybble */
                    bytes[(i - 1)/2] |= nybble;
                } else {
                    /* Odd numbered: upper nybble */
                    bytes[i/2] = nybble << 4;
                }
            }

            error = cbor_encode_byte_string(encoder, bytes, len / 2);
            if (error != CborNoError) {
                printf(
                        "Failed at byte string (%s): ",
                        argv[0]
                );
                print_err(error);
                return -1;
            } else {
                return 1;
            }
        } else if (argv[0][0] == 'd') {
            /* 32-bit float number */
            char *endptr = NULL;
            double d = strtod(&(argv[0][1]), &endptr);

            if (!endptr || (*endptr)) {
                /* Invalid number */
                printf("Invalid double %s\n", argv[0]);
                return -1;
            }

            error = cbor_encode_double(encoder, d);
            if (error != CborNoError) {
                printf(
                        "Failed at double (%s): ",
                        argv[0]
                );
                print_err(error);
                return -1;
            } else {
                return 1;
            }
        } else if (argv[0][0] == 'f') {
            /* 32-bit float number */
            char *endptr = NULL;
            float f = strtof(&(argv[0][1]), &endptr);

            if (!endptr || (*endptr)) {
                /* Invalid number */
                printf("Invalid float %s\n", argv[0]);
                return -1;
            }

            error = cbor_encode_float(encoder, f);
            if (error != CborNoError) {
                printf(
                        "Failed at float (%s): ",
                        argv[0]
                );
                print_err(error);
                return -1;
            } else {
                return 1;
            }
        } else if (argv[0][0] == 'u') {
            /* Unsigned integer (positive) number */
            char *endptr = NULL;
            unsigned long long ull = strtoull(&(argv[0][1]), &endptr, 0);

            if (!endptr || (*endptr)) {
                /* Invalid number */
                printf("Invalid unsigned integer %s\n", argv[0]);
                return -1;
            }

            error = cbor_encode_uint(encoder, ull);
            if (error != CborNoError) {
                printf(
                        "Failed at unsigned integer (%s): ",
                        argv[0]
                );
                print_err(error);
                return -1;
            } else {
                return 1;
            }
        } else if (argv[0][0] == '-') {
            /* Unsigned integer (negative) number */
            char *endptr = NULL;
            unsigned long long ull = strtoull(&(argv[0][1]), &endptr, 0);

            if (!endptr || (*endptr)) {
                /* Invalid number */
                printf("Invalid negative unsigned integer %s\n", argv[0]);
                return -1;
            }

            error = cbor_encode_negative_int(encoder, ull);
            if (error != CborNoError) {
                printf(
                        "Failed at negative unsigned integer (%s): ",
                        argv[0]
                );
                print_err(error);
                return -1;
            } else {
                return 1;
            }
        } else {
            printf("Unknown command: %s", argv[0]);
            return -1;
        }
    } else {
        /* No arguments to consume. */
        printf("End of arguments.\n");
        return 0;
    }
}

/**
 * Interpret the arguments given and execute one of the `tinycbor` routines,
 * in an array context.
 *
 * \param[inout]    encoder     CBOREncoder instance
 * \param[in]       len         Length of the array.
 * \param[in]       argc        Number of command line arguments remaining
 * \param[in]       argv        Command line arguments' values
 *
 * \retval          ≤0          CBOR error occurred, stop here.
 * \retval          >0          Number of arguments consumed.
 */
int exec_arg_array(CborEncoder * const encoder, size_t len, int argc, char **argv) {
    int consumed = 0;
    CborEncoder container;
    CborError error;

    error = cbor_encoder_create_array(encoder, &container, len);
    if (error != CborNoError) {
        printf("Failed to create array (length=%lu): ", len);
        print_err(error);
        consumed = -1;
    } else {
        while ((consumed >= 0) && (argc > 0) && (argv[0][0] != ']')) {
            int arg_consumed = exec_arg(&container, argc, argv);

            if (arg_consumed > 0) {
                consumed += arg_consumed;
                argc -= arg_consumed;
                argv += arg_consumed;
            } else {
                /* Error condition */
                printf(
                        "Failed inside array context (after %d arguments).\n",
                        consumed
                      );
                consumed = -1;
            }
        }

        if (consumed >= 0) {
            printf("Close array after %d arguments\n", consumed);

            /* Count end-of-array */
            consumed++;

            error = cbor_encoder_close_container(encoder, &container);
            if (error != CborNoError) {
                printf("Failed to finish array (length=%lu): ", len);
                print_err(error);
                consumed = -1;
            }
        }
    }

    return consumed;
}

/**
 * Interpret the arguments given and execute one of the `tinycbor` routines,
 * in a map context.
 *
 * \param[inout]    encoder     CBOREncoder instance
 * \param[in]       len         Length of the map.
 * \param[in]       argc        Number of command line arguments remaining
 * \param[in]       argv        Command line arguments' values
 *
 * \retval          ≤0          CBOR error occurred, stop here.
 * \retval          >0          Number of arguments consumed.
 */
int exec_arg_map(CborEncoder * const encoder, size_t len, int argc, char **argv) {
    int consumed = 0;
    CborEncoder container;
    CborError error;

    error = cbor_encoder_create_map(encoder, &container, len);
    if (error != CborNoError) {
        printf("Failed to create map (length=%lu): ", len);
        print_err(error);
        consumed = -1;
    } else {
        while ((consumed >= 0) && (argc > 0) && (argv[0][0] != '}')) {
            int arg_consumed = exec_arg(&container, argc, argv);

            if (arg_consumed > 0) {
                consumed += arg_consumed;
                argc -= arg_consumed;
                argv += arg_consumed;
            } else {
                /* Error condition */
                printf(
                        "Failed inside map context (after %d arguments).\n",
                        consumed
                      );
                consumed = -1;
            }
        }

        if (consumed >= 0) {
            printf("Close map after %d arguments\n", consumed);

            /* Count end-of-map */
            consumed++;

            error = cbor_encoder_close_container(encoder, &container);
            if (error != CborNoError) {
                printf("Failed to finish map (length=%lu): ", len);
                print_err(error);
                consumed = -1;
            }
        }
    }

    return consumed;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf(
                "Usage: %s <filename> <commands> ...\n"
                "Valid commands:\n"
                "\t{\tStart an unknown-length map\n"
                "\t[\tStart an unknown-length array\n"
                "\tmap(<len>) {\tStart a map of length <len>\n"
                "\tarray(<len>) [\tStart an array of length <len>\n"
                "\ts<text>\tInsert a text string\n"
                "\tx<hex>\tInsert a byte string\n"
                "\tu<num>\tInsert an unsigned positive integer\n"
                "\t-<num>\tInsert an unsigned negative integer\n"
                "\td<num>\tInsert a 64-bit float\n"
                "\tf<num>\tInsert a 32-bit float\n"
                "\tf, t\tInsert FALSE or TRUE (case insensitive)\n"
                "\tn, u\tInsert NULL or UNDEFINED (case insensitive)\n"
                "\nInside maps:\n"
                "\t}\tEnd the current map\n"
                "\nInside arrays:\n"
                "\t]\tEnd the current array\n",
                argv[0]
        );
        return 1;
    } else {
        struct filewriter context;
        CborEncoder encoder;
        CborError error;

        /* Open the file for writing, create if needed */
        error = filewriter_open(
                &encoder,                               /* CBOR context */
                &context,                               /* Writer context */
                argv[1],                                /* File name */
                O_CREAT,                                /* Open flags */
                S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH   /* File permissions */
        );

        if (error != CborNoError) {
            printf("Failed to open %s for writing: ", argv[1]);
            print_err(error);
        } else {
            argv += 2;
            argc -= 2;

            while (argc > 0) {
                int consumed = exec_arg(&encoder, argc, argv);

                if (consumed > 0) {
                    argc -= consumed;
                    argv += consumed;
                } else {
                    break;
                }
            }

            error = filewriter_close(&context);
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
