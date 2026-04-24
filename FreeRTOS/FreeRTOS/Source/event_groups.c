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
 * 事件组（Event Group）：以位图形式管理多路事件同步。
 * 任务可等待若干位被置位（任意一位或全部），支持阻塞、超时与「集合点」式 xEventGroupSync。
 * 依赖 FreeRTOSConfig.h 中 configUSE_EVENT_GROUPS == 1；内部通过任务事件链表与调度器协作。
 */

/* Standard includes. */
#include <stdlib.h>

/* 在实现文件内包含 task.h 时定义 MPU_WRAPPERS_INCLUDED_FROM_API_FILE，避免此处 API 被包一层 MPU 包装。 */
#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "event_groups.h"

/* 头文件包含结束后取消宏，使本文件内符号以特权实现方式链接（MPU 端口要求）。 */
#undef MPU_WRAPPERS_INCLUDED_FROM_API_FILE

/* 若 configUSE_EVENT_GROUPS != 1，则本文件整段不参与编译（由文件末尾 #endif 闭合）。 */
#if ( configUSE_EVENT_GROUPS == 1 )

    typedef struct EventGroupDef_t
    {
        EventBits_t uxEventBits;              /**< 当前事件位图（应用可见位须避开内核保留的控制位）。 */
        List_t xTasksWaitingForBits;          /**< 因等待某几位而阻塞的任务挂在此无序事件链表上。 */

        #if ( configUSE_TRACE_FACILITY == 1 )
            UBaseType_t uxEventGroupNumber;   /**< 调试/跟踪用的编号。 */
        #endif

        #if ( ( configSUPPORT_STATIC_ALLOCATION == 1 ) && ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) )
            uint8_t ucStaticallyAllocated;    /**< pdTRUE：静态缓冲区创建，删除时不得 vPortFree。 */
        #endif
    } EventGroup_t;

/*-----------------------------------------------------------*/

/*
 * 判断 uxCurrentEventBits 是否满足等待条件。
 * xWaitForAllBits 为 pdTRUE：uxBitsToWaitFor 中每一位都须在 uxCurrentEventBits 中已置位；
 * 为 pdFALSE：uxBitsToWaitFor 中任一位被置位即满足。
 */
    static BaseType_t prvTestWaitCondition( const EventBits_t uxCurrentEventBits,
                                            const EventBits_t uxBitsToWaitFor,
                                            const BaseType_t xWaitForAllBits ) PRIVILEGED_FUNCTION;

/*-----------------------------------------------------------*/

    #if ( configSUPPORT_STATIC_ALLOCATION == 1 )

        /* 使用调用方提供的 StaticEventGroup_t 内存创建事件组（无 pvPortMalloc）。 */
        EventGroupHandle_t xEventGroupCreateStatic( StaticEventGroup_t * pxEventGroupBuffer )
        {
            EventGroup_t * pxEventBits;

            traceENTER_xEventGroupCreateStatic( pxEventGroupBuffer );

            /* 必须由调用方传入已分配好的静态缓冲区。 */
            configASSERT( pxEventGroupBuffer );

            #if ( configASSERT_DEFINED == 1 )
            {
                /* 静态缓冲区类型必须与内核实际使用的 EventGroup_t 等大，防止用户 typedef 不一致。 */
                volatile size_t xSize = sizeof( StaticEventGroup_t );
                configASSERT( xSize == sizeof( EventGroup_t ) );
            }
            #endif /* configASSERT_DEFINED */

            /* 将用户缓冲区视作 EventGroup_t 使用。 */
            /* MISRA Ref 11.3.1 [Misaligned access] */
            /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-113 */
            /* coverity[misra_c_2012_rule_11_3_violation] */
            pxEventBits = ( EventGroup_t * ) pxEventGroupBuffer;

            if( pxEventBits != NULL )
            {
                pxEventBits->uxEventBits = 0;
                vListInitialise( &( pxEventBits->xTasksWaitingForBits ) );

                #if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
                {
                    /* 同时支持动/静态时，标记为静态以便 vEventGroupDelete 不误释放。 */
                    pxEventBits->ucStaticallyAllocated = pdTRUE;
                }
                #endif /* configSUPPORT_DYNAMIC_ALLOCATION */

                traceEVENT_GROUP_CREATE( pxEventBits );
            }
            else
            {
                /* 仅当缓冲区指针非法时进入；正常静态变量不应为 NULL。 */
                traceEVENT_GROUP_CREATE_FAILED();
            }

            traceRETURN_xEventGroupCreateStatic( pxEventBits );

            return pxEventBits;
        }

    #endif /* configSUPPORT_STATIC_ALLOCATION */
/*-----------------------------------------------------------*/

    #if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )

        /* 动态分配 EventGroup_t 并初始化位图与等待链表。 */
        EventGroupHandle_t xEventGroupCreate( void )
        {
            EventGroup_t * pxEventBits;

            traceENTER_xEventGroupCreate();

            /* MISRA Ref 11.5.1 [Malloc memory assignment] */
            /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-115 */
            /* coverity[misra_c_2012_rule_11_5_violation] */
            pxEventBits = ( EventGroup_t * ) pvPortMalloc( sizeof( EventGroup_t ) );

            if( pxEventBits != NULL )
            {
                pxEventBits->uxEventBits = 0;
                vListInitialise( &( pxEventBits->xTasksWaitingForBits ) );

                #if ( configSUPPORT_STATIC_ALLOCATION == 1 )
                {
                    /* 标记为堆分配，删除时可 vPortFree（与静态创建区分）。 */
                    pxEventBits->ucStaticallyAllocated = pdFALSE;
                }
                #endif /* configSUPPORT_STATIC_ALLOCATION */

                traceEVENT_GROUP_CREATE( pxEventBits );
            }
            else
            {
                traceEVENT_GROUP_CREATE_FAILED();
            }

            traceRETURN_xEventGroupCreate( pxEventBits );

            return pxEventBits;
        }

    #endif /* configSUPPORT_DYNAMIC_ALLOCATION */
/*-----------------------------------------------------------*/

    /*
     * 集合点同步：原子地置位 uxBitsToSet，再判断 (原值|新置位) 是否已包含 uxBitsToWaitFor 的全部位。
     * 若已齐：清除等待的各位并立即返回；否则可阻塞至超时。常用于多任务屏障式握手。
     */
    EventBits_t xEventGroupSync( EventGroupHandle_t xEventGroup,
                                 const EventBits_t uxBitsToSet,
                                 const EventBits_t uxBitsToWaitFor,
                                 TickType_t xTicksToWait )
    {
        EventBits_t uxOriginalBitValue, uxReturn;
        EventGroup_t * pxEventBits = xEventGroup;
        BaseType_t xAlreadyYielded;
        BaseType_t xTimeoutOccurred = pdFALSE;

        traceENTER_xEventGroupSync( xEventGroup, uxBitsToSet, uxBitsToWaitFor, xTicksToWait );

        /* 等待掩码不得使用内核保留控制位；且须至少等待一位。 */
        configASSERT( ( uxBitsToWaitFor & eventEVENT_BITS_CONTROL_BYTES ) == 0 );
        configASSERT( uxBitsToWaitFor != 0 );
        #if ( ( INCLUDE_xTaskGetSchedulerState == 1 ) || ( configUSE_TIMERS == 1 ) )
        {
            configASSERT( !( ( xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED ) && ( xTicksToWait != 0 ) ) );
        }
        #endif

        vTaskSuspendAll();
        {
            uxOriginalBitValue = pxEventBits->uxEventBits;

            /* 先置位，再判断是否已满足「全部等待位」条件（与 WaitBits 的语义在此固定为等齐）。 */
            ( void ) xEventGroupSetBits( xEventGroup, uxBitsToSet );

            if( ( ( uxOriginalBitValue | uxBitsToSet ) & uxBitsToWaitFor ) == uxBitsToWaitFor )
            {
                /* 集合条件已满足，无需阻塞。 */
                uxReturn = ( uxOriginalBitValue | uxBitsToSet );

                /* 集合点语义：满足后清除所等待的各位（可能已被其它路径清过）。 */
                pxEventBits->uxEventBits &= ~uxBitsToWaitFor;

                xTicksToWait = 0;
            }
            else
            {
                if( xTicksToWait != ( TickType_t ) 0 )
                {
                    traceEVENT_GROUP_SYNC_BLOCK( xEventGroup, uxBitsToSet, uxBitsToWaitFor );

                    /* 将等待掩码及控制标志写入当前任务事件项，挂入事件组等待链表并阻塞。 */
                    vTaskPlaceOnUnorderedEventList( &( pxEventBits->xTasksWaitingForBits ), ( uxBitsToWaitFor | eventCLEAR_EVENTS_ON_EXIT_BIT | eventWAIT_FOR_ALL_BITS ), xTicksToWait );

                    /* 唤醒后会重设 uxReturn；此处仅为消除部分编译器「未初始化返回」误报。 */
                    uxReturn = 0;
                }
                else
                {
                    /* 未齐且不允许阻塞：返回当前位图（视为未等到集合）。 */
                    uxReturn = pxEventBits->uxEventBits;
                    xTimeoutOccurred = pdTRUE;
                }
            }
        }
        xAlreadyYielded = xTaskResumeAll();

        if( xTicksToWait != ( TickType_t ) 0 )
        {
            /* 若恢复调度时尚未切换走，主动触发一次让步以便进入阻塞态。 */
            if( xAlreadyYielded == pdFALSE )
            {
                taskYIELD_WITHIN_API();
            }
            else
            {
                mtCOVERAGE_TEST_MARKER();
            }

            /* 被唤醒后从任务事件项取回原因及位图；超时时再读临界区内的当前组值。 */
            uxReturn = uxTaskResetEventItemValue();

            if( ( uxReturn & eventUNBLOCKED_DUE_TO_BIT_SET ) == ( EventBits_t ) 0 )
            {
                /* 超时路径：返回此刻事件组上的位。 */
                taskENTER_CRITICAL();
                {
                    uxReturn = pxEventBits->uxEventBits;

                    /* 超时与真正置位存在竞态：若在阻塞期间其它任务已凑齐等待位，此处仍须按集合点清除。 */
                    if( ( uxReturn & uxBitsToWaitFor ) == uxBitsToWaitFor )
                    {
                        pxEventBits->uxEventBits &= ~uxBitsToWaitFor;
                    }
                    else
                    {
                        mtCOVERAGE_TEST_MARKER();
                    }
                }
                taskEXIT_CRITICAL();

                xTimeoutOccurred = pdTRUE;
            }
            else
            {
                /* 因匹配等待位而解除阻塞。 */
            }

            /* 去掉内核存放在返回值中的控制位，仅向应用返回事件位。 */
            uxReturn &= ~eventEVENT_BITS_CONTROL_BYTES;
        }

        traceEVENT_GROUP_SYNC_END( xEventGroup, uxBitsToSet, uxBitsToWaitFor, xTimeoutOccurred );

        ( void ) xTimeoutOccurred; /* 关闭跟踪宏时避免未使用变量告警。 */

        traceRETURN_xEventGroupSync( uxReturn );

        return uxReturn;
    }
/*-----------------------------------------------------------*/

    /*
     * 等待 uxBitsToWaitFor 中若干位：xWaitForAllBits 决定「任一位」或「全部位」；
     * xClearOnExit 决定在成功等到后是否自动清除这些位；xTicksToWait 为阻塞超时。
     */
    EventBits_t xEventGroupWaitBits( EventGroupHandle_t xEventGroup,
                                     const EventBits_t uxBitsToWaitFor,
                                     const BaseType_t xClearOnExit,
                                     const BaseType_t xWaitForAllBits,
                                     TickType_t xTicksToWait )
    {
        EventGroup_t * pxEventBits = xEventGroup;
        EventBits_t uxReturn, uxControlBits = 0;
        BaseType_t xWaitConditionMet, xAlreadyYielded;
        BaseType_t xTimeoutOccurred = pdFALSE;

        traceENTER_xEventGroupWaitBits( xEventGroup, uxBitsToWaitFor, xClearOnExit, xWaitForAllBits, xTicksToWait );

        /* 不得等待内核保留位；且 uxBitsToWaitFor 不能为 0。 */
        configASSERT( xEventGroup );
        configASSERT( ( uxBitsToWaitFor & eventEVENT_BITS_CONTROL_BYTES ) == 0 );
        configASSERT( uxBitsToWaitFor != 0 );
        #if ( ( INCLUDE_xTaskGetSchedulerState == 1 ) || ( configUSE_TIMERS == 1 ) )
        {
            configASSERT( !( ( xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED ) && ( xTicksToWait != 0 ) ) );
        }
        #endif

        vTaskSuspendAll();
        {
            const EventBits_t uxCurrentEventBits = pxEventBits->uxEventBits;

            /* 进入阻塞前先检查条件是否已满足（避免无谓阻塞）。 */
            xWaitConditionMet = prvTestWaitCondition( uxCurrentEventBits, uxBitsToWaitFor, xWaitForAllBits );

            if( xWaitConditionMet != pdFALSE )
            {
                /* 已满足：直接返回当前位图，不再阻塞。 */
                uxReturn = uxCurrentEventBits;
                xTicksToWait = ( TickType_t ) 0;

                /* 若要求在「成功等待」后清除，则此处清除所等待的位。 */
                if( xClearOnExit != pdFALSE )
                {
                    pxEventBits->uxEventBits &= ~uxBitsToWaitFor;
                }
                else
                {
                    mtCOVERAGE_TEST_MARKER();
                }
            }
            else if( xTicksToWait == ( TickType_t ) 0 )
            {
                /* 未满足且不阻塞：立即返回当前位值（常用于轮询式非阻塞等待）。 */
                uxReturn = uxCurrentEventBits;
                xTimeoutOccurred = pdTRUE;
            }
            else
            {
                /* 将本次调用的「退出时是否清除」「是否等全部位」编码进控制位，供唤醒路径使用。 */
                if( xClearOnExit != pdFALSE )
                {
                    uxControlBits |= eventCLEAR_EVENTS_ON_EXIT_BIT;
                }
                else
                {
                    mtCOVERAGE_TEST_MARKER();
                }

                if( xWaitForAllBits != pdFALSE )
                {
                    uxControlBits |= eventWAIT_FOR_ALL_BITS;
                }
                else
                {
                    mtCOVERAGE_TEST_MARKER();
                }

                /* 等待掩码 | 控制位 写入任务事件项，挂链并阻塞至置位或超时。 */
                vTaskPlaceOnUnorderedEventList( &( pxEventBits->xTasksWaitingForBits ), ( uxBitsToWaitFor | uxControlBits ), xTicksToWait );

                /* 同 Sync：消除编译器对 uxReturn 未赋值的误报。 */
                uxReturn = 0;

                traceEVENT_GROUP_WAIT_BITS_BLOCK( xEventGroup, uxBitsToWaitFor );
            }
        }
        xAlreadyYielded = xTaskResumeAll();

        if( xTicksToWait != ( TickType_t ) 0 )
        {
            if( xAlreadyYielded == pdFALSE )
            {
                taskYIELD_WITHIN_API();
            }
            else
            {
                mtCOVERAGE_TEST_MARKER();
            }

            /* 从阻塞返回：根据事件项判断是位置位唤醒还是超时。 */
            uxReturn = uxTaskResetEventItemValue();

            if( ( uxReturn & eventUNBLOCKED_DUE_TO_BIT_SET ) == ( EventBits_t ) 0 )
            {
                taskENTER_CRITICAL();
                {
                    /* 超时：读当前组值。 */
                    uxReturn = pxEventBits->uxEventBits;

                    /* 离开阻塞到进入临界区之间，其它任务可能已置位，若此时条件已满足且要求清除则补清。 */
                    if( prvTestWaitCondition( uxReturn, uxBitsToWaitFor, xWaitForAllBits ) != pdFALSE )
                    {
                        if( xClearOnExit != pdFALSE )
                        {
                            pxEventBits->uxEventBits &= ~uxBitsToWaitFor;
                        }
                        else
                        {
                            mtCOVERAGE_TEST_MARKER();
                        }
                    }
                    else
                    {
                        mtCOVERAGE_TEST_MARKER();
                    }

                    xTimeoutOccurred = pdTRUE;
                }
                taskEXIT_CRITICAL();
            }
            else
            {
                /* 因位匹配而唤醒。 */
            }

            /* 剥离内核控制位，仅返回事件位。 */
            uxReturn &= ~eventEVENT_BITS_CONTROL_BYTES;
        }

        traceEVENT_GROUP_WAIT_BITS_END( xEventGroup, uxBitsToWaitFor, xTimeoutOccurred );

        ( void ) xTimeoutOccurred; /* 关闭跟踪宏时避免未使用变量告警。 */

        traceRETURN_xEventGroupWaitBits( uxReturn );

        return uxReturn;
    }
/*-----------------------------------------------------------*/

    /* 在临界区内清除指定位；返回值为清除前的整组事件位（应用位不得含控制字节）。 */
    EventBits_t xEventGroupClearBits( EventGroupHandle_t xEventGroup,
                                      const EventBits_t uxBitsToClear )
    {
        EventGroup_t * pxEventBits = xEventGroup;
        EventBits_t uxReturn;

        traceENTER_xEventGroupClearBits( xEventGroup, uxBitsToClear );

        configASSERT( xEventGroup );
        /* 清除掩码不得包含内核用于链表项编码的控制字节。 */
        configASSERT( ( uxBitsToClear & eventEVENT_BITS_CONTROL_BYTES ) == 0 );

        taskENTER_CRITICAL();
        {
            traceEVENT_GROUP_CLEAR_BITS( xEventGroup, uxBitsToClear );

            /* 先保存清除前的位图作为返回值。 */
            uxReturn = pxEventBits->uxEventBits;

            /* 再按掩码清零。 */
            pxEventBits->uxEventBits &= ~uxBitsToClear;
        }
        taskEXIT_CRITICAL();

        traceRETURN_xEventGroupClearBits( uxReturn );

        return uxReturn;
    }
/*-----------------------------------------------------------*/

    #if ( ( INCLUDE_xTimerPendFunctionCall == 1 ) && ( configUSE_TIMERS == 1 ) )

        /*
         * ISR 内清位：将清除请求投递到定时器守护任务上下文执行，避免在中断里长时间持锁。
         */
        BaseType_t xEventGroupClearBitsFromISR( EventGroupHandle_t xEventGroup,
                                                const EventBits_t uxBitsToClear )
        {
            BaseType_t xReturn;

            traceENTER_xEventGroupClearBitsFromISR( xEventGroup, uxBitsToClear );

            traceEVENT_GROUP_CLEAR_BITS_FROM_ISR( xEventGroup, uxBitsToClear );
            xReturn = xTimerPendFunctionCallFromISR( &vEventGroupClearBitsCallback, ( void * ) xEventGroup, ( uint32_t ) uxBitsToClear, NULL );

            traceRETURN_xEventGroupClearBitsFromISR( xReturn );

            return xReturn;
        }

    #endif /* if ( ( INCLUDE_xTimerPendFunctionCall == 1 ) && ( configUSE_TIMERS == 1 ) ) */
/*-----------------------------------------------------------*/

    /* 在中断内安全读取当前事件位（短临界区，不做唤醒逻辑）。 */
    EventBits_t xEventGroupGetBitsFromISR( EventGroupHandle_t xEventGroup )
    {
        UBaseType_t uxSavedInterruptStatus;
        EventGroup_t const * const pxEventBits = xEventGroup;
        EventBits_t uxReturn;

        traceENTER_xEventGroupGetBitsFromISR( xEventGroup );

        /* MISRA Ref 4.7.1 [Return value shall be checked] */
        /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#dir-47 */
        /* coverity[misra_c_2012_directive_4_7_violation] */
        uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();
        {
            uxReturn = pxEventBits->uxEventBits;
        }
        taskEXIT_CRITICAL_FROM_ISR( uxSavedInterruptStatus );

        traceRETURN_xEventGroupGetBitsFromISR( uxReturn );

        return uxReturn;
    }
/*-----------------------------------------------------------*/

    /*
     * 置位 uxBitsToSet，并扫描等待链表：若某任务等待条件已满足则解除阻塞；
     * 根据各任务项上的控制位决定是否在本次调用末尾批量清除部分事件位。
     */
    EventBits_t xEventGroupSetBits( EventGroupHandle_t xEventGroup,
                                    const EventBits_t uxBitsToSet )
    {
        ListItem_t * pxListItem;
        ListItem_t * pxNext;
        ListItem_t const * pxListEnd;
        List_t const * pxList;
        EventBits_t uxBitsToClear = 0, uxBitsWaitedFor, uxControlBits, uxReturnBits;
        EventGroup_t * pxEventBits = xEventGroup;
        BaseType_t xMatchFound = pdFALSE;

        traceENTER_xEventGroupSetBits( xEventGroup, uxBitsToSet );

        configASSERT( xEventGroup );
        /* 应用层不得置位内核保留的控制字节。 */
        configASSERT( ( uxBitsToSet & eventEVENT_BITS_CONTROL_BYTES ) == 0 );

        pxList = &( pxEventBits->xTasksWaitingForBits );
        pxListEnd = listGET_END_MARKER( pxList );
        vTaskSuspendAll();
        {
            traceEVENT_GROUP_SET_BITS( xEventGroup, uxBitsToSet );

            pxListItem = listGET_HEAD_ENTRY( pxList );

            /* 先合并新置位。 */
            pxEventBits->uxEventBits |= uxBitsToSet;

            /* 遍历每个阻塞任务的事件链表项，检查其等待掩码是否已满足。 */
            while( pxListItem != pxListEnd )
            {
                pxNext = listGET_NEXT( pxListItem );
                uxBitsWaitedFor = listGET_LIST_ITEM_VALUE( pxListItem );
                xMatchFound = pdFALSE;

                /* 链表项 value 高字节携带控制标志，低部为应用等待的位掩码。 */
                uxControlBits = uxBitsWaitedFor & eventEVENT_BITS_CONTROL_BYTES;
                uxBitsWaitedFor &= ~eventEVENT_BITS_CONTROL_BYTES;

                if( ( uxControlBits & eventWAIT_FOR_ALL_BITS ) == ( EventBits_t ) 0 )
                {
                    /* 等待「任一位」：与结果非 0 即匹配。 */
                    if( ( uxBitsWaitedFor & pxEventBits->uxEventBits ) != ( EventBits_t ) 0 )
                    {
                        xMatchFound = pdTRUE;
                    }
                    else
                    {
                        mtCOVERAGE_TEST_MARKER();
                    }
                }
                else if( ( uxBitsWaitedFor & pxEventBits->uxEventBits ) == uxBitsWaitedFor )
                {
                    /* 等待「全部位」：掩码与当前位图按位与后须等于等待掩码本身。 */
                    xMatchFound = pdTRUE;
                }
                else
                {
                    /* 要求全部位仍未齐，继续留在等待链。 */
                }

                if( xMatchFound != pdFALSE )
                {
                    /* 匹配：若该任务要求唤醒后清除所等待位，则累加到 uxBitsToClear 稍后统一处理。 */
                    if( ( uxControlBits & eventCLEAR_EVENTS_ON_EXIT_BIT ) != ( EventBits_t ) 0 )
                    {
                        uxBitsToClear |= uxBitsWaitedFor;
                    }
                    else
                    {
                        mtCOVERAGE_TEST_MARKER();
                    }

                    /* 唤醒前把当前位图 OR 上 eventUNBLOCKED_DUE_TO_BIT_SET，便于 Wait/Sync 区分超时与匹配。 */
                    vTaskRemoveFromUnorderedEventList( pxListItem, pxEventBits->uxEventBits | eventUNBLOCKED_DUE_TO_BIT_SET );
                }

                /* 必须用迭代前保存的 pxNext：当前项可能已被移入就绪链，pxNext 仍指向原链下一项。 */
                pxListItem = pxNext;
            }

            /* 对所有要求「退出清除」的等待方，一次性清除对应事件位。 */
            pxEventBits->uxEventBits &= ~uxBitsToClear;

            /* 返回置位及唤醒处理完成后的位图快照。 */
            uxReturnBits = pxEventBits->uxEventBits;
        }
        ( void ) xTaskResumeAll();

        traceRETURN_xEventGroupSetBits( uxReturnBits );

        return uxReturnBits;
    }
/*-----------------------------------------------------------*/

    /* 删除事件组：先唤醒所有仍在等待的任务，再按分配方式决定是否释放内存。 */
    void vEventGroupDelete( EventGroupHandle_t xEventGroup )
    {
        EventGroup_t * pxEventBits = xEventGroup;
        const List_t * pxTasksWaitingForBits;

        traceENTER_vEventGroupDelete( xEventGroup );

        configASSERT( pxEventBits );

        pxTasksWaitingForBits = &( pxEventBits->xTasksWaitingForBits );

        vTaskSuspendAll();
        {
            traceEVENT_GROUP_DELETE( xEventGroup );

            while( listCURRENT_LIST_LENGTH( pxTasksWaitingForBits ) > ( UBaseType_t ) 0 )
            {
                /* 组即将销毁，被唤醒任务侧不会看到有效事件位（由上层语义处理）。 */
                configASSERT( pxTasksWaitingForBits->xListEnd.pxNext != ( const ListItem_t * ) &( pxTasksWaitingForBits->xListEnd ) );
                vTaskRemoveFromUnorderedEventList( pxTasksWaitingForBits->xListEnd.pxNext, eventUNBLOCKED_DUE_TO_BIT_SET );
            }
        }
        ( void ) xTaskResumeAll();

        #if ( ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) && ( configSUPPORT_STATIC_ALLOCATION == 0 ) )
        {
            /* 仅动态分配：必然由堆创建，直接释放。 */
            vPortFree( pxEventBits );
        }
        #elif ( ( configSUPPORT_DYNAMIC_ALLOCATION == 1 ) && ( configSUPPORT_STATIC_ALLOCATION == 1 ) )
        {
            /* 动静态并存：仅对堆上对象调用 vPortFree。 */
            if( pxEventBits->ucStaticallyAllocated == ( uint8_t ) pdFALSE )
            {
                vPortFree( pxEventBits );
            }
            else
            {
                mtCOVERAGE_TEST_MARKER();
            }
        }
        #endif /* configSUPPORT_DYNAMIC_ALLOCATION */

        traceRETURN_vEventGroupDelete();
    }
/*-----------------------------------------------------------*/

    #if ( configSUPPORT_STATIC_ALLOCATION == 1 )
        /* 若句柄对应静态创建的事件组，则通过输出参数返回其 StaticEventGroup_t 指针。 */
        BaseType_t xEventGroupGetStaticBuffer( EventGroupHandle_t xEventGroup,
                                               StaticEventGroup_t ** ppxEventGroupBuffer )
        {
            BaseType_t xReturn;
            EventGroup_t * pxEventBits = xEventGroup;

            traceENTER_xEventGroupGetStaticBuffer( xEventGroup, ppxEventGroupBuffer );

            configASSERT( pxEventBits );
            configASSERT( ppxEventGroupBuffer );

            #if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
            {
                /* 仅当 ucStaticallyAllocated 为真时返回缓冲区地址。 */
                if( pxEventBits->ucStaticallyAllocated == ( uint8_t ) pdTRUE )
                {
                    /* MISRA Ref 11.3.1 [Misaligned access] */
                    /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-113 */
                    /* coverity[misra_c_2012_rule_11_3_violation] */
                    *ppxEventGroupBuffer = ( StaticEventGroup_t * ) pxEventBits;
                    xReturn = pdTRUE;
                }
                else
                {
                    xReturn = pdFALSE;
                }
            }
            #else /* configSUPPORT_DYNAMIC_ALLOCATION */
            {
                /* 未启用动态分配时，句柄必定指向静态缓冲区。 */
                /* MISRA Ref 11.3.1 [Misaligned access] */
                /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-113 */
                /* coverity[misra_c_2012_rule_11_3_violation] */
                *ppxEventGroupBuffer = ( StaticEventGroup_t * ) pxEventBits;
                xReturn = pdTRUE;
            }
            #endif /* configSUPPORT_DYNAMIC_ALLOCATION */

            traceRETURN_xEventGroupGetStaticBuffer( xReturn );

            return xReturn;
        }
    #endif /* configSUPPORT_STATIC_ALLOCATION */
/*-----------------------------------------------------------*/

/* 内部回调：在定时器任务上下文中执行由 ISR 挂起的置位操作。 */
    void vEventGroupSetBitsCallback( void * pvEventGroup,
                                     uint32_t ulBitsToSet )
    {
        traceENTER_vEventGroupSetBitsCallback( pvEventGroup, ulBitsToSet );

        /* MISRA Ref 11.5.4 [Callback function parameter] */
        /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-115 */
        /* coverity[misra_c_2012_rule_11_5_violation] */
        ( void ) xEventGroupSetBits( pvEventGroup, ( EventBits_t ) ulBitsToSet );

        traceRETURN_vEventGroupSetBitsCallback();
    }
/*-----------------------------------------------------------*/

/* 内部回调：在定时器任务上下文中执行由 ISR 挂起的清位操作。 */
    void vEventGroupClearBitsCallback( void * pvEventGroup,
                                       uint32_t ulBitsToClear )
    {
        traceENTER_vEventGroupClearBitsCallback( pvEventGroup, ulBitsToClear );

        /* MISRA Ref 11.5.4 [Callback function parameter] */
        /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-115 */
        /* coverity[misra_c_2012_rule_11_5_violation] */
        ( void ) xEventGroupClearBits( pvEventGroup, ( EventBits_t ) ulBitsToClear );

        traceRETURN_vEventGroupClearBitsCallback();
    }
/*-----------------------------------------------------------*/

    static BaseType_t prvTestWaitCondition( const EventBits_t uxCurrentEventBits,
                                            const EventBits_t uxBitsToWaitFor,
                                            const BaseType_t xWaitForAllBits )
    {
        BaseType_t xWaitConditionMet = pdFALSE;

        if( xWaitForAllBits == pdFALSE )
        {
            /* 任一等待位已置位即满足。 */
            if( ( uxCurrentEventBits & uxBitsToWaitFor ) != ( EventBits_t ) 0 )
            {
                xWaitConditionMet = pdTRUE;
            }
            else
            {
                mtCOVERAGE_TEST_MARKER();
            }
        }
        else
        {
            /* uxBitsToWaitFor 中每一位均须在 uxCurrentEventBits 中为 1。 */
            if( ( uxCurrentEventBits & uxBitsToWaitFor ) == uxBitsToWaitFor )
            {
                xWaitConditionMet = pdTRUE;
            }
            else
            {
                mtCOVERAGE_TEST_MARKER();
            }
        }

        return xWaitConditionMet;
    }
/*-----------------------------------------------------------*/

    #if ( ( INCLUDE_xTimerPendFunctionCall == 1 ) && ( configUSE_TIMERS == 1 ) )

        /* ISR 内置位：通过挂起函数调用延后到定时器守护任务中执行 xEventGroupSetBits。 */
        BaseType_t xEventGroupSetBitsFromISR( EventGroupHandle_t xEventGroup,
                                              const EventBits_t uxBitsToSet,
                                              BaseType_t * pxHigherPriorityTaskWoken )
        {
            BaseType_t xReturn;

            traceENTER_xEventGroupSetBitsFromISR( xEventGroup, uxBitsToSet, pxHigherPriorityTaskWoken );

            traceEVENT_GROUP_SET_BITS_FROM_ISR( xEventGroup, uxBitsToSet );
            xReturn = xTimerPendFunctionCallFromISR( &vEventGroupSetBitsCallback, ( void * ) xEventGroup, ( uint32_t ) uxBitsToSet, pxHigherPriorityTaskWoken );

            traceRETURN_xEventGroupSetBitsFromISR( xReturn );

            return xReturn;
        }

    #endif /* if ( ( INCLUDE_xTimerPendFunctionCall == 1 ) && ( configUSE_TIMERS == 1 ) ) */
/*-----------------------------------------------------------*/

    #if ( configUSE_TRACE_FACILITY == 1 )

        /* 返回调试编号（未启用跟踪或未设置时可能为 0）。 */
        UBaseType_t uxEventGroupGetNumber( void * xEventGroup )
        {
            UBaseType_t xReturn;

            /* MISRA Ref 11.5.2 [Opaque pointer] */
            /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-115 */
            /* coverity[misra_c_2012_rule_11_5_violation] */
            EventGroup_t const * pxEventBits = ( EventGroup_t * ) xEventGroup;

            traceENTER_uxEventGroupGetNumber( xEventGroup );

            if( xEventGroup == NULL )
            {
                xReturn = 0;
            }
            else
            {
                xReturn = pxEventBits->uxEventGroupNumber;
            }

            traceRETURN_uxEventGroupGetNumber( xReturn );

            return xReturn;
        }

    #endif /* configUSE_TRACE_FACILITY */
/*-----------------------------------------------------------*/

    #if ( configUSE_TRACE_FACILITY == 1 )

        /* 为事件组设置调试/跟踪用编号。 */
        void vEventGroupSetNumber( void * xEventGroup,
                                   UBaseType_t uxEventGroupNumber )
        {
            traceENTER_vEventGroupSetNumber( xEventGroup, uxEventGroupNumber );

            /* MISRA Ref 11.5.2 [Opaque pointer] */
            /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-115 */
            /* coverity[misra_c_2012_rule_11_5_violation] */
            ( ( EventGroup_t * ) xEventGroup )->uxEventGroupNumber = uxEventGroupNumber;

            traceRETURN_vEventGroupSetNumber();
        }

    #endif /* configUSE_TRACE_FACILITY */
/*-----------------------------------------------------------*/

/* configUSE_EVENT_GROUPS != 1 时，本文件自首个 #if 起的实现均不编译。 */
#endif /* configUSE_EVENT_GROUPS == 1 */
