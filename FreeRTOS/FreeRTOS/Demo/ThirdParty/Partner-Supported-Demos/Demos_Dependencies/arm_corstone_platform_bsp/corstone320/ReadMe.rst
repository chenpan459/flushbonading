Copyright Arm Limited 2020-2023. All rights reserved.
Non-confidential

Releases
    version="1.0.0" => ARM.SSE_320_BSP.1.0.0.pack

Overview
    The SSE-320 BSP pack provides necessary source files to start application
    development on the Corstone-SSE-320 FVP model (Arm
    Corstone™ SSE-320 with Cortex®-M85 and Ethos™-U85 Example Subsystem)
    Further information on the pack usage is described in the pack user guide:
    Documents/SSE_320_BSP_User_Guide.pdf

Limitations
    1. This pack supports and is tested with
            - Arm compiler armclang 6,
            - Arm GCC version 11.3,
            - IAR iccarm C/C++ compiler,
            - and LLVM Embedded Toolchain for Arm.
    2. This pack requires minimum CMSIS `v6.0.0`.
    3. This pack requires minimum CMSIS-Toolbox `v3.1.0`.

Components
    I.   Platform description files:
          Set of header files that define the programming model of the platform.
          Peripheral structures and base addresses, system registers, pins and
          interrupts. Device configuration file and device definitions.
    II.  Device files:
          Contains startup, main function, compiler linker files, and a set of native drivers.
    III. CMSIS drivers:
          These files implement the CMSIS abstraction to the native drivers.
          Contains CMSIS-Driver implementation for the SSE-320 peripherals.
          This pack contains a newly defined CMSIS interface for the PPC, MPC,
          KMU, SAM and LCM peripherals.
    IV.  Documents:
          This pack contains the Technical Reference Manual (TRM) document for
          Corstone-320, the Technical Overview of Corstone-SSE-320 FVP.

Known issues
    1. Some of the source files generate compile time warnings. Warnings can be
       removed from the project by selecting "AC5-like warnings" level inside
       the target options.

Examples
    # Blinky example
      A reference blinky example for the pack. Please check
      SSE_320_BSP_Pack_User_Guide.pdf for details on how to
      build and run the example.
    # Vio example
      A reference vio example for the pack. Please check
      SSE_320_BSP_Pack_User_Guide.pdf for details on how to
      build and run the example.

Minimal include requirements
    The following headers have to be included for successful compilation:
    - SSE320.h              (Device header)
    - cmsis_driver_config.h     (Native drivers' configurations)
