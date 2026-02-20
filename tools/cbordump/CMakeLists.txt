# Copyright (C) 2025 Intel Corporation
# SPDX-License-Identifier: MIT
if(NOT WIN32)
  tinycbor_add_executable(cbordump cbordump.c)
  target_include_directories(cbordump PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
  target_link_libraries(cbordump tinycbor)
  install(TARGETS cbordump)
endif()
