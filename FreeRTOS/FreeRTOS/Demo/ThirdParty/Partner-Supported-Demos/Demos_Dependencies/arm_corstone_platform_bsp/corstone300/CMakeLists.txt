# Copyright 2023 Arm Limited and/or its affiliates
# <open-source-office@arm.com>
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.21)

target_compile_definitions(arm-corstone-platform-bsp
    PUBLIC
        CMSIS_device_header="SSE300MPS3.h"
)

target_include_directories(arm-corstone-platform-bsp
    PUBLIC
        .
        ${CMAKE_CURRENT_LIST_DIR}/Board/Device_Definition
        ${CMAKE_CURRENT_LIST_DIR}/Board/Platform
        ${CMAKE_CURRENT_LIST_DIR}/CMSIS_Driver
        ${CMAKE_CURRENT_LIST_DIR}/CMSIS_Driver/Config
        ${CMAKE_CURRENT_LIST_DIR}/Device/Include
)

target_sources(arm-corstone-platform-bsp
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/Device/Source/Driver_MPC.c
        ${CMAKE_CURRENT_LIST_DIR}/CMSIS_Driver/Driver_SPI.c
        ${CMAKE_CURRENT_LIST_DIR}/CMSIS_Driver/Driver_USART.c
        ${CMAKE_CURRENT_LIST_DIR}/CMSIS_Driver/Driver_I2C.c
        ${CMAKE_CURRENT_LIST_DIR}/Device/Source/Driver_SSE300_PPC.c
        ${CMAKE_CURRENT_LIST_DIR}/Board/Device_Definition/device_definition.c
        ${CMAKE_CURRENT_LIST_DIR}/Device/Source/system_SSE300MPS3.c
        ${CMAKE_CURRENT_LIST_DIR}/Device/Source/startup_SSE300MPS3.c

        # Drivers
        ${CMAKE_CURRENT_LIST_DIR}/Device/Source/systimer_armv8-m_drv.c
        ${CMAKE_CURRENT_LIST_DIR}/Device/Source/mpc_sie_drv.c
        ${CMAKE_CURRENT_LIST_DIR}/Device/Source/syscounter_armv8-m_read_drv.c
        ${CMAKE_CURRENT_LIST_DIR}/Device/Source/mpu_armv8m_drv.c
        ${CMAKE_CURRENT_LIST_DIR}/Device/Source/syswdog_armv8-m_drv.c
        ${CMAKE_CURRENT_LIST_DIR}/Device/Source/vio_fvp.c
        ${CMAKE_CURRENT_LIST_DIR}/Device/Source/spi_pl022_drv.c
        ${CMAKE_CURRENT_LIST_DIR}/Device/Source/ppc_sse300_drv.c
        ${CMAKE_CURRENT_LIST_DIR}/Device/Source/audio_i2s_mps3_drv.c
        ${CMAKE_CURRENT_LIST_DIR}/Device/Source/uart_cmsdk_drv.c
        ${CMAKE_CURRENT_LIST_DIR}/Device/Source/syscounter_armv8-m_cntrl_drv.c
        ${CMAKE_CURRENT_LIST_DIR}/Device/Source/i2c_sbcon_drv.c
        ${CMAKE_CURRENT_LIST_DIR}/Device/Source/emulated_flash_drv.c
        ${CMAKE_CURRENT_LIST_DIR}/Device/Source/gpio_cmsdk_drv.c
        ${CMAKE_CURRENT_LIST_DIR}/Device/Source/arm_mps3_io_drv.c
)

target_link_libraries(arm-corstone-platform-bsp
    PUBLIC
        cmsis-headers
)
