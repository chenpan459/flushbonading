/*
 * FreeRTOS Kernel <DEVELOPMENT BRANCH>
 * Copyright (C) 2021 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

/*
 * FreeRTOS 软件定时器实现（timers.c）：
 * 由定时器守护任务（daemon）通过命令队列 xTimerQueue 串行处理启动/停止/复位/改周期等；
 * 活动定时器按到期时间排序存放在双缓冲链表（应对 tick 溢出），到期在守护任务上下文中调用回调。
 * 公开 API 与宏多在 timers.h；本文件为内核实现。
 */

/* Standard includes. */
#include <stdlib.h>

/* 本文件为内核实现：定义后再包含 task.h，避免 API 被展开为 MPU 用户态包装。 */
#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"

#if ( INCLUDE_xTimerPendFunctionCall == 1 ) && ( configUSE_TIMERS == 0 )
    #error configUSE_TIMERS must be set to 1 to make the xTimerPendFunctionCall() function available.
#endif

/* 头文件包含结束后取消宏，保证本文件符号以特权实现链接（MPU 端口）。 */
#undef MPU_WRAPPERS_INCLUDED_FROM_API_FILE


/* 若 FreeRTOSConfig.h 中 configUSE_TIMERS 为 0，则本文件整段由预处理器跳过（至文件末尾 #endif）。 */
#if ( configUSE_TIMERS == 1 )

/* 杂项常量。 */
    #define tmrNO_DELAY                    ( ( TickType_t ) 0U )
    #define tmrMAX_TIME_BEFORE_OVERFLOW    ( ( TickType_t ) -1 )

    #ifndef configTIMER_SERVICE_TASK_NAME
        #define configTIMER_SERVICE_TASK_NAME    "Tmr Svc"
    #endif

    #if ( ( configNUMBER_OF_CORES > 1 ) && ( configUSE_CORE_AFFINITY == 1 ) )

        #ifndef configTIMER_SERVICE_TASK_CORE_AFFINITY
            #define configTIMER_SERVICE_TASK_CORE_AFFINITY    tskNO_AFFINITY
        #endif
    #endif /* #if ( ( configNUMBER_OF_CORES > 1 ) && ( configUSE_CORE_AFFINITY == 1 ) ) */

/* Timer_t.ucStatus 状态位。 */
    #define tmrSTATUS_IS_ACTIVE                  ( 0x01U )
    #define tmrSTATUS_IS_STATICALLY_ALLOCATED    ( 0x02U )
    #define tmrSTATUS_IS_AUTORELOAD              ( 0x04U )

/* 软件定时器本体；结构体名保留 tmrTimerControl 以兼容内核感知调试器。 */
    typedef struct tmrTimerControl                                               /* The old naming convention is used to prevent breaking kernel aware debuggers. */
    {
        const char * pcTimerName;                                                /**< 调试用名称。 */
        ListItem_t xTimerListItem;                                               /**< 挂入活动定时器链表。 */
        TickType_t xTimerPeriodInTicks;                                          /**< 周期（tick）。 */
        void * pvTimerID;                                                        /**< 用户自定义 ID（同回调多定时器时区分）。 */
        portTIMER_CALLBACK_ATTRIBUTE TimerCallbackFunction_t pxCallbackFunction; /**< 到期回调。 */
        #if ( configUSE_TRACE_FACILITY == 1 )
            UBaseType_t uxTimerNumber;                                           /**< 跟踪工具用编号。 */
        #endif
        uint8_t ucStatus;                                                        /**< 活动/静态分配/自动重载等位标志。 */
    } xTIMER;

/* 对外类型名 Timer_t。 */
    typedef xTIMER Timer_t;

/*
 * 守护任务消息：xMessageID 区分「定时器命令」与「延迟执行普通回调」（后者需 INCLUDE_xTimerPendFunctionCall）。
 */
    typedef struct tmrTimerParameters
    {
        TickType_t xMessageValue; /**< 命令附加参数（如改周期的新周期值）。 */
        Timer_t * pxTimer;        /**< 目标定时器。 */
    } TimerParameter_t;


    typedef struct tmrCallbackParameters
    {
        portTIMER_CALLBACK_ATTRIBUTE
        PendedFunction_t pxCallbackFunction; /* 待执行的延迟函数。 */
        void * pvParameter1;                 /* 回调第一参数。 */
        uint32_t ulParameter2;               /* 回调第二参数。 */
    } CallbackParameters_t;

    typedef struct tmrTimerQueueMessage
    {
        BaseType_t xMessageID; /**< 发往守护任务的命令 ID（或负值表示延迟回调）。 */
        union
        {
            TimerParameter_t xTimerParameters;

            #if ( INCLUDE_xTimerPendFunctionCall == 1 )
                CallbackParameters_t xCallbackParameters;
            #endif /* INCLUDE_xTimerPendFunctionCall */
        } u;
    } DaemonTaskMessage_t;

/* 活动定时器双表 + 当前/溢出侧指针；仅守护任务可安全访问。置于文件作用域以兼容调试器。 */
    PRIVILEGED_DATA static List_t xActiveTimerList1;
    PRIVILEGED_DATA static List_t xActiveTimerList2;
    PRIVILEGED_DATA static List_t * pxCurrentTimerList;
    PRIVILEGED_DATA static List_t * pxOverflowTimerList;

/* 各任务向定时器守护任务投递命令/回调请求的队列。 */
    PRIVILEGED_DATA static QueueHandle_t xTimerQueue = NULL;
    PRIVILEGED_DATA static TaskHandle_t xTimerTaskHandle = NULL;

/*-----------------------------------------------------------*/

    /* 首次使用前初始化活动链表与 xTimerQueue。 */
    static void prvCheckForValidListAndQueue( void ) PRIVILEGED_FUNCTION;

    /* 定时器守护任务：主循环内处理到期与命令队列。 */
    static portTASK_FUNCTION_PROTO( prvTimerTask, pvParameters ) PRIVILEGED_FUNCTION;

    /* 从 xTimerQueue 取出并执行一条或多条命令。 */
    static void prvProcessReceivedCommands( void ) PRIVILEGED_FUNCTION;

    /* 将定时器按 xNextExpiryTime 插入当前侧或溢出侧活动链表；若已过期则返回 pdTRUE。 */
    static BaseType_t prvInsertTimerInActiveList( Timer_t * const pxTimer,
                                                  const TickType_t xNextExpiryTime,
                                                  const TickType_t xTimeNow,
                                                  const TickType_t xCommandTime ) PRIVILEGED_FUNCTION;

    /* 自动重载定时器：补齐积压周期并多次回调，直至下次到期晚于 xTimeNow。 */
    static void prvReloadTimer( Timer_t * const pxTimer,
                                TickType_t xExpiredTime,
                                const TickType_t xTimeNow ) PRIVILEGED_FUNCTION;

    /* 链表头定时器到期：自动重载则重新入表，最后调用回调。 */
    static void prvProcessExpiredTimer( const TickType_t xNextExpireTime,
                                        const TickType_t xTimeNow ) PRIVILEGED_FUNCTION;

    /* tick 溢出：先处理当前表上仍挂着的到期项，再交换当前/溢出链表指针。 */
    static void prvSwitchTimerLists( void ) PRIVILEGED_FUNCTION;

    /* 读取当前 tick；若相对上次采样发生回绕则切换定时器表并置 *pxTimerListsWereSwitched。 */
    static TickType_t prvSampleTimeNow( BaseType_t * const pxTimerListsWereSwitched ) PRIVILEGED_FUNCTION;

    /* 返回当前活动表上最近到期时间；空表则返回 0 并使 *pxListWasEmpty 为 pdTRUE。 */
    static TickType_t prvGetNextExpireTime( BaseType_t * const pxListWasEmpty ) PRIVILEGED_FUNCTION;

    /* 已到期则处理回调；否则按下一到期或队列消息阻塞守护任务。 */
    static void prvProcessTimerOrBlockTask( const TickType_t xNextExpireTime,
                                            BaseType_t xListWasEmpty ) PRIVILEGED_FUNCTION;

    /* Timer_t 内存就绪后填充字段（不启动定时器）。 */
    static void prvInitialiseNewTimer( const char * const pcTimerName,
                                       const TickType_t xTimerPeriodInTicks,
                                       const BaseType_t xAutoReload,
                                       void * const pvTimerID,
                                       TimerCallbackFunction_t pxCallbackFunction,
                                       Timer_t * pxNewTimer ) PRIVILEGED_FUNCTION;
/*-----------------------------------------------------------*/

    /*
     * 由 vTaskStartScheduler 在 configUSE_TIMERS==1 时调用：确保队列/链表已建，再创建定时器守护任务。
     */
    BaseType_t xTimerCreateTimerTask( void )
    {
        BaseType_t xReturn = pdFAIL;

        traceENTER_xTimerCreateTimerTask();

        /* This function is called when the scheduler is started if
         * configUSE_TIMERS is set to 1.  Check that the infrastructure used by the
         * timer service task has been created/initialised.  If timers have already
         * been created then the initialisation will already have been performed. */
        prvCheckForValidListAndQueue();

        if( xTimerQueue != NULL )
        {
            #if ( ( configNUMBER_OF_CORES > 1 ) && ( configUSE_CORE_AFFINITY == 1 ) )
            {
                #if ( configSUPPORT_STATIC_ALLOCATION == 1 )
                {
                    StaticTask_t * pxTimerTaskTCBBuffer = NULL;
                    StackType_t * pxTimerTaskStackBuffer = NULL;
                    configSTACK_DEPTH_TYPE uxTimerTaskStackSize;

                    vApplicationGetTimerTaskMemory( &pxTimerTaskTCBBuffer, &pxTimerTaskStackBuffer, &uxTimerTaskStackSize );
                    xTimerTaskHandle = xTaskCreateStaticAffinitySet( &prvTimerTask,
                                                                     configTIMER_SERVICE_TASK_NAME,
                                                                     uxTimerTaskStackSize,
                                                                     NULL,
                                                                     ( ( UBaseType_t ) configTIMER_TASK_PRIORITY ) | portPRIVILEGE_BIT,
                                                                     pxTimerTaskStackBuffer,
                                                                     pxTimerTaskTCBBuffer,
                                                                     configTIMER_SERVICE_TASK_CORE_AFFINITY );

                    if( xTimerTaskHandle != NULL )
                    {
                        xReturn = pdPASS;
                    }
                }
                #else /* if ( configSUPPORT_STATIC_ALLOCATION == 1 ) */
                {
                    xReturn = xTaskCreateAffinitySet( &prvTimerTask,
                                                      configTIMER_SERVICE_TASK_NAME,
                                                      configTIMER_TASK_STACK_DEPTH,
                                                      NULL,
                                                      ( ( UBaseType_t ) configTIMER_TASK_PRIORITY ) | portPRIVILEGE_BIT,
                                                      configTIMER_SERVICE_TASK_CORE_AFFINITY,
                                                      &xTimerTaskHandle );
                }
                #endif /* configSUPPORT_STATIC_ALLOCATION */
            }
            #else /* #if ( ( configNUMBER_OF_CORES > 1 ) && ( configUSE_CORE_AFFINITY == 1 ) ) */
            {
                #if ( configSUPPORT_STATIC_ALLOCATION == 1 )
                {
                    StaticTask_t * pxTimerTaskTCBBuffer = NULL;
                    StackType_t * pxTimerTaskStackBuffer = NULL;
                    configSTACK_DEPTH_TYPE uxTimerTaskStackSize;

                    vApplicationGetTimerTaskMemory( &pxTimerTaskTCBBuffer, &pxTimerTaskStackBuffer, &uxTimerTaskStackSize );
                    xTimerTaskHandle = xTaskCreateStatic( &prvTimerTask,
                                                          configTIMER_SERVICE_TASK_NAME,
                                                          uxTimerTaskStackSize,
                                                          NULL,
                                                          ( ( UBaseType_t ) configTIMER_TASK_PRIORITY ) | portPRIVILEGE_BIT,
                                                          pxTimerTaskStackBuffer,
                                                          pxTimerTaskTCBBuffer );

                    if( xTimerTaskHandle != NULL )
                    {
                        xReturn = pdPASS;
                    }
                }
                #else /* if ( configSUPPORT_STATIC_ALLOCATION == 1 ) */
                {
                    xReturn = xTaskCreate( &prvTimerTask,
                                           configTIMER_SERVICE_TASK_NAME,
                                           configTIMER_TASK_STACK_DEPTH,
                                           NULL,
                                           ( ( UBaseType_t ) configTIMER_TASK_PRIORITY ) | portPRIVILEGE_BIT,
                                           &xTimerTaskHandle );
                }
                #endif /* configSUPPORT_STATIC_ALLOCATION */
            }
            #endif /* #if ( ( configNUMBER_OF_CORES > 1 ) && ( configUSE_CORE_AFFINITY == 1 ) ) */
        }
        else
        {
            mtCOVERAGE_TEST_MARKER();
        }

        configASSERT( xReturn );

        traceRETURN_xTimerCreateTimerTask( xReturn );

        return xReturn;
    }
/*-----------------------------------------------------------*/

    #if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )

        /* 动态分配 Timer_t 并初始化；返回句柄或 NULL。 */
        TimerHandle_t xTimerCreate( const char * const pcTimerName,
                                    const TickType_t xTimerPeriodInTicks,
                                    const BaseType_t xAutoReload,
                                    void * const pvTimerID,
                                    TimerCallbackFunction_t pxCallbackFunction )
        {
            Timer_t * pxNewTimer;

            traceENTER_xTimerCreate( pcTimerName, xTimerPeriodInTicks, xAutoReload, pvTimerID, pxCallbackFunction );

            /* MISRA Ref 11.5.1 [Malloc memory assignment] */
            /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-115 */
            /* coverity[misra_c_2012_rule_11_5_violation] */
            pxNewTimer = ( Timer_t * ) pvPortMalloc( sizeof( Timer_t ) );

            if( pxNewTimer != NULL )
            {
                /* Status is thus far zero as the timer is not created statically
                 * and has not been started.  The auto-reload bit may get set in
                 * prvInitialiseNewTimer. */
                pxNewTimer->ucStatus = 0x00;
                prvInitialiseNewTimer( pcTimerName, xTimerPeriodInTicks, xAutoReload, pvTimerID, pxCallbackFunction, pxNewTimer );
            }

            traceRETURN_xTimerCreate( pxNewTimer );

            return pxNewTimer;
        }

    #endif /* configSUPPORT_DYNAMIC_ALLOCATION */
/*-----------------------------------------------------------*/

    #if ( configSUPPORT_STATIC_ALLOCATION == 1 )

        /* 使用调用方提供的 StaticTimer_t 内存创建定时器。 */
        TimerHandle_t xTimerCreateStatic( const char * const pcTimerName,
                                          const TickType_t xTimerPeriodInTicks,
                                          const BaseType_t xAutoReload,
                                          void * const pvTimerID,
                                          TimerCallbackFunction_t pxCallbackFunction,
                                          StaticTimer_t * pxTimerBuffer )
        {
            Timer_t * pxNewTimer;

            traceENTER_xTimerCreateStatic( pcTimerName, xTimerPeriodInTicks, xAutoReload, pvTimerID, pxCallbackFunction, pxTimerBuffer );

            #if ( configASSERT_DEFINED == 1 )
            {
                /* Sanity check that the size of the structure used to declare a
                 * variable of type StaticTimer_t equals the size of the real timer
                 * structure. */
                volatile size_t xSize = sizeof( StaticTimer_t );
                configASSERT( xSize == sizeof( Timer_t ) );
                ( void ) xSize; /* Prevent unused variable warning when configASSERT() is not defined. */
            }
            #endif /* configASSERT_DEFINED */

            /* A pointer to a StaticTimer_t structure MUST be provided, use it. */
            configASSERT( pxTimerBuffer );
            /* MISRA Ref 11.3.1 [Misaligned access] */
            /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-113 */
            /* coverity[misra_c_2012_rule_11_3_violation] */
            pxNewTimer = ( Timer_t * ) pxTimerBuffer;

            if( pxNewTimer != NULL )
            {
                /* Timers can be created statically or dynamically so note this
                 * timer was created statically in case it is later deleted.  The
                 * auto-reload bit may get set in prvInitialiseNewTimer(). */
                pxNewTimer->ucStatus = ( uint8_t ) tmrSTATUS_IS_STATICALLY_ALLOCATED;

                prvInitialiseNewTimer( pcTimerName, xTimerPeriodInTicks, xAutoReload, pvTimerID, pxCallbackFunction, pxNewTimer );
            }

            traceRETURN_xTimerCreateStatic( pxNewTimer );

            return pxNewTimer;
        }

    #endif /* configSUPPORT_STATIC_ALLOCATION */
/*-----------------------------------------------------------*/

    /* 填充成员、初始化链表项、设置自动重载位；周期须大于 0。 */
    static void prvInitialiseNewTimer( const char * const pcTimerName,
                                       const TickType_t xTimerPeriodInTicks,
                                       const BaseType_t xAutoReload,
                                       void * const pvTimerID,
                                       TimerCallbackFunction_t pxCallbackFunction,
                                       Timer_t * pxNewTimer )
    {
        /* 0 is not a valid value for xTimerPeriodInTicks. */
        configASSERT( ( xTimerPeriodInTicks > 0 ) );

        /* Ensure the infrastructure used by the timer service task has been
         * created/initialised. */
        prvCheckForValidListAndQueue();

        /* Initialise the timer structure members using the function
         * parameters. */
        pxNewTimer->pcTimerName = pcTimerName;
        pxNewTimer->xTimerPeriodInTicks = xTimerPeriodInTicks;
        pxNewTimer->pvTimerID = pvTimerID;
        pxNewTimer->pxCallbackFunction = pxCallbackFunction;
        vListInitialiseItem( &( pxNewTimer->xTimerListItem ) );

        if( xAutoReload != pdFALSE )
        {
            pxNewTimer->ucStatus |= ( uint8_t ) tmrSTATUS_IS_AUTORELOAD;
        }

        traceTIMER_CREATE( pxNewTimer );
    }
/*-----------------------------------------------------------*/

    /* 任务上下文向守护队列发送定时器命令（命令 ID 须小于 tmrFIRST_FROM_ISR_COMMAND）。 */
    BaseType_t xTimerGenericCommandFromTask( TimerHandle_t xTimer,
                                             const BaseType_t xCommandID,
                                             const TickType_t xOptionalValue,
                                             BaseType_t * const pxHigherPriorityTaskWoken,
                                             const TickType_t xTicksToWait )
    {
        BaseType_t xReturn = pdFAIL;
        DaemonTaskMessage_t xMessage;

        ( void ) pxHigherPriorityTaskWoken;

        traceENTER_xTimerGenericCommandFromTask( xTimer, xCommandID, xOptionalValue, pxHigherPriorityTaskWoken, xTicksToWait );

        /* Send a message to the timer service task to perform a particular action
         * on a particular timer definition. */
        if( ( xTimerQueue != NULL ) && ( xTimer != NULL ) )
        {
            /* Send a command to the timer service task to start the xTimer timer. */
            xMessage.xMessageID = xCommandID;
            xMessage.u.xTimerParameters.xMessageValue = xOptionalValue;
            xMessage.u.xTimerParameters.pxTimer = xTimer;

            configASSERT( xCommandID < tmrFIRST_FROM_ISR_COMMAND );

            if( xCommandID < tmrFIRST_FROM_ISR_COMMAND )
            {
                if( xTaskGetSchedulerState() == taskSCHEDULER_RUNNING )
                {
                    xReturn = xQueueSendToBack( xTimerQueue, &xMessage, xTicksToWait );
                }
                else
                {
                    xReturn = xQueueSendToBack( xTimerQueue, &xMessage, tmrNO_DELAY );
                }
            }

            traceTIMER_COMMAND_SEND( xTimer, xCommandID, xOptionalValue, xReturn );
        }
        else
        {
            mtCOVERAGE_TEST_MARKER();
        }

        traceRETURN_xTimerGenericCommandFromTask( xReturn );

        return xReturn;
    }
/*-----------------------------------------------------------*/

    /* ISR 安全地向守护队列发送命令（ID >= tmrFIRST_FROM_ISR_COMMAND）。 */
    BaseType_t xTimerGenericCommandFromISR( TimerHandle_t xTimer,
                                            const BaseType_t xCommandID,
                                            const TickType_t xOptionalValue,
                                            BaseType_t * const pxHigherPriorityTaskWoken,
                                            const TickType_t xTicksToWait )
    {
        BaseType_t xReturn = pdFAIL;
        DaemonTaskMessage_t xMessage;

        ( void ) xTicksToWait;

        traceENTER_xTimerGenericCommandFromISR( xTimer, xCommandID, xOptionalValue, pxHigherPriorityTaskWoken, xTicksToWait );

        /* Send a message to the timer service task to perform a particular action
         * on a particular timer definition. */
        if( ( xTimerQueue != NULL ) && ( xTimer != NULL ) )
        {
            /* Send a command to the timer service task to start the xTimer timer. */
            xMessage.xMessageID = xCommandID;
            xMessage.u.xTimerParameters.xMessageValue = xOptionalValue;
            xMessage.u.xTimerParameters.pxTimer = xTimer;

            configASSERT( xCommandID >= tmrFIRST_FROM_ISR_COMMAND );

            if( xCommandID >= tmrFIRST_FROM_ISR_COMMAND )
            {
                xReturn = xQueueSendToBackFromISR( xTimerQueue, &xMessage, pxHigherPriorityTaskWoken );
            }

            traceTIMER_COMMAND_SEND( xTimer, xCommandID, xOptionalValue, xReturn );
        }
        else
        {
            mtCOVERAGE_TEST_MARKER();
        }

        traceRETURN_xTimerGenericCommandFromISR( xReturn );

        return xReturn;
    }
/*-----------------------------------------------------------*/

    /* 返回定时器守护任务句柄；调度未启动时可能为 NULL（由 configASSERT 约束）。 */
    TaskHandle_t xTimerGetTimerDaemonTaskHandle( void )
    {
        traceENTER_xTimerGetTimerDaemonTaskHandle();

        /* If xTimerGetTimerDaemonTaskHandle() is called before the scheduler has been
         * started, then xTimerTaskHandle will be NULL. */
        configASSERT( ( xTimerTaskHandle != NULL ) );

        traceRETURN_xTimerGetTimerDaemonTaskHandle( xTimerTaskHandle );

        return xTimerTaskHandle;
    }
/*-----------------------------------------------------------*/

    /* 返回定时器周期（tick）。 */
    TickType_t xTimerGetPeriod( TimerHandle_t xTimer )
    {
        Timer_t * pxTimer = xTimer;

        traceENTER_xTimerGetPeriod( xTimer );

        configASSERT( xTimer );

        traceRETURN_xTimerGetPeriod( pxTimer->xTimerPeriodInTicks );

        return pxTimer->xTimerPeriodInTicks;
    }
/*-----------------------------------------------------------*/

    /* 运行时切换单次/自动重载模式（改 ucStatus 位）。 */
    void vTimerSetReloadMode( TimerHandle_t xTimer,
                              const BaseType_t xAutoReload )
    {
        Timer_t * pxTimer = xTimer;

        traceENTER_vTimerSetReloadMode( xTimer, xAutoReload );

        configASSERT( xTimer );
        taskENTER_CRITICAL();
        {
            if( xAutoReload != pdFALSE )
            {
                pxTimer->ucStatus |= ( uint8_t ) tmrSTATUS_IS_AUTORELOAD;
            }
            else
            {
                pxTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_AUTORELOAD );
            }
        }
        taskEXIT_CRITICAL();

        traceRETURN_vTimerSetReloadMode();
    }
/*-----------------------------------------------------------*/

    /* 是否为自动重载定时器。 */
    BaseType_t xTimerGetReloadMode( TimerHandle_t xTimer )
    {
        Timer_t * pxTimer = xTimer;
        BaseType_t xReturn;

        traceENTER_xTimerGetReloadMode( xTimer );

        configASSERT( xTimer );
        portBASE_TYPE_ENTER_CRITICAL();
        {
            if( ( pxTimer->ucStatus & tmrSTATUS_IS_AUTORELOAD ) == 0U )
            {
                /* Not an auto-reload timer. */
                xReturn = pdFALSE;
            }
            else
            {
                /* Is an auto-reload timer. */
                xReturn = pdTRUE;
            }
        }
        portBASE_TYPE_EXIT_CRITICAL();

        traceRETURN_xTimerGetReloadMode( xReturn );

        return xReturn;
    }

    /* xTimerGetReloadMode 的 UBaseType_t 包装。 */
    UBaseType_t uxTimerGetReloadMode( TimerHandle_t xTimer )
    {
        UBaseType_t uxReturn;

        traceENTER_uxTimerGetReloadMode( xTimer );

        uxReturn = ( UBaseType_t ) xTimerGetReloadMode( xTimer );

        traceRETURN_uxTimerGetReloadMode( uxReturn );

        return uxReturn;
    }
/*-----------------------------------------------------------*/

    /* 返回链表项中记录的下次到期 tick（活动表中有效）。 */
    TickType_t xTimerGetExpiryTime( TimerHandle_t xTimer )
    {
        Timer_t * pxTimer = xTimer;
        TickType_t xReturn;

        traceENTER_xTimerGetExpiryTime( xTimer );

        configASSERT( xTimer );
        xReturn = listGET_LIST_ITEM_VALUE( &( pxTimer->xTimerListItem ) );

        traceRETURN_xTimerGetExpiryTime( xReturn );

        return xReturn;
    }
/*-----------------------------------------------------------*/

    #if ( configSUPPORT_STATIC_ALLOCATION == 1 )
        /* 若为静态创建则写出 StaticTimer_t 指针。 */
        BaseType_t xTimerGetStaticBuffer( TimerHandle_t xTimer,
                                          StaticTimer_t ** ppxTimerBuffer )
        {
            BaseType_t xReturn;
            Timer_t * pxTimer = xTimer;

            traceENTER_xTimerGetStaticBuffer( xTimer, ppxTimerBuffer );

            configASSERT( ppxTimerBuffer != NULL );

            if( ( pxTimer->ucStatus & tmrSTATUS_IS_STATICALLY_ALLOCATED ) != 0U )
            {
                /* MISRA Ref 11.3.1 [Misaligned access] */
                /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-113 */
                /* coverity[misra_c_2012_rule_11_3_violation] */
                *ppxTimerBuffer = ( StaticTimer_t * ) pxTimer;
                xReturn = pdTRUE;
            }
            else
            {
                xReturn = pdFALSE;
            }

            traceRETURN_xTimerGetStaticBuffer( xReturn );

            return xReturn;
        }
    #endif /* configSUPPORT_STATIC_ALLOCATION */
/*-----------------------------------------------------------*/

    /* 调试用定时器名字符串。 */
    const char * pcTimerGetName( TimerHandle_t xTimer )
    {
        Timer_t * pxTimer = xTimer;

        traceENTER_pcTimerGetName( xTimer );

        configASSERT( xTimer );

        traceRETURN_pcTimerGetName( pxTimer->pcTimerName );

        return pxTimer->pcTimerName;
    }
/*-----------------------------------------------------------*/

    /* 自动重载：循环插入并回调直到下次到期落在未来。 */
    static void prvReloadTimer( Timer_t * const pxTimer,
                                TickType_t xExpiredTime,
                                const TickType_t xTimeNow )
    {
        /* Insert the timer into the appropriate list for the next expiry time.
         * If the next expiry time has already passed, advance the expiry time,
         * call the callback function, and try again. */
        while( prvInsertTimerInActiveList( pxTimer, ( xExpiredTime + pxTimer->xTimerPeriodInTicks ), xTimeNow, xExpiredTime ) != pdFALSE )
        {
            /* Advance the expiry time. */
            xExpiredTime += pxTimer->xTimerPeriodInTicks;

            /* Call the timer callback. */
            traceTIMER_EXPIRED( pxTimer );
            pxTimer->pxCallbackFunction( ( TimerHandle_t ) pxTimer );
        }
    }
/*-----------------------------------------------------------*/

    /* 处理当前活动表表头已到期定时器：去链、自动重载或清活动位、调回调。 */
    static void prvProcessExpiredTimer( const TickType_t xNextExpireTime,
                                        const TickType_t xTimeNow )
    {
        /* MISRA Ref 11.5.3 [Void pointer assignment] */
        /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-115 */
        /* coverity[misra_c_2012_rule_11_5_violation] */
        Timer_t * const pxTimer = ( Timer_t * ) listGET_OWNER_OF_HEAD_ENTRY( pxCurrentTimerList );

        /* Remove the timer from the list of active timers.  A check has already
         * been performed to ensure the list is not empty. */

        ( void ) uxListRemove( &( pxTimer->xTimerListItem ) );

        /* If the timer is an auto-reload timer then calculate the next
         * expiry time and re-insert the timer in the list of active timers. */
        if( ( pxTimer->ucStatus & tmrSTATUS_IS_AUTORELOAD ) != 0U )
        {
            prvReloadTimer( pxTimer, xNextExpireTime, xTimeNow );
        }
        else
        {
            pxTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_ACTIVE );
        }

        /* Call the timer callback. */
        traceTIMER_EXPIRED( pxTimer );
        pxTimer->pxCallbackFunction( ( TimerHandle_t ) pxTimer );
    }
/*-----------------------------------------------------------*/

    /* 守护任务主循环：取下一到期 → 阻塞或处理到期 → 排空命令队列。 */
    static portTASK_FUNCTION( prvTimerTask, pvParameters )
    {
        TickType_t xNextExpireTime;
        BaseType_t xListWasEmpty;

        /* Just to avoid compiler warnings. */
        ( void ) pvParameters;

        #if ( configUSE_DAEMON_TASK_STARTUP_HOOK == 1 )
        {
            /* Allow the application writer to execute some code in the context of
             * this task at the point the task starts executing.  This is useful if the
             * application includes initialisation code that would benefit from
             * executing after the scheduler has been started. */
            vApplicationDaemonTaskStartupHook();
        }
        #endif /* configUSE_DAEMON_TASK_STARTUP_HOOK */

        for( ; configCONTROL_INFINITE_LOOP(); )
        {
            /* Query the timers list to see if it contains any timers, and if so,
             * obtain the time at which the next timer will expire. */
            xNextExpireTime = prvGetNextExpireTime( &xListWasEmpty );

            /* If a timer has expired, process it.  Otherwise, block this task
             * until either a timer does expire, or a command is received. */
            prvProcessTimerOrBlockTask( xNextExpireTime, xListWasEmpty );

            /* Empty the command queue. */
            prvProcessReceivedCommands();
        }
    }
/*-----------------------------------------------------------*/

    /* 在调度挂起区内判断是否已到期；否则用 vQueueWaitForMessageRestricted 限时等命令。 */
    static void prvProcessTimerOrBlockTask( const TickType_t xNextExpireTime,
                                            BaseType_t xListWasEmpty )
    {
        TickType_t xTimeNow;
        BaseType_t xTimerListsWereSwitched;

        vTaskSuspendAll();
        {
            /* Obtain the time now to make an assessment as to whether the timer
             * has expired or not.  If obtaining the time causes the lists to switch
             * then don't process this timer as any timers that remained in the list
             * when the lists were switched will have been processed within the
             * prvSampleTimeNow() function. */
            xTimeNow = prvSampleTimeNow( &xTimerListsWereSwitched );

            if( xTimerListsWereSwitched == pdFALSE )
            {
                /* The tick count has not overflowed, has the timer expired? */
                if( ( xListWasEmpty == pdFALSE ) && ( xNextExpireTime <= xTimeNow ) )
                {
                    ( void ) xTaskResumeAll();
                    prvProcessExpiredTimer( xNextExpireTime, xTimeNow );
                }
                else
                {
                    /* The tick count has not overflowed, and the next expire
                     * time has not been reached yet.  This task should therefore
                     * block to wait for the next expire time or a command to be
                     * received - whichever comes first.  The following line cannot
                     * be reached unless xNextExpireTime > xTimeNow, except in the
                     * case when the current timer list is empty. */
                    if( xListWasEmpty != pdFALSE )
                    {
                        /* The current timer list is empty - is the overflow list
                         * also empty? */
                        xListWasEmpty = listLIST_IS_EMPTY( pxOverflowTimerList );
                    }

                    vQueueWaitForMessageRestricted( xTimerQueue, ( xNextExpireTime - xTimeNow ), xListWasEmpty );

                    if( xTaskResumeAll() == pdFALSE )
                    {
                        /* Yield to wait for either a command to arrive, or the
                         * block time to expire.  If a command arrived between the
                         * critical section being exited and this yield then the yield
                         * will not cause the task to block. */
                        taskYIELD_WITHIN_API();
                    }
                    else
                    {
                        mtCOVERAGE_TEST_MARKER();
                    }
                }
            }
            else
            {
                ( void ) xTaskResumeAll();
            }
        }
    }
/*-----------------------------------------------------------*/

    /* 读当前活动表最近到期 tick；空表返回 0 以便 tick 回绕时再评估。 */
    static TickType_t prvGetNextExpireTime( BaseType_t * const pxListWasEmpty )
    {
        TickType_t xNextExpireTime;

        /* Timers are listed in expiry time order, with the head of the list
         * referencing the task that will expire first.  Obtain the time at which
         * the timer with the nearest expiry time will expire.  If there are no
         * active timers then just set the next expire time to 0.  That will cause
         * this task to unblock when the tick count overflows, at which point the
         * timer lists will be switched and the next expiry time can be
         * re-assessed.  */
        *pxListWasEmpty = listLIST_IS_EMPTY( pxCurrentTimerList );

        if( *pxListWasEmpty == pdFALSE )
        {
            xNextExpireTime = listGET_ITEM_VALUE_OF_HEAD_ENTRY( pxCurrentTimerList );
        }
        else
        {
            /* Ensure the task unblocks when the tick count rolls over. */
            xNextExpireTime = ( TickType_t ) 0U;
        }

        return xNextExpireTime;
    }
/*-----------------------------------------------------------*/

    /* 采样 xTaskGetTickCount；检测相对上次采样是否回绕并必要时切换定时器双表。 */
    static TickType_t prvSampleTimeNow( BaseType_t * const pxTimerListsWereSwitched )
    {
        TickType_t xTimeNow;
        PRIVILEGED_DATA static TickType_t xLastTime = ( TickType_t ) 0U;

        xTimeNow = xTaskGetTickCount();

        if( xTimeNow < xLastTime )
        {
            prvSwitchTimerLists();
            *pxTimerListsWereSwitched = pdTRUE;
        }
        else
        {
            *pxTimerListsWereSwitched = pdFALSE;
        }

        xLastTime = xTimeNow;

        return xTimeNow;
    }
/*-----------------------------------------------------------*/

    /* 设置链表项到期值并入表；若相对命令时刻已视为过期则返回立即处理标志。 */
    static BaseType_t prvInsertTimerInActiveList( Timer_t * const pxTimer,
                                                  const TickType_t xNextExpiryTime,
                                                  const TickType_t xTimeNow,
                                                  const TickType_t xCommandTime )
    {
        BaseType_t xProcessTimerNow = pdFALSE;

        listSET_LIST_ITEM_VALUE( &( pxTimer->xTimerListItem ), xNextExpiryTime );
        listSET_LIST_ITEM_OWNER( &( pxTimer->xTimerListItem ), pxTimer );

        if( xNextExpiryTime <= xTimeNow )
        {
            /* Has the expiry time elapsed between the command to start/reset a
             * timer was issued, and the time the command was processed? */
            if( ( ( TickType_t ) ( xTimeNow - xCommandTime ) ) >= pxTimer->xTimerPeriodInTicks )
            {
                /* The time between a command being issued and the command being
                 * processed actually exceeds the timers period.  */
                xProcessTimerNow = pdTRUE;
            }
            else
            {
                vListInsert( pxOverflowTimerList, &( pxTimer->xTimerListItem ) );
            }
        }
        else
        {
            if( ( xTimeNow < xCommandTime ) && ( xNextExpiryTime >= xCommandTime ) )
            {
                /* If, since the command was issued, the tick count has overflowed
                 * but the expiry time has not, then the timer must have already passed
                 * its expiry time and should be processed immediately. */
                xProcessTimerNow = pdTRUE;
            }
            else
            {
                vListInsert( pxCurrentTimerList, &( pxTimer->xTimerListItem ) );
            }
        }

        return xProcessTimerNow;
    }
/*-----------------------------------------------------------*/

    /* 非阻塞读尽 xTimerQueue：执行延迟回调或按 xMessageID 处理定时器命令。 */
    static void prvProcessReceivedCommands( void )
    {
        DaemonTaskMessage_t xMessage = { 0 };
        Timer_t * pxTimer;
        BaseType_t xTimerListsWereSwitched;
        TickType_t xTimeNow;

        while( xQueueReceive( xTimerQueue, &xMessage, tmrNO_DELAY ) != pdFAIL )
        {
            #if ( INCLUDE_xTimerPendFunctionCall == 1 )
            {
                /* Negative commands are pended function calls rather than timer
                 * commands. */
                if( xMessage.xMessageID < ( BaseType_t ) 0 )
                {
                    const CallbackParameters_t * const pxCallback = &( xMessage.u.xCallbackParameters );

                    /* The timer uses the xCallbackParameters member to request a
                     * callback be executed.  Check the callback is not NULL. */
                    configASSERT( pxCallback );

                    /* Call the function. */
                    pxCallback->pxCallbackFunction( pxCallback->pvParameter1, pxCallback->ulParameter2 );
                }
                else
                {
                    mtCOVERAGE_TEST_MARKER();
                }
            }
            #endif /* INCLUDE_xTimerPendFunctionCall */

            /* Commands that are positive are timer commands rather than pended
             * function calls. */
            if( xMessage.xMessageID >= ( BaseType_t ) 0 )
            {
                /* The messages uses the xTimerParameters member to work on a
                 * software timer. */
                pxTimer = xMessage.u.xTimerParameters.pxTimer;

                if( pxTimer != NULL )
                {
                    if( listIS_CONTAINED_WITHIN( NULL, &( pxTimer->xTimerListItem ) ) == pdFALSE )
                    {
                        /* The timer is in a list, remove it. */
                        ( void ) uxListRemove( &( pxTimer->xTimerListItem ) );
                    }
                    else
                    {
                        mtCOVERAGE_TEST_MARKER();
                    }

                    traceTIMER_COMMAND_RECEIVED( pxTimer, xMessage.xMessageID, xMessage.u.xTimerParameters.xMessageValue );

                    /* In this case the xTimerListsWereSwitched parameter is not used, but
                     *  it must be present in the function call.  prvSampleTimeNow() must be
                     *  called after the message is received from xTimerQueue so there is no
                     *  possibility of a higher priority task adding a message to the message
                     *  queue with a time that is ahead of the timer daemon task (because it
                     *  pre-empted the timer daemon task after the xTimeNow value was set). */
                    xTimeNow = prvSampleTimeNow( &xTimerListsWereSwitched );

                    switch( xMessage.xMessageID )
                    {
                        case tmrCOMMAND_START:
                        case tmrCOMMAND_START_FROM_ISR:
                        case tmrCOMMAND_RESET:
                        case tmrCOMMAND_RESET_FROM_ISR:
                            /* Start or restart a timer. */
                            pxTimer->ucStatus |= ( uint8_t ) tmrSTATUS_IS_ACTIVE;

                            if( prvInsertTimerInActiveList( pxTimer, xMessage.u.xTimerParameters.xMessageValue + pxTimer->xTimerPeriodInTicks, xTimeNow, xMessage.u.xTimerParameters.xMessageValue ) != pdFALSE )
                            {
                                /* The timer expired before it was added to the active
                                 * timer list.  Process it now. */
                                if( ( pxTimer->ucStatus & tmrSTATUS_IS_AUTORELOAD ) != 0U )
                                {
                                    prvReloadTimer( pxTimer, xMessage.u.xTimerParameters.xMessageValue + pxTimer->xTimerPeriodInTicks, xTimeNow );
                                }
                                else
                                {
                                    pxTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_ACTIVE );
                                }

                                /* Call the timer callback. */
                                traceTIMER_EXPIRED( pxTimer );
                                pxTimer->pxCallbackFunction( ( TimerHandle_t ) pxTimer );
                            }
                            else
                            {
                                mtCOVERAGE_TEST_MARKER();
                            }

                            break;

                        case tmrCOMMAND_STOP:
                        case tmrCOMMAND_STOP_FROM_ISR:
                            /* The timer has already been removed from the active list. */
                            pxTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_ACTIVE );
                            break;

                        case tmrCOMMAND_CHANGE_PERIOD:
                        case tmrCOMMAND_CHANGE_PERIOD_FROM_ISR:
                            pxTimer->ucStatus |= ( uint8_t ) tmrSTATUS_IS_ACTIVE;
                            pxTimer->xTimerPeriodInTicks = xMessage.u.xTimerParameters.xMessageValue;
                            configASSERT( ( pxTimer->xTimerPeriodInTicks > 0 ) );

                            /* The new period does not really have a reference, and can
                             * be longer or shorter than the old one.  The command time is
                             * therefore set to the current time, and as the period cannot
                             * be zero the next expiry time can only be in the future,
                             * meaning (unlike for the xTimerStart() case above) there is
                             * no fail case that needs to be handled here. */
                            ( void ) prvInsertTimerInActiveList( pxTimer, ( xTimeNow + pxTimer->xTimerPeriodInTicks ), xTimeNow, xTimeNow );
                            break;

                        case tmrCOMMAND_DELETE:
                            #if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
                            {
                                /* The timer has already been removed from the active list,
                                 * just free up the memory if the memory was dynamically
                                 * allocated. */
                                if( ( pxTimer->ucStatus & tmrSTATUS_IS_STATICALLY_ALLOCATED ) == ( uint8_t ) 0 )
                                {
                                    vPortFree( pxTimer );
                                }
                                else
                                {
                                    pxTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_ACTIVE );
                                }
                            }
                            #else /* if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) */
                            {
                                /* If dynamic allocation is not enabled, the memory
                                 * could not have been dynamically allocated. So there is
                                 * no need to free the memory - just mark the timer as
                                 * "not active". */
                                pxTimer->ucStatus &= ( ( uint8_t ) ~tmrSTATUS_IS_ACTIVE );
                            }
                            #endif /* configSUPPORT_DYNAMIC_ALLOCATION */
                            break;

                        default:
                            /* Don't expect to get here. */
                            break;
                    }
                }
                else
                {
                    mtCOVERAGE_TEST_MARKER();
                }
            }
        }
    }
/*-----------------------------------------------------------*/

    /* tick 溢出：先清空当前表上到期项，再交换当前/溢出活动表指针。 */
    static void prvSwitchTimerLists( void )
    {
        TickType_t xNextExpireTime;
        List_t * pxTemp;

        /* The tick count has overflowed.  The timer lists must be switched.
         * If there are any timers still referenced from the current timer list
         * then they must have expired and should be processed before the lists
         * are switched. */
        while( listLIST_IS_EMPTY( pxCurrentTimerList ) == pdFALSE )
        {
            xNextExpireTime = listGET_ITEM_VALUE_OF_HEAD_ENTRY( pxCurrentTimerList );

            /* Process the expired timer.  For auto-reload timers, be careful to
             * process only expirations that occur on the current list.  Further
             * expirations must wait until after the lists are switched. */
            prvProcessExpiredTimer( xNextExpireTime, tmrMAX_TIME_BEFORE_OVERFLOW );
        }

        pxTemp = pxCurrentTimerList;
        pxCurrentTimerList = pxOverflowTimerList;
        pxOverflowTimerList = pxTemp;
    }
/*-----------------------------------------------------------*/

    /* 临界区内一次性初始化双链表与命令队列（幂等）。 */
    static void prvCheckForValidListAndQueue( void )
    {
        /* Check that the list from which active timers are referenced, and the
         * queue used to communicate with the timer service, have been
         * initialised. */
        taskENTER_CRITICAL();
        {
            if( xTimerQueue == NULL )
            {
                vListInitialise( &xActiveTimerList1 );
                vListInitialise( &xActiveTimerList2 );
                pxCurrentTimerList = &xActiveTimerList1;
                pxOverflowTimerList = &xActiveTimerList2;

                #if ( configSUPPORT_STATIC_ALLOCATION == 1 )
                {
                    /* The timer queue is allocated statically in case
                     * configSUPPORT_DYNAMIC_ALLOCATION is 0. */
                    PRIVILEGED_DATA static StaticQueue_t xStaticTimerQueue;
                    PRIVILEGED_DATA static uint8_t ucStaticTimerQueueStorage[ ( size_t ) configTIMER_QUEUE_LENGTH * sizeof( DaemonTaskMessage_t ) ];

                    xTimerQueue = xQueueCreateStatic( ( UBaseType_t ) configTIMER_QUEUE_LENGTH, ( UBaseType_t ) sizeof( DaemonTaskMessage_t ), &( ucStaticTimerQueueStorage[ 0 ] ), &xStaticTimerQueue );
                }
                #else
                {
                    xTimerQueue = xQueueCreate( ( UBaseType_t ) configTIMER_QUEUE_LENGTH, ( UBaseType_t ) sizeof( DaemonTaskMessage_t ) );
                }
                #endif /* if ( configSUPPORT_STATIC_ALLOCATION == 1 ) */

                #if ( configQUEUE_REGISTRY_SIZE > 0 )
                {
                    if( xTimerQueue != NULL )
                    {
                        vQueueAddToRegistry( xTimerQueue, "TmrQ" );
                    }
                    else
                    {
                        mtCOVERAGE_TEST_MARKER();
                    }
                }
                #endif /* configQUEUE_REGISTRY_SIZE */
            }
            else
            {
                mtCOVERAGE_TEST_MARKER();
            }
        }
        taskEXIT_CRITICAL();
    }
/*-----------------------------------------------------------*/

    /* 查询定时器是否处于活动（已启动且未停止）状态。 */
    BaseType_t xTimerIsTimerActive( TimerHandle_t xTimer )
    {
        BaseType_t xReturn;
        Timer_t * pxTimer = xTimer;

        traceENTER_xTimerIsTimerActive( xTimer );

        configASSERT( xTimer );

        /* Is the timer in the list of active timers? */
        portBASE_TYPE_ENTER_CRITICAL();
        {
            if( ( pxTimer->ucStatus & tmrSTATUS_IS_ACTIVE ) == 0U )
            {
                xReturn = pdFALSE;
            }
            else
            {
                xReturn = pdTRUE;
            }
        }
        portBASE_TYPE_EXIT_CRITICAL();

        traceRETURN_xTimerIsTimerActive( xReturn );

        return xReturn;
    }
/*-----------------------------------------------------------*/

    /* 临界区内读取 pvTimerID。 */
    void * pvTimerGetTimerID( const TimerHandle_t xTimer )
    {
        Timer_t * const pxTimer = xTimer;
        void * pvReturn;

        traceENTER_pvTimerGetTimerID( xTimer );

        configASSERT( xTimer );

        taskENTER_CRITICAL();
        {
            pvReturn = pxTimer->pvTimerID;
        }
        taskEXIT_CRITICAL();

        traceRETURN_pvTimerGetTimerID( pvReturn );

        return pvReturn;
    }
/*-----------------------------------------------------------*/

    /* 临界区内更新 pvTimerID。 */
    void vTimerSetTimerID( TimerHandle_t xTimer,
                           void * pvNewID )
    {
        Timer_t * const pxTimer = xTimer;

        traceENTER_vTimerSetTimerID( xTimer, pvNewID );

        configASSERT( xTimer );

        taskENTER_CRITICAL();
        {
            pxTimer->pvTimerID = pvNewID;
        }
        taskEXIT_CRITICAL();

        traceRETURN_vTimerSetTimerID();
    }
/*-----------------------------------------------------------*/

    #if ( INCLUDE_xTimerPendFunctionCall == 1 )

        /* ISR 中将普通函数投递到守护任务执行（与定时器命令共用队列）。 */
        BaseType_t xTimerPendFunctionCallFromISR( PendedFunction_t xFunctionToPend,
                                                  void * pvParameter1,
                                                  uint32_t ulParameter2,
                                                  BaseType_t * pxHigherPriorityTaskWoken )
        {
            DaemonTaskMessage_t xMessage;
            BaseType_t xReturn;

            traceENTER_xTimerPendFunctionCallFromISR( xFunctionToPend, pvParameter1, ulParameter2, pxHigherPriorityTaskWoken );

            /* Complete the message with the function parameters and post it to the
             * daemon task. */
            xMessage.xMessageID = tmrCOMMAND_EXECUTE_CALLBACK_FROM_ISR;
            xMessage.u.xCallbackParameters.pxCallbackFunction = xFunctionToPend;
            xMessage.u.xCallbackParameters.pvParameter1 = pvParameter1;
            xMessage.u.xCallbackParameters.ulParameter2 = ulParameter2;

            xReturn = xQueueSendFromISR( xTimerQueue, &xMessage, pxHigherPriorityTaskWoken );

            tracePEND_FUNC_CALL_FROM_ISR( xFunctionToPend, pvParameter1, ulParameter2, xReturn );
            traceRETURN_xTimerPendFunctionCallFromISR( xReturn );

            return xReturn;
        }

    #endif /* INCLUDE_xTimerPendFunctionCall */
/*-----------------------------------------------------------*/

    #if ( INCLUDE_xTimerPendFunctionCall == 1 )

        /* 任务上下文延迟执行回调（需 xTimerQueue 已存在）。 */
        BaseType_t xTimerPendFunctionCall( PendedFunction_t xFunctionToPend,
                                           void * pvParameter1,
                                           uint32_t ulParameter2,
                                           TickType_t xTicksToWait )
        {
            DaemonTaskMessage_t xMessage;
            BaseType_t xReturn;

            traceENTER_xTimerPendFunctionCall( xFunctionToPend, pvParameter1, ulParameter2, xTicksToWait );

            /* This function can only be called after a timer has been created or
             * after the scheduler has been started because, until then, the timer
             * queue does not exist. */
            configASSERT( xTimerQueue );

            /* Complete the message with the function parameters and post it to the
             * daemon task. */
            xMessage.xMessageID = tmrCOMMAND_EXECUTE_CALLBACK;
            xMessage.u.xCallbackParameters.pxCallbackFunction = xFunctionToPend;
            xMessage.u.xCallbackParameters.pvParameter1 = pvParameter1;
            xMessage.u.xCallbackParameters.ulParameter2 = ulParameter2;

            xReturn = xQueueSendToBack( xTimerQueue, &xMessage, xTicksToWait );

            tracePEND_FUNC_CALL( xFunctionToPend, pvParameter1, ulParameter2, xReturn );
            traceRETURN_xTimerPendFunctionCall( xReturn );

            return xReturn;
        }

    #endif /* INCLUDE_xTimerPendFunctionCall */
/*-----------------------------------------------------------*/

    #if ( configUSE_TRACE_FACILITY == 1 )

        /* 读取跟踪用 uxTimerNumber。 */
        UBaseType_t uxTimerGetTimerNumber( TimerHandle_t xTimer )
        {
            traceENTER_uxTimerGetTimerNumber( xTimer );

            traceRETURN_uxTimerGetTimerNumber( ( ( Timer_t * ) xTimer )->uxTimerNumber );

            return ( ( Timer_t * ) xTimer )->uxTimerNumber;
        }

    #endif /* configUSE_TRACE_FACILITY */
/*-----------------------------------------------------------*/

    #if ( configUSE_TRACE_FACILITY == 1 )

        /* 设置跟踪用 uxTimerNumber。 */
        void vTimerSetTimerNumber( TimerHandle_t xTimer,
                                   UBaseType_t uxTimerNumber )
        {
            traceENTER_vTimerSetTimerNumber( xTimer, uxTimerNumber );

            ( ( Timer_t * ) xTimer )->uxTimerNumber = uxTimerNumber;

            traceRETURN_vTimerSetTimerNumber();
        }

    #endif /* configUSE_TRACE_FACILITY */
/*-----------------------------------------------------------*/

    /*
     * 复位本文件全局状态（队列与守护任务句柄），供应用在不复位硬件的情况下重启调度器前调用。
     */
    void vTimerResetState( void )
    {
        xTimerQueue = NULL;
        xTimerTaskHandle = NULL;
    }
/*-----------------------------------------------------------*/

#endif /* configUSE_TIMERS == 1; 为 0 时本文件整段不参与编译 */
