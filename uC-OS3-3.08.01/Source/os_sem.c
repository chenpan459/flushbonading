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
*                                         SEMAPHORE MANAGEMENT
*
* File    : os_sem.c
* Version : V3.08.01
*********************************************************************************************************
*/

#define  MICRIUM_SOURCE
#include "os.h"

#ifdef VSC_INCLUDE_SOURCE_FILE_NAMES
const  CPU_CHAR  *os_sem__c = "$Id: $";
#endif


#if (OS_CFG_SEM_EN > 0u)
/*
************************************************************************************************************************
*                                                  CREATE A SEMAPHORE
*
* Description: This function creates a semaphore.
*
* Arguments  : p_sem         is a pointer to the semaphore to initialize.  Your application is responsible for
*                            allocating storage for the semaphore.
*
*              p_name        is a pointer to the name you would like to give the semaphore.
*
*              cnt           is the initial value for the semaphore.
*                            If used to share resources, you should initialize to the number of resources available.
*                            If used to signal the occurrence of event(s) then you should initialize to 0.
*
*              p_err         is a pointer to a variable that will contain an error code returned by this function.
*
*                                OS_ERR_NONE                    If the call was successful
*                                OS_ERR_CREATE_ISR              If you called this function from an ISR
*                                OS_ERR_ILLEGAL_CREATE_RUN_TIME If you are trying to create the semaphore after you
*                                                                 called OSSafetyCriticalStart()
*                                OS_ERR_OBJ_PTR_NULL            If 'p_sem'  is a NULL pointer
*                                OS_ERR_OBJ_CREATED             If the semaphore was already created
*
* Returns    : none
*
* Note(s)    : none
************************************************************************************************************************
*/

void  OSSemCreate (OS_SEM      *p_sem,
                   CPU_CHAR    *p_name,
                   OS_SEM_CTR   cnt,
                   OS_ERR      *p_err)
{
    CPU_SR_ALLOC();


#ifdef OS_SAFETY_CRITICAL
    if (p_err == (OS_ERR *)0) {
        OS_SAFETY_CRITICAL_EXCEPTION();
        return;
    }
#endif

#ifdef OS_SAFETY_CRITICAL_IEC61508
    if (OSSafetyCriticalStartFlag == OS_TRUE) {
       *p_err = OS_ERR_ILLEGAL_CREATE_RUN_TIME;
        return;
    }
#endif

#if (OS_CFG_CALLED_FROM_ISR_CHK_EN > 0u)
    if (OSIntNestingCtr > 0u) {                                 /* Not allowed to be called from an ISR                 */
       *p_err = OS_ERR_CREATE_ISR;
        return;
    }
#endif

#if (OS_CFG_ARG_CHK_EN > 0u)
    if (p_sem == (OS_SEM *)0) {                                 /* Validate 'p_sem'                                     */
       *p_err = OS_ERR_OBJ_PTR_NULL;
        return;
    }
#endif

    /* 进入临界区后再写对象元数据，避免并发创建/访问导致状态不一致。 */
    CPU_CRITICAL_ENTER();
#if (OS_OBJ_TYPE_REQ > 0u)
#if (OS_CFG_OBJ_CREATED_CHK_EN > 0u)
    if (p_sem->Type == OS_OBJ_TYPE_SEM) {
        CPU_CRITICAL_EXIT();
        *p_err = OS_ERR_OBJ_CREATED;
        return;
    }
#endif
    p_sem->Type    = OS_OBJ_TYPE_SEM;                           /* Mark the data structure as a semaphore               */
#endif
    /* 计数语义：
     * - 资源型信号量：cnt 通常等于资源数；
     * - 事件型信号量：cnt 通常初始化为 0，等待 post 后变为可获取。 */
    p_sem->Ctr     = cnt;                                       /* Set semaphore value                                  */
#if (OS_CFG_TS_EN > 0u)
    p_sem->TS      = 0u;
#endif
#if (OS_CFG_DBG_EN > 0u)
    p_sem->NamePtr = p_name;                                    /* Save the name of the semaphore                       */
#else
    (void)p_name;
#endif
    /* 所有等待该信号量的任务都挂在 PendList 上，按优先级排队。 */
    OS_PendListInit(&p_sem->PendList);                          /* Initialize the waiting list                          */

#if (OS_CFG_DBG_EN > 0u)
    OS_SemDbgListAdd(p_sem);
    OSSemQty++;
#endif

    OS_TRACE_SEM_CREATE(p_sem, p_name);

    CPU_CRITICAL_EXIT();
   *p_err = OS_ERR_NONE;
}


/*
************************************************************************************************************************
*                                                  DELETE A SEMAPHORE
*
* Description: This function deletes a semaphore.
*
* Arguments  : p_sem         is a pointer to the semaphore to delete
*
*              opt           determines delete options as follows:
*
*                                OS_OPT_DEL_NO_PEND          Delete semaphore ONLY if no task pending
*                                OS_OPT_DEL_ALWAYS           Deletes the semaphore even if tasks are waiting.
*                                                            In this case, all the tasks pending will be readied.
*
*              p_err         is a pointer to a variable that will contain an error code returned by this function.
*
*                                OS_ERR_NONE                    The call was successful and the semaphore was deleted
*                                OS_ERR_DEL_ISR                 If you attempted to delete the semaphore from an ISR
*                                OS_ERR_ILLEGAL_DEL_RUN_TIME    If you are trying to delete the semaphore after you called
*                                                                 OSStart()
*                                OS_ERR_OBJ_PTR_NULL            If 'p_sem' is a NULL pointer
*                                OS_ERR_OBJ_TYPE                If 'p_sem' is not pointing at a semaphore
*                                OS_ERR_OPT_INVALID             An invalid option was specified
*                                OS_ERR_OS_NOT_RUNNING          If uC/OS-III is not running yet
*                                OS_ERR_TASK_WAITING            One or more tasks were waiting on the semaphore
*
* Returns    : == 0          if no tasks were waiting on the semaphore, or upon error.
*              >  0          if one or more tasks waiting on the semaphore are now readied and informed.
*
* Note(s)    : 1) This function must be used with care.  Tasks that would normally expect the presence of the semaphore
*                 MUST check the return code of OSSemPend().
*              2) Because ALL tasks pending on the semaphore will be readied, you MUST be careful in applications where
*                 the semaphore is used for mutual exclusion because the resource(s) will no longer be guarded by the
*                 semaphore.
************************************************************************************************************************
*/

#if (OS_CFG_SEM_DEL_EN > 0u)
OS_OBJ_QTY  OSSemDel (OS_SEM  *p_sem,
                      OS_OPT   opt,
                      OS_ERR  *p_err)
{
    OS_OBJ_QTY     nbr_tasks;
    OS_PEND_LIST  *p_pend_list;
    OS_TCB        *p_tcb;
    CPU_TS         ts;
    CPU_SR_ALLOC();


#ifdef OS_SAFETY_CRITICAL
    if (p_err == (OS_ERR *)0) {
        OS_SAFETY_CRITICAL_EXCEPTION();
        return (0u);
    }
#endif

    OS_TRACE_SEM_DEL_ENTER(p_sem, opt);

#ifdef OS_SAFETY_CRITICAL_IEC61508
    if (OSSafetyCriticalStartFlag == OS_TRUE) {
        OS_TRACE_SEM_DEL_EXIT(OS_ERR_ILLEGAL_DEL_RUN_TIME);
       *p_err = OS_ERR_ILLEGAL_DEL_RUN_TIME;
        return (0u);
    }
#endif

#if (OS_CFG_CALLED_FROM_ISR_CHK_EN > 0u)
    if (OSIntNestingCtr > 0u) {                                 /* Not allowed to delete a semaphore from an ISR        */
        OS_TRACE_SEM_DEL_EXIT(OS_ERR_DEL_ISR);
       *p_err = OS_ERR_DEL_ISR;
        return (0u);
    }
#endif

#if (OS_CFG_INVALID_OS_CALLS_CHK_EN > 0u)
    if (OSRunning != OS_STATE_OS_RUNNING) {                     /* Is the kernel running?                               */
        OS_TRACE_SEM_DEL_EXIT(OS_ERR_OS_NOT_RUNNING);
       *p_err = OS_ERR_OS_NOT_RUNNING;
        return (0u);
    }
#endif

#if (OS_CFG_ARG_CHK_EN > 0u)
    if (p_sem == (OS_SEM *)0) {                                 /* Validate 'p_sem'                                     */
        OS_TRACE_SEM_DEL_EXIT(OS_ERR_OBJ_PTR_NULL);
       *p_err = OS_ERR_OBJ_PTR_NULL;
        return (0u);
    }
#endif

#if (OS_CFG_OBJ_TYPE_CHK_EN > 0u)
    if (p_sem->Type != OS_OBJ_TYPE_SEM) {                       /* Make sure semaphore was created                      */
        OS_TRACE_SEM_DEL_EXIT(OS_ERR_OBJ_TYPE);
       *p_err = OS_ERR_OBJ_TYPE;
        return (0u);
    }
#endif

    /* 删除语义必须在临界区内完成：检查等待链、摘链、清对象应是原子过程。 */
    CPU_CRITICAL_ENTER();
    p_pend_list = &p_sem->PendList;
    nbr_tasks   = 0u;
    switch (opt) {
        case OS_OPT_DEL_NO_PEND:                                /* Delete semaphore only if no task waiting             */
             /* 保守删除：只允许在无人等待时删除，避免破坏调用方同步契约。 */
             if (p_pend_list->HeadPtr == (OS_TCB *)0) {
#if (OS_CFG_DBG_EN > 0u)
                 OS_SemDbgListRemove(p_sem);
                 OSSemQty--;
#endif
                 OS_TRACE_SEM_DEL(p_sem);
                 OS_SemClr(p_sem);
                 CPU_CRITICAL_EXIT();
                *p_err = OS_ERR_NONE;
             } else {
                 CPU_CRITICAL_EXIT();
                *p_err = OS_ERR_TASK_WAITING;
             }
             break;

        case OS_OPT_DEL_ALWAYS:                                 /* Always delete the semaphore                          */
             /* 强制删除：将所有等待任务以“对象被删除”原因唤醒并返回错误。 */
#if (OS_CFG_TS_EN > 0u)
             ts = OS_TS_GET();                                  /* Get local time stamp so all tasks get the same time  */
#else
             ts = 0u;
#endif
             while (p_pend_list->HeadPtr != (OS_TCB *)0) {      /* Remove all tasks on the pend list                    */
                 p_tcb = p_pend_list->HeadPtr;
                 OS_PendAbort(p_tcb,
                              ts,
                              OS_STATUS_PEND_DEL);
                 nbr_tasks++;
             }
#if (OS_CFG_DBG_EN > 0u)
             OS_SemDbgListRemove(p_sem);
             OSSemQty--;
#endif
             OS_TRACE_SEM_DEL(p_sem);
             OS_SemClr(p_sem);
             CPU_CRITICAL_EXIT();
             OSSched();                                         /* Find highest priority task ready to run              */
            *p_err = OS_ERR_NONE;
             break;

        default:
             CPU_CRITICAL_EXIT();
            *p_err = OS_ERR_OPT_INVALID;
             break;
    }

    OS_TRACE_SEM_DEL_EXIT(*p_err);

    return (nbr_tasks);
}
#endif


/*
************************************************************************************************************************
*                                                  PEND ON SEMAPHORE
*
* Description: This function waits for a semaphore.
*
* Arguments  : p_sem         is a pointer to the semaphore
*
*              timeout       is an optional timeout period (in clock ticks).  If non-zero, your task will wait for the
*                            resource up to the amount of time (in 'ticks') specified by this argument.  If you specify
*                            0, however, your task will wait forever at the specified semaphore or, until the resource
*                            becomes available (or the event occurs).
*
*              opt           determines whether the user wants to block if the semaphore is available or not:
*
*                                OS_OPT_PEND_BLOCKING
*                                OS_OPT_PEND_NON_BLOCKING
*
*              p_ts          is a pointer to a variable that will receive the timestamp of when the semaphore was posted
*                            or pend aborted or the semaphore deleted.  If you pass a NULL pointer (i.e. (CPU_TS*)0)
*                            then you will not get the timestamp.  In other words, passing a NULL pointer is valid
*                            and indicates that you don't need the timestamp.
*
*              p_err         is a pointer to a variable that will contain an error code returned by this function.
*
*                                OS_ERR_NONE               The call was successful and your task owns the resource
*                                                          or, the event you are waiting for occurred
*                                OS_ERR_OBJ_DEL            If 'p_sem' was deleted
*                                OS_ERR_OBJ_PTR_NULL       If 'p_sem' is a NULL pointer
*                                OS_ERR_OBJ_TYPE           If 'p_sem' is not pointing at a semaphore
*                                OS_ERR_OPT_INVALID        If you specified an invalid value for 'opt'
*                                OS_ERR_OS_NOT_RUNNING     If uC/OS-III is not running yet
*                                OS_ERR_PEND_ABORT         If the pend was aborted by another task
*                                OS_ERR_PEND_ISR           If you called this function from an ISR and the result
*                                                          would lead to a suspension
*                                OS_ERR_PEND_WOULD_BLOCK   If you specified non-blocking but the semaphore was not
*                                                          available
*                                OS_ERR_SCHED_LOCKED       If you called this function when the scheduler is locked
*                                OS_ERR_STATUS_INVALID     Pend status is invalid
*                                OS_ERR_TIMEOUT            The semaphore was not received within the specified
*                                                          timeout
*                                OS_ERR_TICK_DISABLED      If kernel ticks are disabled and a timeout is specified
*
*
* Returns    : The current value of the semaphore counter or 0 if not available.
*
* Note(s)    : This API 'MUST NOT' be called from a timer callback function.
************************************************************************************************************************
*/

OS_SEM_CTR  OSSemPend (OS_SEM   *p_sem,
                       OS_TICK   timeout,
                       OS_OPT    opt,
                       CPU_TS   *p_ts,
                       OS_ERR   *p_err)
{
    OS_SEM_CTR  ctr;
    CPU_SR_ALLOC();


#if (OS_CFG_TS_EN == 0u)
    (void)p_ts;                                                /* Prevent compiler warning for not using 'ts'          */
#endif

#ifdef OS_SAFETY_CRITICAL
    if (p_err == (OS_ERR *)0) {
        OS_SAFETY_CRITICAL_EXCEPTION();
        return (0u);
    }
#endif

    OS_TRACE_SEM_PEND_ENTER(p_sem, timeout, opt, p_ts);

#if (OS_CFG_TICK_EN == 0u)
    if (timeout != 0u) {
       *p_err = OS_ERR_TICK_DISABLED;
        OS_TRACE_SEM_PEND_FAILED(p_sem);
        OS_TRACE_SEM_PEND_EXIT(OS_ERR_TICK_DISABLED);
        return (0u);
    }
#endif

#if (OS_CFG_CALLED_FROM_ISR_CHK_EN > 0u)
    if (OSIntNestingCtr > 0u) {                                 /* Not allowed to call from an ISR                      */
        if ((opt & OS_OPT_PEND_NON_BLOCKING) != OS_OPT_PEND_NON_BLOCKING) {
            OS_TRACE_SEM_PEND_FAILED(p_sem);
            OS_TRACE_SEM_PEND_EXIT(OS_ERR_PEND_ISR);
           *p_err = OS_ERR_PEND_ISR;
            return (0u);
        }
    }
#endif

#if (OS_CFG_INVALID_OS_CALLS_CHK_EN > 0u)
    if (OSRunning != OS_STATE_OS_RUNNING) {                     /* Is the kernel running?                               */
        OS_TRACE_SEM_PEND_EXIT(OS_ERR_OS_NOT_RUNNING);
       *p_err = OS_ERR_OS_NOT_RUNNING;
        return (0u);
    }
#endif

#if (OS_CFG_ARG_CHK_EN > 0u)
    if (p_sem == (OS_SEM *)0) {                                 /* Validate 'p_sem'                                     */
        OS_TRACE_SEM_PEND_EXIT(OS_ERR_OBJ_PTR_NULL);
       *p_err = OS_ERR_OBJ_PTR_NULL;
        return (0u);
    }
    switch (opt) {                                              /* Validate 'opt'                                       */
        case OS_OPT_PEND_BLOCKING:
        case OS_OPT_PEND_NON_BLOCKING:
             break;

        default:
             OS_TRACE_SEM_PEND_FAILED(p_sem);
             OS_TRACE_SEM_PEND_EXIT(OS_ERR_OPT_INVALID);
            *p_err = OS_ERR_OPT_INVALID;
             return (0u);
    }
#endif

#if (OS_CFG_OBJ_TYPE_CHK_EN > 0u)
    if (p_sem->Type != OS_OBJ_TYPE_SEM) {                       /* Make sure semaphore was created                      */
        OS_TRACE_SEM_PEND_FAILED(p_sem);
        OS_TRACE_SEM_PEND_EXIT(OS_ERR_OBJ_TYPE);
       *p_err = OS_ERR_OBJ_TYPE;
        return (0u);
    }
#endif


    /* Pend 主流程：
     * 1) 若计数>0，直接消耗一个计数并返回（快速路径）；
     * 2) 若不可用且非阻塞，立即返回 WOULD_BLOCK；
     * 3) 否则挂入 pend 链并触发调度，等待 post/abort/timeout/del。 */
    CPU_CRITICAL_ENTER();
    if (p_sem->Ctr > 0u) {                                      /* Resource available?                                  */
        /* 快速路径：当前就能拿到信号量，不发生任务切换。 */
        p_sem->Ctr--;                                           /* Yes, caller may proceed                              */
#if (OS_CFG_TS_EN > 0u)
        if (p_ts != (CPU_TS *)0) {
           *p_ts = p_sem->TS;                                   /* get timestamp of last post                           */
        }
#endif
        ctr   = p_sem->Ctr;
        OS_TRACE_SEM_PEND(p_sem);
        CPU_CRITICAL_EXIT();
        OS_TRACE_SEM_PEND_EXIT(OS_ERR_NONE);
       *p_err = OS_ERR_NONE;
        return (ctr);
    }

    if ((opt & OS_OPT_PEND_NON_BLOCKING) != 0u) {               /* Caller wants to block if not available?              */
        /* 非阻塞模式：资源不可用时立刻返回，不进入等待链。 */
#if (OS_CFG_TS_EN > 0u)
        if (p_ts != (CPU_TS *)0) {
           *p_ts = 0u;
        }
#endif
        ctr   = p_sem->Ctr;                                     /* No                                                   */
        CPU_CRITICAL_EXIT();
        OS_TRACE_SEM_PEND_FAILED(p_sem);
        OS_TRACE_SEM_PEND_EXIT(OS_ERR_PEND_WOULD_BLOCK);
       *p_err = OS_ERR_PEND_WOULD_BLOCK;
        return (ctr);
    } else {                                                    /* Yes                                                  */
        if (OSSchedLockNestingCtr > 0u) {                       /* Can't pend when the scheduler is locked              */
#if (OS_CFG_TS_EN > 0u)
            if (p_ts != (CPU_TS *)0) {
               *p_ts = 0u;
            }
#endif
            CPU_CRITICAL_EXIT();
            OS_TRACE_SEM_PEND_FAILED(p_sem);
            OS_TRACE_SEM_PEND_EXIT(OS_ERR_SCHED_LOCKED);
           *p_err = OS_ERR_SCHED_LOCKED;
            return (0u);
        }
    }

    /* 阻塞路径：记录等待关系（对象=信号量、等待类型=SEM、可选超时）并让出 CPU。 */
    OS_Pend((OS_PEND_OBJ *)((void *)p_sem),                     /* Block task pending on Semaphore                      */
            OSTCBCurPtr,
            OS_TASK_PEND_ON_SEM,
            timeout);
    CPU_CRITICAL_EXIT();
    OS_TRACE_SEM_PEND_BLOCK(p_sem);
    OSSched();                                                  /* Find the next highest priority task ready to run     */

    CPU_CRITICAL_ENTER();
    /* 被重新调度回来后，根据 PendStatus 区分唤醒原因。 */
    switch (OSTCBCurPtr->PendStatus) {
        case OS_STATUS_PEND_OK:                                 /* We got the semaphore                                 */
#if (OS_CFG_TS_EN > 0u)
             if (p_ts != (CPU_TS *)0) {
                *p_ts = OSTCBCurPtr->TS;
             }
#endif
             OS_TRACE_SEM_PEND(p_sem);
            *p_err = OS_ERR_NONE;
             break;

        case OS_STATUS_PEND_ABORT:                              /* Indicate that we aborted                             */
#if (OS_CFG_TS_EN > 0u)
             if (p_ts != (CPU_TS *)0) {
                *p_ts = OSTCBCurPtr->TS;
             }
#endif
             OS_TRACE_SEM_PEND_FAILED(p_sem);
            *p_err = OS_ERR_PEND_ABORT;
             break;

        case OS_STATUS_PEND_TIMEOUT:                            /* Indicate that we didn't get semaphore within timeout */
#if (OS_CFG_TS_EN > 0u)
             if (p_ts != (CPU_TS *)0) {
                *p_ts = 0u;
             }
#endif
             OS_TRACE_SEM_PEND_FAILED(p_sem);
            *p_err = OS_ERR_TIMEOUT;
             break;

        case OS_STATUS_PEND_DEL:                                /* Indicate that object pended on has been deleted      */
#if (OS_CFG_TS_EN > 0u)
             if (p_ts != (CPU_TS *)0) {
                *p_ts = OSTCBCurPtr->TS;
             }
#endif
             OS_TRACE_SEM_PEND_FAILED(p_sem);
            *p_err = OS_ERR_OBJ_DEL;
             break;

        default:
             OS_TRACE_SEM_PEND_FAILED(p_sem);
            *p_err = OS_ERR_STATUS_INVALID;
             CPU_CRITICAL_EXIT();
             OS_TRACE_SEM_PEND_EXIT(*p_err);
             return (0u);
    }
    ctr = p_sem->Ctr;
    CPU_CRITICAL_EXIT();
    OS_TRACE_SEM_PEND_EXIT(*p_err);
    return (ctr);
}


/*
************************************************************************************************************************
*                                             ABORT WAITING ON A SEMAPHORE
*
* Description: This function aborts & readies any tasks currently waiting on a semaphore.  This function should be used
*              to fault-abort the wait on the semaphore, rather than to normally signal the semaphore via OSSemPost().
*
* Arguments  : p_sem     is a pointer to the semaphore
*
*              opt       determines the type of ABORT performed:
*
*                            OS_OPT_PEND_ABORT_1          ABORT wait for a single task (HPT) waiting on the semaphore
*                            OS_OPT_PEND_ABORT_ALL        ABORT wait for ALL tasks that are  waiting on the semaphore
*                            OS_OPT_POST_NO_SCHED         Do not call the scheduler
*
*              p_err     is a pointer to a variable that will contain an error code returned by this function.
*
*                            OS_ERR_NONE                  At least one task waiting on the semaphore was readied and
*                                                         informed of the aborted wait; check return value for the
*                                                         number of tasks whose wait on the semaphore was aborted.
*                            OS_ERR_OBJ_PTR_NULL          If 'p_sem' is a NULL pointer.
*                            OS_ERR_OBJ_TYPE              If 'p_sem' is not pointing at a semaphore
*                            OS_ERR_OPT_INVALID           If you specified an invalid option
*                            OS_ERR_OS_NOT_RUNNING        If uC/OS-III is not running yet
*                            OS_ERR_PEND_ABORT_ISR        If you called this function from an ISR
*                            OS_ERR_PEND_ABORT_NONE       No task were pending
*
* Returns    : == 0          if no tasks were waiting on the semaphore, or upon error.
*              >  0          if one or more tasks waiting on the semaphore are now readied and informed.
*
* Note(s)    : none
************************************************************************************************************************
*/

#if (OS_CFG_SEM_PEND_ABORT_EN > 0u)
OS_OBJ_QTY  OSSemPendAbort (OS_SEM  *p_sem,
                            OS_OPT   opt,
                            OS_ERR  *p_err)
{
    OS_PEND_LIST  *p_pend_list;
    OS_TCB        *p_tcb;
    CPU_TS         ts;
    OS_OBJ_QTY     nbr_tasks;
    CPU_SR_ALLOC();


#ifdef OS_SAFETY_CRITICAL
    if (p_err == (OS_ERR *)0) {
        OS_SAFETY_CRITICAL_EXCEPTION();
        return (0u);
    }
#endif

#if (OS_CFG_CALLED_FROM_ISR_CHK_EN > 0u)
    if (OSIntNestingCtr > 0u) {                                 /* Not allowed to Pend Abort from an ISR                */
       *p_err =  OS_ERR_PEND_ABORT_ISR;
        return (0u);
    }
#endif

#if (OS_CFG_INVALID_OS_CALLS_CHK_EN > 0u)
    if (OSRunning != OS_STATE_OS_RUNNING) {                     /* Is the kernel running?                               */
       *p_err = OS_ERR_OS_NOT_RUNNING;
        return (0u);
    }
#endif

#if (OS_CFG_ARG_CHK_EN > 0u)
    if (p_sem == (OS_SEM *)0) {                                 /* Validate 'p_sem'                                     */
       *p_err =  OS_ERR_OBJ_PTR_NULL;
        return (0u);
    }
    switch (opt) {                                              /* Validate 'opt'                                       */
        case OS_OPT_PEND_ABORT_1:
        case OS_OPT_PEND_ABORT_ALL:
        case OS_OPT_PEND_ABORT_1   | OS_OPT_POST_NO_SCHED:
        case OS_OPT_PEND_ABORT_ALL | OS_OPT_POST_NO_SCHED:
             break;

        default:
            *p_err =  OS_ERR_OPT_INVALID;
             return (0u);
    }
#endif

#if (OS_CFG_OBJ_TYPE_CHK_EN > 0u)
    if (p_sem->Type != OS_OBJ_TYPE_SEM) {                       /* Make sure semaphore was created                      */
       *p_err =  OS_ERR_OBJ_TYPE;
        return (0u);
    }
#endif

    /* Abort 流程：把等待任务的 pend 状态改为 ABORT 并转就绪。 */
    CPU_CRITICAL_ENTER();
    p_pend_list = &p_sem->PendList;
    if (p_pend_list->HeadPtr == (OS_TCB *)0) {                  /* Any task waiting on semaphore?                       */
        CPU_CRITICAL_EXIT();                                    /* No                                                   */
       *p_err =  OS_ERR_PEND_ABORT_NONE;
        return (0u);
    }

    nbr_tasks = 0u;
#if (OS_CFG_TS_EN > 0u)
    ts        = OS_TS_GET();                                    /* Get local time stamp so all tasks get the same time  */
#else
    ts        = 0u;
#endif
    while (p_pend_list->HeadPtr != (OS_TCB *)0) {
        p_tcb = p_pend_list->HeadPtr;
        OS_PendAbort(p_tcb,
                     ts,
                     OS_STATUS_PEND_ABORT);
        nbr_tasks++;
        if (opt != OS_OPT_PEND_ABORT_ALL) {                     /* Pend abort all tasks waiting?                        */
            /* 只中止一个等待者（队首最高优先级）。 */
            break;                                              /* No                                                   */
        }
    }
    CPU_CRITICAL_EXIT();

    if ((opt & OS_OPT_POST_NO_SCHED) == 0u) {
        OSSched();                                              /* Run the scheduler                                    */
    }

   *p_err = OS_ERR_NONE;
    return (nbr_tasks);
}
#endif


/*
************************************************************************************************************************
*                                                 POST TO A SEMAPHORE
*
* Description: This function signals a semaphore.
*
* Arguments  : p_sem    is a pointer to the semaphore
*
*              opt      determines the type of POST performed:
*
*                           OS_OPT_POST_1            POST and ready only the highest priority task waiting on semaphore
*                                                    (if tasks are waiting).
*                           OS_OPT_POST_ALL          POST to ALL tasks that are waiting on the semaphore
*
*                           OS_OPT_POST_NO_SCHED     Do not call the scheduler
*
*
*              p_err    is a pointer to a variable that will contain an error code returned by this function.
*
*                           OS_ERR_NONE              The call was successful and the semaphore was signaled
*                           OS_ERR_OBJ_PTR_NULL      If 'p_sem' is a NULL pointer
*                           OS_ERR_OBJ_TYPE          If 'p_sem' is not pointing at a semaphore
*                           OS_ERR_OPT_INVALID       If you specified an invalid option
*                           OS_ERR_OS_NOT_RUNNING    If uC/OS-III is not running yet
*                           OS_ERR_SEM_OVF           If the post would cause the semaphore count to overflow
*
* Returns    : The current value of the semaphore counter or 0 upon error.
*
* Note(s)    : 1) OS_OPT_POST_NO_SCHED can be added with one of the other options.
************************************************************************************************************************
*/

OS_SEM_CTR  OSSemPost (OS_SEM  *p_sem,
                       OS_OPT   opt,
                       OS_ERR  *p_err)
{
    OS_SEM_CTR     ctr;
    OS_PEND_LIST  *p_pend_list;
    OS_TCB        *p_tcb;
    OS_TCB        *p_tcb_next;
    CPU_TS         ts;
    CPU_SR_ALLOC();


#ifdef OS_SAFETY_CRITICAL
    if (p_err == (OS_ERR *)0) {
        OS_SAFETY_CRITICAL_EXCEPTION();
        return (0u);
    }
#endif

    OS_TRACE_SEM_POST_ENTER(p_sem, opt);

#if (OS_CFG_INVALID_OS_CALLS_CHK_EN > 0u)
    if (OSRunning != OS_STATE_OS_RUNNING) {                     /* Is the kernel running?                               */
        OS_TRACE_SEM_POST_EXIT(OS_ERR_OS_NOT_RUNNING);
       *p_err = OS_ERR_OS_NOT_RUNNING;
        return (0u);
    }
#endif

#if (OS_CFG_ARG_CHK_EN > 0u)
    if (p_sem == (OS_SEM *)0) {                                 /* Validate 'p_sem'                                     */
        OS_TRACE_SEM_POST_FAILED(p_sem);
        OS_TRACE_SEM_POST_EXIT(OS_ERR_OBJ_PTR_NULL);
       *p_err  = OS_ERR_OBJ_PTR_NULL;
        return (0u);
    }
    switch (opt) {                                              /* Validate 'opt'                                       */
        case OS_OPT_POST_1:
        case OS_OPT_POST_ALL:
        case OS_OPT_POST_1   | OS_OPT_POST_NO_SCHED:
        case OS_OPT_POST_ALL | OS_OPT_POST_NO_SCHED:
             break;

        default:
             OS_TRACE_SEM_POST_FAILED(p_sem);
             OS_TRACE_SEM_POST_EXIT(OS_ERR_OPT_INVALID);
            *p_err =  OS_ERR_OPT_INVALID;
             return (0u);
    }
#endif

#if (OS_CFG_OBJ_TYPE_CHK_EN > 0u)
    if (p_sem->Type != OS_OBJ_TYPE_SEM) {                       /* Make sure semaphore was created                      */
        OS_TRACE_SEM_POST_FAILED(p_sem);
        OS_TRACE_SEM_POST_EXIT(OS_ERR_OBJ_TYPE);
       *p_err = OS_ERR_OBJ_TYPE;
        return (0u);
    }
#endif
#if (OS_CFG_TS_EN > 0u)
    ts = OS_TS_GET();                                           /* Get timestamp                                        */
#else
    ts = 0u;
#endif

    OS_TRACE_SEM_POST(p_sem);
    /* Post 主流程：
     * - 无等待者：递增计数（受溢出保护）；
     * - 有等待者：直接唤醒一个或全部等待任务，不累加计数。 */
    CPU_CRITICAL_ENTER();
    p_pend_list = &p_sem->PendList;
    if (p_pend_list->HeadPtr == (OS_TCB *)0) {                  /* Any task waiting on semaphore?                       */
        /* 无等待任务时，post 等价于计数 +1。 */
        if (p_sem->Ctr == (OS_SEM_CTR)-1) {
           CPU_CRITICAL_EXIT();
          *p_err = OS_ERR_SEM_OVF;
           OS_TRACE_SEM_POST_EXIT(*p_err);
           return (0u);
        }
        p_sem->Ctr++;                                           /* No                                                   */
        ctr       = p_sem->Ctr;
#if (OS_CFG_TS_EN > 0u)
        p_sem->TS = ts;                                         /* Save timestamp in semaphore control block            */
#endif
        CPU_CRITICAL_EXIT();
       *p_err     = OS_ERR_NONE;
        OS_TRACE_SEM_POST_EXIT(*p_err);
        return (ctr);
    }

    /* 有等待任务时，post 直接把资源交给等待者（优先级顺序）。 */
    p_tcb = p_pend_list->HeadPtr;
    while (p_tcb != (OS_TCB *)0) {
        p_tcb_next = p_tcb->PendNextPtr;
        OS_Post((OS_PEND_OBJ *)((void *)p_sem),
                p_tcb,
                (void *)0,
                0u,
                ts);
        if ((opt & OS_OPT_POST_ALL) == 0u) {                     /* Post to all tasks waiting?                           */
            /* 默认只唤醒一个（队首最高优先级）。 */
            break;                                              /* No                                                   */
        }
        p_tcb = p_tcb_next;
    }
    CPU_CRITICAL_EXIT();
    if ((opt & OS_OPT_POST_NO_SCHED) == 0u) {
        OSSched();                                              /* Run the scheduler                                    */
    }
   *p_err = OS_ERR_NONE;

    OS_TRACE_SEM_POST_EXIT(*p_err);
    return (0u);
}


/*
************************************************************************************************************************
*                                                    SET SEMAPHORE
*
* Description: This function sets the semaphore count to the value specified as an argument.  Typically, this value
*              would be 0 but of course, we can set the semaphore to any value.
*
*              You would typically use this function when a semaphore is used as a signaling mechanism
*              and, you want to reset the count value.
*
* Arguments  : p_sem     is a pointer to the semaphore
*
*              cnt       is the new value for the semaphore count.  You would pass 0 to reset the semaphore count.
*
*              p_err     is a pointer to a variable that will contain an error code returned by this function.
*
*                            OS_ERR_NONE           The call was successful and the semaphore value was set
*                            OS_ERR_OBJ_PTR_NULL   If 'p_sem' is a NULL pointer
*                            OS_ERR_OBJ_TYPE       If 'p_sem' is not pointing to a semaphore
*                            OS_ERR_SET_ISR        If called from an ISR
*                            OS_ERR_TASK_WAITING   If tasks are waiting on the semaphore
*
* Returns    : None
*
* Note(s)    : none
************************************************************************************************************************
*/

#if (OS_CFG_SEM_SET_EN > 0u)
void  OSSemSet (OS_SEM      *p_sem,
                OS_SEM_CTR   cnt,
                OS_ERR      *p_err)
{
    OS_PEND_LIST  *p_pend_list;
    CPU_SR_ALLOC();


#ifdef OS_SAFETY_CRITICAL
    if (p_err == (OS_ERR *)0) {
        OS_SAFETY_CRITICAL_EXCEPTION();
        return;
    }
#endif

#if (OS_CFG_CALLED_FROM_ISR_CHK_EN > 0u)
    if (OSIntNestingCtr > 0u) {                                 /* Can't call this function from an ISR                 */
       *p_err = OS_ERR_SET_ISR;
        return;
    }
#endif

#if (OS_CFG_ARG_CHK_EN > 0u)
    if (p_sem == (OS_SEM *)0) {                                 /* Validate 'p_sem'                                     */
       *p_err = OS_ERR_OBJ_PTR_NULL;
        return;
    }
#endif

#if (OS_CFG_OBJ_TYPE_CHK_EN > 0u)
    if (p_sem->Type != OS_OBJ_TYPE_SEM) {                       /* Make sure semaphore was created                      */
       *p_err = OS_ERR_OBJ_TYPE;
        return;
    }
#endif

   *p_err = OS_ERR_NONE;
    /* Set 仅修改计数值，不直接唤醒任务。
     * 语义上通常用于“重置事件计数”，而不是替代 post。 */
    CPU_CRITICAL_ENTER();
    if (p_sem->Ctr > 0u) {                                      /* See if semaphore already has a count                 */
        p_sem->Ctr = cnt;                                       /* Yes, set it to the new value specified.              */
    } else {
        p_pend_list = &p_sem->PendList;                         /* No                                                   */
        if (p_pend_list->HeadPtr == (OS_TCB *)0) {              /* See if task(s) waiting?                              */
            p_sem->Ctr = cnt;                                   /* No, OK to set the value                              */
        } else {
           /* 有等待者时拒绝直接改值，防止破坏等待语义。 */
           *p_err      = OS_ERR_TASK_WAITING;
        }
    }
    CPU_CRITICAL_EXIT();
}
#endif


/*
************************************************************************************************************************
*                                           CLEAR THE CONTENTS OF A SEMAPHORE
*
* Description: This function is called by OSSemDel() to clear the contents of a semaphore
*

* Argument(s): p_sem      is a pointer to the semaphore to clear
*              -----
*
* Returns    : none
*
* Note(s)    : 1) This function is INTERNAL to uC/OS-III and your application MUST NOT call it.
************************************************************************************************************************
*/

void  OS_SemClr (OS_SEM  *p_sem)
{
    /* 删除对象后的统一清理：类型清空、计数清零、时间戳清零、等待链复位。 */
#if (OS_OBJ_TYPE_REQ > 0u)
    p_sem->Type    = OS_OBJ_TYPE_NONE;                          /* Mark the data structure as a NONE                    */
#endif
    p_sem->Ctr     = 0u;                                        /* Set semaphore value                                  */
#if (OS_CFG_TS_EN > 0u)
    p_sem->TS      = 0u;                                        /* Clear the time stamp                                 */
#endif
#if (OS_CFG_DBG_EN > 0u)
    p_sem->NamePtr = (CPU_CHAR *)((void *)"?SEM");
#endif
    OS_PendListInit(&p_sem->PendList);                          /* Initialize the waiting list                          */
}


/*
************************************************************************************************************************
*                                        ADD/REMOVE SEMAPHORE TO/FROM DEBUG LIST
*
* Description: These functions are called by uC/OS-III to add or remove a semaphore to/from the debug list.
*
* Arguments  : p_sem     is a pointer to the semaphore to add/remove
*
* Returns    : none
*
* Note(s)    : These functions are INTERNAL to uC/OS-III and your application should not call it.
************************************************************************************************************************
*/

#if (OS_CFG_DBG_EN > 0u)
void  OS_SemDbgListAdd (OS_SEM  *p_sem)
{
    /* 头插到调试链表，便于 kernel-aware 调试器遍历系统所有信号量。 */
    p_sem->DbgNamePtr               = (CPU_CHAR *)((void *)" ");
    p_sem->DbgPrevPtr               = (OS_SEM *)0;
    if (OSSemDbgListPtr == (OS_SEM *)0) {
        p_sem->DbgNextPtr           = (OS_SEM *)0;
    } else {
        p_sem->DbgNextPtr           =  OSSemDbgListPtr;
        OSSemDbgListPtr->DbgPrevPtr =  p_sem;
    }
    OSSemDbgListPtr                 =  p_sem;
}


void  OS_SemDbgListRemove (OS_SEM  *p_sem)
{
    OS_SEM  *p_sem_next;
    OS_SEM  *p_sem_prev;


    p_sem_prev = p_sem->DbgPrevPtr;
    p_sem_next = p_sem->DbgNextPtr;

    /* 三种摘链场景：头结点 / 尾结点 / 中间结点。 */
    if (p_sem_prev == (OS_SEM *)0) {
        OSSemDbgListPtr = p_sem_next;
        if (p_sem_next != (OS_SEM *)0) {
            p_sem_next->DbgPrevPtr = (OS_SEM *)0;
        }
        p_sem->DbgNextPtr = (OS_SEM *)0;

    } else if (p_sem_next == (OS_SEM *)0) {
        p_sem_prev->DbgNextPtr = (OS_SEM *)0;
        p_sem->DbgPrevPtr      = (OS_SEM *)0;

    } else {
        p_sem_prev->DbgNextPtr =  p_sem_next;
        p_sem_next->DbgPrevPtr =  p_sem_prev;
        p_sem->DbgNextPtr      = (OS_SEM *)0;
        p_sem->DbgPrevPtr      = (OS_SEM *)0;
    }
}
#endif
#endif
