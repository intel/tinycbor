# /****************************************************************************
# **
# ** Copyright (C) 2026 Topogamer0
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

include(TinyCBORHelpers)

# The library and the fuzzers must be compiled with the instrumentation the
# fuzzing engine needs (for libFuzzer, -fsanitize=fuzzer-no-link in
# CMAKE_C_FLAGS) and any sanitizers. LIB_FUZZING_ENGINE is what provides
# main(), as in OSS-Fuzz (which sets it in the environment).
if(DEFINED ENV{LIB_FUZZING_ENGINE})
  set(LIB_FUZZING_ENGINE "$ENV{LIB_FUZZING_ENGINE}" CACHE STRING "Fuzzing engine to link the fuzzers with")
else()
  set(LIB_FUZZING_ENGINE "-fsanitize=fuzzer" CACHE STRING "Fuzzing engine to link the fuzzers with")
  if(NOT CMAKE_C_FLAGS MATCHES "-fsanitize=[^ ]*fuzzer")
    message(WARNING "CMAKE_C_FLAGS has no -fsanitize=fuzzer-no-link: the fuzzers will not be instrumented")
  endif()
endif()

# The fuzzing engines are written in C++
enable_language(CXX)

set(fuzzers encoder parser pretty validate)
if(WITH_CBOR2JSON)
  list(APPEND fuzzers tojson)
endif()

foreach(fuzzer ${fuzzers})
  tinycbor_add_executable(${fuzzer}_fuzzer ${fuzzer}_fuzzer.c)
  target_link_libraries(${fuzzer}_fuzzer ${LIB_FUZZING_ENGINE})
  set_target_properties(${fuzzer}_fuzzer PROPERTIES LINKER_LANGUAGE CXX)
endforeach()
