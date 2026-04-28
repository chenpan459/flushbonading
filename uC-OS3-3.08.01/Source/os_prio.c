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
*                                          PRIORITY MANAGEMENT
*
* File    : os_prio.c
* Version : V3.08.01
*********************************************************************************************************
*/

#define  MICRIUM_SOURCE
#include "os.h"

#ifdef VSC_INCLUDE_SOURCE_FILE_NAMES
const  CPU_CHAR  *os_prio__c = "$Id: $";
#endif


/*
************************************************************************************************************************
*                                               INITIALIZE THE PRIORITY LIST
*
* Description: This function is called by uC/OS-III to initialize the list of ready priorities.
*
* Arguments  : none
*
* Returns    : none
*
* Note       : This function is INTERNAL to uC/OS-III and your application should not call it.
************************************************************************************************************************
*/

void  OS_PrioInit (void)
{
    CPU_DATA  i;


                                                                /* 位图 bit=1 表示“该优先级存在就绪任务”。               */
                                                                /* Clear the bitmap table ... no task is ready          */
    for (i = 0u; i < OS_PRIO_TBL_SIZE; i++) {
         OSPrioTbl[i] = 0u;
    }

#if (OS_CFG_TASK_IDLE_EN == 0u)
    OS_PrioInsert ((OS_PRIO)(OS_CFG_PRIO_MAX - 1u));            /* Insert what would be the idle task                   */
#endif
}

/*
************************************************************************************************************************
*                                           GET HIGHEST PRIORITY TASK WAITING
*
* Description: This function is called by other uC/OS-III services to determine the highest priority task
*              waiting on the event.
*
* Arguments  : none
*
* Returns    : The priority of the Highest Priority Task (HPT) waiting for the event
*
* Note(s)    : 1) This function is INTERNAL to uC/OS-III and your application MUST NOT call it.
************************************************************************************************************************
*/

OS_PRIO  OS_PrioGetHighest (void)
{
#if   (OS_CFG_PRIO_MAX <= (CPU_CFG_DATA_SIZE * 8u))             /* Optimize for less than word size nbr of priorities   */
    /* 快速路径：单字位图，CLZ 直接给出最高就绪优先级索引。 */
    return ((OS_PRIO)CPU_CntLeadZeros(OSPrioTbl[0]));


#elif (OS_CFG_PRIO_MAX <= (2u * (CPU_CFG_DATA_SIZE * 8u)))      /* Optimize for    2x the word size nbr of priorities   */
    /* 双字位图：先检查第一字，为空再带偏移检查第二字。 */
    if (OSPrioTbl[0] == 0u) {
        return ((OS_PRIO)((OS_PRIO)CPU_CntLeadZeros(OSPrioTbl[1]) + (CPU_CFG_DATA_SIZE * 8u)));
    } else {
        return ((OS_PRIO)((OS_PRIO)CPU_CntLeadZeros(OSPrioTbl[0])));
    }


#else
    CPU_DATA  *p_tbl;
    OS_PRIO    prio;


    prio  = 0u;
    p_tbl = &OSPrioTbl[0];
    /* 通用路径：先扫描到首个非零字，再定位首个置位 bit。 */
    while (*p_tbl == 0u) {                                      /* Search the bitmap table for the highest priority     */
        prio = (OS_PRIO)(prio + (CPU_CFG_DATA_SIZE * 8u));      /* Compute the step of each CPU_DATA entry              */
        p_tbl++;
    }
    prio += (OS_PRIO)CPU_CntLeadZeros(*p_tbl);                  /* Find the position of the first bit set at the entry  */

    return (prio);
#endif
}

/*
************************************************************************************************************************
*                                                  INSERT PRIORITY
*
* Description: This function is called to insert a priority in the priority table.
*
* Arguments  : prio     is the priority to insert
*
* Returns    : none
*
* Note(s)    : 1) This function is INTERNAL to uC/OS-III and your application MUST NOT call it.
************************************************************************************************************************
*/

void  OS_PrioInsert (OS_PRIO  prio)
{
#if   (OS_CFG_PRIO_MAX <= (CPU_CFG_DATA_SIZE * 8u))             /* Optimize for less than word size nbr of priorities   */
    /* 将 prio 对应 bit 置 1：该就绪优先级桶变为非空。 */
    OSPrioTbl[0] |= (CPU_DATA)1u << (((CPU_CFG_DATA_SIZE * 8u) - 1u) - prio);


#elif (OS_CFG_PRIO_MAX <= (2u * (CPU_CFG_DATA_SIZE * 8u)))      /* Optimize for    2x the word size nbr of priorities   */
    if (prio < (CPU_CFG_DATA_SIZE * 8u)) {
        OSPrioTbl[0] |= (CPU_DATA)1u << (((CPU_CFG_DATA_SIZE * 8u) - 1u) - prio);
    } else {
        OSPrioTbl[1] |= (CPU_DATA)1u << (((CPU_CFG_DATA_SIZE * 8u) - 1u) - (prio - (CPU_CFG_DATA_SIZE * 8u)));
    }


#else
    CPU_DATA  bit_nbr;
    OS_PRIO   ix;

    ix             = (OS_PRIO)(prio /  (CPU_CFG_DATA_SIZE * 8u));
    bit_nbr        = (CPU_DATA)prio & ((CPU_CFG_DATA_SIZE * 8u) - 1u);
    OSPrioTbl[ix] |= (CPU_DATA)1u << (((CPU_CFG_DATA_SIZE * 8u) - 1u) - bit_nbr);
#endif
}

/*
************************************************************************************************************************
*                                                   REMOVE PRIORITY
*
* Description: This function is called to remove a priority in the priority table.
*
* Arguments  : prio     is the priority to remove
*
* Returns    : none
*
* Note(s)    : 1) This function is INTERNAL to uC/OS-III and your application MUST NOT call it.
************************************************************************************************************************
*/

void  OS_PrioRemove (OS_PRIO  prio)
{
#if   (OS_CFG_PRIO_MAX <= (CPU_CFG_DATA_SIZE * 8u))             /* Optimize for less than word size nbr of priorities   */
    /* 将 prio 对应 bit 清 0：该就绪优先级桶变为空。 */
    OSPrioTbl[0] &= ~((CPU_DATA)1u << (((CPU_CFG_DATA_SIZE * 8u) - 1u) - prio));


#elif (OS_CFG_PRIO_MAX <= (2u * (CPU_CFG_DATA_SIZE * 8u)))      /* Optimize for    2x the word size nbr of priorities   */
    if (prio < (CPU_CFG_DATA_SIZE * 8u)) {
        OSPrioTbl[0] &= ~((CPU_DATA)1u << (((CPU_CFG_DATA_SIZE * 8u) - 1u) - prio));
    } else {
        OSPrioTbl[1] &= ~((CPU_DATA)1u << (((CPU_CFG_DATA_SIZE * 8u) - 1u) - (prio - (CPU_CFG_DATA_SIZE * 8u))));
    }


#else
    CPU_DATA  bit_nbr;
    OS_PRIO   ix;

    ix             =   (OS_PRIO)(prio  /   (CPU_CFG_DATA_SIZE * 8u));
    bit_nbr        =   (CPU_DATA)prio  &  ((CPU_CFG_DATA_SIZE * 8u) - 1u);
    OSPrioTbl[ix] &= ~((CPU_DATA)  1u << (((CPU_CFG_DATA_SIZE * 8u) - 1u) - bit_nbr));
#endif
}
