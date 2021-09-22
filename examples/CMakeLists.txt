# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

cmake_minimum_required(VERSION 3.10)

add_executable(simplereader
    ${CMAKE_CURRENT_LIST_DIR}/simplereader.c
)

target_link_libraries(simplereader
    PRIVATE
        intel::tinycbor
)
