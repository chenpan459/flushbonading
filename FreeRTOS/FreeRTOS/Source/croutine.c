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
 * 本文件实现 FreeRTOS「协程」（Co-routine）调度：合作式多协程，由应用周期性调用
 * vCoRoutineSchedule() 推进；协程通过 crDELAY、队列 API 等主动让出执行权。
 * 需 configUSE_CO_ROUTINES=1；与抢占式任务（tasks.c）为两套机制，新设计通常优先任务。
 */

#include "FreeRTOS.h"
#include "task.h"
#include "croutine.h"

/* 未启用协程时整文件不参与编译。 */
#if ( configUSE_CO_ROUTINES != 0 )

/*
 * Some kernel aware debuggers require data to be viewed to be global, rather
 * than file scope.
 * 部分支持内核感知的调试器要求被观察的数据为全局作用域，故可通过端口宏去掉 static。
 */
    #ifdef portREMOVE_STATIC_QUALIFIER
        #define static
    #endif


/* 就绪与阻塞协程使用的链表。 ----------------------------------*/
    static List_t pxReadyCoRoutineLists[ configMAX_CO_ROUTINE_PRIORITIES ]; /**< 按优先级划分的就绪协程链表（每优先级一条）。 */
    static List_t xDelayedCoRoutineList1;                                   /**< 延时协程链表 1。 */
    static List_t xDelayedCoRoutineList2;                                   /**< 延时协程链表 2：tick 溢出时与当前延时表交换，避免唤醒时间比较错误。 */
    static List_t * pxDelayedCoRoutineList = NULL;                          /**< 当前使用的「正常」延时协程链表指针。 */
    static List_t * pxOverflowDelayedCoRoutineList = NULL;                  /**< 当前用于存放「唤醒时间已相对 tick 溢出」的延时协程链表指针。 */
    static List_t xPendingReadyCoRoutineList;                               /**< 由中断置为就绪的协程暂存区；ISR 不能直接改就绪表，故先入此表再由调度器移入就绪表。 */

/* 本文件内其它私有变量。 --------------------------------------*/
    CRCB_t * pxCurrentCoRoutine = NULL;                                     /**< 当前正在运行的协程控制块（可为空）。 */
    static UBaseType_t uxTopCoRoutineReadyPriority = ( UBaseType_t ) 0U;     /**< 就绪协程中的最高优先级（加速查找）。 */
    static TickType_t xCoRoutineTickCount = ( TickType_t ) 0U;              /**< 协程子系统维护的 tick 计数（与任务 tick 同步推进）。 */
    static TickType_t xLastTickCount = ( TickType_t ) 0U;                   /**< 上次检查延时链表时的任务 tick 基准。 */
    static TickType_t xPassedTicks = ( TickType_t ) 0U;                     /**< 自上次调度以来需推进的 tick 数。 */

/* 协程创建后的初始状态编号。 */
    #define corINITIAL_STATE    ( 0 )

/*
 * 将 pxCRCB 所指协程插入对应优先级的就绪链表尾部。
 * 会访问就绪链表，禁止在中断服务程序内使用。
 */
    #define prvAddCoRoutineToReadyQueue( pxCRCB )                                                                               \
    do {                                                                                                                        \
        if( ( pxCRCB )->uxPriority > uxTopCoRoutineReadyPriority )                                                              \
        {                                                                                                                       \
            uxTopCoRoutineReadyPriority = ( pxCRCB )->uxPriority;                                                               \
        }                                                                                                                       \
        vListInsertEnd( ( List_t * ) &( pxReadyCoRoutineLists[ ( pxCRCB )->uxPriority ] ), &( ( pxCRCB )->xGenericListItem ) ); \
    } while( 0 )

/* 初始化调度器使用的各链表；在创建第一个协程时自动调用。 */
    static void prvInitialiseCoRoutineLists( void );

/*
 * 中断内置就绪的协程不能直接进就绪表（无互斥）。先挂到 xPendingReadyCoRoutineList，
 * 再由协程调度器移入真正的就绪优先级链表。
 */
    static void prvCheckPendingReadyList( void );

/*
 * 扫描当前延时链表，将已到唤醒时刻的协程移入就绪表。
 * 链表按唤醒时间排序：一旦遇到尚未到期的协程，其后项均更晚，可结束本层循环。
 */
    static void prvCheckDelayedList( void );

/*-----------------------------------------------------------*/

    /*
     * 创建协程：分配 CRCB，初始化状态与链表项，加入对应优先级就绪队列。
     * pxCoRoutineCode 为协程函数体；uxIndex 为传入该函数的第二参数（用户自定义编号）。
     */
    BaseType_t xCoRoutineCreate( crCOROUTINE_CODE pxCoRoutineCode,
                                 UBaseType_t uxPriority,
                                 UBaseType_t uxIndex )
    {
        BaseType_t xReturn;
        CRCB_t * pxCoRoutine;

        traceENTER_xCoRoutineCreate( pxCoRoutineCode, uxPriority, uxIndex );

        /* 为协程控制块 CRCB 分配堆内存。 */
        /* MISRA Ref 11.5.1 [Malloc memory assignment] */
        /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-115 */
        /* coverity[misra_c_2012_rule_11_5_violation] */
        pxCoRoutine = ( CRCB_t * ) pvPortMalloc( sizeof( CRCB_t ) );

        if( pxCoRoutine )
        {
            /* 首个协程：pxCurrentCoRoutine 尚为空，需先初始化各链表再登记当前协程。 */
            if( pxCurrentCoRoutine == NULL )
            {
                pxCurrentCoRoutine = pxCoRoutine;
                prvInitialiseCoRoutineLists();
            }

            /* 优先级夹紧到合法范围。 */
            if( uxPriority >= configMAX_CO_ROUTINE_PRIORITIES )
            {
                uxPriority = configMAX_CO_ROUTINE_PRIORITIES - 1;
            }

            /* 用入参填写 CRCB 基本字段。 */
            pxCoRoutine->uxState = corINITIAL_STATE;
            pxCoRoutine->uxPriority = uxPriority;
            pxCoRoutine->uxIndex = uxIndex;
            pxCoRoutine->pxCoRoutineFunction = pxCoRoutineCode;

            /* 初始化通用链表项与事件链表项。 */
            vListInitialiseItem( &( pxCoRoutine->xGenericListItem ) );
            vListInitialiseItem( &( pxCoRoutine->xEventListItem ) );

            /* 链表项 owner 指回 CRCB，便于从 ListItem_t 反查所属协程。 */
            listSET_LIST_ITEM_OWNER( &( pxCoRoutine->xGenericListItem ), pxCoRoutine );
            listSET_LIST_ITEM_OWNER( &( pxCoRoutine->xEventListItem ), pxCoRoutine );

            /* 事件链表按优先级排序：数值越大优先级越高（与任务侧惯例一致）。 */
            listSET_LIST_ITEM_VALUE( &( pxCoRoutine->xEventListItem ), ( ( TickType_t ) configMAX_CO_ROUTINE_PRIORITIES - ( TickType_t ) uxPriority ) );

            /* 初始化完成，加入正确优先级的就绪队列尾部。 */
            prvAddCoRoutineToReadyQueue( pxCoRoutine );

            xReturn = pdPASS;
        }
        else
        {
            xReturn = errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY;
        }

        traceRETURN_xCoRoutineCreate( xReturn );

        return xReturn;
    }
/*-----------------------------------------------------------*/

    /*
     * 当前协程延时 xTicksToDelay：从就绪表移除，按唤醒时刻插入延时表（或溢出延时表）。
     * 若 pxEventList 非空，同时挂到该事件链表（此时要求关中断调用，与注释一致）。
     */
    void vCoRoutineAddToDelayedList( TickType_t xTicksToDelay,
                                     List_t * pxEventList )
    {
        TickType_t xTimeToWake;

        traceENTER_vCoRoutineAddToDelayedList( xTicksToDelay, pxEventList );

        /* 计算唤醒时刻；TickType_t 溢出由双延时表机制处理，此处允许回绕。 */
        xTimeToWake = xCoRoutineTickCount + xTicksToDelay;

        /* 同一 ListItem 不能同时挂在两个表：先离开就绪表再插入延时/阻塞表。 */
        ( void ) uxListRemove( ( ListItem_t * ) &( pxCurrentCoRoutine->xGenericListItem ) );

        /* 按唤醒时间有序插入。 */
        listSET_LIST_ITEM_VALUE( &( pxCurrentCoRoutine->xGenericListItem ), xTimeToWake );

        if( xTimeToWake < xCoRoutineTickCount )
        {
            /* 相对当前 tick 已「溢出」的唤醒时间，放入溢出延时表。 */
            vListInsert( ( List_t * ) pxOverflowDelayedCoRoutineList, ( ListItem_t * ) &( pxCurrentCoRoutine->xGenericListItem ) );
        }
        else
        {
            /* 未溢出，放入当前使用的正常延时表。 */
            vListInsert( ( List_t * ) pxDelayedCoRoutineList, ( ListItem_t * ) &( pxCurrentCoRoutine->xGenericListItem ) );
        }

        if( pxEventList )
        {
            /* 同时阻塞在某个事件队列上；调用方须保证关中断（与队列/信号量用法配套）。 */
            vListInsert( pxEventList, &( pxCurrentCoRoutine->xEventListItem ) );
        }

        traceRETURN_vCoRoutineAddToDelayedList();
    }
/*-----------------------------------------------------------*/

    /* 把 ISR 放入「待就绪」链表的协程逐个移入真正就绪优先级链表。 */
    static void prvCheckPendingReadyList( void )
    {
        /* 待就绪链由 ISR 与调度器共享：摘头项时短暂关中断。 */
        while( listLIST_IS_EMPTY( &xPendingReadyCoRoutineList ) == pdFALSE )
        {
            CRCB_t * pxUnblockedCRCB;

            /* 临界区内只从待就绪表取下事件项，避免与 ISR 并发撕裂链表。 */
            portDISABLE_INTERRUPTS();
            {
                pxUnblockedCRCB = ( CRCB_t * ) listGET_OWNER_OF_HEAD_ENTRY( ( &xPendingReadyCoRoutineList ) );
                ( void ) uxListRemove( &( pxUnblockedCRCB->xEventListItem ) );
            }
            portENABLE_INTERRUPTS();

            /* 若仍在某延时/阻塞链上，此处移除通用项后加入就绪表。 */
            ( void ) uxListRemove( &( pxUnblockedCRCB->xGenericListItem ) );
            prvAddCoRoutineToReadyQueue( pxUnblockedCRCB );
        }
    }
/*-----------------------------------------------------------*/

    /* 根据任务调度器 tick 推进协程 tick，并处理到期的延时协程。 */
    static void prvCheckDelayedList( void )
    {
        CRCB_t * pxCRCB;

        /* 与 xTaskGetTickCount() 对齐，补算自上次以来经过的 tick 数。 */
        xPassedTicks = xTaskGetTickCount() - xLastTickCount;

        while( xPassedTicks )
        {
            xCoRoutineTickCount++;
            xPassedTicks--;

            /* 协程 tick 从最大值回绕到 0：交换「当前延时表」与「溢出延时表」。 */
            if( xCoRoutineTickCount == 0 )
            {
                List_t * pxTemp;

                /* 回绕瞬间当前延时表应为空；否则说明链表状态异常。 */
                pxTemp = pxDelayedCoRoutineList;
                pxDelayedCoRoutineList = pxOverflowDelayedCoRoutineList;
                pxOverflowDelayedCoRoutineList = pxTemp;
            }

            /* 本 tick 内检查是否有延时到期（表头即最早唤醒时刻）。 */
            while( listLIST_IS_EMPTY( pxDelayedCoRoutineList ) == pdFALSE )
            {
                pxCRCB = ( CRCB_t * ) listGET_OWNER_OF_HEAD_ENTRY( pxDelayedCoRoutineList );

                if( xCoRoutineTickCount < listGET_LIST_ITEM_VALUE( &( pxCRCB->xGenericListItem ) ) )
                {
                    /* 表头仍未到唤醒时刻，后续项更晚，无需继续扫描。 */
                    break;
                }

                portDISABLE_INTERRUPTS();
                {
                    /* 临界区前一刻可能已被 ISR 改挂待就绪表；此处移除 generic 项仍安全，
                     * 若已不在事件链上则 pxContainer 为 NULL，不重复移除。 */
                    ( void ) uxListRemove( &( pxCRCB->xGenericListItem ) );

                    /* 若还在某事件链表上，一并摘除。 */
                    if( pxCRCB->xEventListItem.pxContainer )
                    {
                        ( void ) uxListRemove( &( pxCRCB->xEventListItem ) );
                    }
                }
                portENABLE_INTERRUPTS();

                prvAddCoRoutineToReadyQueue( pxCRCB );
            }
        }

        xLastTickCount = xCoRoutineTickCount;
    }
/*-----------------------------------------------------------*/

    /*
     * 协程调度入口：应由应用主循环或某任务反复调用。
     * 处理待就绪与延时到期后，在最高非空就绪优先级上轮转执行一个协程体。
     */
    void vCoRoutineSchedule( void )
    {
        traceENTER_vCoRoutineSchedule();

        /* pxDelayedCoRoutineList 非空表示已完成 prvInitialiseCoRoutineLists（首个协程创建时调用）。 */
        if( pxDelayedCoRoutineList != NULL )
        {
            /* ISR 唤醒的协程：从待就绪表并入就绪表。 */
            prvCheckPendingReadyList();

            /* 推进协程 tick 并唤醒到期延时协程。 */
            prvCheckDelayedList();

            /* 自记录的最高优先级向下找到第一个非空就绪队列。 */
            while( listLIST_IS_EMPTY( &( pxReadyCoRoutineLists[ uxTopCoRoutineReadyPriority ] ) ) )
            {
                if( uxTopCoRoutineReadyPriority == 0 )
                {
                    /* 无任何就绪协程。 */
                    return;
                }

                --uxTopCoRoutineReadyPriority;
            }

            /* 同优先级内用 listGET_OWNER_OF_NEXT_ENTRY 轮转，近似时间片公平。 */
            listGET_OWNER_OF_NEXT_ENTRY( pxCurrentCoRoutine, &( pxReadyCoRoutineLists[ uxTopCoRoutineReadyPriority ] ) );

            /* 调用协程函数（合作式：函数内通过 cr 宏让出）。 */
            ( pxCurrentCoRoutine->pxCoRoutineFunction )( pxCurrentCoRoutine, pxCurrentCoRoutine->uxIndex );
        }

        traceRETURN_vCoRoutineSchedule();
    }
/*-----------------------------------------------------------*/

    /* 初始化就绪数组、双延时表与待就绪表，并设定当前/溢出延时表指针。 */
    static void prvInitialiseCoRoutineLists( void )
    {
        UBaseType_t uxPriority;

        for( uxPriority = 0; uxPriority < configMAX_CO_ROUTINE_PRIORITIES; uxPriority++ )
        {
            vListInitialise( ( List_t * ) &( pxReadyCoRoutineLists[ uxPriority ] ) );
        }

        vListInitialise( ( List_t * ) &xDelayedCoRoutineList1 );
        vListInitialise( ( List_t * ) &xDelayedCoRoutineList2 );
        vListInitialise( ( List_t * ) &xPendingReadyCoRoutineList );

        /* 初始：表 1 为当前延时表，表 2 为溢出延时表；tick 回绕时二者互换。 */
        pxDelayedCoRoutineList = &xDelayedCoRoutineList1;
        pxOverflowDelayedCoRoutineList = &xDelayedCoRoutineList2;
    }
/*-----------------------------------------------------------*/

    /*
     * 在中断内从事件链表唤醒队首协程：从事件表移除，挂入待就绪表（非直接进就绪表）。
     * 调用方已保证 pxEventList 非空。返回值表示是否应请求上下文切换（与当前协程优先级比较）。
     */
    BaseType_t xCoRoutineRemoveFromEventList( const List_t * pxEventList )
    {
        CRCB_t * pxUnblockedCRCB;
        BaseType_t xReturn;

        traceENTER_xCoRoutineRemoveFromEventList( pxEventList );

        /* 仅操作事件链与待就绪链；不能直接操作各优先级就绪链表。 */
        pxUnblockedCRCB = ( CRCB_t * ) listGET_OWNER_OF_HEAD_ENTRY( pxEventList );
        ( void ) uxListRemove( &( pxUnblockedCRCB->xEventListItem ) );
        vListInsertEnd( ( List_t * ) &( xPendingReadyCoRoutineList ), &( pxUnblockedCRCB->xEventListItem ) );

        /* 被唤醒者优先级不低于当前运行协程时，通常需要尽快切换（由上层 port 决定）。 */
        if( pxUnblockedCRCB->uxPriority >= pxCurrentCoRoutine->uxPriority )
        {
            xReturn = pdTRUE;
        }
        else
        {
            xReturn = pdFALSE;
        }

        traceRETURN_xCoRoutineRemoveFromEventList( xReturn );

        return xReturn;
    }
/*-----------------------------------------------------------*/

/*
 * 复位本文件内静态状态；正常上电时由首次创建协程路径初始化。
 * 应用若在重启调度器前需清空协程子系统，应调用本函数。
 */
    void vCoRoutineResetState( void )
    {
        /* 延时链表指针置空，下次创建首个协程时会重新 prvInitialiseCoRoutineLists。 */
        pxDelayedCoRoutineList = NULL;
        pxOverflowDelayedCoRoutineList = NULL;

        /* 其余文件级变量恢复初值。 */
        pxCurrentCoRoutine = NULL;
        uxTopCoRoutineReadyPriority = ( UBaseType_t ) 0U;
        xCoRoutineTickCount = ( TickType_t ) 0U;
        xLastTickCount = ( TickType_t ) 0U;
        xPassedTicks = ( TickType_t ) 0U;
    }
/*-----------------------------------------------------------*/

#endif /* configUSE_CO_ROUTINES：为 0 时上方代码不编译 */
