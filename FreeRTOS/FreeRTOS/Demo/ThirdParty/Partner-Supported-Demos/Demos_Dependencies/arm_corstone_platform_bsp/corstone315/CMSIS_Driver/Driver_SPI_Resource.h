/*
 * Copyright (c) 2024 ARM Limited
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
#ifndef DRIVER_SPI_RESOURCE_H_
#define DRIVER_SPI_RESOURCE_H_

#include "Driver_SPI.h"

typedef uint32_t (*SPI_Init_Host_Port) (void);
typedef uint32_t (*SPI_Uninit_Host_Port) (void);
typedef uint32_t (*SPI_Assert_Host_CS) (void);
typedef uint32_t (*SPI_Deassert_Host_CS) (void);

typedef enum _ARM_SPI_TXRX_STATUS {
    ARM_SPI_IDLE = 0,
    ARM_SPI_TX_ONGOING,
    ARM_SPI_RX_ONGOING,
    ARM_SPI_TXRX_ONGOING,
    ARM_SPI_ABORTED,
} ARM_SPI_TXRX_STATUS;

typedef struct _SPIx_Resources {
    struct spi_pl022_dev_t* dev;           /* SPI device structure */
    ARM_SPI_STATUS status;                 /* SPI dev status */
    ARM_SPI_SignalEvent_t cb_event;        /* Callback function for events */
    bool is_cs_software_controlled;        /* SPI chip select is software controlled */
    bool is_initialized;                   /* Flag for initialization */
    bool is_powered;                       /* Flag for power state */
    uint32_t irq_line;                     /* Interrupt line number */
    uint32_t item_num;                     /* Number of items to transfer */
    const void *tx_buff;                   /* TX buffer pointer */
    void *rx_buff;                         /* RX buffer pointer */
    uint32_t num_tx_items_processed;       /* Number of items transfered */
    uint32_t num_rx_items_processed;       /* Number of items transfered */
    ARM_SPI_TXRX_STATUS txrx_status;       /* State of transactions */
    uint16_t default_tx_value;             /* Default TX value */
    SPI_Init_Host_Port init_host_port;     /* Callback function for host IRQ init */
    SPI_Uninit_Host_Port uninit_host_port; /* Callback function for host IRQ uninit */
    SPI_Assert_Host_CS assert_host_ss;     /* Callback function for enabling host IRQ */
    SPI_Deassert_Host_CS deassert_host_ss; /* Callback function for disabling host IRQ */
} SPIx_Resources;

#endif /* DRIVER_SPI_RESOURCE_H_ */
