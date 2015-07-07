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

#ifndef COMPILERSUPPORT_H
#define COMPILERSUPPORT_H

#ifndef _BSD_SOURCE
#  define _BSD_SOURCE
#endif
#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#ifndef UINT32_MAX
/* C99 requires it in stdint.h, but some systems lack it */
#  define UINT32_MAX    (0xffffffffU)
#endif

#ifndef __has_builtin
#  define __has_builtin(x)  0
#endif

#if (defined(__GNUC__) && (__GNUC__ * 100 + __GNUC_MINOR__ >= 403)) || \
    (__has_builtin(__builtin_bswap64) && __has_builtin(__builtin_bswap32))
#  if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#    define cbor_ntohll     __builtin_bswap64
#    define cbor_htonll     __builtin_bswap64
#    define cbor_ntohl      __builtin_bswap32
#    define cbor_htonl      __builtin_bswap32
#    ifdef __INTEL_COMPILER
#      define cbor_ntohs    _bswap16
#      define cbor_htons    _bswap16
#    elif (__GNUC__ * 100 + __GNUC_MINOR__ >= 608) || __has_builtin(__builtin_bswap16)
#      define cbor_ntohs    __builtin_bswap16
#      define cbor_htons    __builtin_bswap16
#    else
#      define cbor_ntohs(x) (((uint16_t)x >> 8) | ((uint16_t)x << 8))
#      define cbor_htons    cbor_ntohs
#    endif
#  else
#    define cbor_ntohll
#    define cbor_htonll
#    define cbor_ntohl
#    define cbor_htonl
#    define cbor_ntohs
#    define cbor_htons
#  endif
#elif defined(__sun)
#  include <sys/byteorder.h>
#elif defined(_MSC_VER)
/* MSVC, which implies Windows, which implies little-endian and sizeof(long) == 4 */
#  define cbor_ntohll       _byteswap_uint64
#  define cbor_htonll       _byteswap_uint64
#  define cbor_ntohl        _byteswap_ulong
#  define cbor_htonl        _byteswap_ulong
#  define cbor_ntohs        _byteswap_ushort
#  define cbor_htons        _byteswap_ushort
#endif
#ifndef cbor_ntohs
#  include <arpa/inet.h>
#  define cbor_ntohs        ntohs
#  define cbor_htons        htons
#endif
#ifndef cbor_ntohl
#  include <arpa/inet.h>
#  define cbor_ntohl        ntohl
#  define cbor_htonl        htonl
#endif
#ifndef cbor_ntohll
#  define cbor_ntohll       ntohll
#  define cbor_htonll       htonll
/* ntohll isn't usually defined */
#  ifndef ntohll
#    if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#      define ntohll
#      define htonll
#    elif defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#      define ntohll(x)       ((ntohl((uint32_t)(x)) * UINT64_C(0x100000000)) + (ntohl((x) >> 32)))
#      define htonll          ntohll
#    else
#      error "Unable to determine byte order!"
#    endif
#  endif
#endif


#ifdef __cplusplus
#  define CONST_CAST(t, v)  const_cast<t>(v)
#else
// C-style const_cast without triggering a warning with -Wcast-qual
#  define CONST_CAST(t, v)  (t)(uintptr_t)(v)
#endif

#ifdef __GNUC__
#  define likely(x)     __builtin_expect(!!(x), 1)
#  define unlikely(x)   __builtin_expect(!!(x), 0)
#  define unreachable() __builtin_unreachable()
#elif defined(_MSC_VER)
#  define likely(x)     (x)
#  define unlikely(x)   (x)
#  define unreachable() __assume(0)
#else
#  define likely(x)     (x)
#  define unlikely(x)   (x)
#  define unreachable() do {} while (0)
#endif

#if defined(__GNUC__) && !defined(__INTEL_COMPILER) && !defined(__clang__) && \
    (__GNUC__ * 100 + __GNUC_MINOR__ >= 404)
#  pragma GCC optimize("-ffunction-sections")
#endif

static inline bool add_check_overflow(size_t v1, size_t v2, size_t *r)
{
#if (defined(__GNUC__) && (__GNUC__ >= 5)) || __has_builtin(__builtin_add_overflow)
    return __builtin_add_overflow(v1, v2, r);
#else
    // unsigned additions are well-defined
    *r = v1 + v2;
    return v1 > v1 + v2;
#endif
}

#endif // COMPILERSUPPORT_H

