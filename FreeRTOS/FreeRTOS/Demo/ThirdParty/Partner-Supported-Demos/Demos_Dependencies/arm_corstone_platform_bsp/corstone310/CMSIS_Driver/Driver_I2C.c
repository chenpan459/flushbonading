/*
 * Copyright (c) 2016-2022 ARM Limited
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

#include "Driver_I2C.h"

#include "SSE310MPS3.h"
#include "cmsis_driver_config.h"
#include "i2c_sbcon_drv.h"
#include "RTE_Components.h"

/* Driver version */
#define ARM_I2C_DRV_VERSION    ARM_DRIVER_VERSION_MAJOR_MINOR(2, 2)

#ifndef ARG_UNUSED
#define ARG_UNUSED(arg)        (void)arg
#endif

#if (defined (RTE_I2C0) && (RTE_I2C0 == 1)) || \
    (defined (RTE_I2C1) && (RTE_I2C1 == 1)) || \
    (defined (RTE_I2C2) && (RTE_I2C2 == 1)) || \
    (defined (RTE_I2C3) && (RTE_I2C3 == 1))

/* Driver Version */
static const ARM_DRIVER_VERSION DriverVersion = {
    ARM_I2C_API_VERSION,
    ARM_I2C_DRV_VERSION
};

/* Driver Capabilities */
static const ARM_I2C_CAPABILITIES DriverCapabilities = {
    1,  /* supports 10-bit addressing */
    0   /* reserved */
};

static ARM_DRIVER_VERSION ARM_I2C_GetVersion(void)
{
    return DriverVersion;
}

static ARM_I2C_CAPABILITIES ARM_I2C_GetCapabilities(void)
{
    return DriverCapabilities;
}

typedef struct {
    struct i2c_sbcon_dev_t* dev;        /* I2C device */
    uint32_t nbr_bytes;                 /* Number of bytes transfered */
    ARM_I2C_STATUS status;              /* I2C dev status */
    ARM_I2C_SignalEvent_t cb_event;     /* Callback function for events */
} I2Cx_Resources;

static int32_t ARM_I2Cx_Initialize(I2Cx_Resources* i2c_dev)
{
    enum i2c_sbcon_error_t ret;

    ret = i2c_sbcon_init(i2c_dev->dev, SystemCoreClock);

    if(ret != I2C_ERR_NONE) {
        return ARM_DRIVER_ERROR;
    }

    return ARM_DRIVER_OK;
}


static int32_t ARM_I2Cx_Uninitialize(I2Cx_Resources* i2c_dev)
{
    enum i2c_sbcon_error_t ret;

    ret = i2c_sbcon_reset(i2c_dev->dev);

    if(ret != I2C_ERR_NONE) {
        return ARM_DRIVER_ERROR;
    }

    return ARM_DRIVER_OK;
}

static int32_t ARM_I2Cx_PowerControl(I2Cx_Resources* i2c_dev,
                                     ARM_POWER_STATE state)
{
    ARG_UNUSED(i2c_dev);

    switch (state) {
        case ARM_POWER_OFF:
        case ARM_POWER_LOW:
            return ARM_DRIVER_ERROR_UNSUPPORTED;
        case ARM_POWER_FULL:
            /* Nothing to be done. It's already full power*/
            break;
        /* default:  The default is not defined intentionally to force the
         *           compiler to check that all the enumeration values are
         *           covered in the switch.*/
    }

    return ARM_DRIVER_OK;
}

static int32_t ARM_I2Cx_MasterTransmit(I2Cx_Resources* i2c_dev, uint32_t addr,
                                       const uint8_t *data, uint32_t num,
                                       bool xfer_pending)
{
    i2c_dev->status.direction = 0;
    i2c_dev->nbr_bytes = 0;
    (void)i2c_sbcon_master_transmit(i2c_dev->dev, (uint16_t) addr, data,
                                        num, xfer_pending, &i2c_dev->nbr_bytes);

    return ARM_DRIVER_OK;
}

static int32_t ARM_I2Cx_MasterReceive(I2Cx_Resources* i2c_dev, uint32_t addr,
                                      uint8_t *data, uint32_t num,
                                      bool xfer_pending)
{
    enum i2c_sbcon_error_t ret;

    /* Update driver status */
    i2c_dev->status.direction = 1;

    i2c_dev->nbr_bytes = 0;
    ret = i2c_sbcon_master_receive(i2c_dev->dev, (uint16_t) addr, data, num,
                                       xfer_pending, &i2c_dev->nbr_bytes);
    if(ret != I2C_ERR_NONE) {
        return ARM_DRIVER_ERROR;
    }

    return ARM_DRIVER_OK;
}

static int32_t ARM_I2Cx_SlaveTransmit(I2Cx_Resources* i2c_dev,
                                      const uint8_t *data, uint32_t num)
{
    ARG_UNUSED(i2c_dev);
    ARG_UNUSED(data);
    ARG_UNUSED(num);

    return ARM_DRIVER_ERROR_UNSUPPORTED;
}

static int32_t ARM_I2Cx_SlaveReceive(I2Cx_Resources* i2c_dev, uint8_t *data,
                             uint32_t num)
{
    ARG_UNUSED(i2c_dev);
    ARG_UNUSED(data);
    ARG_UNUSED(num);

    return ARM_DRIVER_ERROR_UNSUPPORTED;
}

static int32_t ARM_I2Cx_GetDataCount(const I2Cx_Resources* i2c_dev)
{
    return (int32_t)i2c_dev->nbr_bytes;
}

static int32_t ARM_I2Cx_Control(I2Cx_Resources* i2c_dev, uint32_t control,
                                uint32_t arg)
{
  enum i2c_sbcon_error_t ret;

    switch (control)
    {
        case ARM_I2C_BUS_SPEED:
            switch (arg)
            {
                case ARM_I2C_BUS_SPEED_STANDARD:
                    /* Standard Speed (100kHz) */
                    ret = i2c_sbcon_set_freq(i2c_dev->dev, 1000000);
                    break;
                case ARM_I2C_BUS_SPEED_FAST:
                    /* Fast Speed (400kHz) */
                    ret = i2c_sbcon_set_freq(i2c_dev->dev, 4000000);
                    break;
                case ARM_I2C_BUS_SPEED_FAST_PLUS:
                    /* Fast+ Speed (1MHz) */
                    ret = i2c_sbcon_set_freq(i2c_dev->dev, 100000000);
                    break;
                case ARM_I2C_BUS_SPEED_HIGH:
                    /* Fast+ Speed (3.4MHz) */
                    ret = i2c_sbcon_set_freq(i2c_dev->dev, 340000000);
                    break;
                default:
                    return ARM_DRIVER_ERROR_UNSUPPORTED;
            }
            break;

        case ARM_I2C_BUS_CLEAR:
            ret = i2c_sbcon_reset(i2c_dev->dev); /* Reset device */
            break;

        default:
            return ARM_DRIVER_ERROR_UNSUPPORTED;
    }

    if(ret != I2C_ERR_NONE) {
        return ARM_DRIVER_ERROR;
    }

    return ARM_DRIVER_OK;
}

static ARM_I2C_STATUS ARM_I2Cx_GetStatus(const I2Cx_Resources* i2c_dev)
{
    return i2c_dev->status;
}

#if (defined (RTE_I2C0) && (RTE_I2C0 == 1))
/* I2C0 Driver wrapper functions */
static I2Cx_Resources ARM_I2C0_DEV = {
#if (defined (__DOMAIN_NS) && (__DOMAIN_NS == 1))
    .dev = &I2C0_SBCON_DEV_NS,
#else
    .dev = &I2C0_SBCON_DEV_S,
#endif
    .nbr_bytes = 0,
    .status = {
        .busy             = 0,
        .mode             = 1, /* As Slave mode is not supported */
        .direction        = 0,
        .arbitration_lost = 0,
        .bus_error        = 0,
     },
    .cb_event = NULL,
};

static int32_t ARM_I2C0_Initialize (ARM_I2C_SignalEvent_t cb_event)
{
    ARM_I2C0_DEV.cb_event = cb_event;

    return ARM_I2Cx_Initialize(&ARM_I2C0_DEV);
}

static int32_t ARM_I2C0_Uninitialize(void)
{
    return ARM_I2Cx_Uninitialize(&ARM_I2C0_DEV);
}

static int32_t ARM_I2C0_PowerControl(ARM_POWER_STATE state)
{
    return ARM_I2Cx_PowerControl(&ARM_I2C0_DEV, state);
}

static int32_t ARM_I2C0_MasterTransmit(uint32_t addr, const uint8_t *data,
                                       uint32_t num, bool xfer_pending)
{
    return ARM_I2Cx_MasterTransmit(&ARM_I2C0_DEV, addr, data, num, xfer_pending);
}

static int32_t ARM_I2C0_MasterReceive(uint32_t addr, uint8_t *data,
                                      uint32_t num, bool xfer_pending)
{
    return (ARM_I2Cx_MasterReceive(&ARM_I2C0_DEV, addr, data, num, xfer_pending));
}

static int32_t ARM_I2C0_SlaveTransmit(const uint8_t *data, uint32_t num)
{
    return (ARM_I2Cx_SlaveTransmit(&ARM_I2C0_DEV, data, num));
}

static int32_t ARM_I2C0_SlaveReceive(uint8_t *data, uint32_t num)
{
    return (ARM_I2Cx_SlaveReceive(&ARM_I2C0_DEV, data, num));
}

static int32_t ARM_I2C0_GetDataCount(void)
{
    return (ARM_I2Cx_GetDataCount(&ARM_I2C0_DEV));
}

static int32_t ARM_I2C0_Control(uint32_t control, uint32_t arg)
{
    return (ARM_I2Cx_Control(&ARM_I2C0_DEV, control, arg));
}

static ARM_I2C_STATUS ARM_I2C0_GetStatus(void)
{
    return (ARM_I2Cx_GetStatus(&ARM_I2C0_DEV));
}

/* I2C0 Driver Control Block */
extern ARM_DRIVER_I2C Driver_I2C0;
ARM_DRIVER_I2C Driver_I2C0 = {
    ARM_I2C_GetVersion,
    ARM_I2C_GetCapabilities,
    ARM_I2C0_Initialize,
    ARM_I2C0_Uninitialize,
    ARM_I2C0_PowerControl,
    ARM_I2C0_MasterTransmit,
    ARM_I2C0_MasterReceive,
    ARM_I2C0_SlaveTransmit,
    ARM_I2C0_SlaveReceive,
    ARM_I2C0_GetDataCount,
    ARM_I2C0_Control,
    ARM_I2C0_GetStatus
};
#endif

#if (defined (RTE_I2C1) && (RTE_I2C1 == 1))
/* I2C1 Driver wrapper functions */
static I2Cx_Resources ARM_I2C1_DEV = {
#if (defined (__DOMAIN_NS) && (__DOMAIN_NS == 1))
    .dev = &I2C1_SBCON_DEV_NS,
#else
    .dev = &I2C1_SBCON_DEV_S,
#endif
    .nbr_bytes = 0,
    .status = {
        .busy             = 0,
        .mode             = 1, /* As Slave mode is not supported */
        .direction        = 0,
        .arbitration_lost = 0,
        .bus_error        = 0,
     },
    .cb_event = NULL,
};

static int32_t ARM_I2C1_Initialize (ARM_I2C_SignalEvent_t cb_event)
{
    ARM_I2C1_DEV.cb_event = cb_event;

    return ARM_I2Cx_Initialize(&ARM_I2C1_DEV);
}

static int32_t ARM_I2C1_Uninitialize(void)
{
    return (ARM_I2Cx_Uninitialize(&ARM_I2C1_DEV));
}

static int32_t ARM_I2C1_PowerControl(ARM_POWER_STATE state)
{
    return (ARM_I2Cx_PowerControl(&ARM_I2C1_DEV, state));
}

static int32_t ARM_I2C1_MasterTransmit(uint32_t addr, const uint8_t *data,
                                       uint32_t num, bool xfer_pending)
{
    return (ARM_I2Cx_MasterTransmit(&ARM_I2C1_DEV, addr, data,
                                    num, xfer_pending));
}

static int32_t ARM_I2C1_MasterReceive(uint32_t addr, uint8_t *data,
                                      uint32_t num, bool xfer_pending)
{
    return (ARM_I2Cx_MasterReceive(&ARM_I2C1_DEV, addr, data,
                                   num, xfer_pending));
}

static int32_t ARM_I2C1_SlaveTransmit(const uint8_t *data, uint32_t num)
{
    return (ARM_I2Cx_SlaveTransmit(&ARM_I2C1_DEV, data, num));
}

static int32_t ARM_I2C1_SlaveReceive(uint8_t *data, uint32_t num)
{
    return (ARM_I2Cx_SlaveReceive(&ARM_I2C1_DEV, data, num));
}

static int32_t ARM_I2C1_GetDataCount(void)
{
    return (ARM_I2Cx_GetDataCount(&ARM_I2C1_DEV));
}

static int32_t ARM_I2C1_Control(uint32_t control, uint32_t arg)
{
    return (ARM_I2Cx_Control(&ARM_I2C1_DEV, control, arg));
}

static ARM_I2C_STATUS ARM_I2C1_GetStatus(void)
{
    return (ARM_I2Cx_GetStatus(&ARM_I2C1_DEV));
}

/* I2C1 Driver Control Block */
extern ARM_DRIVER_I2C Driver_I2C1;
ARM_DRIVER_I2C Driver_I2C1 = {
    ARM_I2C_GetVersion,
    ARM_I2C_GetCapabilities,
    ARM_I2C1_Initialize,
    ARM_I2C1_Uninitialize,
    ARM_I2C1_PowerControl,
    ARM_I2C1_MasterTransmit,
    ARM_I2C1_MasterReceive,
    ARM_I2C1_SlaveTransmit,
    ARM_I2C1_SlaveReceive,
    ARM_I2C1_GetDataCount,
    ARM_I2C1_Control,
    ARM_I2C1_GetStatus
};
#endif

#if (defined (RTE_I2C2) && (RTE_I2C2 == 1))
/* I2C2 Driver wrapper functions */
static I2Cx_Resources ARM_I2C2_DEV = {
#if (defined (__DOMAIN_NS) && (__DOMAIN_NS == 1))
    .dev = &I2C2_SBCON_DEV_NS,
#else
    .dev = &I2C2_SBCON_DEV_S,
#endif
    .nbr_bytes = 0,
    .status = {
        .busy             = 0,
        .mode             = 1, /* As Slave mode is not supported */
        .direction        = 0,
        .arbitration_lost = 0,
        .bus_error        = 0,
     },
    .cb_event = NULL,
};

static int32_t ARM_I2C2_Initialize (ARM_I2C_SignalEvent_t cb_event)
{
    ARM_I2C2_DEV.cb_event = cb_event;

    return ARM_I2Cx_Initialize(&ARM_I2C2_DEV);
}

static int32_t ARM_I2C2_Uninitialize(void)
{
    return (ARM_I2Cx_Uninitialize(&ARM_I2C2_DEV));
}

static int32_t ARM_I2C2_PowerControl(ARM_POWER_STATE state)
{
    return (ARM_I2Cx_PowerControl(&ARM_I2C2_DEV, state));
}

static int32_t ARM_I2C2_MasterTransmit(uint32_t addr, const uint8_t *data,
                                       uint32_t num, bool xfer_pending)
{
    return (ARM_I2Cx_MasterTransmit(&ARM_I2C2_DEV, addr, data,
                                    num, xfer_pending));
}

static int32_t ARM_I2C2_MasterReceive(uint32_t addr, uint8_t *data,
                                      uint32_t num, bool xfer_pending)
{
    return (ARM_I2Cx_MasterReceive(&ARM_I2C2_DEV, addr, data,
                                   num, xfer_pending));
}

static int32_t ARM_I2C2_SlaveTransmit(const uint8_t *data, uint32_t num)
{
    return (ARM_I2Cx_SlaveTransmit(&ARM_I2C2_DEV, data, num));
}

static int32_t ARM_I2C2_SlaveReceive(uint8_t *data, uint32_t num)
{
    return (ARM_I2Cx_SlaveReceive(&ARM_I2C2_DEV, data, num));
}

static int32_t ARM_I2C2_GetDataCount(void)
{
    return (ARM_I2Cx_GetDataCount(&ARM_I2C2_DEV));
}

static int32_t ARM_I2C2_Control(uint32_t control, uint32_t arg)
{
    return (ARM_I2Cx_Control(&ARM_I2C2_DEV, control, arg));
}

static ARM_I2C_STATUS ARM_I2C2_GetStatus(void)
{
    return (ARM_I2Cx_GetStatus(&ARM_I2C2_DEV));
}

/* I2C2 Driver Control Block */
extern ARM_DRIVER_I2C Driver_I2C2;
ARM_DRIVER_I2C Driver_I2C2 = {
    ARM_I2C_GetVersion,
    ARM_I2C_GetCapabilities,
    ARM_I2C2_Initialize,
    ARM_I2C2_Uninitialize,
    ARM_I2C2_PowerControl,
    ARM_I2C2_MasterTransmit,
    ARM_I2C2_MasterReceive,
    ARM_I2C2_SlaveTransmit,
    ARM_I2C2_SlaveReceive,
    ARM_I2C2_GetDataCount,
    ARM_I2C2_Control,
    ARM_I2C2_GetStatus
};
#endif

#if (defined (RTE_I2C3) && (RTE_I2C3 == 1))
/* I2C3 Driver wrapper functions */
static I2Cx_Resources ARM_I2C3_DEV = {
#if (defined (__DOMAIN_NS) && (__DOMAIN_NS == 1))
    .dev = &I2C3_SBCON_DEV_NS,
#else
    .dev = &I2C3_SBCON_DEV_S,
#endif
    .nbr_bytes = 0,
    .status = {
        .busy             = 0,
        .mode             = 1, /* As Slave mode is not supported */
        .direction        = 0,
        .arbitration_lost = 0,
        .bus_error        = 0,
     },
    .cb_event = NULL,
};

static int32_t ARM_I2C3_Initialize (ARM_I2C_SignalEvent_t cb_event)
{
    ARM_I2C3_DEV.cb_event = cb_event;

    return ARM_I2Cx_Initialize(&ARM_I2C3_DEV);
}

static int32_t ARM_I2C3_Uninitialize(void)
{
    return (ARM_I2Cx_Uninitialize(&ARM_I2C3_DEV));
}

static int32_t ARM_I2C3_PowerControl(ARM_POWER_STATE state)
{
    return (ARM_I2Cx_PowerControl(&ARM_I2C3_DEV, state));
}

static int32_t ARM_I2C3_MasterTransmit(uint32_t addr, const uint8_t *data,
                                       uint32_t num, bool xfer_pending)
{
    return (ARM_I2Cx_MasterTransmit(&ARM_I2C3_DEV, addr, data,
                                    num, xfer_pending));
}

static int32_t ARM_I2C3_MasterReceive(uint32_t addr, uint8_t *data,
                                      uint32_t num, bool xfer_pending)
{
    return (ARM_I2Cx_MasterReceive(&ARM_I2C3_DEV, addr, data,
                                   num, xfer_pending));
}

static int32_t ARM_I2C3_SlaveTransmit(const uint8_t *data, uint32_t num)
{
    return (ARM_I2Cx_SlaveTransmit(&ARM_I2C3_DEV, data, num));
}

static int32_t ARM_I2C3_SlaveReceive(uint8_t *data, uint32_t num)
{
    return (ARM_I2Cx_SlaveReceive(&ARM_I2C3_DEV, data, num));
}

static int32_t ARM_I2C3_GetDataCount(void)
{
    return (ARM_I2Cx_GetDataCount(&ARM_I2C3_DEV));
}

static int32_t ARM_I2C3_Control(uint32_t control, uint32_t arg)
{
    return (ARM_I2Cx_Control(&ARM_I2C3_DEV, control, arg));
}

static ARM_I2C_STATUS ARM_I2C3_GetStatus(void)
{
    return (ARM_I2Cx_GetStatus(&ARM_I2C3_DEV));
}

/* I2C3 Driver Control Block */
extern ARM_DRIVER_I2C Driver_I2C3;
ARM_DRIVER_I2C Driver_I2C3 = {
    ARM_I2C_GetVersion,
    ARM_I2C_GetCapabilities,
    ARM_I2C3_Initialize,
    ARM_I2C3_Uninitialize,
    ARM_I2C3_PowerControl,
    ARM_I2C3_MasterTransmit,
    ARM_I2C3_MasterReceive,
    ARM_I2C3_SlaveTransmit,
    ARM_I2C3_SlaveReceive,
    ARM_I2C3_GetDataCount,
    ARM_I2C3_Control,
    ARM_I2C3_GetStatus
};
#endif
#endif
