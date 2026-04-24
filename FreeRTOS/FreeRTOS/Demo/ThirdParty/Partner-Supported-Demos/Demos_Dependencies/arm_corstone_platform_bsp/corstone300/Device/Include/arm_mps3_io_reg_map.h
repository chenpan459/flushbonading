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
 * \file arm_mps3_io_reg_map.h
 * \brief Register map for ARM MPS3 FPGAIO.
 */

#ifndef __ARM_MPS3_IO_REG_MAP_H__
#define __ARM_MPS3_IO_REG_MAP_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MPS3 IO register map structure */
struct arm_mps3_io_reg_map_t {
    volatile uint32_t fpgaio_leds;      /* Offset: 0x000 (R/W) LED connections
                                         *         [31:10] : Reserved
                                         *         [9:0]  : FPGAIO LEDs */
    volatile uint32_t reserved[1];
    volatile uint32_t fpgaio_buttons;   /* Offset: 0x008 (R/ ) Buttons
                                         *         [31:2] : Reserved
                                         *         [1:0]  : Buttons */
    volatile uint32_t reserved2[1];
    volatile uint32_t fpgaio_clk1hz;    /* Offset: 0x010 (R/W) 1Hz upcounter */
    volatile uint32_t fpgaio_clk100hz;  /* Offset: 0x014 (R/W) 100Hz
                                         *                  up counter */
    volatile uint32_t fpgaio_counter;   /* Offset: 0x018 (R/W) Cycle Up
                                         *                  Counter */
    volatile uint32_t fpgaio_prescale;  /* Offset: 0x01C (R/W)
                                         *         [31:0] : reload value
                                         *                  for prescale
                                         *                  counter */
    volatile uint32_t fpgaio_pscntr;    /* Offset: 0x020 (R/ ) Current value
                                         *                  of the pre-scaler
                                         *                  counter */
    volatile uint32_t reserved3[1];
    volatile uint32_t fpgaio_switches;  /* Offset: 0x028 (R/ ) Denotes the
                                         *                  state of the FPGAIO
                                         *                  user switches
                                         *         [31:8] : Reserved
                                         *         [7:0]  : FPGAIO switches */
    volatile uint32_t reserved4[8];
    volatile uint32_t fpgaio_misc;      /* Offset: 0x04C (R/W) Misc control
                                         *         [31:3] : Reserved
                                         *         [2]    : SHIELD1_SPI_nCS
                                         *         [1]    : SHIELD0_SPI_nCS
                                         *         [0]    : ADC_SPI_nCS */
};

#ifdef __cplusplus
}
#endif

#endif /* __ARM_MPS3_IO_REG_MAP_H__ */
