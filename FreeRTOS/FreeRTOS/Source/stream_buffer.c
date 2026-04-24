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
 * 流缓冲（Stream Buffer）与消息缓冲（Message Buffer）实现。
 *
 * 环形字节区用 xHead（下一写入下标）、xTail（下一读出下标）与 xLength 描述；为区分「满」与「空」，
 * 逻辑上最多只使用 xLength-1 字节有效载荷，故 xStreamBufferSpacesAvailable 会比物理长度少 1。
 *
 * 阻塞同步依赖任务通知（configUSE_TASK_NOTIFICATIONS）：发送/接收在等空间或等数据时挂起自身句柄
 * 到 xTaskWaitingToSend / xTaskWaitingToReceive，对方在数据量跨越 xTriggerLevelBytes 时通过
 * sbRECEIVE_COMPLETED / sbSEND_COMPLETED（或可选实例回调）发出 xTaskNotify。
 *
 * 消息缓冲在载荷前多存 configMESSAGE_BUFFER_LENGTH_TYPE 表示的单条长度；流缓冲为连续字节流可部分读写。
 * 批处理缓冲（batching）通过 ucFlags 与接收侧「至少凑满 trigger 才返回」语义配合。
 */

/* Standard includes. */
#include <string.h>

/* 内核实现文件：避免 task.h 将本模块 API 展开为 MPU 用户态包装。 */
#define MPU_WRAPPERS_INCLUDED_FROM_API_FILE

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "stream_buffer.h"

/* 头文件包含结束后取消宏，MPU 端口下本文件按特权实现链接。 */
#undef MPU_WRAPPERS_INCLUDED_FROM_API_FILE

/* configUSE_STREAM_BUFFERS==0 时整文件不编译（见文末 #endif）。 */
#if ( configUSE_STREAM_BUFFERS == 1 )

    #if ( configUSE_TASK_NOTIFICATIONS != 1 )
        #error configUSE_TASK_NOTIFICATIONS must be set to 1 to build stream_buffer.c
    #endif

    #if ( INCLUDE_xTaskGetCurrentTaskHandle != 1 )
        #error INCLUDE_xTaskGetCurrentTaskHandle must be set to 1 to build stream_buffer.c
    #endif

/*
 * 默认「接收完成」通知：挂起调度后，若有人在等发送（缓冲区曾满），对其 xTaskNotifyIndexed 唤醒，
 * 并清空 xTaskWaitingToSend。应用可在包含 stream_buffer.h 前自定义 sbRECEIVE_COMPLETED 覆盖。
 */
    #ifndef sbRECEIVE_COMPLETED
        #define sbRECEIVE_COMPLETED( pxStreamBuffer )                                 \
    do                                                                                \
    {                                                                                 \
        vTaskSuspendAll();                                                            \
        {                                                                             \
            if( ( pxStreamBuffer )->xTaskWaitingToSend != NULL )                      \
            {                                                                         \
                ( void ) xTaskNotifyIndexed( ( pxStreamBuffer )->xTaskWaitingToSend,  \
                                             ( pxStreamBuffer )->uxNotificationIndex, \
                                             ( uint32_t ) 0,                          \
                                             eNoAction );                             \
                ( pxStreamBuffer )->xTaskWaitingToSend = NULL;                        \
            }                                                                         \
        }                                                                             \
        ( void ) xTaskResumeAll();                                                    \
    } while( 0 )
    #endif /* sbRECEIVE_COMPLETED */

    /* configUSE_SB_COMPLETED_CALLBACK==1 时优先调实例的 pxReceiveCompletedCallback，否则走 sbRECEIVE_COMPLETED。 */
    #if ( configUSE_SB_COMPLETED_CALLBACK == 1 )
        #define prvRECEIVE_COMPLETED( pxStreamBuffer )                                           \
    do {                                                                                         \
        if( ( pxStreamBuffer )->pxReceiveCompletedCallback != NULL )                             \
        {                                                                                        \
            ( pxStreamBuffer )->pxReceiveCompletedCallback( ( pxStreamBuffer ), pdFALSE, NULL ); \
        }                                                                                        \
        else                                                                                     \
        {                                                                                        \
            sbRECEIVE_COMPLETED( ( pxStreamBuffer ) );                                           \
        }                                                                                        \
    } while( 0 )
    #else /* if ( configUSE_SB_COMPLETED_CALLBACK == 1 ) */
        #define prvRECEIVE_COMPLETED( pxStreamBuffer )    sbRECEIVE_COMPLETED( ( pxStreamBuffer ) )
    #endif /* if ( configUSE_SB_COMPLETED_CALLBACK == 1 ) */

    /* ISR 版接收完成：在 taskENTER_CRITICAL_FROM_ISR 内通知等发送任务，并写回 pxHigherPriorityTaskWoken。 */
    #ifndef sbRECEIVE_COMPLETED_FROM_ISR
        #define sbRECEIVE_COMPLETED_FROM_ISR( pxStreamBuffer,                                \
                                              pxHigherPriorityTaskWoken )                    \
    do {                                                                                     \
        UBaseType_t uxSavedInterruptStatus;                                                  \
                                                                                             \
        uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();                              \
        {                                                                                    \
            if( ( pxStreamBuffer )->xTaskWaitingToSend != NULL )                             \
            {                                                                                \
                ( void ) xTaskNotifyIndexedFromISR( ( pxStreamBuffer )->xTaskWaitingToSend,  \
                                                    ( pxStreamBuffer )->uxNotificationIndex, \
                                                    ( uint32_t ) 0,                          \
                                                    eNoAction,                               \
                                                    ( pxHigherPriorityTaskWoken ) );         \
                ( pxStreamBuffer )->xTaskWaitingToSend = NULL;                               \
            }                                                                                \
        }                                                                                    \
        taskEXIT_CRITICAL_FROM_ISR( uxSavedInterruptStatus );                                \
    } while( 0 )
    #endif /* sbRECEIVE_COMPLETED_FROM_ISR */

    #if ( configUSE_SB_COMPLETED_CALLBACK == 1 )
        /* ISR 路径：有实例回调则调回调（传入 FromISR 标志），否则默认通知宏。 */
        #define prvRECEIVE_COMPLETED_FROM_ISR( pxStreamBuffer,                                                           \
                                               pxHigherPriorityTaskWoken )                                               \
    do {                                                                                                                 \
        if( ( pxStreamBuffer )->pxReceiveCompletedCallback != NULL )                                                     \
        {                                                                                                                \
            ( pxStreamBuffer )->pxReceiveCompletedCallback( ( pxStreamBuffer ), pdTRUE, ( pxHigherPriorityTaskWoken ) ); \
        }                                                                                                                \
        else                                                                                                             \
        {                                                                                                                \
            sbRECEIVE_COMPLETED_FROM_ISR( ( pxStreamBuffer ), ( pxHigherPriorityTaskWoken ) );                           \
        }                                                                                                                \
    } while( 0 )
    #else /* if ( configUSE_SB_COMPLETED_CALLBACK == 1 ) */
        #define prvRECEIVE_COMPLETED_FROM_ISR( pxStreamBuffer, pxHigherPriorityTaskWoken ) \
    sbRECEIVE_COMPLETED_FROM_ISR( ( pxStreamBuffer ), ( pxHigherPriorityTaskWoken ) )
    #endif /* if ( configUSE_SB_COMPLETED_CALLBACK == 1 ) */

/*
 * 默认「发送完成」通知：若有任务因缓冲区空而阻塞在接收侧（xTaskWaitingToReceive），
 * 通过 xTaskNotifyIndexed 唤醒。注意宏体未用 do{}while(0) 包裹，与官方一致。
 */
    #ifndef sbSEND_COMPLETED
        #define sbSEND_COMPLETED( pxStreamBuffer )                                  \
    vTaskSuspendAll();                                                              \
    {                                                                               \
        if( ( pxStreamBuffer )->xTaskWaitingToReceive != NULL )                     \
        {                                                                           \
            ( void ) xTaskNotifyIndexed( ( pxStreamBuffer )->xTaskWaitingToReceive, \
                                         ( pxStreamBuffer )->uxNotificationIndex,   \
                                         ( uint32_t ) 0,                            \
                                         eNoAction );                               \
            ( pxStreamBuffer )->xTaskWaitingToReceive = NULL;                       \
        }                                                                           \
    }                                                                               \
    ( void ) xTaskResumeAll()
    #endif /* sbSEND_COMPLETED */

    #if ( configUSE_SB_COMPLETED_CALLBACK == 1 )
        #define prvSEND_COMPLETED( pxStreamBuffer )                                           \
    do {                                                                                      \
        if( ( pxStreamBuffer )->pxSendCompletedCallback != NULL )                             \
        {                                                                                     \
            ( pxStreamBuffer )->pxSendCompletedCallback( ( pxStreamBuffer ), pdFALSE, NULL ); \
        }                                                                                     \
        else                                                                                  \
        {                                                                                     \
            sbSEND_COMPLETED( ( pxStreamBuffer ) );                                           \
        }                                                                                     \
    } while( 0 )
    #else /* if ( configUSE_SB_COMPLETED_CALLBACK == 1 ) */
        #define prvSEND_COMPLETED( pxStreamBuffer )    sbSEND_COMPLETED( ( pxStreamBuffer ) )
    #endif /* if ( configUSE_SB_COMPLETED_CALLBACK == 1 ) */

    /* ISR 内发送完成：临界区内通知 xTaskWaitingToReceive。 */
    #ifndef sbSEND_COMPLETE_FROM_ISR
        #define sbSEND_COMPLETE_FROM_ISR( pxStreamBuffer, pxHigherPriorityTaskWoken )          \
    do {                                                                                       \
        UBaseType_t uxSavedInterruptStatus;                                                    \
                                                                                               \
        uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();                                \
        {                                                                                      \
            if( ( pxStreamBuffer )->xTaskWaitingToReceive != NULL )                            \
            {                                                                                  \
                ( void ) xTaskNotifyIndexedFromISR( ( pxStreamBuffer )->xTaskWaitingToReceive, \
                                                    ( pxStreamBuffer )->uxNotificationIndex,   \
                                                    ( uint32_t ) 0,                            \
                                                    eNoAction,                                 \
                                                    ( pxHigherPriorityTaskWoken ) );           \
                ( pxStreamBuffer )->xTaskWaitingToReceive = NULL;                              \
            }                                                                                  \
        }                                                                                      \
        taskEXIT_CRITICAL_FROM_ISR( uxSavedInterruptStatus );                                  \
    } while( 0 )
    #endif /* sbSEND_COMPLETE_FROM_ISR */

    #if ( configUSE_SB_COMPLETED_CALLBACK == 1 )
        #define prvSEND_COMPLETE_FROM_ISR( pxStreamBuffer, pxHigherPriorityTaskWoken )                                \
    do {                                                                                                              \
        if( ( pxStreamBuffer )->pxSendCompletedCallback != NULL )                                                     \
        {                                                                                                             \
            ( pxStreamBuffer )->pxSendCompletedCallback( ( pxStreamBuffer ), pdTRUE, ( pxHigherPriorityTaskWoken ) ); \
        }                                                                                                             \
        else                                                                                                          \
        {                                                                                                             \
            sbSEND_COMPLETE_FROM_ISR( ( pxStreamBuffer ), ( pxHigherPriorityTaskWoken ) );                            \
        }                                                                                                             \
    } while( 0 )
    #else /* if ( configUSE_SB_COMPLETED_CALLBACK == 1 ) */
        #define prvSEND_COMPLETE_FROM_ISR( pxStreamBuffer, pxHigherPriorityTaskWoken ) \
    sbSEND_COMPLETE_FROM_ISR( ( pxStreamBuffer ), ( pxHigherPriorityTaskWoken ) )
    #endif /* if ( configUSE_SB_COMPLETED_CALLBACK == 1 ) */

    /* 消息缓冲中每条消息前前缀的长度字段所占字节数。 */
    #define sbBYTES_TO_STORE_MESSAGE_LENGTH    ( sizeof( configMESSAGE_BUFFER_LENGTH_TYPE ) )

    /* ucFlags 位标志：消息缓冲 / 静态分配 / 批处理缓冲（接收需达到 trigger 才解除阻塞）。 */
    #define sbFLAGS_IS_MESSAGE_BUFFER          ( ( uint8_t ) 1 ) /* 离散消息模式（带长度前缀）。 */
    #define sbFLAGS_IS_STATICALLY_ALLOCATED    ( ( uint8_t ) 2 ) /* 控制块或存储由用户提供，删除时不释放。 */
    #define sbFLAGS_IS_BATCHING_BUFFER         ( ( uint8_t ) 4 ) /* 批处理流缓冲：接收侧与 trigger 配合（原文 exceededs 为笔误）。 */

/*-----------------------------------------------------------*/

typedef struct StreamBufferDef_t
{
    volatile size_t xTail;                       /**< 下一读出位置下标（含未提交链式读时的临时游标）。 */
    volatile size_t xHead;                       /**< 下一写入位置下标；真正提交写后再赋回结构体。 */
    size_t xLength;                              /**< pucBuffer 长度（环形模数）；有效载荷最多 xLength-1。 */
    size_t xTriggerLevelBytes;                   /**< 已缓冲字节数 ≥ 该值时才 prvSEND_COMPLETED 唤醒等接收方。 */
    volatile TaskHandle_t xTaskWaitingToReceive; /**< 因数据不足而阻塞读的任务；无则 NULL。 */
    volatile TaskHandle_t xTaskWaitingToSend;    /**< 因空间不足而阻塞写的任务（消息缓冲满时）；无则 NULL。 */
    uint8_t * pucBuffer;                         /**< 环形数据区首指针。 */
    uint8_t ucFlags;                              /**< sbFLAGS_* 组合。 */

    #if ( configUSE_TRACE_FACILITY == 1 )
        UBaseType_t uxStreamBufferNumber; /**< 调试用编号。 */
    #endif

    #if ( configUSE_SB_COMPLETED_CALLBACK == 1 )
        StreamBufferCallbackFunction_t pxSendCompletedCallback;    /**< 发送侧完成回调；NULL 则用 sbSEND_COMPLETED。 */
        StreamBufferCallbackFunction_t pxReceiveCompletedCallback; /**< 接收侧完成回调；NULL 则用 sbRECEIVE_COMPLETED。 */
    #endif
    UBaseType_t uxNotificationIndex; /**< xTaskNotify 使用的通知数组下标，默认 tskDEFAULT_INDEX_TO_NOTIFY。 */
} StreamBuffer_t;

/* 计算 [xTail, xHead) 环形区间内可读字节数（0…xLength-1）。 */
static size_t prvBytesInBuffer( const StreamBuffer_t * const pxStreamBuffer ) PRIVILEGED_FUNCTION;

/*
 * 从 pucData 拷贝 xCount 字节到环形区，从逻辑写指针 xHead 起绕环写；
 * 不修改 pxStreamBuffer->xHead，便于消息缓冲「先写长度再写载荷」两阶段原子提交。
 * 返回值：写完后的下一写位置；调用方最后赋给 pxStreamBuffer->xHead。
 */
static size_t prvWriteBytesToBuffer( StreamBuffer_t * const pxStreamBuffer,
                                     const uint8_t * pucData,
                                     size_t xCount,
                                     size_t xHead ) PRIVILEGED_FUNCTION;

/*
 * 消息模式：先读出长度前缀，再读载荷，且受 xBufferLengthBytes 限制；流模式：在 xBytesAvailable 内尽量多读。
 * 实际搬运由 prvReadBytesFromBuffer 完成，最后一次性更新 xTail。
 */
static size_t prvReadMessageFromBuffer( StreamBuffer_t * pxStreamBuffer,
                                        void * pvRxData,
                                        size_t xBufferLengthBytes,
                                        size_t xBytesAvailable ) PRIVILEGED_FUNCTION;

/*
 * 消息模式：空间够则先链式写入长度再写数据；流模式：写入不超过 xSpace 的尽可能多的字节。
 * 返回本次实际写入载荷字节数（不含长度前缀字节）；内部调用 prvWriteBytesToBuffer。
 */
static size_t prvWriteMessageToBuffer( StreamBuffer_t * const pxStreamBuffer,
                                       const void * pvTxData,
                                       size_t xDataLengthBytes,
                                       size_t xSpace,
                                       size_t xRequiredSpace ) PRIVILEGED_FUNCTION;

/*
 * 从 xTail 起绕环读出 xCount 字节到 pucData；不立即改 pxStreamBuffer->xTail，便于消息模式两阶段读。
 * 返回读完后下一读位置，由调用方赋给 xTail。
 */
static size_t prvReadBytesFromBuffer( StreamBuffer_t * pxStreamBuffer,
                                      uint8_t * pucData,
                                      size_t xCount,
                                      size_t xTail ) PRIVILEGED_FUNCTION;

/* 由 GenericCreate / GenericCreateStatic 调用：清零控制块、挂接缓冲区与 trigger、通知下标等。 */
static void prvInitialiseNewStreamBuffer( StreamBuffer_t * const pxStreamBuffer,
                                          uint8_t * const pucBuffer,
                                          size_t xBufferSizeBytes,
                                          size_t xTriggerLevelBytes,
                                          uint8_t ucFlags,
                                          StreamBufferCallbackFunction_t pxSendCompletedCallback,
                                          StreamBufferCallbackFunction_t pxReceiveCompletedCallback ) PRIVILEGED_FUNCTION;

/*-----------------------------------------------------------*/
    #if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
    /*
     * 一次 pvPortMalloc 分配 StreamBuffer_t + 环形区，总长 xBufferSizeBytes+1+sizeof(StreamBuffer_t) 的「+1」
     * 用于实现上使「可报告空闲」与直觉一致（官方注释所述 quirk）。
     */
    StreamBufferHandle_t xStreamBufferGenericCreate( size_t xBufferSizeBytes,
                                                     size_t xTriggerLevelBytes,
                                                     BaseType_t xStreamBufferType,
                                                     StreamBufferCallbackFunction_t pxSendCompletedCallback,
                                                     StreamBufferCallbackFunction_t pxReceiveCompletedCallback )
    {
        void * pvAllocatedMemory;
        uint8_t ucFlags;

        traceENTER_xStreamBufferGenericCreate( xBufferSizeBytes, xTriggerLevelBytes, xStreamBufferType, pxSendCompletedCallback, pxReceiveCompletedCallback );

        if( xStreamBufferType == sbTYPE_MESSAGE_BUFFER )
        {
            /* 消息缓冲：须至少容纳一条长度前缀。 */
            ucFlags = sbFLAGS_IS_MESSAGE_BUFFER;
            configASSERT( xBufferSizeBytes > sbBYTES_TO_STORE_MESSAGE_LENGTH );
        }
        else if( xStreamBufferType == sbTYPE_STREAM_BATCHING_BUFFER )
        {
            ucFlags = sbFLAGS_IS_BATCHING_BUFFER;
            configASSERT( xBufferSizeBytes > 0 );
        }
        else
        {
            /* 普通流缓冲。 */
            ucFlags = 0;
            configASSERT( xBufferSizeBytes > 0 );
        }

        configASSERT( xTriggerLevelBytes <= xBufferSizeBytes );

        /* trigger==0 会在空缓冲时也满足「已达触发」语义，故强制为 1。 */
        if( xTriggerLevelBytes == ( size_t ) 0 )
        {
            xTriggerLevelBytes = ( size_t ) 1;
        }

        if( xBufferSizeBytes < ( xBufferSizeBytes + 1U + sizeof( StreamBuffer_t ) ) )
        {
            xBufferSizeBytes++;
            pvAllocatedMemory = pvPortMalloc( xBufferSizeBytes + sizeof( StreamBuffer_t ) );
        }
        else
        {
            pvAllocatedMemory = NULL;
        }

        if( pvAllocatedMemory != NULL )
        {
            /* MISRA Ref 11.5.1 [Malloc memory assignment] */
            /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-115 */
            /* coverity[misra_c_2012_rule_11_5_violation] */
            prvInitialiseNewStreamBuffer( ( StreamBuffer_t * ) pvAllocatedMemory,                         /* Structure at the start of the allocated memory. */
                                                                                                          /* MISRA Ref 11.5.1 [Malloc memory assignment] */
                                                                                                          /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-115 */
                                                                                                          /* coverity[misra_c_2012_rule_11_5_violation] */
                                          ( ( uint8_t * ) pvAllocatedMemory ) + sizeof( StreamBuffer_t ), /* Storage area follows. */
                                          xBufferSizeBytes,
                                          xTriggerLevelBytes,
                                          ucFlags,
                                          pxSendCompletedCallback,
                                          pxReceiveCompletedCallback );

            traceSTREAM_BUFFER_CREATE( ( ( StreamBuffer_t * ) pvAllocatedMemory ), xStreamBufferType );
        }
        else
        {
            traceSTREAM_BUFFER_CREATE_FAILED( xStreamBufferType );
        }

        traceRETURN_xStreamBufferGenericCreate( pvAllocatedMemory );

        /* MISRA Ref 11.5.1 [Malloc memory assignment] */
        /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-115 */
        /* coverity[misra_c_2012_rule_11_5_violation] */
        return ( StreamBufferHandle_t ) pvAllocatedMemory;
    }
    #endif /* configSUPPORT_DYNAMIC_ALLOCATION */
/*-----------------------------------------------------------*/

    #if ( configSUPPORT_STATIC_ALLOCATION == 1 )

    /* 控制块与 pucStreamBufferStorageArea 均由调用方提供；ucFlags 含 IS_STATICALLY_ALLOCATED。 */
    StreamBufferHandle_t xStreamBufferGenericCreateStatic( size_t xBufferSizeBytes,
                                                           size_t xTriggerLevelBytes,
                                                           BaseType_t xStreamBufferType,
                                                           uint8_t * const pucStreamBufferStorageArea,
                                                           StaticStreamBuffer_t * const pxStaticStreamBuffer,
                                                           StreamBufferCallbackFunction_t pxSendCompletedCallback,
                                                           StreamBufferCallbackFunction_t pxReceiveCompletedCallback )
    {
        /* MISRA Ref 11.3.1 [Misaligned access] */
        /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-113 */
        /* coverity[misra_c_2012_rule_11_3_violation] */
        StreamBuffer_t * const pxStreamBuffer = ( StreamBuffer_t * ) pxStaticStreamBuffer;
        StreamBufferHandle_t xReturn;
        uint8_t ucFlags;

        traceENTER_xStreamBufferGenericCreateStatic( xBufferSizeBytes, xTriggerLevelBytes, xStreamBufferType, pucStreamBufferStorageArea, pxStaticStreamBuffer, pxSendCompletedCallback, pxReceiveCompletedCallback );

        configASSERT( pucStreamBufferStorageArea );
        configASSERT( pxStaticStreamBuffer );
        configASSERT( xTriggerLevelBytes <= xBufferSizeBytes );

        if( xTriggerLevelBytes == ( size_t ) 0 )
        {
            xTriggerLevelBytes = ( size_t ) 1;
        }

        if( xStreamBufferType == sbTYPE_MESSAGE_BUFFER )
        {
            ucFlags = sbFLAGS_IS_MESSAGE_BUFFER | sbFLAGS_IS_STATICALLY_ALLOCATED;
            configASSERT( xBufferSizeBytes > sbBYTES_TO_STORE_MESSAGE_LENGTH );
        }
        else if( xStreamBufferType == sbTYPE_STREAM_BATCHING_BUFFER )
        {
            ucFlags = sbFLAGS_IS_BATCHING_BUFFER | sbFLAGS_IS_STATICALLY_ALLOCATED;
            configASSERT( xBufferSizeBytes > 0 );
        }
        else
        {
            ucFlags = sbFLAGS_IS_STATICALLY_ALLOCATED;
        }

        #if ( configASSERT_DEFINED == 1 )
        {
            volatile size_t xSize = sizeof( StaticStreamBuffer_t );
            configASSERT( xSize == sizeof( StreamBuffer_t ) );
        }
        #endif /* configASSERT_DEFINED */

        if( ( pucStreamBufferStorageArea != NULL ) && ( pxStaticStreamBuffer != NULL ) )
        {
            prvInitialiseNewStreamBuffer( pxStreamBuffer,
                                          pucStreamBufferStorageArea,
                                          xBufferSizeBytes,
                                          xTriggerLevelBytes,
                                          ucFlags,
                                          pxSendCompletedCallback,
                                          pxReceiveCompletedCallback );

            /* 标记静态分配：vStreamBufferDelete 时仅擦除控制块而不释放存储。 */
            pxStreamBuffer->ucFlags |= sbFLAGS_IS_STATICALLY_ALLOCATED;

            traceSTREAM_BUFFER_CREATE( pxStreamBuffer, xStreamBufferType );

            /* MISRA Ref 11.3.1 [Misaligned access] */
            /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-113 */
            /* coverity[misra_c_2012_rule_11_3_violation] */
            xReturn = ( StreamBufferHandle_t ) pxStaticStreamBuffer;
        }
        else
        {
            xReturn = NULL;
            traceSTREAM_BUFFER_CREATE_STATIC_FAILED( xReturn, xStreamBufferType );
        }

        traceRETURN_xStreamBufferGenericCreateStatic( xReturn );

        return xReturn;
    }
    #endif /* ( configSUPPORT_STATIC_ALLOCATION == 1 ) */
/*-----------------------------------------------------------*/

    #if ( configSUPPORT_STATIC_ALLOCATION == 1 )
    /* 若为静态创建，返回存储区指针与 StaticStreamBuffer_t 指针。 */
    BaseType_t xStreamBufferGetStaticBuffers( StreamBufferHandle_t xStreamBuffer,
                                              uint8_t ** ppucStreamBufferStorageArea,
                                              StaticStreamBuffer_t ** ppxStaticStreamBuffer )
    {
        BaseType_t xReturn;
        StreamBuffer_t * const pxStreamBuffer = xStreamBuffer;

        traceENTER_xStreamBufferGetStaticBuffers( xStreamBuffer, ppucStreamBufferStorageArea, ppxStaticStreamBuffer );

        configASSERT( pxStreamBuffer );
        configASSERT( ppucStreamBufferStorageArea );
        configASSERT( ppxStaticStreamBuffer );

        if( ( pxStreamBuffer->ucFlags & sbFLAGS_IS_STATICALLY_ALLOCATED ) != ( uint8_t ) 0 )
        {
            *ppucStreamBufferStorageArea = pxStreamBuffer->pucBuffer;
            /* MISRA Ref 11.3.1 [Misaligned access] */
            /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-113 */
            /* coverity[misra_c_2012_rule_11_3_violation] */
            *ppxStaticStreamBuffer = ( StaticStreamBuffer_t * ) pxStreamBuffer;
            xReturn = pdTRUE;
        }
        else
        {
            xReturn = pdFALSE;
        }

        traceRETURN_xStreamBufferGetStaticBuffers( xReturn );

        return xReturn;
    }
    #endif /* configSUPPORT_STATIC_ALLOCATION */
/*-----------------------------------------------------------*/

/* 删除流缓冲：动态分配时一次 vPortFree 释放控制块+数据区；静态则 memset 控制块防误用。 */
void vStreamBufferDelete( StreamBufferHandle_t xStreamBuffer )
{
    StreamBuffer_t * pxStreamBuffer = xStreamBuffer;

    traceENTER_vStreamBufferDelete( xStreamBuffer );

    configASSERT( pxStreamBuffer );

    traceSTREAM_BUFFER_DELETE( xStreamBuffer );

    if( ( pxStreamBuffer->ucFlags & sbFLAGS_IS_STATICALLY_ALLOCATED ) == ( uint8_t ) pdFALSE )
    {
        #if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
        {
            /* 单次 malloc 整块，单次 free。 */
            vPortFree( ( void * ) pxStreamBuffer );
        }
        #else
        {
            /* Should not be possible to get here, ucFlags must be corrupt.
             * Force an assert. */
            configASSERT( xStreamBuffer == ( StreamBufferHandle_t ) ~0 );
        }
        #endif
    }
    else
    {
        /* 静态缓冲区不可释放：清零控制块，后续误用易触发 configASSERT。 */
        ( void ) memset( pxStreamBuffer, 0x00, sizeof( StreamBuffer_t ) );
    }

    traceRETURN_vStreamBufferDelete();
}
/*-----------------------------------------------------------*/

/* 无任务阻塞在读写侧时，重新 prvInitialiseNewStreamBuffer；保留 ucFlags 与可选回调、跟踪号。 */
BaseType_t xStreamBufferReset( StreamBufferHandle_t xStreamBuffer )
{
    StreamBuffer_t * const pxStreamBuffer = xStreamBuffer;
    BaseType_t xReturn = pdFAIL;
    StreamBufferCallbackFunction_t pxSendCallback = NULL, pxReceiveCallback = NULL;

    #if ( configUSE_TRACE_FACILITY == 1 )
        UBaseType_t uxStreamBufferNumber;
    #endif

    traceENTER_xStreamBufferReset( xStreamBuffer );

    configASSERT( pxStreamBuffer );

    #if ( configUSE_TRACE_FACILITY == 1 )
    {
        uxStreamBufferNumber = pxStreamBuffer->uxStreamBufferNumber; /* 暂存，prvInitialise 会清零控制块后再写回 */
    }
    #endif

    taskENTER_CRITICAL();
    {
        if( ( pxStreamBuffer->xTaskWaitingToReceive == NULL ) && ( pxStreamBuffer->xTaskWaitingToSend == NULL ) )
        {
            #if ( configUSE_SB_COMPLETED_CALLBACK == 1 )
            {
                pxSendCallback = pxStreamBuffer->pxSendCompletedCallback;
                pxReceiveCallback = pxStreamBuffer->pxReceiveCompletedCallback;
            }
            #endif

            prvInitialiseNewStreamBuffer( pxStreamBuffer,
                                          pxStreamBuffer->pucBuffer,
                                          pxStreamBuffer->xLength,
                                          pxStreamBuffer->xTriggerLevelBytes,
                                          pxStreamBuffer->ucFlags,
                                          pxSendCallback,
                                          pxReceiveCallback );

            #if ( configUSE_TRACE_FACILITY == 1 )
            {
                pxStreamBuffer->uxStreamBufferNumber = uxStreamBufferNumber;
            }
            #endif

            traceSTREAM_BUFFER_RESET( xStreamBuffer );

            xReturn = pdPASS;
        }
    }
    taskEXIT_CRITICAL();

    traceRETURN_xStreamBufferReset( xReturn );

    return xReturn;
}
/*-----------------------------------------------------------*/

/* ISR 版复位：逻辑同 xStreamBufferReset，用 taskENTER_CRITICAL_FROM_ISR。 */
BaseType_t xStreamBufferResetFromISR( StreamBufferHandle_t xStreamBuffer )
{
    StreamBuffer_t * const pxStreamBuffer = xStreamBuffer;
    BaseType_t xReturn = pdFAIL;
    StreamBufferCallbackFunction_t pxSendCallback = NULL, pxReceiveCallback = NULL;
    UBaseType_t uxSavedInterruptStatus;

    #if ( configUSE_TRACE_FACILITY == 1 )
        UBaseType_t uxStreamBufferNumber;
    #endif

    traceENTER_xStreamBufferResetFromISR( xStreamBuffer );

    configASSERT( pxStreamBuffer );

    #if ( configUSE_TRACE_FACILITY == 1 )
    {
        uxStreamBufferNumber = pxStreamBuffer->uxStreamBufferNumber; /* 暂存，复位后再写回 */
    }
    #endif

    /* MISRA Ref 4.7.1 [Return value shall be checked] */
    /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#dir-47 */
    /* coverity[misra_c_2012_directive_4_7_violation] */
    uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();
    {
        if( ( pxStreamBuffer->xTaskWaitingToReceive == NULL ) && ( pxStreamBuffer->xTaskWaitingToSend == NULL ) )
        {
            #if ( configUSE_SB_COMPLETED_CALLBACK == 1 )
            {
                pxSendCallback = pxStreamBuffer->pxSendCompletedCallback;
                pxReceiveCallback = pxStreamBuffer->pxReceiveCompletedCallback;
            }
            #endif

            prvInitialiseNewStreamBuffer( pxStreamBuffer,
                                          pxStreamBuffer->pucBuffer,
                                          pxStreamBuffer->xLength,
                                          pxStreamBuffer->xTriggerLevelBytes,
                                          pxStreamBuffer->ucFlags,
                                          pxSendCallback,
                                          pxReceiveCallback );

            #if ( configUSE_TRACE_FACILITY == 1 )
            {
                pxStreamBuffer->uxStreamBufferNumber = uxStreamBufferNumber;
            }
            #endif

            traceSTREAM_BUFFER_RESET_FROM_ISR( xStreamBuffer );

            xReturn = pdPASS;
        }
    }
    taskEXIT_CRITICAL_FROM_ISR( uxSavedInterruptStatus );

    traceRETURN_xStreamBufferResetFromISR( xReturn );

    return xReturn;
}
/*-----------------------------------------------------------*/

/* 设置触发字节数：缓冲内已存数据 ≥ 该值时才唤醒阻塞读；须 < xLength，0 会被改为 1。 */
BaseType_t xStreamBufferSetTriggerLevel( StreamBufferHandle_t xStreamBuffer,
                                         size_t xTriggerLevel )
{
    StreamBuffer_t * const pxStreamBuffer = xStreamBuffer;
    BaseType_t xReturn;

    traceENTER_xStreamBufferSetTriggerLevel( xStreamBuffer, xTriggerLevel );

    configASSERT( pxStreamBuffer );

    if( xTriggerLevel == ( size_t ) 0 )
    {
        xTriggerLevel = ( size_t ) 1;
    }

    if( xTriggerLevel < pxStreamBuffer->xLength )
    {
        pxStreamBuffer->xTriggerLevelBytes = xTriggerLevel;
        xReturn = pdPASS;
    }
    else
    {
        xReturn = pdFALSE;
    }

    traceRETURN_xStreamBufferSetTriggerLevel( xReturn );

    return xReturn;
}
/*-----------------------------------------------------------*/

/*
 * 返回当前可再写入的字节数（最多 xLength-1）。读 xTail/xHead 非原子时用 do-while：
 * 若两次读之间 xTail 被并发更新则重算，避免错判空间。
 */
size_t xStreamBufferSpacesAvailable( StreamBufferHandle_t xStreamBuffer )
{
    const StreamBuffer_t * const pxStreamBuffer = xStreamBuffer;
    size_t xSpace;
    size_t xOriginalTail;

    traceENTER_xStreamBufferSpacesAvailable( xStreamBuffer );

    configASSERT( pxStreamBuffer );

    do
    {
        xOriginalTail = pxStreamBuffer->xTail;
        xSpace = pxStreamBuffer->xLength + pxStreamBuffer->xTail;
        xSpace -= pxStreamBuffer->xHead;
    } while( xOriginalTail != pxStreamBuffer->xTail );

    xSpace -= ( size_t ) 1; /* 保留一字节区分满/空，故可写空间比环长少 1。 */

    if( xSpace >= pxStreamBuffer->xLength )
    {
        xSpace -= pxStreamBuffer->xLength;
    }
    else
    {
        mtCOVERAGE_TEST_MARKER();
    }

    traceRETURN_xStreamBufferSpacesAvailable( xSpace );

    return xSpace;
}
/*-----------------------------------------------------------*/

/* 当前可读字节数，等于 prvBytesInBuffer。 */
size_t xStreamBufferBytesAvailable( StreamBufferHandle_t xStreamBuffer )
{
    const StreamBuffer_t * const pxStreamBuffer = xStreamBuffer;
    size_t xReturn;

    traceENTER_xStreamBufferBytesAvailable( xStreamBuffer );

    configASSERT( pxStreamBuffer );

    xReturn = prvBytesInBuffer( pxStreamBuffer );

    traceRETURN_xStreamBufferBytesAvailable( xReturn );

    return xReturn;
}
/*-----------------------------------------------------------*/

/*
 * 任务上下文发送：消息缓冲要求整包空间否则不写；流缓冲可写部分数据。
 * 空间不足时清通知、登记 xTaskWaitingToSend 后 xTaskNotifyWaitIndexed 阻塞，被唤醒后 prvWriteMessageToBuffer。
 * 写成功后若 prvBytesInBuffer >= trigger 则 prvSEND_COMPLETED 唤醒接收方。
 */
size_t xStreamBufferSend( StreamBufferHandle_t xStreamBuffer,
                          const void * pvTxData,
                          size_t xDataLengthBytes,
                          TickType_t xTicksToWait )
{
    StreamBuffer_t * const pxStreamBuffer = xStreamBuffer;
    size_t xReturn, xSpace = 0;
    size_t xRequiredSpace = xDataLengthBytes;
    TimeOut_t xTimeOut;
    size_t xMaxReportedSpace = 0;

    traceENTER_xStreamBufferSend( xStreamBuffer, pvTxData, xDataLengthBytes, xTicksToWait );

    configASSERT( pvTxData );
    configASSERT( pxStreamBuffer );

    xMaxReportedSpace = pxStreamBuffer->xLength - ( size_t ) 1;

    if( ( pxStreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER ) != ( uint8_t ) 0 )
    {
        xRequiredSpace += sbBYTES_TO_STORE_MESSAGE_LENGTH;

        configASSERT( xRequiredSpace > xDataLengthBytes );

        if( xRequiredSpace > xMaxReportedSpace )
        {
            /* 整包永远放不下：不阻塞，直接返回 0。 */
            xTicksToWait = ( TickType_t ) 0;
        }
        else
        {
            mtCOVERAGE_TEST_MARKER();
        }
    }
    else
    {
        if( xRequiredSpace > xMaxReportedSpace )
        {
            xRequiredSpace = xMaxReportedSpace;
        }
        else
        {
            mtCOVERAGE_TEST_MARKER();
        }
    }

    if( xTicksToWait != ( TickType_t ) 0 )
    {
        vTaskSetTimeOutState( &xTimeOut );

        do
        {
            taskENTER_CRITICAL();
            {
                xSpace = xStreamBufferSpacesAvailable( pxStreamBuffer );

                if( xSpace < xRequiredSpace )
                {
                    ( void ) xTaskNotifyStateClearIndexed( NULL, pxStreamBuffer->uxNotificationIndex );

                    configASSERT( pxStreamBuffer->xTaskWaitingToSend == NULL );
                    pxStreamBuffer->xTaskWaitingToSend = xTaskGetCurrentTaskHandle();
                }
                else
                {
                    taskEXIT_CRITICAL();
                    break;
                }
            }
            taskEXIT_CRITICAL();

            traceBLOCKING_ON_STREAM_BUFFER_SEND( xStreamBuffer );
            ( void ) xTaskNotifyWaitIndexed( pxStreamBuffer->uxNotificationIndex, ( uint32_t ) 0, ( uint32_t ) 0, NULL, xTicksToWait );
            pxStreamBuffer->xTaskWaitingToSend = NULL;
        } while( xTaskCheckForTimeOut( &xTimeOut, &xTicksToWait ) == pdFALSE );
    }
    else
    {
        mtCOVERAGE_TEST_MARKER();
    }

    if( xSpace == ( size_t ) 0 )
    {
        xSpace = xStreamBufferSpacesAvailable( pxStreamBuffer );
    }
    else
    {
        mtCOVERAGE_TEST_MARKER();
    }

    xReturn = prvWriteMessageToBuffer( pxStreamBuffer, pvTxData, xDataLengthBytes, xSpace, xRequiredSpace );

    if( xReturn > ( size_t ) 0 )
    {
        traceSTREAM_BUFFER_SEND( xStreamBuffer, xReturn );

        /* 已达触发字节数则通知阻塞在接收侧的任务（或实例发送完成回调）。 */
        if( prvBytesInBuffer( pxStreamBuffer ) >= pxStreamBuffer->xTriggerLevelBytes )
        {
            prvSEND_COMPLETED( pxStreamBuffer );
        }
        else
        {
            mtCOVERAGE_TEST_MARKER();
        }
    }
    else
    {
        mtCOVERAGE_TEST_MARKER();
        traceSTREAM_BUFFER_SEND_FAILED( xStreamBuffer );
    }

    traceRETURN_xStreamBufferSend( xReturn );

    return xReturn;
}
/*-----------------------------------------------------------*/

/* ISR 发送：不阻塞；写成功后达 trigger 时 prvSEND_COMPLETE_FROM_ISR。 */
size_t xStreamBufferSendFromISR( StreamBufferHandle_t xStreamBuffer,
                                 const void * pvTxData,
                                 size_t xDataLengthBytes,
                                 BaseType_t * const pxHigherPriorityTaskWoken )
{
    StreamBuffer_t * const pxStreamBuffer = xStreamBuffer;
    size_t xReturn, xSpace;
    size_t xRequiredSpace = xDataLengthBytes;

    traceENTER_xStreamBufferSendFromISR( xStreamBuffer, pvTxData, xDataLengthBytes, pxHigherPriorityTaskWoken );

    configASSERT( pvTxData );
    configASSERT( pxStreamBuffer );

    if( ( pxStreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER ) != ( uint8_t ) 0 )
    {
        xRequiredSpace += sbBYTES_TO_STORE_MESSAGE_LENGTH;

        configASSERT( xRequiredSpace > xDataLengthBytes );
    }
    else
    {
        mtCOVERAGE_TEST_MARKER();
    }

    xSpace = xStreamBufferSpacesAvailable( pxStreamBuffer );
    xReturn = prvWriteMessageToBuffer( pxStreamBuffer, pvTxData, xDataLengthBytes, xSpace, xRequiredSpace );

    if( xReturn > ( size_t ) 0 )
    {
        if( prvBytesInBuffer( pxStreamBuffer ) >= pxStreamBuffer->xTriggerLevelBytes )
        {
            /* MISRA Ref 4.7.1 [Return value shall be checked] */
            /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#dir-47 */
            /* coverity[misra_c_2012_directive_4_7_violation] */
            prvSEND_COMPLETE_FROM_ISR( pxStreamBuffer, pxHigherPriorityTaskWoken );
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

    traceSTREAM_BUFFER_SEND_FROM_ISR( xStreamBuffer, xReturn );
    traceRETURN_xStreamBufferSendFromISR( xReturn );

    return xReturn;
}
/*-----------------------------------------------------------*/

static size_t prvWriteMessageToBuffer( StreamBuffer_t * const pxStreamBuffer,
                                       const void * pvTxData,
                                       size_t xDataLengthBytes,
                                       size_t xSpace,
                                       size_t xRequiredSpace )
{
    size_t xNextHead = pxStreamBuffer->xHead;
    configMESSAGE_BUFFER_LENGTH_TYPE xMessageLength;

    if( ( pxStreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER ) != ( uint8_t ) 0 )
    {
        xMessageLength = ( configMESSAGE_BUFFER_LENGTH_TYPE ) xDataLengthBytes;

        configASSERT( ( size_t ) xMessageLength == xDataLengthBytes );

        if( xSpace >= xRequiredSpace )
        {
            /* 先只写长度前缀到环；xHead 仍由返回的 xNextHead 链式传递，最后再写正文。 */
            xNextHead = prvWriteBytesToBuffer( pxStreamBuffer, ( const uint8_t * ) &( xMessageLength ), sbBYTES_TO_STORE_MESSAGE_LENGTH, xNextHead );
        }
        else
        {
            xDataLengthBytes = 0;
        }
    }
    else
    {
        xDataLengthBytes = configMIN( xDataLengthBytes, xSpace );
    }

    if( xDataLengthBytes != ( size_t ) 0 )
    {
        /* MISRA Ref 11.5.5 [Void pointer assignment] */
        /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-115 */
        /* coverity[misra_c_2012_rule_11_5_violation] */
        pxStreamBuffer->xHead = prvWriteBytesToBuffer( pxStreamBuffer, ( const uint8_t * ) pvTxData, xDataLengthBytes, xNextHead );
    }

    return xDataLengthBytes;
}
/*-----------------------------------------------------------*/

/*
 * 任务上下文接收：消息缓冲需先凑够长度前缀再读体；批处理缓冲将 xBytesToStoreMessageLength 设为 trigger，
 * 迫使「至少 trigger 字节才解除阻塞」；普通流缓冲为 0。
 * 不足则清通知、挂 xTaskWaitingToReceive 后阻塞；读后 prvRECEIVE_COMPLETED 可能唤醒等发送方。
 */
size_t xStreamBufferReceive( StreamBufferHandle_t xStreamBuffer,
                             void * pvRxData,
                             size_t xBufferLengthBytes,
                             TickType_t xTicksToWait )
{
    StreamBuffer_t * const pxStreamBuffer = xStreamBuffer;
    size_t xReceivedLength = 0, xBytesAvailable, xBytesToStoreMessageLength;

    traceENTER_xStreamBufferReceive( xStreamBuffer, pvRxData, xBufferLengthBytes, xTicksToWait );

    configASSERT( pvRxData );
    configASSERT( pxStreamBuffer );

    if( ( pxStreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER ) != ( uint8_t ) 0 )
    {
        xBytesToStoreMessageLength = sbBYTES_TO_STORE_MESSAGE_LENGTH;
    }
    else if( ( pxStreamBuffer->ucFlags & sbFLAGS_IS_BATCHING_BUFFER ) != ( uint8_t ) 0 )
    {
        xBytesToStoreMessageLength = pxStreamBuffer->xTriggerLevelBytes;
    }
    else
    {
        xBytesToStoreMessageLength = 0;
    }

    if( xTicksToWait != ( TickType_t ) 0 )
    {
        taskENTER_CRITICAL();
        {
            xBytesAvailable = prvBytesInBuffer( pxStreamBuffer );

            if( xBytesAvailable <= xBytesToStoreMessageLength )
            {
                ( void ) xTaskNotifyStateClearIndexed( NULL, pxStreamBuffer->uxNotificationIndex );

                configASSERT( pxStreamBuffer->xTaskWaitingToReceive == NULL );
                pxStreamBuffer->xTaskWaitingToReceive = xTaskGetCurrentTaskHandle();
            }
            else
            {
                mtCOVERAGE_TEST_MARKER();
            }
        }
        taskEXIT_CRITICAL();

        if( xBytesAvailable <= xBytesToStoreMessageLength )
        {
            traceBLOCKING_ON_STREAM_BUFFER_RECEIVE( xStreamBuffer );
            ( void ) xTaskNotifyWaitIndexed( pxStreamBuffer->uxNotificationIndex, ( uint32_t ) 0, ( uint32_t ) 0, NULL, xTicksToWait );
            pxStreamBuffer->xTaskWaitingToReceive = NULL;

            xBytesAvailable = prvBytesInBuffer( pxStreamBuffer );
        }
        else
        {
            mtCOVERAGE_TEST_MARKER();
        }
    }
    else
    {
        xBytesAvailable = prvBytesInBuffer( pxStreamBuffer );
    }

    /* 可读量须大于「门槛」：消息模式为长度前缀长；批处理为 trigger；流为 0。 */
    if( xBytesAvailable > xBytesToStoreMessageLength )
    {
        xReceivedLength = prvReadMessageFromBuffer( pxStreamBuffer, pvRxData, xBufferLengthBytes, xBytesAvailable );

        if( xReceivedLength != ( size_t ) 0 )
        {
            traceSTREAM_BUFFER_RECEIVE( xStreamBuffer, xReceivedLength );
            prvRECEIVE_COMPLETED( xStreamBuffer );
        }
        else
        {
            mtCOVERAGE_TEST_MARKER();
        }
    }
    else
    {
        traceSTREAM_BUFFER_RECEIVE_FAILED( xStreamBuffer );
        mtCOVERAGE_TEST_MARKER();
    }

    traceRETURN_xStreamBufferReceive( xReceivedLength );

    return xReceivedLength;
}
/*-----------------------------------------------------------*/

/* 仅消息缓冲：窥视下一条消息载荷长度（不消费）；非消息缓冲返回 0。 */
size_t xStreamBufferNextMessageLengthBytes( StreamBufferHandle_t xStreamBuffer )
{
    StreamBuffer_t * const pxStreamBuffer = xStreamBuffer;
    size_t xReturn, xBytesAvailable;
    configMESSAGE_BUFFER_LENGTH_TYPE xTempReturn;

    traceENTER_xStreamBufferNextMessageLengthBytes( xStreamBuffer );

    configASSERT( pxStreamBuffer );

    if( ( pxStreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER ) != ( uint8_t ) 0 )
    {
        xBytesAvailable = prvBytesInBuffer( pxStreamBuffer );

        if( xBytesAvailable > sbBYTES_TO_STORE_MESSAGE_LENGTH )
        {
            /* 从当前 xTail 拷贝长度前缀到 xTempReturn；返回值未写回 xTail，故不消费环内数据。 */
            ( void ) prvReadBytesFromBuffer( pxStreamBuffer, ( uint8_t * ) &xTempReturn, sbBYTES_TO_STORE_MESSAGE_LENGTH, pxStreamBuffer->xTail );
            xReturn = ( size_t ) xTempReturn;
        }
        else
        {
            configASSERT( xBytesAvailable == 0 );
            xReturn = 0;
        }
    }
    else
    {
        xReturn = 0;
    }

    traceRETURN_xStreamBufferNextMessageLengthBytes( xReturn );

    return xReturn;
}
/*-----------------------------------------------------------*/

/* ISR 接收：无阻塞；若读出字节则 prvRECEIVE_COMPLETED_FROM_ISR（批处理在 ISR 路径 xBytesToStoreMessageLength 恒为 0）。 */
size_t xStreamBufferReceiveFromISR( StreamBufferHandle_t xStreamBuffer,
                                    void * pvRxData,
                                    size_t xBufferLengthBytes,
                                    BaseType_t * const pxHigherPriorityTaskWoken )
{
    StreamBuffer_t * const pxStreamBuffer = xStreamBuffer;
    size_t xReceivedLength = 0, xBytesAvailable, xBytesToStoreMessageLength;

    traceENTER_xStreamBufferReceiveFromISR( xStreamBuffer, pvRxData, xBufferLengthBytes, pxHigherPriorityTaskWoken );

    configASSERT( pvRxData );
    configASSERT( pxStreamBuffer );

    if( ( pxStreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER ) != ( uint8_t ) 0 )
    {
        xBytesToStoreMessageLength = sbBYTES_TO_STORE_MESSAGE_LENGTH;
    }
    else
    {
        xBytesToStoreMessageLength = 0;
    }

    xBytesAvailable = prvBytesInBuffer( pxStreamBuffer );

    if( xBytesAvailable > xBytesToStoreMessageLength )
    {
        xReceivedLength = prvReadMessageFromBuffer( pxStreamBuffer, pvRxData, xBufferLengthBytes, xBytesAvailable );

        if( xReceivedLength != ( size_t ) 0 )
        {
            /* MISRA Ref 4.7.1 [Return value shall be checked] */
            /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#dir-47 */
            /* coverity[misra_c_2012_directive_4_7_violation] */
            prvRECEIVE_COMPLETED_FROM_ISR( pxStreamBuffer, pxHigherPriorityTaskWoken );
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

    traceSTREAM_BUFFER_RECEIVE_FROM_ISR( xStreamBuffer, xReceivedLength );
    traceRETURN_xStreamBufferReceiveFromISR( xReceivedLength );

    return xReceivedLength;
}
/*-----------------------------------------------------------*/

static size_t prvReadMessageFromBuffer( StreamBuffer_t * pxStreamBuffer,
                                        void * pvRxData,
                                        size_t xBufferLengthBytes,
                                        size_t xBytesAvailable )
{
    size_t xCount, xNextMessageLength;
    configMESSAGE_BUFFER_LENGTH_TYPE xTempNextMessageLength;
    size_t xNextTail = pxStreamBuffer->xTail;

    if( ( pxStreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER ) != ( uint8_t ) 0 )
    {
        xNextTail = prvReadBytesFromBuffer( pxStreamBuffer, ( uint8_t * ) &xTempNextMessageLength, sbBYTES_TO_STORE_MESSAGE_LENGTH, xNextTail );
        xNextMessageLength = ( size_t ) xTempNextMessageLength;

        xBytesAvailable -= sbBYTES_TO_STORE_MESSAGE_LENGTH;

        if( xNextMessageLength > xBufferLengthBytes )
        {
            /* 用户缓冲区小于消息体长度：不读载荷，最终 xCount 为 0 且不提交 xTail。 */
            xNextMessageLength = 0;
        }
        else
        {
            mtCOVERAGE_TEST_MARKER();
        }
    }
    else
    {
        xNextMessageLength = xBufferLengthBytes;
    }

    xCount = configMIN( xNextMessageLength, xBytesAvailable );

    if( xCount != ( size_t ) 0 )
    {
        /* MISRA Ref 11.5.5 [Void pointer assignment] */
        /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-115 */
        /* coverity[misra_c_2012_rule_11_5_violation] */
        pxStreamBuffer->xTail = prvReadBytesFromBuffer( pxStreamBuffer, ( uint8_t * ) pvRxData, xCount, xNextTail );
    }

    return xCount;
}
/*-----------------------------------------------------------*/

/* 无未读字节时 xHead 与 xTail 相等。 */
BaseType_t xStreamBufferIsEmpty( StreamBufferHandle_t xStreamBuffer )
{
    const StreamBuffer_t * const pxStreamBuffer = xStreamBuffer;
    BaseType_t xReturn;
    size_t xTail;

    traceENTER_xStreamBufferIsEmpty( xStreamBuffer );

    configASSERT( pxStreamBuffer );

    xTail = pxStreamBuffer->xTail;

    if( pxStreamBuffer->xHead == xTail )
    {
        xReturn = pdTRUE;
    }
    else
    {
        xReturn = pdFALSE;
    }

    traceRETURN_xStreamBufferIsEmpty( xReturn );

    return xReturn;
}
/*-----------------------------------------------------------*/

/* 消息缓冲「满」需预留长度前缀槽：空闲 ≤ sizeof(长度) 即满；流缓冲无此前缀。 */
BaseType_t xStreamBufferIsFull( StreamBufferHandle_t xStreamBuffer )
{
    BaseType_t xReturn;
    size_t xBytesToStoreMessageLength;
    const StreamBuffer_t * const pxStreamBuffer = xStreamBuffer;

    traceENTER_xStreamBufferIsFull( xStreamBuffer );

    configASSERT( pxStreamBuffer );

    if( ( pxStreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER ) != ( uint8_t ) 0 )
    {
        xBytesToStoreMessageLength = sbBYTES_TO_STORE_MESSAGE_LENGTH;
    }
    else
    {
        xBytesToStoreMessageLength = 0;
    }

    if( xStreamBufferSpacesAvailable( xStreamBuffer ) <= xBytesToStoreMessageLength )
    {
        xReturn = pdTRUE;
    }
    else
    {
        xReturn = pdFALSE;
    }

    traceRETURN_xStreamBufferIsFull( xReturn );

    return xReturn;
}
/*-----------------------------------------------------------*/

/* 应用或驱动在 ISR 中自行写入缓冲后调用：若存在 xTaskWaitingToReceive 则发通知（零负载 notify）。 */
BaseType_t xStreamBufferSendCompletedFromISR( StreamBufferHandle_t xStreamBuffer,
                                              BaseType_t * pxHigherPriorityTaskWoken )
{
    StreamBuffer_t * const pxStreamBuffer = xStreamBuffer;
    BaseType_t xReturn;
    UBaseType_t uxSavedInterruptStatus;

    traceENTER_xStreamBufferSendCompletedFromISR( xStreamBuffer, pxHigherPriorityTaskWoken );

    configASSERT( pxStreamBuffer );

    /* MISRA Ref 4.7.1 [Return value shall be checked] */
    /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#dir-47 */
    /* coverity[misra_c_2012_directive_4_7_violation] */
    uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();
    {
        if( ( pxStreamBuffer )->xTaskWaitingToReceive != NULL )
        {
            ( void ) xTaskNotifyIndexedFromISR( ( pxStreamBuffer )->xTaskWaitingToReceive,
                                                ( pxStreamBuffer )->uxNotificationIndex,
                                                ( uint32_t ) 0,
                                                eNoAction,
                                                pxHigherPriorityTaskWoken );
            ( pxStreamBuffer )->xTaskWaitingToReceive = NULL;
            xReturn = pdTRUE;
        }
        else
        {
            xReturn = pdFALSE;
        }
    }
    taskEXIT_CRITICAL_FROM_ISR( uxSavedInterruptStatus );

    traceRETURN_xStreamBufferSendCompletedFromISR( xReturn );

    return xReturn;
}
/*-----------------------------------------------------------*/

/* 与上对称：ISR 中自行读出数据后调用，唤醒可能阻塞在 xTaskWaitingToSend 的发送任务。 */
BaseType_t xStreamBufferReceiveCompletedFromISR( StreamBufferHandle_t xStreamBuffer,
                                                 BaseType_t * pxHigherPriorityTaskWoken )
{
    StreamBuffer_t * const pxStreamBuffer = xStreamBuffer;
    BaseType_t xReturn;
    UBaseType_t uxSavedInterruptStatus;

    traceENTER_xStreamBufferReceiveCompletedFromISR( xStreamBuffer, pxHigherPriorityTaskWoken );

    configASSERT( pxStreamBuffer );

    /* MISRA Ref 4.7.1 [Return value shall be checked] */
    /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#dir-47 */
    /* coverity[misra_c_2012_directive_4_7_violation] */
    uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();
    {
        if( ( pxStreamBuffer )->xTaskWaitingToSend != NULL )
        {
            ( void ) xTaskNotifyIndexedFromISR( ( pxStreamBuffer )->xTaskWaitingToSend,
                                                ( pxStreamBuffer )->uxNotificationIndex,
                                                ( uint32_t ) 0,
                                                eNoAction,
                                                pxHigherPriorityTaskWoken );
            ( pxStreamBuffer )->xTaskWaitingToSend = NULL;
            xReturn = pdTRUE;
        }
        else
        {
            xReturn = pdFALSE;
        }
    }
    taskEXIT_CRITICAL_FROM_ISR( uxSavedInterruptStatus );

    traceRETURN_xStreamBufferReceiveCompletedFromISR( xReturn );

    return xReturn;
}
/*-----------------------------------------------------------*/

static size_t prvWriteBytesToBuffer( StreamBuffer_t * const pxStreamBuffer,
                                     const uint8_t * pucData,
                                     size_t xCount,
                                     size_t xHead )
{
    size_t xFirstLength;

    configASSERT( xCount > ( size_t ) 0 );

    /* 从 xHead 到环尾连续可写长度可能小于 xCount，剩余段从 pucBuffer[0] 续写。 */
    xFirstLength = configMIN( pxStreamBuffer->xLength - xHead, xCount );

    configASSERT( ( xHead + xFirstLength ) <= pxStreamBuffer->xLength );
    ( void ) memcpy( ( void * ) ( &( pxStreamBuffer->pucBuffer[ xHead ] ) ), ( const void * ) pucData, xFirstLength );

    if( xCount > xFirstLength )
    {
        configASSERT( ( xCount - xFirstLength ) <= pxStreamBuffer->xLength );
        ( void ) memcpy( ( void * ) pxStreamBuffer->pucBuffer, ( const void * ) &( pucData[ xFirstLength ] ), xCount - xFirstLength );
    }
    else
    {
        mtCOVERAGE_TEST_MARKER();
    }

    xHead += xCount;

    if( xHead >= pxStreamBuffer->xLength )
    {
        xHead -= pxStreamBuffer->xLength;
    }
    else
    {
        mtCOVERAGE_TEST_MARKER();
    }

    return xHead;
}
/*-----------------------------------------------------------*/

static size_t prvReadBytesFromBuffer( StreamBuffer_t * pxStreamBuffer,
                                      uint8_t * pucData,
                                      size_t xCount,
                                      size_t xTail )
{
    size_t xFirstLength;

    configASSERT( xCount != ( size_t ) 0 );

    /* 从 xTail 到环尾可读连续段；不足则从缓冲区头继续 memcpy。 */
    xFirstLength = configMIN( pxStreamBuffer->xLength - xTail, xCount );

    configASSERT( xFirstLength <= xCount );
    configASSERT( ( xTail + xFirstLength ) <= pxStreamBuffer->xLength );
    ( void ) memcpy( ( void * ) pucData, ( const void * ) &( pxStreamBuffer->pucBuffer[ xTail ] ), xFirstLength );

    if( xCount > xFirstLength )
    {
        ( void ) memcpy( ( void * ) &( pucData[ xFirstLength ] ), ( void * ) ( pxStreamBuffer->pucBuffer ), xCount - xFirstLength );
    }
    else
    {
        mtCOVERAGE_TEST_MARKER();
    }

    xTail += xCount;

    if( xTail >= pxStreamBuffer->xLength )
    {
        xTail -= pxStreamBuffer->xLength;
    }

    return xTail;
}
/*-----------------------------------------------------------*/

static size_t prvBytesInBuffer( const StreamBuffer_t * const pxStreamBuffer )
{
    /* 环形距离：(Head - Tail) mod xLength，结果范围 0…xLength-1。 */
    size_t xCount;

    xCount = pxStreamBuffer->xLength + pxStreamBuffer->xHead;
    xCount -= pxStreamBuffer->xTail;

    if( xCount >= pxStreamBuffer->xLength )
    {
        xCount -= pxStreamBuffer->xLength;
    }
    else
    {
        mtCOVERAGE_TEST_MARKER();
    }

    return xCount;
}
/*-----------------------------------------------------------*/

static void prvInitialiseNewStreamBuffer( StreamBuffer_t * const pxStreamBuffer,
                                          uint8_t * const pucBuffer,
                                          size_t xBufferSizeBytes,
                                          size_t xTriggerLevelBytes,
                                          uint8_t ucFlags,
                                          StreamBufferCallbackFunction_t pxSendCompletedCallback,
                                          StreamBufferCallbackFunction_t pxReceiveCompletedCallback )
{
    #if ( configASSERT_DEFINED == 1 )
    {
        /* 整区写 0x55：触达物理映射并便于调试辨认（避免与栈填充 0xA5 混淆）。 */
        #define STREAM_BUFFER_BUFFER_WRITE_VALUE    ( 0x55 )
        configASSERT( memset( pucBuffer, ( int ) STREAM_BUFFER_BUFFER_WRITE_VALUE, xBufferSizeBytes ) == pucBuffer );
    }
    #endif

    ( void ) memset( ( void * ) pxStreamBuffer, 0x00, sizeof( StreamBuffer_t ) ); /* 控制块清零，head/tail 初值为 0 */
    pxStreamBuffer->pucBuffer = pucBuffer;
    pxStreamBuffer->xLength = xBufferSizeBytes;
    pxStreamBuffer->xTriggerLevelBytes = xTriggerLevelBytes;
    pxStreamBuffer->ucFlags = ucFlags;
    pxStreamBuffer->uxNotificationIndex = tskDEFAULT_INDEX_TO_NOTIFY;
    #if ( configUSE_SB_COMPLETED_CALLBACK == 1 )
    {
        pxStreamBuffer->pxSendCompletedCallback = pxSendCompletedCallback;
        pxStreamBuffer->pxReceiveCompletedCallback = pxReceiveCompletedCallback;
    }
    #else
    {
        /* MISRA Ref 11.1.1 [Object type casting] */
        /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-111 */
        /* coverity[misra_c_2012_rule_11_1_violation] */
        ( void ) pxSendCompletedCallback;

        /* MISRA Ref 11.1.1 [Object type casting] */
        /* More details at: https://github.com/FreeRTOS/FreeRTOS-Kernel/blob/main/MISRA.md#rule-111 */
        /* coverity[misra_c_2012_rule_11_1_violation] */
        ( void ) pxReceiveCompletedCallback;
    }
    #endif /* if ( configUSE_SB_COMPLETED_CALLBACK == 1 ) */
}
/*-----------------------------------------------------------*/

/* 返回本缓冲使用的任务通知数组下标。 */
UBaseType_t uxStreamBufferGetStreamBufferNotificationIndex( StreamBufferHandle_t xStreamBuffer )
{
    StreamBuffer_t * const pxStreamBuffer = xStreamBuffer;

    traceENTER_uxStreamBufferGetStreamBufferNotificationIndex( xStreamBuffer );

    configASSERT( pxStreamBuffer );

    traceRETURN_uxStreamBufferGetStreamBufferNotificationIndex( pxStreamBuffer->uxNotificationIndex );

    return pxStreamBuffer->uxNotificationIndex;
}
/*-----------------------------------------------------------*/

/* 须在无阻塞任务等待时修改通知下标，避免永远无法唤醒。 */
void vStreamBufferSetStreamBufferNotificationIndex( StreamBufferHandle_t xStreamBuffer,
                                                    UBaseType_t uxNotificationIndex )
{
    StreamBuffer_t * const pxStreamBuffer = xStreamBuffer;

    traceENTER_vStreamBufferSetStreamBufferNotificationIndex( xStreamBuffer, uxNotificationIndex );

    configASSERT( ( pxStreamBuffer != NULL ) && ( pxStreamBuffer->xTaskWaitingToReceive == NULL ) );
    configASSERT( ( pxStreamBuffer != NULL ) && ( pxStreamBuffer->xTaskWaitingToSend == NULL ) );

    configASSERT( uxNotificationIndex < configTASK_NOTIFICATION_ARRAY_ENTRIES );

    pxStreamBuffer->uxNotificationIndex = uxNotificationIndex;

    traceRETURN_vStreamBufferSetStreamBufferNotificationIndex();
}
/*-----------------------------------------------------------*/

    #if ( configUSE_TRACE_FACILITY == 1 )

    /* 返回 configUSE_TRACE_FACILITY 下记录的流缓冲编号。 */
    UBaseType_t uxStreamBufferGetStreamBufferNumber( StreamBufferHandle_t xStreamBuffer )
    {
        traceENTER_uxStreamBufferGetStreamBufferNumber( xStreamBuffer );

        traceRETURN_uxStreamBufferGetStreamBufferNumber( xStreamBuffer->uxStreamBufferNumber );

        return xStreamBuffer->uxStreamBufferNumber;
    }

    #endif /* configUSE_TRACE_FACILITY */
/*-----------------------------------------------------------*/

    #if ( configUSE_TRACE_FACILITY == 1 )

    void vStreamBufferSetStreamBufferNumber( StreamBufferHandle_t xStreamBuffer,
                                             UBaseType_t uxStreamBufferNumber )
    {
        traceENTER_vStreamBufferSetStreamBufferNumber( xStreamBuffer, uxStreamBufferNumber );

        xStreamBuffer->uxStreamBufferNumber = uxStreamBufferNumber;

        traceRETURN_vStreamBufferSetStreamBufferNumber();
    }

    #endif /* configUSE_TRACE_FACILITY */
/*-----------------------------------------------------------*/

    #if ( configUSE_TRACE_FACILITY == 1 )

    /* 非零表示按消息缓冲创建（与 sbFLAGS_IS_MESSAGE_BUFFER 一致）。 */
    uint8_t ucStreamBufferGetStreamBufferType( StreamBufferHandle_t xStreamBuffer )
    {
        traceENTER_ucStreamBufferGetStreamBufferType( xStreamBuffer );

        traceRETURN_ucStreamBufferGetStreamBufferType( ( uint8_t ) ( xStreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER ) );

        return( ( uint8_t ) ( xStreamBuffer->ucFlags & sbFLAGS_IS_MESSAGE_BUFFER ) );
    }

    #endif /* configUSE_TRACE_FACILITY */
/*-----------------------------------------------------------*/

/* configUSE_STREAM_BUFFERS==0 时本文件自上方 #if 起均不编译。 */
#endif /* configUSE_STREAM_BUFFERS == 1 */
