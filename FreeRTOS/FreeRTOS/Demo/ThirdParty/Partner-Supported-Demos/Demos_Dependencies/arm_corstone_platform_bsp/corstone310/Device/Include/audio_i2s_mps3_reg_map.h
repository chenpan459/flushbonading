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
 * \file audio_i2s_mps3_reg_map.h
 * \brief Register map for Audio I2S
 */

#ifndef __AUDIO_I2S_MPS3_REG_MAP__
#define __AUDIO_I2S_MPS3_REG_MAP__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief Audio I2S register map structure
 */
struct audio_i2s_mps3_reg_map_t{
        /* Offset: 0x000 (R/W) Control Register    */
    volatile uint32_t control;
        /* Offset: 0x004 (R/W) Status Register    */
    volatile uint32_t status;
        /* Offset: 0x008 (R/W) Error Register    */
    volatile uint32_t error;
        /* Offset: 0x00C (R/W) Clock Divide Ratio Register    */
    volatile uint32_t divide;
        /* Offset: 0x010 (W) Transmit Buffer FIFO Data Register    */
    volatile uint32_t txbuf;
        /* Offset: 0x014 (R) Receive Buffer FIFO Data Register    */
    volatile uint32_t rxbuf;
        /*!< Offset: 0x018-0x2FF Reserved */
    volatile const uint32_t reserved[14];
        /* Offset: 0x300 (R/W) Integration Test Control Register    */
    volatile uint32_t itcr;
        /* Offset: 0x304 (R/W) Integration Test Input Register    */
    volatile uint32_t itip1;
        /* Offset: 0x308 (R/W) Integration Test Output Register    */
    volatile uint32_t itop1;
};

#ifdef __cplusplus
}
#endif

#endif /* __AUDIO_I2S_MPS3_REG_MAP__ */
