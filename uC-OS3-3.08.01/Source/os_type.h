/*
*********************************************************************************************************
*                                              uC/OS-III
*                                        The Real-Time Kernel
*
*                    Copyright 2009-2021 Silicon Laboratories Inc. www.silabs.com
*
*                                 SPDX-License-Identifier: APACHE-2.0
*
*               This software is subject to an open source license and is distributed by
*                Silicon Laboratories Inc. pursuant to the terms of the Apache License,
*                    Version 2.0 available at www.apache.org/licenses/LICENSE-2.0.
*
*********************************************************************************************************
*/

/*
*********************************************************************************************************
* File    : os_type.h
* Version : V3.08.01
*********************************************************************************************************
*/

#ifndef   OS_TYPE_H
#define   OS_TYPE_H

#ifdef    VSC_INCLUDE_H_FILE_NAMES
const     CPU_CHAR  *os_type__h = "$Id: $";
#endif

/*
************************************************************************************************************************
*                                                 INCLUDE HEADER FILES
************************************************************************************************************************
*/

                                                       /*       Description                                    # Bits */
                                                       /*                                               <recommended> */
                                                       /* ----------------------------------------------------------- */
/* 本文件定义 uC/OS-III 的基础类型别名。
 * 设计目标：
 * 1) 把“语义类型”与“CPU 原生位宽”解耦，便于跨平台移植；
 * 2) 通过统一命名让各模块接口表达更清晰（如 OS_TICK、OS_PRIO）；
 * 3) 在资源受限平台上可按配置裁剪位宽，兼顾 RAM 与性能。 */

typedef   CPU_INT16U      OS_CPU_USAGE;                /* CPU Usage 0..10000                                  <16>/32 */
                                                       /* CPU 使用率，放大 100 倍（0..10000 表示 0.00%..100.00%）。 */

typedef   CPU_INT32U      OS_CTR;                      /* Counter,                                                 32 */
                                                       /* 通用计数器类型，常用于统计次数与累积事件。 */

typedef   CPU_INT32U      OS_CTX_SW_CTR;               /* Counter of context switches,                             32 */
                                                       /* 任务切换计数器，反映调度活跃度。 */

typedef   CPU_INT32U      OS_CYCLES;                   /* CPU clock cycles,                                   <32>/64 */
                                                       /* 周期计数值，常用于性能测量与执行时长统计。 */

typedef   CPU_INT32U      OS_FLAGS;                    /* Event flags,                                      8/16/<32> */
                                                       /* 事件标志位容器：每个 bit 表示一个独立事件条件。 */

typedef   CPU_INT32U      OS_IDLE_CTR;                 /* Holds the number of times the idle task runs,       <32>/64 */
                                                       /* 空闲任务循环计数，用于 CPU 占用率估算基线。 */

typedef   CPU_INT16U      OS_MEM_QTY;                  /* Number of memory blocks,                            <16>/32 */
typedef   CPU_INT16U      OS_MEM_SIZE;                 /* Size in bytes of a memory block,                    <16>/32 */
                                                       /* 固定块内存池：块数量与块大小，分别描述容量与粒度。 */

typedef   CPU_INT16U      OS_MSG_QTY;                  /* Number of OS_MSGs in the msg pool,                  <16>/32 */
typedef   CPU_INT16U      OS_MSG_SIZE;                 /* Size of messages in number of bytes,                <16>/32 */
                                                       /* 消息系统：描述符数量与单条消息字节长度。 */

typedef   CPU_INT08U      OS_NESTING_CTR;              /* Interrupt and scheduler nesting,                  <8>/16/32 */
                                                       /* 嵌套计数：用于 ISR 嵌套层级与调度锁层级管理。 */

typedef   CPU_INT16U      OS_OBJ_QTY;                  /* Number of kernel objects counter,                   <16>/32 */
typedef   CPU_INT32U      OS_OBJ_TYPE;                 /* Special flag to determine object type,                   32 */
                                                       /* 内核对象数量统计与对象类型标识（运行期类型校验）。 */

typedef   CPU_INT16U      OS_OPT;                      /* Holds function options,                             <16>/32 */
                                                       /* API 选项位域（OS_OPT_*），通过按位组合控制行为。 */

typedef   CPU_INT08U      OS_PRIO;                     /* Priority of a task,                               <8>/16/32 */
                                                       /* 任务优先级类型：数值越小，优先级越高。 */

typedef   CPU_INT16U      OS_QTY;                      /* Quantity                                            <16>/32 */
                                                       /* 通用“数量”语义类型，用于对象个数等字段。 */

typedef   CPU_INT32U      OS_RATE_HZ;                  /* Rate in Hertz                                            32 */
                                                       /* 频率（Hz）语义类型，如 Tick 频率、定时任务频率。 */

#if (CPU_CFG_ADDR_SIZE == CPU_WORD_SIZE_64)            /* Task register                                  8/16/<32/64> */
typedef   CPU_INT64U      OS_REG;
#else
typedef   CPU_INT32U      OS_REG;
#endif
typedef   CPU_INT08U      OS_REG_ID;                   /* Index to task register                            <8>/16/32 */
                                                       /* 任务寄存器值/索引：为每任务扩展少量应用私有槽位。 */

typedef   CPU_INT32U      OS_SEM_CTR;                  /* Semaphore value                                     16/<32> */
                                                       /* 信号量计数器类型，表示可用资源/信号数量。 */

typedef   CPU_INT08U      OS_STATE;                    /* State variable                                    <8>/16/32 */
                                                       /* 状态机字段类型：任务状态、对象状态等统一表示。 */

typedef   CPU_INT08U      OS_STATUS;                   /* Status                                            <8>/16/32 */
                                                       /* 操作状态/挂起状态字段类型。 */

typedef   CPU_INT32U      OS_TICK;                     /* Clock tick counter                                  <32>/64 */
                                                       /* 系统时基计数类型：用于延时、超时、周期调度。 */

#endif
