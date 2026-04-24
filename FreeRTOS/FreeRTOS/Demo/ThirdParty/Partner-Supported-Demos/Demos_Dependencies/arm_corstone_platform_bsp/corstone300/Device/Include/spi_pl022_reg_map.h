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
 * \file spi_pl022_reg_map.h
 * \brief Register map for SPI PL022
 */

#ifndef SPI_PL022_REG_MAP
#define SPI_PL022_REG_MAP

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Structure for the SSP Primary Cell device registers */
struct spi_pl022_dev_reg_map_t {
    volatile uint32_t sspcr0;        /* Control register 0 */
    volatile uint32_t sspcr1;        /* Control register 1 */
    volatile uint32_t sspdr;         /* Data register */
    volatile uint32_t sspsr;         /* Status register */
    volatile uint32_t sspcpsr;       /* Clock prescale register */
    volatile uint32_t sspimsc;       /* Interrupt mask set or clear register */
    volatile uint32_t sspris;        /* Raw interrupt status register */
    volatile uint32_t sspmis;        /* Masked interrupt status register */
    volatile uint32_t sspicr;        /* Interrupt clear register */
    volatile uint32_t sspdmacr;      /* DMA control register */
    volatile uint32_t reserved[1006];/* Reserved from Base+0x28-0xFE0 */
    volatile uint32_t sspperiphid0;  /* Peripheral id register 0 */
    volatile uint32_t sspperiphid1;  /* Peripheral id register 1 */
    volatile uint32_t sspperiphid2;  /* Peripheral id register 2 */
    volatile uint32_t sspperiphid3;  /* Peripheral id register 3 */
    volatile uint32_t ssppcellid0;   /* Primary cell id register 0 */
    volatile uint32_t ssppcellid1;   /* Primary cell id register 1 */
    volatile uint32_t ssppcellid2;   /* Primary cell id register 2 */
    volatile uint32_t ssppcellid3;   /* Primary cell id register 3 */
};

#ifdef __cplusplus
}
#endif

#endif /* SPI_PL022_REG_MAP */
