# Copyright (C) 2025 Intel Corporation
# SPDX-License-Identifier: MIT
function(tinycbor_add_executable target)
  add_executable(${target} ${ARGN})
  target_link_libraries(${target} tinycbor)
  target_compile_options(${target} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:-W3>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
  )
endfunction()
