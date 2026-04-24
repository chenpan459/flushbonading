# Copyright 2023-2024 Arm Limited and/or its affiliates
# <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.21)

add_library(cmsis-headers INTERFACE)

target_include_directories(cmsis-headers
    INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/CMSIS_6/CMSIS/Driver/Include
        ${CMAKE_CURRENT_LIST_DIR}/CMSIS_6/CMSIS/Driver/VIO/Include
        ${CMAKE_CURRENT_LIST_DIR}/CMSIS_6/CMSIS/Core/Include
        ${CMAKE_CURRENT_LIST_DIR}/CMSIS_6/CMSIS/Core/Include/m-profile
)

