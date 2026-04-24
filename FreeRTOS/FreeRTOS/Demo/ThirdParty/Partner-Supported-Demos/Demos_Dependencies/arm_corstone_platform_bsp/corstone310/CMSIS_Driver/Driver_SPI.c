/*
 * Copyright (c) 2016-2024 ARM Limited
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

#include "Driver_SPI.h"

#include "RTE_Components.h"
#include  CMSIS_device_header
#include "cmsis_driver_config.h"
#include "spi_pl022_drv.h"
#include "Driver_SPI_Resource.h"

#ifndef ARG_UNUSED
#define ARG_UNUSED(arg)  (void)arg
#endif

#ifndef CS_SW_CTRL
#define CS_SW_CTRL 1
#endif

/* Driver version */
#define ARM_SPI_DRV_VERSION  ARM_DRIVER_VERSION_MAJOR_MINOR(2, 2)

#if (defined (RTE_SPI0) && (RTE_SPI0 == 1)) || \
    (defined (RTE_SPI1) && (RTE_SPI1 == 1)) || \
    (defined (RTE_SPI2) && (RTE_SPI2 == 1)) || \
    (defined (RTE_SPI3) && (RTE_SPI3 == 1)) || \
    (defined (RTE_SPI4) && (RTE_SPI4 == 1))

/* Driver Version */
static const ARM_DRIVER_VERSION DriverVersion = {
    ARM_SPI_API_VERSION,
    ARM_SPI_DRV_VERSION
};

/* Driver Capabilities */
static const ARM_SPI_CAPABILITIES DriverCapabilities = {
    0, /* Simplex Mode (Master and Slave) */
    1, /* TI Synchronous Serial Interface */
    1, /* Microwire Interface */
    0, /* Signal Mode Fault event: \ref ARM_SPI_EVENT_MODE_FAULT */
    0  /* Reserved */
};

static ARM_DRIVER_VERSION ARM_SPI_GetVersion(void)
{
    return DriverVersion;
}


static ARM_SPI_CAPABILITIES ARM_SPI_GetCapabilities(void)
{
    return DriverCapabilities;
}

void setup_resources (SPIx_Resources* spi_dev, const void *tx_buff, void *rx_buff, uint32_t item_num, ARM_SPI_TXRX_STATUS status)
{
    uint16_t temp;

    spi_dev->tx_buff = tx_buff;
    spi_dev->rx_buff = rx_buff;
    spi_dev->item_num = item_num;
    spi_dev->txrx_status = status;
    spi_dev->num_tx_items_processed = 0;
    spi_dev->num_rx_items_processed = 0;

    /* RX FIFO flush */
    while (spi_pl022_get_status(spi_dev->dev) & SPI_PL022_SSPSR_RNE_MSK) {
        spi_pl022_read(spi_dev->dev, &temp);
    }
}

static void ARM_SPI_SignalEvent(SPIx_Resources* spi_dev, uint32_t event)
{
    if (spi_dev->cb_event == NULL) {
        return;
    }

    spi_dev->cb_event (event);
}

static int32_t ARM_SPIx_Initialize(SPIx_Resources* spi_dev)
{
    enum spi_pl022_error_t ret;

    if (spi_dev->is_initialized) {
        return ARM_DRIVER_OK;
    }

    if (spi_dev->init_host_port()) {
        return ARM_DRIVER_ERROR;
    }

    ret = spi_pl022_init(spi_dev->dev, PeripheralClock);

    if(ret != SPI_PL022_ERR_NONE) {
        return ARM_DRIVER_ERROR;
    }

    spi_dev->is_initialized = true;
    spi_dev->txrx_status = ARM_SPI_IDLE;

    return ARM_DRIVER_OK;
}

static int32_t ARM_SPIx_Uninitialize(SPIx_Resources* spi_dev)
{

    if (spi_dev->is_initialized == false) {
        return ARM_DRIVER_OK;
    }

    if (spi_dev->is_powered) {

        spi_pl022_disable_interrupt(spi_dev->dev, (SPI_PL022_RX_OR_INTR_MSK | SPI_PL022_RX_TO_INTR_MSK |
                                    SPI_PL022_RX_FIFO_INTR_MSK | SPI_PL022_TX_FIFO_INTR_MSK));

        NVIC_DisableIRQ(spi_dev->irq_line);

        while (spi_pl022_get_status(spi_dev->dev) & SPI_PL022_SSPSR_BSY_MSK);

        spi_pl022_dev_disable(spi_dev->dev);

        setup_resources(spi_dev, NULL, NULL, 0, ARM_SPI_IDLE);

        spi_dev->is_powered = false;
    }

    if (spi_dev->uninit_host_port()) {
        return ARM_DRIVER_ERROR;
    }

    spi_dev->is_initialized = false;

    return ARM_DRIVER_OK;
}

static int32_t ARM_SPIx_PowerControl(SPIx_Resources* spi_dev,
                                     ARM_POWER_STATE state)
{

    if (!spi_dev->is_initialized) {
        return ARM_DRIVER_ERROR;
    }

    switch (state) {
        case ARM_POWER_OFF:
            if (!spi_dev->is_powered) {
                return ARM_DRIVER_OK;
            }

            spi_pl022_disable_interrupt(spi_dev->dev, (SPI_PL022_RX_OR_INTR_MSK | SPI_PL022_RX_TO_INTR_MSK |
                                        SPI_PL022_RX_FIFO_INTR_MSK | SPI_PL022_TX_FIFO_INTR_MSK));

            NVIC_DisableIRQ(spi_dev->irq_line);

            while (spi_pl022_get_status(spi_dev->dev) & SPI_PL022_SSPSR_BSY_MSK);

            spi_pl022_dev_disable(spi_dev->dev);

            setup_resources(spi_dev, NULL, NULL, 0, ARM_SPI_IDLE);

            spi_dev->is_powered = false;
            break;
        case ARM_POWER_LOW:
            return ARM_DRIVER_ERROR_UNSUPPORTED;
        case ARM_POWER_FULL:

            if (spi_dev->is_powered) {
                return ARM_DRIVER_OK;
            }

            spi_pl022_dev_enable(spi_dev->dev);

            NVIC_ClearPendingIRQ(spi_dev->irq_line);

            NVIC_EnableIRQ(spi_dev->irq_line);

            spi_pl022_enable_interrupt(spi_dev->dev, (SPI_PL022_RX_OR_INTR_MSK));

            spi_dev->is_powered = true;
            break;
        /* default:  The default is not defined intentionally to force the
         *           compiler to check that all the enumeration values are
         *           covered in the switch.*/
    }

    return ARM_DRIVER_OK;
}

static int32_t ARM_SPIx_Send(SPIx_Resources* spi_dev, const void* data,
                             uint32_t num)
{
    if (!spi_dev->is_powered) {
        return ARM_DRIVER_ERROR;
    }

    if(data == NULL || num == 0) {
        return ARM_DRIVER_ERROR_PARAMETER;
    }

    /* Update SPI resources. */
    setup_resources(spi_dev, data, NULL, num, ARM_SPI_TX_ONGOING);

    /* Enable TX FIFO half empty or less. Rest of the transfer is handled in ISR */
    spi_pl022_enable_interrupt(spi_dev->dev, SPI_PL022_TX_FIFO_INTR_MSK);

    return ARM_DRIVER_OK;
}

static int32_t ARM_SPIx_Receive(SPIx_Resources* spi_dev, void* data,
                                uint32_t num)
{
    if (!spi_dev->is_powered) {
        return ARM_DRIVER_ERROR;
    }

    if(data == NULL || num == 0) {
        return ARM_DRIVER_ERROR_PARAMETER;
    }

    /* Update SPI resources. */
    setup_resources(spi_dev, NULL, data, num, ARM_SPI_RX_ONGOING);

    /* Enable TX FIFO half empty or less. Rest of the transfer is handled in ISR */
    spi_pl022_enable_interrupt(spi_dev->dev, SPI_PL022_TX_FIFO_INTR_MSK);

    return ARM_DRIVER_OK;
}

static int32_t ARM_SPIx_Transfer(SPIx_Resources* spi_dev, const void* data_out,
                                 void* data_in, uint32_t num)
{
    if (!spi_dev->is_powered) {
        return ARM_DRIVER_ERROR;
    }

    if(data_out == NULL || num == 0) {
        return ARM_DRIVER_ERROR_PARAMETER;
    }

    /* Update SPI resources. */
    setup_resources(spi_dev, data_out, data_in, num, ARM_SPI_TXRX_ONGOING);

    /* Enable TX FIFO half empty or less. Rest of the transfer is handled in ISR */
    spi_pl022_enable_interrupt(spi_dev->dev, SPI_PL022_TX_FIFO_INTR_MSK);

    return ARM_DRIVER_OK;
}

static uint32_t ARM_SPIx_GetDataCount(const SPIx_Resources* spi_dev)
{
    if (spi_dev->txrx_status == ARM_SPI_RX_ONGOING) {
        return spi_dev->num_rx_items_processed;
    } else {
        return spi_dev->num_tx_items_processed;
    }
}

static int32_t ARM_SPIx_Control(SPIx_Resources* spi_dev, uint32_t control,
                                uint32_t arg)
{
    uint32_t format_mode;
    uint32_t options;
    enum spi_pl022_error_t ret;
    uint32_t word_size;
    struct spi_pl022_ctrl_cfg_t spi_cfg;

    if (!spi_dev->is_powered) {
        return ARM_DRIVER_ERROR;
    }

    ret = spi_pl022_get_ctrl_cfg(spi_dev->dev, &spi_cfg);
    if(ret != SPI_PL022_ERR_NONE) {
        return ARM_DRIVER_ERROR;
    }

    options = control & ARM_SPI_CONTROL_Msk;

    switch(options)
    {
        case ARM_SPI_MODE_MASTER:
            spi_cfg.spi_mode = 0;
            spi_cfg.bit_rate = arg;
            break;
        case ARM_SPI_SET_BUS_SPEED:
            spi_cfg.bit_rate = arg;
            break;
        case ARM_SPI_GET_BUS_SPEED:
            return (int32_t)spi_cfg.bit_rate;
        case ARM_SPI_CONTROL_SS:
            if(spi_dev->is_cs_software_controlled) {
                if(arg == ARM_SPI_SS_ACTIVE) {
                    if (spi_dev->assert_host_ss()) {
                        return ARM_DRIVER_ERROR;
                    }
                }
                else if(arg == ARM_SPI_SS_INACTIVE) {
                    if (spi_dev->deassert_host_ss()) {
                        return ARM_DRIVER_ERROR;
                    }
                }
                else {
                    return ARM_DRIVER_ERROR_PARAMETER;
                }
            }
            else {
                  return ARM_DRIVER_ERROR;
            }
            break;
        case ARM_SPI_SET_DEFAULT_TX_VALUE:
            spi_dev->default_tx_value = arg;
            break;
        case ARM_SPI_ABORT_TRANSFER:
            spi_dev->item_num = spi_dev->num_tx_items_processed;
            spi_pl022_disable_interrupt(spi_dev->dev, SPI_PL022_TX_FIFO_INTR_MSK);
            spi_pl022_clear_interrupt(spi_dev->dev, SPI_PL022_TX_FIFO_INTR_MSK);
            while ((spi_pl022_get_status(spi_dev->dev) & SPI_PL022_SSPSR_BSY_MSK));
            spi_dev->txrx_status = ARM_SPI_ABORTED;
            break;
        case ARM_SPI_MODE_INACTIVE:
        case ARM_SPI_MODE_SLAVE:
        case ARM_SPI_MODE_MASTER_SIMPLEX:
        case ARM_SPI_MODE_SLAVE_SIMPLEX:
            return ARM_DRIVER_ERROR_UNSUPPORTED;
        default:
            break;
    }

    word_size = ((control & ARM_SPI_DATA_BITS_Msk) >> ARM_SPI_DATA_BITS_Pos);
    if(word_size > 0 ) {
        if( (word_size >= 4) && (word_size <= 16)) {
            spi_cfg.word_size = (uint8_t)word_size;
        } else {
            return ARM_SPI_ERROR_DATA_BITS;
        }
    }

    format_mode = (options & ARM_SPI_FRAME_FORMAT_Msk);
    if(format_mode > 0) {
        switch(format_mode) {
            case ARM_SPI_CPOL0_CPHA0:
                 spi_cfg.frame_format &= 0x3F;
                 break;
            case ARM_SPI_CPOL0_CPHA1:
                 spi_cfg.frame_format &= 0x3F;
                 spi_cfg.frame_format |= 0x7F;
                 break;
            case ARM_SPI_CPOL1_CPHA0:
                 spi_cfg.frame_format &= 0x3F;
                 spi_cfg.frame_format |= 0xBF;
                 __attribute__((fallthrough));
            case ARM_SPI_CPOL1_CPHA1:
                 spi_cfg.frame_format |= 0xFF;
                 __attribute__((fallthrough));
            case ARM_SPI_TI_SSI:
                 spi_cfg.frame_format &= 0xFC;
                 spi_cfg.frame_format |= SPI_PL022_CFG_FRF_TI;
                 break;
            case ARM_SPI_MICROWIRE:
                 spi_cfg.frame_format &= 0xFC;
                 spi_cfg.frame_format |= SPI_PL022_CFG_FRF_TI;
                 break;
            default:
                 return ARM_SPI_ERROR_FRAME_FORMAT;
        }
    }

    ret = spi_pl022_set_ctrl_cfg(spi_dev->dev, &spi_cfg);

    if(ret != SPI_PL022_ERR_NONE) {
        return ARM_DRIVER_ERROR;
    }

    return ARM_DRIVER_OK;
}

static ARM_SPI_STATUS ARM_SPIx_GetStatus(const SPIx_Resources* spi_dev)
{
    uint32_t status_raw;
    ARM_SPI_STATUS status = {.busy = 0, .data_lost = 0};

    status_raw = spi_pl022_get_status(spi_dev->dev);

    if (status_raw & SPI_PL022_SSPSR_BSY_MSK) {
        status.busy = 1;
    }

    return status;
}

void ARM_SPIx_Handler(SPIx_Resources* spi_dev)
{
    uint32_t irq_status = 0;
    uint32_t event = 0;
    uint16_t temp_item = 0;
    uint16_t tx_temp_count;

    /* Get IRQ status */
    irq_status = spi_pl022_get_masked_irq_status(spi_dev->dev);

    /* Clear TX interrupt, but not disable it */
    spi_pl022_clear_interrupt(spi_dev->dev, SPI_PL022_TX_FIFO_INTR_MSK);

    if(irq_status & SPI_PL022_TX_FIFO_INTR_MSK)
    {
        while(spi_pl022_get_status(spi_dev->dev) & SPI_PL022_SSPSR_RNE_MSK)
        {
            if(spi_dev->num_rx_items_processed <= spi_dev->item_num) {
                if(spi_dev->txrx_status == ARM_SPI_TX_ONGOING) {
                    spi_pl022_read(spi_dev->dev, &temp_item);
                } else {
                    /* TODO: add 16 bit support */
                    spi_pl022_read(spi_dev->dev, &(((uint8_t *)spi_dev->rx_buff)[spi_dev->num_rx_items_processed]));
                }
                spi_dev->num_rx_items_processed++;

            }
            if(spi_dev->num_rx_items_processed >= spi_dev->item_num)
            {
                // End of Transfer
                spi_pl022_disable_interrupt(spi_dev->dev, SPI_PL022_TX_FIFO_INTR_MSK);

                spi_dev->txrx_status = ARM_SPI_IDLE;

                event |= ARM_SPI_EVENT_TRANSFER_COMPLETE;
                ARM_SPI_SignalEvent(spi_dev, event);
                return;
            }
        }

        tx_temp_count = spi_dev->num_tx_items_processed + 4;

        while((spi_dev->num_tx_items_processed < spi_dev->item_num) &&
              (tx_temp_count > spi_dev->num_tx_items_processed)) {
            if (spi_pl022_get_status(spi_dev->dev) & SPI_PL022_SSPSR_TNF_MSK) {
                if(spi_dev->txrx_status == ARM_SPI_RX_ONGOING) {
                    spi_pl022_write_non_blocking(spi_dev->dev,
                        spi_dev->dev->data->ctrl_cfg.spi_mode, &(spi_dev->default_tx_value));
                } else {
                    /* TODO: add 16 bit support */
                    spi_pl022_write_non_blocking(spi_dev->dev,
                        spi_dev->dev->data->ctrl_cfg.spi_mode,
                        &(((uint8_t *)spi_dev->tx_buff)[spi_dev->num_tx_items_processed]));
                }
                spi_dev->num_tx_items_processed++;
            } else {
                break;
            }
        }


    }
}

#if (defined (RTE_SPI0) && (RTE_SPI0 == 1))
/* SPI0 Driver wrapper functions */
SPIx_Resources SPI0_DEV = {
#if (defined (__DOMAIN_NS) && (__DOMAIN_NS == 1))
    .dev = &SPI0_PL022_DEV_NS,
#else
    .dev = &SPI0_PL022_DEV_S,
#endif
    .num_tx_items_processed = 0,
    .num_rx_items_processed = 0,
    .default_tx_value = 0xFFFF,
    .status = {
        .busy       = 0,
        .data_lost  = 0,
        .mode_fault = 0,
      },
    .cb_event = NULL,
    .is_initialized = false,
#ifdef CS_SW_CTRL
    .is_cs_software_controlled = true,
#endif
    .irq_line = SPI_SHIELD0_IRQn,
};

static int32_t ARM_SPI0_Initialize(ARM_SPI_SignalEvent_t cb_event)
{
    SPI0_DEV.cb_event = cb_event;

    return (ARM_SPIx_Initialize(&SPI0_DEV));
}

static int32_t ARM_SPI0_Uninitialize(void)
{
    return (ARM_SPIx_Uninitialize(&SPI0_DEV));
}

static int32_t ARM_SPI0_PowerControl(ARM_POWER_STATE state)
{
    return (ARM_SPIx_PowerControl(&SPI0_DEV, state));
}

static int32_t ARM_SPI0_Send(const void* data, uint32_t num)
{
    return ARM_SPIx_Send(&SPI0_DEV, data, num);
}

static int32_t ARM_SPI0_Receive(void* data, uint32_t num)
{
    return ARM_SPIx_Receive(&SPI0_DEV, data, num);
}

static int32_t ARM_SPI0_Transfer(const void* data_out, void* data_in,
                                 uint32_t num)
{
    return ARM_SPIx_Transfer(&SPI0_DEV, data_out, data_in, num);
}
static uint32_t ARM_SPI0_GetDataCount(void)
{
    return (ARM_SPIx_GetDataCount(&SPI0_DEV));
}
static int32_t ARM_SPI0_Control(uint32_t control, uint32_t arg)
{
    return (ARM_SPIx_Control(&SPI0_DEV, control, arg));
}
static ARM_SPI_STATUS ARM_SPI0_GetStatus(void)
{
    return (ARM_SPIx_GetStatus(&SPI0_DEV));
}

void ARM_SPI0_Handler(void)
{
    ARM_SPIx_Handler(&SPI0_DEV);
}

/* SPI0 Driver Control Block */
extern ARM_DRIVER_SPI Driver_SPI0;
ARM_DRIVER_SPI Driver_SPI0 = {
    ARM_SPI_GetVersion,
    ARM_SPI_GetCapabilities,
    ARM_SPI0_Initialize,
    ARM_SPI0_Uninitialize,
    ARM_SPI0_PowerControl,
    ARM_SPI0_Send,
    ARM_SPI0_Receive,
    ARM_SPI0_Transfer,
    ARM_SPI0_GetDataCount,
    ARM_SPI0_Control,
    ARM_SPI0_GetStatus
};
#endif


#if (defined (RTE_SPI1) && (RTE_SPI1 == 1))
/* SPI1 Driver wrapper functions */
SPIx_Resources SPI1_DEV = {
#if (defined (__DOMAIN_NS) && (__DOMAIN_NS == 1))
    .dev = &SPI1_PL022_DEV_NS,
#else
    .dev = &SPI1_PL022_DEV_S,
#endif
    .num_tx_items_processed = 0,
    .num_rx_items_processed = 0,
    .default_tx_value = 0xFFFF,
    .status = {
        .busy       = 0,
        .data_lost  = 0,
        .mode_fault = 0,
      },
    .cb_event = NULL,
    .is_initialized = false,
#ifdef CS_SW_CTRL
    .is_cs_software_controlled = true,
#endif
    .irq_line = SPI_SHIELD1_IRQn,

};

static int32_t ARM_SPI1_Initialize(ARM_SPI_SignalEvent_t cb_event)
{
    SPI1_DEV.cb_event = cb_event;

    return (ARM_SPIx_Initialize(&SPI1_DEV));
}

static int32_t ARM_SPI1_Uninitialize(void)
{
    return (ARM_SPIx_Uninitialize(&SPI1_DEV));
}

static int32_t ARM_SPI1_PowerControl(ARM_POWER_STATE state)
{
    return (ARM_SPIx_PowerControl(&SPI1_DEV, state));
}

static int32_t ARM_SPI1_Send(const void* data, uint32_t num)
{
    return ARM_SPIx_Send(&SPI1_DEV, data, num);
}

static int32_t ARM_SPI1_Receive(void* data, uint32_t num)
{
    return ARM_SPIx_Receive(&SPI1_DEV, data, num);
}

static int32_t ARM_SPI1_Transfer(const void* data_out, void* data_in,
                                 uint32_t num)
{
    return ARM_SPIx_Transfer(&SPI1_DEV, data_out, data_in, num);
}
static uint32_t ARM_SPI1_GetDataCount(void)
{
    return (ARM_SPIx_GetDataCount(&SPI1_DEV));
}
static int32_t ARM_SPI1_Control(uint32_t control, uint32_t arg)
{
    return (ARM_SPIx_Control(&SPI1_DEV, control, arg));
}
static ARM_SPI_STATUS ARM_SPI1_GetStatus(void)
{
    return (ARM_SPIx_GetStatus(&SPI1_DEV));
}

void ARM_SPI1_Handler(void)
{
    ARM_SPIx_Handler(&SPI1_DEV);
}

/* SPI1 Driver Control Block */
extern ARM_DRIVER_SPI Driver_SPI1;
ARM_DRIVER_SPI Driver_SPI1 = {
    ARM_SPI_GetVersion,
    ARM_SPI_GetCapabilities,
    ARM_SPI1_Initialize,
    ARM_SPI1_Uninitialize,
    ARM_SPI1_PowerControl,
    ARM_SPI1_Send,
    ARM_SPI1_Receive,
    ARM_SPI1_Transfer,
    ARM_SPI1_GetDataCount,
    ARM_SPI1_Control,
    ARM_SPI1_GetStatus
};
#endif

#if (defined (RTE_SPI2) && (RTE_SPI2 == 1))
/* SPI2 Driver wrapper functions */
SPIx_Resources SPI2_DEV = {
#if (defined (__DOMAIN_NS) && (__DOMAIN_NS == 1))
    .dev = &SPI2_PL022_DEV_NS,
#else
    .dev = &SPI2_PL022_DEV_S,
#endif
    .num_tx_items_processed = 0,
    .num_rx_items_processed = 0,
    .default_tx_value = 0xFFFF,
    .status = {
        .busy       = 0,
        .data_lost  = 0,
        .mode_fault = 0,
      },
    .cb_event = NULL,
#ifdef CS_SW_CTRL
    .is_cs_software_controlled = true,
#endif
};

static int32_t ARM_SPI2_Initialize(ARM_SPI_SignalEvent_t cb_event)
{
    SPI2_DEV.cb_event = cb_event;

    return (ARM_SPIx_Initialize(&SPI2_DEV));
}

static int32_t ARM_SPI2_Uninitialize(void)
{
    return (ARM_SPIx_Uninitialize(&SPI2_DEV));
}

static int32_t ARM_SPI2_PowerControl(ARM_POWER_STATE state)
{
    return (ARM_SPIx_PowerControl(&SPI2_DEV, state));
}

static int32_t ARM_SPI2_Send(const void* data, uint32_t num)
{
    return ARM_SPIx_Send(&SPI2_DEV, data, num);
}

static int32_t ARM_SPI2_Receive(void* data, uint32_t num)
{
    return ARM_SPIx_Receive(&SPI2_DEV, data, num);
}

static int32_t ARM_SPI2_Transfer(const void* data_out, void* data_in,
                                 uint32_t num)
{
    return ARM_SPIx_Transfer(&SPI2_DEV, data_out, data_in, num);
}
static uint32_t ARM_SPI2_GetDataCount(void)
{
    return (ARM_SPIx_GetDataCount(&SPI2_DEV));
}
static int32_t ARM_SPI2_Control(uint32_t control, uint32_t arg)
{
    return (ARM_SPIx_Control(&SPI2_DEV, control, arg));
}
static ARM_SPI_STATUS ARM_SPI2_GetStatus(void)
{
    return (ARM_SPIx_GetStatus(&SPI2_DEV));
}

void ARM_SPI2_Handler(void)
{
    ARM_SPIx_Handler(&SPI2_DEV);
}

/* SPI2 Driver Control Block */
extern ARM_DRIVER_SPI Driver_SPI2;
ARM_DRIVER_SPI Driver_SPI2 = {
    ARM_SPI_GetVersion,
    ARM_SPI_GetCapabilities,
    ARM_SPI2_Initialize,
    ARM_SPI2_Uninitialize,
    ARM_SPI2_PowerControl,
    ARM_SPI2_Send,
    ARM_SPI2_Receive,
    ARM_SPI2_Transfer,
    ARM_SPI2_GetDataCount,
    ARM_SPI2_Control,
    ARM_SPI2_GetStatus
};
#endif

#if (defined (RTE_SPI3) && (RTE_SPI3 == 1))
/* SPI3 Driver wrapper functions */
SPIx_Resources SPI3_DEV = {
#if (defined (__DOMAIN_NS) && (__DOMAIN_NS == 1))
    .dev = &SPI3_PL022_DEV_NS,
#else
    .dev = &SPI3_PL022_DEV_S,
#endif
    .num_tx_items_processed = 0,
    .num_rx_items_processed = 0,
    .default_tx_value = 0xFFFF,
    .status = {
        .busy       = 0,
        .data_lost  = 0,
        .mode_fault = 0,
      },
    .cb_event = NULL,
#ifdef CS_SW_CTRL
    .is_cs_software_controlled = true,
#endif
};

static int32_t ARM_SPI3_Initialize(ARM_SPI_SignalEvent_t cb_event)
{
    SPI3_DEV.cb_event = cb_event;

    return (ARM_SPIx_Initialize(&SPI3_DEV));
}

static int32_t ARM_SPI3_Uninitialize(void)
{
    return (ARM_SPIx_Uninitialize(&SPI3_DEV));
}

static int32_t ARM_SPI3_PowerControl(ARM_POWER_STATE state)
{
    return (ARM_SPIx_PowerControl(&SPI3_DEV, state));
}

static int32_t ARM_SPI3_Send(const void* data, uint32_t num)
{
    return ARM_SPIx_Send(&SPI3_DEV, data, num);
}

static int32_t ARM_SPI3_Receive(void* data, uint32_t num)
{
    return ARM_SPIx_Receive(&SPI3_DEV, data, num);
}

static int32_t ARM_SPI3_Transfer(const void* data_out, void* data_in,
                                 uint32_t num)
{
    return ARM_SPIx_Transfer(&SPI3_DEV, data_out, data_in, num);
}
static uint32_t ARM_SPI3_GetDataCount(void)
{
    return (ARM_SPIx_GetDataCount(&SPI3_DEV));
}
static int32_t ARM_SPI3_Control(uint32_t control, uint32_t arg)
{
    return (ARM_SPIx_Control(&SPI3_DEV, control, arg));
}
static ARM_SPI_STATUS ARM_SPI3_GetStatus(void)
{
    return (ARM_SPIx_GetStatus(&SPI3_DEV));
}

void ARM_SPI3_Handler(void)
{
    ARM_SPIx_Handler(&SPI3_DEV);
}

/* SPI3 Driver Control Block */
extern ARM_DRIVER_SPI Driver_SPI3;
ARM_DRIVER_SPI Driver_SPI3 = {
    ARM_SPI_GetVersion,
    ARM_SPI_GetCapabilities,
    ARM_SPI3_Initialize,
    ARM_SPI3_Uninitialize,
    ARM_SPI3_PowerControl,
    ARM_SPI3_Send,
    ARM_SPI3_Receive,
    ARM_SPI3_Transfer,
    ARM_SPI3_GetDataCount,
    ARM_SPI3_Control,
    ARM_SPI3_GetStatus
};
#endif

#if (defined (RTE_SPI4) && (RTE_SPI4 == 1))
/* SPI4 Driver wrapper functions */
SPIx_Resources SPI4_DEV = {
#if (defined (__DOMAIN_NS) && (__DOMAIN_NS == 1))
    .dev = &SPI4_PL022_DEV_NS,
#else
    .dev = &SPI4_PL022_DEV_S,
#endif
    .num_tx_items_processed = 0,
    .num_rx_items_processed = 0,
    .default_tx_value = 0xFFFF,
    .status = {
        .busy       = 0,
        .data_lost  = 0,
        .mode_fault = 0,
      },
    .cb_event = NULL,
#ifdef CS_SW_CTRL
    .is_cs_software_controlled = true,
#endif
};

static int32_t ARM_SPI4_Initialize(ARM_SPI_SignalEvent_t cb_event)
{
    SPI4_DEV.cb_event = cb_event;

    return (ARM_SPIx_Initialize(&SPI4_DEV));
}

static int32_t ARM_SPI4_Uninitialize(void)
{
    return (ARM_SPIx_Uninitialize(&SPI4_DEV));
}

static int32_t ARM_SPI4_PowerControl(ARM_POWER_STATE state)
{
    return (ARM_SPIx_PowerControl(&SPI4_DEV, state));
}

static int32_t ARM_SPI4_Send(const void* data, uint32_t num)
{
    return ARM_SPIx_Send(&SPI4_DEV, data, num);
}

static int32_t ARM_SPI4_Receive(void* data, uint32_t num)
{
    return ARM_SPIx_Receive(&SPI4_DEV, data, num);
}

static int32_t ARM_SPI4_Transfer(const void* data_out, void* data_in,
                                 uint32_t num)
{
    return ARM_SPIx_Transfer(&SPI4_DEV, data_out, data_in, num);
}
static uint32_t ARM_SPI4_GetDataCount(void)
{
    return (ARM_SPIx_GetDataCount(&SPI4_DEV));
}
static int32_t ARM_SPI4_Control(uint32_t control, uint32_t arg)
{
    return (ARM_SPIx_Control(&SPI4_DEV, control, arg));
}
static ARM_SPI_STATUS ARM_SPI4_GetStatus(void)
{
    return (ARM_SPIx_GetStatus(&SPI4_DEV));
}

void ARM_SPI4_Handler(void)
{
    ARM_SPIx_Handler(&SPI4_DEV);
}

/* SPI4 Driver Control Block */
extern ARM_DRIVER_SPI Driver_SPI4;
ARM_DRIVER_SPI Driver_SPI4 = {
    ARM_SPI_GetVersion,
    ARM_SPI_GetCapabilities,
    ARM_SPI4_Initialize,
    ARM_SPI4_Uninitialize,
    ARM_SPI4_PowerControl,
    ARM_SPI4_Send,
    ARM_SPI4_Receive,
    ARM_SPI4_Transfer,
    ARM_SPI4_GetDataCount,
    ARM_SPI4_Control,
    ARM_SPI4_GetStatus
};
#endif
#endif
