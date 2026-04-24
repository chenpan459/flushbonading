/*
 * Copyright (c) 2021-2022 ARM Limited
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

#include "arm_mps3_io_drv.h"
#include "arm_mps3_io_reg_map.h"

/* There is at most 10 LEDs 8 switches and 2 buttons on MPS3 FPGA IO */
#define MAX_PIN_FPGAIO_LED         10UL
#define MAX_PIN_FPGAIO_BUTTON      2UL
#define MAX_PIN_FPGAIO_SWITCH      8UL

/* FPGA system MISC control Register bit fields */
#define ARM_MPS3_IO_SHIELD1_SPI_NCS_OFF    2u
#define ARM_MPS3_IO_SHIELD0_SPI_NCS_OFF    1u
#define ARM_MPS3_IO_ADC_SPI_NCS_OFF        0u

/* Mask to 1 the first X bits */
#define MASK(X)            ((1UL << (X)) - 1)


void arm_mps3_io_write_leds(struct arm_mps3_io_dev_t* dev,
                            enum arm_mps3_io_access_t access,
                            uint8_t pin_num,
                            uint32_t value)
{
    struct arm_mps3_io_reg_map_t* p_mps3_io_port =
                                  (struct arm_mps3_io_reg_map_t*)dev->cfg->base;
    /* Mask of involved bits */
    uint32_t write_mask = 0;

    if (pin_num >= MAX_PIN_FPGAIO_LED) {
        return;
    }

    switch (access) {
    case ARM_MPS3_IO_ACCESS_PIN:
        write_mask = (1UL << pin_num);
        break;
    case ARM_MPS3_IO_ACCESS_PORT:
        write_mask = MASK(MAX_PIN_FPGAIO_LED);
        break;
    /*
     * default: explicitely not used to force to cover all enumeration
     * cases
     */
    }

    if (value) {
        p_mps3_io_port->fpgaio_leds |= write_mask;
    } else {
        p_mps3_io_port->fpgaio_leds &= ~write_mask;
    }

}

uint32_t arm_mps3_io_read_buttons(struct arm_mps3_io_dev_t* dev,
                                  enum arm_mps3_io_access_t access,
                                  uint8_t pin_num)
{
    struct arm_mps3_io_reg_map_t* p_mps3_io_port =
                                  (struct arm_mps3_io_reg_map_t*)dev->cfg->base;
    uint32_t value = 0;

    if (pin_num >= MAX_PIN_FPGAIO_BUTTON) {
        return 0;
    }

    /* Only read significant bits from this register */
    value = p_mps3_io_port->fpgaio_buttons &
            MASK(MAX_PIN_FPGAIO_BUTTON);

    if (access == ARM_MPS3_IO_ACCESS_PIN) {
        value = ((value >> pin_num) & 1UL);
    }

    return value;
}

uint32_t arm_mps3_io_read_switches(struct arm_mps3_io_dev_t* dev,
                                  enum arm_mps3_io_access_t access,
                                  uint8_t pin_num)
{
    struct arm_mps3_io_reg_map_t* p_mps3_io_port =
                                  (struct arm_mps3_io_reg_map_t*)dev->cfg->base;
    uint32_t value = 0;

    if (pin_num >= MAX_PIN_FPGAIO_SWITCH) {
        return 0;
    }

    /* Only read significant bits from this register */
    value = p_mps3_io_port->fpgaio_switches &
            MASK(MAX_PIN_FPGAIO_SWITCH);


    if (access == ARM_MPS3_IO_ACCESS_PIN) {
        value = ((value >> pin_num) & 1UL);
    }

    return value;
}

uint32_t arm_mps3_io_read_leds(struct arm_mps3_io_dev_t* dev,
                               enum arm_mps3_io_access_t access,
                               uint8_t pin_num)
{
    struct arm_mps3_io_reg_map_t* p_mps3_io_port =
                                  (struct arm_mps3_io_reg_map_t*)dev->cfg->base;
    uint32_t value = 0;

    if (pin_num >= MAX_PIN_FPGAIO_LED) {
        return 0;
    }

    /* Only read significant bits from this register */
    value = p_mps3_io_port->fpgaio_leds & MASK(MAX_PIN_FPGAIO_LED);

    if (access == ARM_MPS3_IO_ACCESS_PIN) {
        value = ((value >> pin_num) & 1UL);
    }

    return value;
}

uint32_t arm_mps3_io_read_clk1hz(struct arm_mps3_io_dev_t* dev)
{
    struct arm_mps3_io_reg_map_t* p_mps3_io_port =
                                  (struct arm_mps3_io_reg_map_t*)dev->cfg->base;
    return p_mps3_io_port->fpgaio_clk1hz;
}

void arm_mps3_io_write_clk1hz(struct arm_mps3_io_dev_t* dev,
                                    uint32_t value)
{
    struct arm_mps3_io_reg_map_t* p_mps3_io_port =
                                  (struct arm_mps3_io_reg_map_t*)dev->cfg->base;
     p_mps3_io_port->fpgaio_clk1hz = value;
}

uint32_t arm_mps3_io_read_clk100hz(struct arm_mps3_io_dev_t* dev)
{
    struct arm_mps3_io_reg_map_t* p_mps3_io_port =
                                  (struct arm_mps3_io_reg_map_t*)dev->cfg->base;
    return p_mps3_io_port->fpgaio_clk100hz;
}

void arm_mps3_io_write_clk100hz(struct arm_mps3_io_dev_t* dev,
                                    uint32_t value)
{
    struct arm_mps3_io_reg_map_t* p_mps3_io_port =
                                  (struct arm_mps3_io_reg_map_t*)dev->cfg->base;
    p_mps3_io_port->fpgaio_clk100hz = value;
}

uint32_t arm_mps3_io_read_counter(struct arm_mps3_io_dev_t* dev)
{
    struct arm_mps3_io_reg_map_t* p_mps3_io_port =
                                  (struct arm_mps3_io_reg_map_t*)dev->cfg->base;
    return p_mps3_io_port->fpgaio_counter;
}

void arm_mps3_io_write_counter(struct arm_mps3_io_dev_t* dev,
                                    uint32_t value)
{
    struct arm_mps3_io_reg_map_t* p_mps3_io_port =
                                  (struct arm_mps3_io_reg_map_t*)dev->cfg->base;
    p_mps3_io_port->fpgaio_counter = value;
}

uint32_t arm_mps3_io_read_pscntr(struct arm_mps3_io_dev_t* dev)
{
    struct arm_mps3_io_reg_map_t* p_mps3_io_port =
                                  (struct arm_mps3_io_reg_map_t*)dev->cfg->base;
    return p_mps3_io_port->fpgaio_pscntr;
}

uint32_t arm_mps3_io_read_prescale(struct arm_mps3_io_dev_t* dev)
{
    struct arm_mps3_io_reg_map_t* p_mps3_io_port =
                                  (struct arm_mps3_io_reg_map_t*)dev->cfg->base;
    return p_mps3_io_port->fpgaio_prescale;
}

void arm_mps3_io_write_prescale(struct arm_mps3_io_dev_t* dev,
                                    uint32_t value)
{
    struct arm_mps3_io_reg_map_t* p_mps3_io_port =
                                  (struct arm_mps3_io_reg_map_t*)dev->cfg->base;
    p_mps3_io_port->fpgaio_prescale = value;
}

uint32_t arm_mps3_io_read_misc(struct arm_mps3_io_dev_t* dev)
{
    struct arm_mps3_io_reg_map_t* p_mps3_io_port =
                                  (struct arm_mps3_io_reg_map_t*)dev->cfg->base;
    return p_mps3_io_port->fpgaio_misc;
}

void arm_mps3_io_write_misc(struct arm_mps3_io_dev_t* dev,
                          uint32_t value)
{
    struct arm_mps3_io_reg_map_t* p_mps3_io_port =
                                  (struct arm_mps3_io_reg_map_t*)dev->cfg->base;
    p_mps3_io_port->fpgaio_misc = value;
}

void arm_mps3_io_enable_shield0_spi_ncs(struct arm_mps3_io_dev_t* dev)
{
    struct arm_mps3_io_reg_map_t* p_mps3_io_port =
                                  (struct arm_mps3_io_reg_map_t*)dev->cfg->base;
    p_mps3_io_port->fpgaio_misc |= (1U << ARM_MPS3_IO_SHIELD0_SPI_NCS_OFF);
}

void arm_mps3_io_disable_shield0_spi_ncs(struct arm_mps3_io_dev_t* dev)
{
    struct arm_mps3_io_reg_map_t* p_mps3_io_port =
                                  (struct arm_mps3_io_reg_map_t*)dev->cfg->base;
    p_mps3_io_port->fpgaio_misc &= ~(1U << ARM_MPS3_IO_SHIELD0_SPI_NCS_OFF);
}

void arm_mps3_io_enable_shield1_spi_ncs(struct arm_mps3_io_dev_t* dev)
{
    struct arm_mps3_io_reg_map_t* p_mps3_io_port =
                                  (struct arm_mps3_io_reg_map_t*)dev->cfg->base;
    p_mps3_io_port->fpgaio_misc |= (1U << ARM_MPS3_IO_SHIELD1_SPI_NCS_OFF);
}

void arm_mps3_io_disable_shield1_spi_ncs(struct arm_mps3_io_dev_t* dev)
{
    struct arm_mps3_io_reg_map_t* p_mps3_io_port =
                                  (struct arm_mps3_io_reg_map_t*)dev->cfg->base;
    p_mps3_io_port->fpgaio_misc &= ~(1U << ARM_MPS3_IO_SHIELD1_SPI_NCS_OFF);
}

void arm_mps3_io_enable_adc_spi_ncs(struct arm_mps3_io_dev_t* dev)
{
    struct arm_mps3_io_reg_map_t* p_mps3_io_port =
                                  (struct arm_mps3_io_reg_map_t*)dev->cfg->base;
    p_mps3_io_port->fpgaio_misc |= (1U << ARM_MPS3_IO_ADC_SPI_NCS_OFF);
}

void arm_mps3_io_disable_adc_spi_ncs(struct arm_mps3_io_dev_t* dev)
{
    struct arm_mps3_io_reg_map_t* p_mps3_io_port =
                                  (struct arm_mps3_io_reg_map_t*)dev->cfg->base;
    p_mps3_io_port->fpgaio_misc &= ~(1U << ARM_MPS3_IO_ADC_SPI_NCS_OFF);
}
