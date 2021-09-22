# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

include(ExternalProject)

add_executable(json2cbor
    ${CMAKE_CURRENT_LIST_DIR}/json2cbor/json2cbor.c
)

ExternalProject_Add(cjson
    GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
    CMAKE_ARGS -DBUILD_SHARED_AND_STATIC_LIBS=ON
)

target_link_libraries(json2cbor
    PRIVATE
        libcjson.a
        intel::tinycbor
)
