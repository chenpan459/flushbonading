# Copyright 2023-2024 Arm Limited and/or its affiliates
# <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.21)

# CMSIS headers
add_subdirectory(CMSIS)

add_library(arm-corstone-platform-bsp STATIC)

# Target selection
if(ARM_CORSTONE_BSP_TARGET_PLATFORM STREQUAL "corstone300")
    add_subdirectory(corstone300)
elseif(ARM_CORSTONE_BSP_TARGET_PLATFORM STREQUAL "corstone310")
    add_subdirectory(corstone310)
elseif(ARM_CORSTONE_BSP_TARGET_PLATFORM STREQUAL "corstone315")
    add_subdirectory(corstone315)
elseif(ARM_CORSTONE_BSP_TARGET_PLATFORM STREQUAL "corstone320")
    add_subdirectory(corstone320)
endif()
