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

cmake_minimum_required(VERSION 3.10)

project(tinycbor LANGUAGES C VERSION 0.6.0)

# Set path to additional cmake scripts
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})

set(TARGETS_EXPORT_NAME "tinycbor-targets")

# Include additional modules that are used unconditionally
include(GNUInstallDirs)
include(GenerateExportHeader)

add_library(tinycbor
  src/cborencoder.c
  src/cborencoder_float.c
  src/cborencoder_close_container_checked.c
  src/cborerrorstrings.c
  src/cborparser.c
  src/cborparser_dup_string.c
  src/cborparser_float.c
  src/cborpretty.c
  src/cborpretty_stdio.c
  src/cbortojson.c
  src/cborvalidation.c
  src/cbor.h
  src/tinycbor-version.h
)

# Generate export macros
generate_export_header(tinycbor BASE_NAME "cbor" EXPORT_MACRO_NAME "CBOR_API" EXPORT_FILE_NAME "tinycbor-export.h")

# Check for open_memstream and store the result in HAVE_OPEN_MEMSTREAM
include (CheckSymbolExists)
check_symbol_exists(open_memstream stdio.h HAVE_OPEN_MEMSTREAM)
check_symbol_exists(funopen stdio.h HAVE_OPEN_FUNOPEN)
check_symbol_exists(fopencookie stdio.h HAVE_OPEN_FOPENCOOKIE)

if(NOT HAVE_OPEN_MEMSTREAM)
  if (HAVE_OPEN_FUNOPEN AND HAVE_OPEN_FOPENCOOKIE)
    message(STATUS "using open_memstream implementation")
    target_sources(tinycbor PRIVATE src/open_memstream.c)
  else()
    target_compile_definitions(tinycbor PRIVATE WITHOUT_OPEN_MEMSTREAM)
    message(WARNING "funopen and fopencookie unavailable, open_memstream can not be implemented and conversion to JSON will not work properly!")
  endif()
endif()

target_include_directories(tinycbor
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_BINARY_DIR}>"
  PUBLIC "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

# Set version and output name
set_target_properties(tinycbor PROPERTIES
  VERSION "${PROJECT_VERSION}"
  SOVERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")

install(FILES src/cbor.h src/tinycbor-version.h ${CMAKE_BINARY_DIR}/tinycbor-export.h TYPE INCLUDE)
install(
  TARGETS tinycbor
  EXPORT "${TARGETS_EXPORT_NAME}"
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}   # import library
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}   # .so files are libraries
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}   # .dll files are binaries
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}  # this does not actually install anything (but used by downstream projects)
)

set (PROJECT_LIBRARIES tinycbor)
include(PackageConfig)

enable_testing()
add_subdirectory(tests)
