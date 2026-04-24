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
 * 内核双向链表实现：供任务就绪表、延时表、队列阻塞链表等复用。
 * 列表为环形，xListEnd 为哨兵结点，xItemValue 最大以保证有序插入时始终在末尾；
 * 详细 API 与宏见 list.h。
 */

#include <stdlib.h>

/* 本为 .c 实现文件：定义后包含 list.h，避免 task.h 将链表 API 包成 MPU 用户态桩函数。 */
#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE

#include "FreeRTOS.h"
#include "list.h"

/* 头文件包含结束后取消宏，保证本文件内符号按特权实现链接（MPU 端口要求）。 */
#undef MPU_WRAPPERS_INCLUDED_FROM_API_FILE

/*-----------------------------------------------------------
* 公开链表 API，完整文档见 list.h
*----------------------------------------------------------*/

/* 将链表置为空环：仅含 xListEnd 哨兵，pxIndex 指向哨兵，结点计数为 0。 */
void vListInitialise( List_t * const pxList )
{
    traceENTER_vListInitialise( pxList );

    /* xListEnd 既是「末尾标记」也是链表的一环；初始化后表中只有该哨兵结点。 */
    pxList->pxIndex = ( ListItem_t * ) &( pxList->xListEnd );

    listSET_FIRST_LIST_ITEM_INTEGRITY_CHECK_VALUE( &( pxList->xListEnd ) );

    /* 哨兵 xItemValue 取最大，有序插入时所有真实结点都在其前。 */
    pxList->xListEnd.xItemValue = portMAX_DELAY;

    /* 空表时哨兵前后指针均指向自身。 */
    pxList->xListEnd.pxNext = ( ListItem_t * ) &( pxList->xListEnd );
    pxList->xListEnd.pxPrevious = ( ListItem_t * ) &( pxList->xListEnd );

    /* 完整 ListItem_t 时初始化 xListEnd 的 owner/container 及第二道完整性校验。 */
    #if ( configUSE_MINI_LIST_ITEM == 0 )
    {
        pxList->xListEnd.pvOwner = NULL;
        pxList->xListEnd.pxContainer = NULL;
        listSET_SECOND_LIST_ITEM_INTEGRITY_CHECK_VALUE( &( pxList->xListEnd ) );
    }
    #endif

    pxList->uxNumberOfItems = ( UBaseType_t ) 0U;

    /* 若开启完整性校验，写入魔数便于检测内存踩踏。 */
    listSET_LIST_INTEGRITY_CHECK_1_VALUE( pxList );
    listSET_LIST_INTEGRITY_CHECK_2_VALUE( pxList );

    traceRETURN_vListInitialise();
}
/*-----------------------------------------------------------*/

/* 初始化链表项（未入链）：pxContainer 置空，可选写入完整性校验魔数。 */
void vListInitialiseItem( ListItem_t * const pxItem )
{
    traceENTER_vListInitialiseItem( pxItem );

    /* 表示该项尚未挂在任何 List_t 上。 */
    pxItem->pxContainer = NULL;

    listSET_FIRST_LIST_ITEM_INTEGRITY_CHECK_VALUE( pxItem );
    listSET_SECOND_LIST_ITEM_INTEGRITY_CHECK_VALUE( pxItem );

    traceRETURN_vListInitialiseItem();
}
/*-----------------------------------------------------------*/

/*
 * 将 pxNewListItem 插在 pxIndex 之前（环上「末尾」一侧），不按键值排序。
 * 就绪队列等同优先级 TCB 常用此接口，配合 listGET_OWNER_OF_NEXT_ENTRY 实现轮转。
 */
void vListInsertEnd( List_t * const pxList,
                     ListItem_t * const pxNewListItem )
{
    ListItem_t * const pxIndex = pxList->pxIndex;

    traceENTER_vListInsertEnd( pxList, pxNewListItem );

    /* 在定义了 configASSERT 且开启完整性检查时，可尽早发现链表结构被踩。 */
    listTEST_LIST_INTEGRITY( pxList );
    listTEST_LIST_ITEM_INTEGRITY( pxNewListItem );

    /* 新结点插在 pxIndex 与 pxIndex->pxPrevious 之间，成为「下次 NEXT 最后才轮到」的项。 */
    pxNewListItem->pxNext = pxIndex;
    pxNewListItem->pxPrevious = pxIndex->pxPrevious;

    /* Only used during decision coverage testing. */
    mtCOVERAGE_TEST_DELAY();

    pxIndex->pxPrevious->pxNext = pxNewListItem;
    pxIndex->pxPrevious = pxNewListItem;

    /* 记录所属链表，便于 uxListRemove  O(1) 摘除。 */
    pxNewListItem->pxContainer = pxList;

    ( pxList->uxNumberOfItems ) = ( UBaseType_t ) ( pxList->uxNumberOfItems + 1U );

    traceRETURN_vListInsertEnd();
}
/*-----------------------------------------------------------*/

/*
 * 按 pxNewListItem->xItemValue 升序插入；相等键时插在已有同键项之后，
 * 保证同优先级就绪链上各任务轮转公平。值为 portMAX_DELAY 时直接插在哨兵前，避免与哨兵比较死循环。
 */
void vListInsert( List_t * const pxList,
                  ListItem_t * const pxNewListItem )
{
    ListItem_t * pxIterator;
    const TickType_t xValueOfInsertion = pxNewListItem->xItemValue;

    traceENTER_vListInsert( pxList, pxNewListItem );

    listTEST_LIST_INTEGRITY( pxList );
    listTEST_LIST_ITEM_INTEGRITY( pxNewListItem );

    if( xValueOfInsertion == portMAX_DELAY )
    {
        pxIterator = pxList->xListEnd.pxPrevious;
    }
    else
    {
        /* *** NOTE ***********************************************************
         * 中文概要：若 for 循环无法退出，多为栈溢出、中断优先级配置错误、
         * 在不当上下文调用 API、未初始化对象或调度前误开中断等。详见下方英文与官方 FAQ。
        *  If you find your application is crashing here then likely causes are
        *  listed below.  In addition see https://www.freertos.org/Why-FreeRTOS/FAQs for
        *  more tips, and ensure configASSERT() is defined!
        *  https://www.FreeRTOS.org/a00110.html#configASSERT
        *
        *   1) Stack overflow -
        *      see https://www.FreeRTOS.org/Stacks-and-stack-overflow-checking.html
        *   2) Incorrect interrupt priority assignment, especially on Cortex-M
        *      parts where numerically high priority values denote low actual
        *      interrupt priorities, which can seem counter intuitive.  See
        *      https://www.FreeRTOS.org/RTOS-Cortex-M3-M4.html and the definition
        *      of configMAX_SYSCALL_INTERRUPT_PRIORITY on
        *      https://www.FreeRTOS.org/a00110.html
        *   3) Calling an API function from within a critical section or when
        *      the scheduler is suspended, or calling an API function that does
        *      not end in "FromISR" from an interrupt.
        *   4) Using a queue or semaphore before it has been initialised or
        *      before the scheduler has been started (are interrupts firing
        *      before vTaskStartScheduler() has been called?).
        *   5) If the FreeRTOS port supports interrupt nesting then ensure that
        *      the priority of the tick interrupt is at or below
        *      configMAX_SYSCALL_INTERRUPT_PRIORITY.
        **********************************************************************/

        /* 从哨兵出发，找到第一个 xItemValue 大于插入值的结点，新项插在其前。 */
        for( pxIterator = ( ListItem_t * ) &( pxList->xListEnd ); pxIterator->pxNext->xItemValue <= xValueOfInsertion; pxIterator = pxIterator->pxNext )
        {
            /* 循环体为空：仅移动迭代指针。卡死于此请看 NOTE 与官方 FAQ。 */
        }
    }

    pxNewListItem->pxNext = pxIterator->pxNext;
    pxNewListItem->pxNext->pxPrevious = pxNewListItem;
    pxNewListItem->pxPrevious = pxIterator;
    pxIterator->pxNext = pxNewListItem;

    /* 记录所属链表，供后续 O(1) 删除。 */
    pxNewListItem->pxContainer = pxList;

    ( pxList->uxNumberOfItems ) = ( UBaseType_t ) ( pxList->uxNumberOfItems + 1U );

    traceRETURN_vListInsert();
}
/*-----------------------------------------------------------*/

/* 从链表中摘除 pxItemToRemove，并返回该链表现在剩余结点个数。 */
UBaseType_t uxListRemove( ListItem_t * const pxItemToRemove )
{
    /* 通过 pxContainer 反查所在 List_t，无需调用方再传链表指针。 */
    List_t * const pxList = pxItemToRemove->pxContainer;

    traceENTER_uxListRemove( pxItemToRemove );

    pxItemToRemove->pxNext->pxPrevious = pxItemToRemove->pxPrevious;
    pxItemToRemove->pxPrevious->pxNext = pxItemToRemove->pxNext;

    /* Only used during decision coverage testing. */
    mtCOVERAGE_TEST_DELAY();

    /* 若当前遍历游标正指向被删结点，回退到前一项，避免悬空。 */
    if( pxList->pxIndex == pxItemToRemove )
    {
        pxList->pxIndex = pxItemToRemove->pxPrevious;
    }
    else
    {
        mtCOVERAGE_TEST_MARKER();
    }

    pxItemToRemove->pxContainer = NULL; /* 标记已不在任何链上。 */
    ( pxList->uxNumberOfItems ) = ( UBaseType_t ) ( pxList->uxNumberOfItems - 1U );

    traceRETURN_uxListRemove( pxList->uxNumberOfItems );

    return pxList->uxNumberOfItems; /* 删除后链表中剩余项数（不含哨兵计数语义见 list.h）。 */
}
/*-----------------------------------------------------------*/
