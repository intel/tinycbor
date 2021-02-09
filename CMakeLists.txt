# /****************************************************************************
# **
# ** Copyright (C) 2015 Intel Corporation
# **
# ** Permission is hereby granted, free of charge, to any person obtaining a copy
# ** of this software and associated documentation files (the "Software"), to deal
# ** in the Software without restriction, including without limitation the rights
# ** to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# ** copies of the Software, and to permit persons to whom the Software is
# ** furnished to do so, subject to the following conditions:
# **
# ** The above copyright notice and this permission notice shall be included in
# ** all copies or substantial portions of the Software.
# **
# ** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# ** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# ** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# ** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# ** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# ** OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# ** THE SOFTWARE.
# **
# ****************************************************************************/

project(intel_tinycbor LANGUAGES C)

cmake_minimum_required(VERSION 3.10)

option(build_tools "Build the cbor tools. Note: This will install the cjson library as a dependency" OFF)

add_library(tinycbor
    ${CMAKE_CURRENT_LIST_DIR}/src/cborencoder.c
    ${CMAKE_CURRENT_LIST_DIR}/src/cborencoder_close_container_checked.c
    ${CMAKE_CURRENT_LIST_DIR}/src/cborerrorstrings.c
    ${CMAKE_CURRENT_LIST_DIR}/src/cborparser.c
    ${CMAKE_CURRENT_LIST_DIR}/src/cborparser_dup_string.c
    ${CMAKE_CURRENT_LIST_DIR}/src/cborpretty.c
    ${CMAKE_CURRENT_LIST_DIR}/src/cborpretty_stdio.c
    ${CMAKE_CURRENT_LIST_DIR}/src/cbortojson.c
    ${CMAKE_CURRENT_LIST_DIR}/src/cborvalidation.c
    ${CMAKE_CURRENT_LIST_DIR}/src/open_memstream.c
)

target_include_directories(tinycbor
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/src
)

add_library(intel::tinycbor ALIAS tinycbor)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/examples)

if(${build_tools})
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/tools)
endif()
