# uC-OS3-3.08.01 架构分析

## 1. 代码仓整体结构

`uC-OS3-3.08.01` 目录可分为 4 个层次：

- `Source/`：内核通用实现（与 CPU 架构无关），是主分析对象。
- `Ports/`：CPU/编译器移植层，每个架构目录通常有 `os_cpu.h`、`os_cpu_c.c`、`os_cpu_a.*`。
- `TLS/`：线程本地存储（TLS）在不同工具链下的实现。
- `Template/`：工程和 BSP 适配模板，便于新平台快速起步。

`Source/` 下核心文件职责：

- `os_core.c`：内核初始化、启动、调度器、中断进出、就绪/阻塞公共逻辑。
- `os_task.c`：任务生命周期管理（创建、删除、挂起、恢复、优先级、任务消息/信号量）。
- `os_tick.c`：系统节拍与延时/超时链表（delta-list）管理。
- `os_prio.c`：优先级位图（bitmap）与最高优先级查找。
- `os_tmr.c`：软件定时器管理与定时器任务。
- `os_time.c`：时间延时接口（如 `OSTimeDly`）与时基操作。
- `os_sem.c` / `os_mutex.c` / `os_q.c` / `os_flag.c`：同步与通信对象实现。
- `os_msg.c` / `os_mem.c`：消息池与内存块管理。
- `os_stat.c`：统计任务、CPU 占用率和运行时统计。
- `os_cfg_app.c`：由宏配置驱动的静态资源分配（栈、消息池、常量导出）。
- `os.h` / `os_type.h`：统一类型定义、配置开关、API 声明和内核对象定义。

---

## 2. 分层架构视角

从“接口—机制—硬件”角度看，uC/OS-III 采用典型分层：

1) **应用 API 层（Application API）**  
应用调用 `OSTaskCreate()`、`OSSemPend()`、`OSQPost()`、`OSTmrStart()`、`OSTimeDly()` 等。

2) **内核机制层（Kernel Core）**  
包含调度、任务状态机、就绪队列、挂起队列、超时队列、对象等待队列等。  
关键中心在 `os_core.c` + `os_task.c` + `os_tick.c` + `os_prio.c`。

3) **内核对象层（IPC/Sync Objects）**  
Semaphore / Mutex / Queue / Flag / Timer 各文件独立实现，但复用公共的 Pend/Post 流程。

4) **移植抽象层（CPU Port Layer）**  
通过 `OS_TASK_SW()`、`OSCtxSw()`、`OSIntCtxSw()`、`OSStartHighRdy()` 与具体 CPU 绑定。

5) **CPU/编译器相关层（Arch + Toolchain）**  
`Ports/<Arch>/<Compiler>/` 中的汇编与寄存器上下文切换实现。

---

## 3. 启动与运行主流程

### 3.1 初始化阶段：`OSInit()`

`OSInit()` 在 `os_core.c` 中组织初始化顺序，大致是：

- 初始化内核全局状态（中断嵌套计数、调度锁计数、当前/最高就绪任务指针等）。
- 调用 `OSInitHook()` 进入端口层初始化（来自 `os_cpu_c.c`）。
- 初始化优先级表 `OS_PrioInit()` 与就绪链表 `OS_RdyListInit()`。
- 按配置初始化子模块：内存池、消息池、TLS、任务管理、Idle 任务、Tick、统计任务、定时器。
- 调用 `OSCfg_Init()` 绑定和“保活”配置常量。
- 设置 `OSInitialized = OS_TRUE`。

### 3.2 启动调度：`OSStart()`

`OSStart()` 做三件关键事：

- 检查是否已初始化、是否至少存在一个应用任务。
- 计算最高优先级就绪任务，设置 `OSTCBCurPtr/OSTCBHighRdyPtr`。
- 调用端口函数 `OSStartHighRdy()`，切到首个任务上下文（理论上不返回）。

### 3.3 运行期调度

- **任务级调度**：`OSSched()`  
  在非中断上下文决定是否抢占，必要时执行 `OS_TASK_SW()`。

- **中断级调度**：`OSIntEnter()` / `OSIntExit()`  
  中断嵌套退出到最外层且调度器未锁时，`OSIntExit()` 会触发 `OSIntCtxSw()`。

- **调度锁机制**：`OSSchedLock()` / `OSSchedUnlock()`  
  通过嵌套计数抑制上下文切换，解锁后统一触发一次调度。

---

## 4. 核心数据结构与算法

### 4.1 优先级位图（`os_prio.c`）

- `OSPrioTbl[]` 用位图表示“哪些优先级有就绪任务”。
- `OS_PrioGetHighest()` 通过 `CPU_CntLeadZeros()` 快速取最高优先级（数值最小优先级最高）。
- 插入/删除就绪任务时同步更新位图，保证调度查找接近 O(1)。

### 4.2 就绪队列（Ready List）

- `OSRdyList[OS_CFG_PRIO_MAX]`：每个优先级一个双向链表。
- 同优先级多任务时支持队头/队尾操作，配合时间片实现轮转。
- `OS_RdyListMoveHeadToTail()` 用于 round-robin。

### 4.3 Pend 队列（对象等待队列）

- 任务等待对象时，进入对象的 `PendList`（按优先级有序插入）。
- `OS_Pend()` / `OS_Post()` / `OS_PendAbort()` 是多个对象模块共享的核心流程。
- 使得队列、信号量、事件标志、互斥量在“阻塞/唤醒语义”上保持一致。

### 4.4 Tick 超时链表（`os_tick.c`）

- 使用 delta-list（相对时间链表）管理延时与超时任务。
- `OS_TickListInsert()` 在链表中按剩余 tick 插入，并调整相邻节点差值。
- 该结构在“tick 递减 + 到期唤醒”场景下效率高，适合 RTOS 周期调度。

---

## 5. 任务与对象模型

### 5.1 任务管理（`os_task.c`）

- `OSTaskCreate()` 完成 TCB 初始化、栈初始化、可选 TLS/寄存器表初始化。
- 任务状态覆盖：`RDY`、`PEND`、`PEND_TIMEOUT`、`SUSPENDED`、`DLY` 等组合状态。
- 可动态改优先级 `OSTaskChangePrio()`，并处理互斥量优先级继承相关影响。

### 5.2 IPC 与同步对象

- `os_sem.c`：计数信号量。
- `os_mutex.c`：互斥量与优先级继承。
- `os_q.c` / `os_msg.c`：消息队列与消息池。
- `os_flag.c`：事件标志组（多 bit 条件同步）。
- 各对象都围绕“对象状态 + pend 列表 + post 唤醒策略”组织。

### 5.3 软件定时器（`os_tmr.c`）

- 定时器对象创建与状态管理在 `os_tmr.c`。
- 由定时器任务统一扫描/触发 callback，避免在中断里执行复杂回调逻辑。
- 支持 one-shot 和 periodic 两种模式。

---

## 6. 配置与可裁剪性

uC/OS-III 大量使用编译期宏裁剪功能，核心特征：

- `OS_CFG_xxx_EN` 控制模块启停（如 `OS_CFG_TMR_EN`、`OS_CFG_STAT_TASK_EN`）。
- `os_cfg_app.c` 根据配置分配静态资源（Idle/Stat/Tmr 栈、ISR 栈、消息池等）。
- 启用项越少，代码体积与 RAM 占用越低；但诊断能力与功能也随之下降。

这也是该内核适合资源受限 MCU 的关键原因之一。

---

## 7. 移植层接口要点（Porting Contract）

从 `Ports/Template` 看，平台移植最关键的契约是：

- 宏 `OS_TASK_SW()`：触发任务级切换。
- 函数 `OSCtxSw()`：任务上下文切换。
- 函数 `OSIntCtxSw()`：中断退出路径的上下文切换。
- 函数 `OSStartHighRdy()`：启动首个最高优先级任务。
- 钩子函数：`OSInitHook()`、`OSTaskSwHook()`、`OSIdleTaskHook()` 等。

即：**内核决定“何时切换”，端口层负责“如何切换”。**

---

## 8. 典型时序（简化）

### 8.1 从中断唤醒高优先级任务

1. ISR 入口调用 `OSIntEnter()`  
2. ISR 内 `Post` 某对象（可能使高优先级任务就绪）  
3. ISR 末尾调用 `OSIntExit()`  
4. `OSIntExit()` 判断条件满足后触发 `OSIntCtxSw()`  
5. 切换到最高优先级就绪任务执行

### 8.2 任务等待对象并超时

1. 任务执行 `Pend`（如 `OSSemPend`）  
2. 内部调用 `OS_Pend()`：从就绪队列移除并可插入 tick 超时链表  
3. 到期后由 tick 更新逻辑将任务恢复到就绪态  
4. 返回错误码指示超时

---

## 9. 代码阅读建议（面向二次开发）

- 先读 `os_core.c`，建立“启动—调度—中断”的主干认知。
- 再读 `os_task.c` + `os_tick.c`，理解任务状态迁移和超时机制。
- 之后按需求读对象模块：同步优先 `os_sem.c`/`os_mutex.c`，通信优先 `os_q.c`/`os_msg.c`。
- 移植相关直接对照 `Ports/Template` 与目标平台 `Ports/<arch>/...`。
- 最后核对 `os_cfg.h` 与 `os_cfg_app.c`，确认功能裁剪与资源规模是否匹配产品需求。

---

## 10. 架构结论

uC-OS3-3.08.01 的架构特点可概括为：

- **内核机制高度模块化**：调度、任务、tick、对象管理分离清晰。
- **实时性导向的数据结构**：优先级位图 + 多就绪链表 + delta tick 链表。
- **移植边界明确**：调度策略在内核，寄存器现场在端口层。
- **强配置驱动**：编译期裁剪能力强，便于覆盖从低端 MCU 到复杂 SoC 的场景。
- **工程可维护性好**：对象模块风格统一，便于按功能增量理解与定制。

