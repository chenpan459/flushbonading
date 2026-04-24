Copyright Arm Limited 2020-2023. All rights reserved.
Non-confidential

Releases
    version="1.0.0" => ARM.SSE_315_BSP.1.0.0.pack

Overview
    The SSE-315 BSP pack provides necessary source files to start application
    development on the Corstone-SSE-315 FVP model (Arm
    Corstone™ SSE-315 with Cortex®-M85 Example Subsystem)
    Further information on the pack usage is described in the pack user guide:
    Documents/SSE_315_BSP_User_Guide.pdf

Limitations
    1. This pack supports and is tested with
            - Arm compiler armclang 6,
            - Arm GCC version 11.3,
            - and IAR C/C++ compiler.
    2. This pack requires minimum CMSIS\5.9.0 for Cortex-M85 support.
    3. This pack requires minimum Keil MDK v5.38a for Cortex-M85 support.

Components
    I.   Platform description files:
          Set of header files that define the programming model of the platform.
          Peripheral structures and base addresses, system registers, pins and
          interrupts. Device configuration file and device definitions.
    II.  Device files:
          Contains startup, main function, compiler linker files, and a set of native drivers.
    III. CMSIS drivers:
          These files implement the CMSIS abstraction to the native drivers.
          Contains CMSIS-Driver implementation for the SSE-315 peripherals.
          This pack contains a newly defined CMSIS interface for the PPC and MPC
          peripherals.
    IV.  Documents:
          This pack contains the Technical Reference Manual (TRM) document for
          Corstone-315, the Technical Overview of Corstone-SSE-315 FVP.

Examples
    # Blinky example
      A reference blinky example for the pack. Please check
      SSE_315_BSP_Pack_User_Guide.pdf for details on how to
      build and run the example.
    # Vio example
      A reference vio example for the pack. Please check
      SSE_315_BSP_Pack_User_Guide.pdf for details on how to
      build and run the example.

Minimal include requirements
    The following headers have to be included for successful compilation:
    - SSE315.h              (Device header)
    - cmsis_driver_config.h     (Native drivers' configurations)
