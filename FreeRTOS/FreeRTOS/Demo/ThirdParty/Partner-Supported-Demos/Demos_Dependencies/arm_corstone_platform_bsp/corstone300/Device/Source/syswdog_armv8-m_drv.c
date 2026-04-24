/*
 * Copyright (c) 2019-2022 Arm Limited
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
 * \file syswdog_armv8-m_drv.c
 *
 * \brief Driver for Armv8-M System Watchdog
 *
 */

#include <stdint.h>
#include <stdbool.h>
#include "syswdog_armv8-m_drv.h"
#include "syswdog_armv8-m_reg_map.h"

/** Setter bit manipulation macro */
#define SET_BIT(WORD, BIT_INDEX) ((WORD) |= (1u << (BIT_INDEX)))
/** Clearing bit manipulation macro */
#define CLR_BIT(WORD, BIT_INDEX) ((WORD) &= ~(1u << (BIT_INDEX)))
/** Getter bit manipulation macro */
#define GET_BIT(WORD, BIT_INDEX) (bool)(((WORD) & (1u << (BIT_INDEX))))
/** Getter bit-field manipulation macro */
/** Bit mask for given width bit-field manipulation macro */
#define BITMASK(width) ((1u<<(width))-1)
#define GET_BIT_FIELD(WORD, WIDTH, BIT_OFFSET) \
    ((WORD & ((BITMASK(WIDTH)) << BIT_OFFSET)) >> BIT_OFFSET)


#define SYSWDOG_ARMV8_M_REGISTER_BIT_WIDTH          32u
    /*!< Armv8-M System Timer registers bit width */

/**
 * \brief Watchdog Control and Status register bit fields
 */
#define SYSWDOG_ARMV8M_CNTR_WCS_EN_OFF          0u
    /*!< Control and Status register Watchdog Enable bit field offset */
#define SYSWDOG_ARMV8M_CNTR_WCS_WS0_OFF         1u
    /*!< Control and Status register Watchdog Signal 0 bit field offset */
#define SYSWDOG_ARMV8M_CNTR_WCS_WS1_OFF         2u
    /*!< Control and Status register Watchdog Signal 1 bit field offset */

/**
 * \brief Watchdog Interface Identification register bit fields
 */
#define SYSWDOG_ARMV8M_W_IIDR_JEPCODE_OFF          0u
    /*!< Interface Identification register Arm JEP106 code bit field offset */
#define SYSWDOG_ARMV8M_W_IIDR_JEPCODE_SIZE         12u
    /*!< Interface Identification register Arm JEP106 code bit field size */
#define SYSWDOG_ARMV8M_W_IIDR_REV_OFF              12u
    /*!< Interface Identification register Revision number bit field offset */
#define SYSWDOG_ARMV8M_W_IIDR_REV_SIZE             4u
    /*!< Interface Identification register Revision number bit field size */
#define SYSWDOG_ARMV8M_W_IIDR_ARCH_OFF             16u
    /*!< Interface Identification register Architecture ver. bit field offset */
#define SYSWDOG_ARMV8M_W_IIDR_ARCH_SIZE            4u
    /*!< Interface Identification register Architecture ver. bit field size */
#define SYSWDOG_ARMV8M_W_IIDR_ID_OFF               24u
    /*!< Interface Identification register Product ID bit field offset */
#define SYSWDOG_ARMV8M_W_IIDR_ID_SIZE              8u
    /*!< Interface Identification register Product ID bit field size */


void syswdog_armv8_m_enable_wdog(struct syswdog_armv8_m_dev_t* dev)
{
    struct wdog_frame_reg_map_t* p_wdog =
                (struct wdog_frame_reg_map_t*)dev->cfg->base;

    SET_BIT(p_wdog->wcs, SYSWDOG_ARMV8M_CNTR_WCS_EN_OFF);
}

void syswdog_armv8_m_disable_wdog(struct syswdog_armv8_m_dev_t* dev)
{
    struct wdog_frame_reg_map_t* p_wdog =
                (struct wdog_frame_reg_map_t*)dev->cfg->base;

    CLR_BIT(p_wdog->wcs, SYSWDOG_ARMV8M_CNTR_WCS_EN_OFF);
}

bool syswdog_armv8_m_is_wdog_enabled(struct syswdog_armv8_m_dev_t* dev)
{
    struct wdog_frame_reg_map_t* p_wdog =
                (struct wdog_frame_reg_map_t*)dev->cfg->base;

    return GET_BIT(p_wdog->wcs, SYSWDOG_ARMV8M_CNTR_WCS_EN_OFF);
}

bool syswdog_armv8_m_read_irq_status_0(struct syswdog_armv8_m_dev_t* dev)
{
    struct wdog_frame_reg_map_t* p_wdog =
                (struct wdog_frame_reg_map_t*)dev->cfg->base;

    return GET_BIT(p_wdog->wcs, SYSWDOG_ARMV8M_CNTR_WCS_WS0_OFF);
}

bool syswdog_armv8_m_read_irq_status_1(struct syswdog_armv8_m_dev_t* dev)
{
    struct wdog_frame_reg_map_t* p_wdog =
                (struct wdog_frame_reg_map_t*)dev->cfg->base;

    return GET_BIT(p_wdog->wcs, SYSWDOG_ARMV8M_CNTR_WCS_WS1_OFF);
}

void syswdog_armv8_m_set_offset(struct syswdog_armv8_m_dev_t* dev,
                                const uint32_t value)
{
    struct wdog_frame_reg_map_t* p_wdog =
                (struct wdog_frame_reg_map_t*)dev->cfg->base;

    p_wdog->wor = value;
}

uint32_t syswdog_armv8_m_get_offset(struct syswdog_armv8_m_dev_t* dev)
{
    struct wdog_frame_reg_map_t* p_wdog =
                (struct wdog_frame_reg_map_t*)dev->cfg->base;

    return p_wdog->wor;
}

void syswdog_armv8_m_set_compare_value(struct syswdog_armv8_m_dev_t* dev,
                                       const uint64_t value)
{
    struct wdog_frame_reg_map_t* p_wdog =
                (struct wdog_frame_reg_map_t*)dev->cfg->base;

    p_wdog->wcv_high = (uint32_t)(value >> SYSWDOG_ARMV8_M_REGISTER_BIT_WIDTH);
    p_wdog->wcv_low = (uint32_t)(value &
                    (((uint64_t)1 << SYSWDOG_ARMV8_M_REGISTER_BIT_WIDTH) -1 ));
}

uint64_t syswdog_armv8_m_get_compare_value(struct syswdog_armv8_m_dev_t* dev)
{
    struct wdog_frame_reg_map_t* p_wdog =
                (struct wdog_frame_reg_map_t*)dev->cfg->base;

    uint64_t cmp_val = (uint64_t)p_wdog->wcv_high;
    cmp_val <<= SYSWDOG_ARMV8_M_REGISTER_BIT_WIDTH;
    cmp_val |= (uint64_t)p_wdog->wcv_low;

    return cmp_val;
}

void syswdog_armv8_m_refresh_wdog(struct syswdog_armv8_m_dev_t* dev)
{
    struct wdog_frame_reg_map_t* p_wdog =
                (struct wdog_frame_reg_map_t*)dev->cfg->base;

    /* Any write to refrsh reg will causes watchdog refresh. */
    p_wdog->wrr = 1;
}

uint32_t syswdog_armv8_m_get_cntr_product_id(struct syswdog_armv8_m_dev_t* dev)
{
    struct wdog_frame_reg_map_t* p_wdog =
                (struct wdog_frame_reg_map_t*)dev->cfg->base;

    return GET_BIT_FIELD(p_wdog->cnt_w_iidr,
                         SYSWDOG_ARMV8M_W_IIDR_ID_SIZE,
                         SYSWDOG_ARMV8M_W_IIDR_ID_OFF);
}

uint32_t syswdog_armv8_m_get_cntr_architecture_version(
                                struct syswdog_armv8_m_dev_t* dev)
{
    struct wdog_frame_reg_map_t* p_wdog =
                (struct wdog_frame_reg_map_t*)dev->cfg->base;

    return GET_BIT_FIELD(p_wdog->cnt_w_iidr,
                         SYSWDOG_ARMV8M_W_IIDR_ARCH_SIZE,
                         SYSWDOG_ARMV8M_W_IIDR_ARCH_OFF);
}

uint32_t syswdog_armv8_m_get_cntr_revision_number(
                                struct syswdog_armv8_m_dev_t* dev)
{
    struct wdog_frame_reg_map_t* p_wdog =
                (struct wdog_frame_reg_map_t*)dev->cfg->base;

    return GET_BIT_FIELD(p_wdog->cnt_w_iidr,
                         SYSWDOG_ARMV8M_W_IIDR_REV_SIZE,
                         SYSWDOG_ARMV8M_W_IIDR_REV_OFF);
}

uint32_t syswdog_armv8_m_get_cntr_arm_JEP106_code(
                                struct syswdog_armv8_m_dev_t* dev)
{
    struct wdog_frame_reg_map_t* p_wdog =
                (struct wdog_frame_reg_map_t*)dev->cfg->base;

    return GET_BIT_FIELD(p_wdog->cnt_w_iidr,
                         SYSWDOG_ARMV8M_W_IIDR_JEPCODE_SIZE,
                         SYSWDOG_ARMV8M_W_IIDR_JEPCODE_OFF);
}

uint32_t syswdog_armv8_m_get_refr_product_id(struct syswdog_armv8_m_dev_t* dev)
{
    struct wdog_frame_reg_map_t* p_wdog =
                (struct wdog_frame_reg_map_t*)dev->cfg->base;

    return GET_BIT_FIELD(p_wdog->ref_w_iidr,
                         SYSWDOG_ARMV8M_W_IIDR_ID_SIZE,
                         SYSWDOG_ARMV8M_W_IIDR_ID_OFF);
}

uint32_t syswdog_armv8_m_get_refr_architecture_version(
                                struct syswdog_armv8_m_dev_t* dev)
{
    struct wdog_frame_reg_map_t* p_wdog =
                (struct wdog_frame_reg_map_t*)dev->cfg->base;

    return GET_BIT_FIELD(p_wdog->ref_w_iidr,
                         SYSWDOG_ARMV8M_W_IIDR_ARCH_SIZE,
                         SYSWDOG_ARMV8M_W_IIDR_ARCH_OFF);
}

uint32_t syswdog_armv8_m_get_refr_revision_number(
                                struct syswdog_armv8_m_dev_t* dev)
{
    struct wdog_frame_reg_map_t* p_wdog =
                (struct wdog_frame_reg_map_t*)dev->cfg->base;

    return GET_BIT_FIELD(p_wdog->ref_w_iidr,
                         SYSWDOG_ARMV8M_W_IIDR_REV_SIZE,
                         SYSWDOG_ARMV8M_W_IIDR_REV_OFF);
}

uint32_t syswdog_armv8_m_get_refr_arm_JEP106_code(
                                struct syswdog_armv8_m_dev_t* dev)
{
    struct wdog_frame_reg_map_t* p_wdog =
                (struct wdog_frame_reg_map_t*)dev->cfg->base;

    return GET_BIT_FIELD(p_wdog->ref_w_iidr,
                         SYSWDOG_ARMV8M_W_IIDR_JEPCODE_SIZE,
                         SYSWDOG_ARMV8M_W_IIDR_JEPCODE_OFF);
}

void syswdog_armv8_m_init(struct syswdog_armv8_m_dev_t* dev, uint32_t offset)
{
    syswdog_armv8_m_set_offset(dev, offset);
    syswdog_armv8_m_enable_wdog(dev);
}
