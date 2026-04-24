/*
 * Copyright (c) 2021 ARM Limited
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
 * \file mpu_armv8m_reg_map.h
 * \brief Register map for MPU
 */


#ifndef __MPU_ARMV8M_REG_MAP_H__
#define __MPU_ARMV8M_REG_MAP_H__


#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif

struct mpu_armv8m_reg_map_t {
    volatile uint32_t type;     /* Offset: 0x000 (R/ ) Type Register */
    volatile uint32_t ctrl;     /* Offset: 0x004 (R/W) Control Register */
    volatile uint32_t rnr;      /* Offset: 0x008 (R/W) Region Number Register */
    volatile uint32_t rbar_a0;  /* Offset: 0x00C (R/W) Region Base Address Register */
    volatile uint32_t rlar_a0;  /* Offset: 0x010 (R/W) Region Limit Address Register */
    volatile uint32_t rbar_a1;  /* Offset: 0x014 (R/W) Region Base Address Register Alias 1 */
    volatile uint32_t rlar_a1;  /* Offset: 0x018 (R/W) Region Limit Address Register Alias 1 */
    volatile uint32_t rbar_a2;  /* Offset: 0x01C (R/W) Region Base Address Register Alias 2 */
    volatile uint32_t rlar_a2;  /* Offset: 0x020 (R/W) Region Limit Address Register Alias 2 */
    volatile uint32_t rbar_a3;  /* Offset: 0x024 (R/W) Region Base Address Register Alias 3 */
    volatile uint32_t rlar_a3;  /* Offset: 0x028 (R/W) Region Limit Address Register Alias 3 */
    volatile uint32_t reserved[1];
    volatile uint32_t mair0;    /* Offset: 0x030 (R/W) Memory Attribute Indirection Register 0 */
    volatile uint32_t mair1;    /* Offset: 0x034 (R/W) Memory Attribute Indirection Register 1 */
};

#ifdef __cplusplus
}
#endif


#endif /* __MPU_ARMV8M_REG_MAP_H__ */
