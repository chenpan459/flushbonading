/*
 * Copyright (c) 2022 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


/**
 * \file syswdog_armv8-m_reg_map.h
 * \brief Register map for SYSWDOG
 */

#ifndef SYSWDOG_ARMV8_M_REG_MAP
#define SYSWDOG_ARMV8_M_REG_MAP

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief Watchdog Control and Refresh Frame register map structure
 */
struct wdog_frame_reg_map_t {
/* Control Frame registers */
    volatile uint32_t wcs;
        /*!< Offset: 0x0000 (RW) Watchdog Control and Status register */
    volatile uint32_t reserved0;
        /*!< Offset: 0x0004 (RES0) Reserved */
    volatile uint32_t wor;
        /*!< Offset: 0x0008 (RW) Watchdog Offset register */
    volatile uint32_t reserved1;
        /*!< Offset: 0x000C (RES1) Reserved */
    volatile uint32_t wcv_low;
        /*!< Offset: 0x0010 (RW) Watchdog Compare Value low register [31:0] */
    volatile uint32_t wcv_high;
        /*!< Offset: 0x0014 (RW) Watchdog Compare Value high register [63:32] */
    volatile uint32_t reserved2[1005];
        /*!< Offset: 0x0018 (RES2) Reserved */
    volatile uint32_t cnt_w_iidr;
        /*!< Offset: 0xFCC (RO) Watchdog Interface Identification register */
    volatile uint32_t cnt_pidr4;
        /*!< Offset: 0xFD0 Peripheral ID 4 */
    volatile uint32_t reserved3[3];
        /*!< Offset: 0x0FD4 (RES3) Reserved */
    volatile uint32_t cnt_pidr0;
        /*!< Offset: 0xFE0 Peripheral ID 0 */
    volatile uint32_t cnt_pidr1;
        /*!< Offset: 0xFE4 Peripheral ID 1 */
    volatile uint32_t cnt_pidr2;
        /*!< Offset: 0xFE8 Peripheral ID 2 */
    volatile uint32_t cnt_pidr3;
        /*!< Offset: 0xFEC Peripheral ID 3 */
    volatile uint32_t cnt_cidr0;
        /*!< Offset: 0xFF0 Component ID 0 */
    volatile uint32_t cnt_cidr1;
        /*!< Offset: 0xFF4 Component ID 1 */
    volatile uint32_t cnt_cidr2;
        /*!< Offset: 0xFF8 Component ID 2 */
    volatile uint32_t cnt_cidr3;
        /*!< Offset: 0xFFC Component ID 3 */
/* Refresh Frame registers */
    volatile uint32_t wrr;
        /*!< Offset: 0x1000 (RW) Watchdog Refresh register */
    volatile uint32_t reserved4[1010];
        /*!< Offset: 0x1004 (RES4) Reserved */
    volatile uint32_t ref_w_iidr;
        /*!< Offset: 0x1FCC (RO) Watchdog Interface Identification register */
    volatile uint32_t ref_pidr4;
        /*!< Offset: 0x1FD0 Peripheral ID 4 */
    volatile uint32_t reserved5[3];
        /*!< Offset: 0x1FD4 (RES5) Reserved */
    volatile uint32_t ref_pidr0;
        /*!< Offset: 0x1FE0 Peripheral ID 0 */
    volatile uint32_t ref_pidr1;
        /*!< Offset: 0x1FE4 Peripheral ID 1 */
    volatile uint32_t ref_pidr2;
        /*!< Offset: 0x1FE8 Peripheral ID 2 */
    volatile uint32_t ref_pidr3;
        /*!< Offset: 0x1FEC Peripheral ID 3 */
    volatile uint32_t ref_cidr0;
        /*!< Offset: 0x1FF0 Component ID 0 */
    volatile uint32_t ref_cidr1;
        /*!< Offset: 0x1FF4 Component ID 1 */
    volatile uint32_t ref_cidr2;
        /*!< Offset: 0x1FF8 Component ID 2 */
    volatile uint32_t ref_cidr3;
        /*!< Offset: 0x1FFC Component ID 3 */
};

#ifdef __cplusplus
}
#endif

#endif /* SYSWDOG_ARMV8_M_REG_MAP */
