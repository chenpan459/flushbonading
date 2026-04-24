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
 * \file gpio_cmsdk_reg_map.h
 * \brief Register map for GPIO
 */

#ifndef __GPIO_CMSDK_REG_MAP__
#define __GPIO_CMSDK_REG_MAP__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GPIO register map structure */
struct gpio_cmsdk_reg_map_t {
    volatile uint32_t  data;           /* Offset: 0x000 (R/W) Data register */
    volatile uint32_t  dataout;        /* Offset: 0x004 (R/W) Data output
                                        *                     latch register */
    volatile uint32_t  reserved0[2];
    volatile uint32_t  outenableset;   /* Offset: 0x010 (R/W) Output enable
                                        *                     set register */
    volatile uint32_t  outenableclr;   /* Offset: 0x014 (R/W) Output enable
                                        *                     clear register */
    volatile uint32_t  altfuncset;     /* Offset: 0x018 (R/W) Alternate function
                                        *                     set register */
    volatile uint32_t  altfuncclr;     /* Offset: 0x01C (R/W) Alternate function
                                        *                     clear register */
    volatile uint32_t  intenset;       /* Offset: 0x020 (R/W) Interrupt enable
                                        *                     set register */
    volatile uint32_t  intenclr;       /* Offset: 0x024 (R/W) Interrupt enable
                                        *                     clear register */
    volatile uint32_t  inttypeset;     /* Offset: 0x028 (R/W) Interrupt type
                                        *                     set register */
    volatile uint32_t  inttypeclr;     /* Offset: 0x02C (R/W) Interrupt type
                                        *                     clear register */
    volatile uint32_t  intpolset;      /* Offset: 0x030 (R/W) Interrupt polarity
                                        *                     set register */
    volatile uint32_t  intpolclr;      /* Offset: 0x034 (R/W) Interrupt polarity
                                        *                     clear register */
    union {
        volatile uint32_t  intstatus;  /* Offset: 0x038 (R/ ) Interrupt status
                                        *                     register */
        volatile uint32_t  intclear;   /* Offset: 0x038 ( /W) Interrupt clear
                                        *                     register */
    }intreg;
    volatile uint32_t reserved1[997];
    volatile uint32_t pid4;            /* Peripheral ID Register 4 */
    volatile uint32_t pid0;            /* Peripheral ID Register 0 */
    volatile uint32_t pid1;            /* Peripheral ID Register 1 */
    volatile uint32_t pid2;            /* Peripheral ID Register 2 */
    volatile uint32_t pid3;            /* Peripheral ID Register 3 */
    volatile uint32_t cid0;            /* Component ID Register 0 */
    volatile uint32_t cid1;            /* Component ID Register 1 */
    volatile uint32_t cid2;            /* Component ID Register 2 */
    volatile uint32_t cid4;            /* Component ID Register 3 */
};

#ifdef __cplusplus
}
#endif

#endif /* __GPIO_CMSDK_REG_MAP__ */
