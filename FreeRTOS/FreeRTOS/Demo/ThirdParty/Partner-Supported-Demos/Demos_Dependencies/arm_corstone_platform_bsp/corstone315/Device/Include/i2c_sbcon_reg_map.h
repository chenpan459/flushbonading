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
 * \file i2c_sbcon_reg_map.h
 * \brief Register map for I2C SBCon
 */

#ifndef I2C_SBCON_REG_MAP
#define I2C_SBCON_REG_MAP

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* I2C SBCon register map structure */
struct i2c_sbcon_ctrl_t {
    union {
        /* Offset: 0x000 Control Status Register (r/ ) */
        volatile uint32_t status;
        /* Offset: 0x000 Control Set Register    ( /w) */
        volatile uint32_t set;
    } ctrl_reg;
    /* Offset: 0x004 Control Clear Register    ( /w) */
    volatile uint32_t clear_reg;
};

#ifdef __cplusplus
}
#endif

#endif /* I2C_SBCON_REG_MAP */
