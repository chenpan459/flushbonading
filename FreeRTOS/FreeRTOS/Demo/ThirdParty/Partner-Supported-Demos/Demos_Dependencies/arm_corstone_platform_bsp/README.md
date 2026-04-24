# Arm Corstone-3xx Platform CMSIS-Driver Based Board Support Package

## Overview 

Board support package (BSP) library for Corstone-3xx family of target platforms
implemented using the [CMSIS-Driver][cmsis-driver-documentation] software API.

Supported target platforms:
* [Corstone-300][corstone-300-documentation]
* [Corstone-310][corstone-310-documentation]

## Build System

The project uses [CMake][cmake-documentation] as its default build system.

## Selecting the target platform

You first need to select the supported target platform for which you want to
use the BSP.

This is done by setting the `ARM_CORSTONE_BSP_TARGET_PLATFORM` CMake variable
to the desired value. See the table below for all supported platforms:

| Platform      | ARM_CORSTONE_BSP_TARGET_PLATFORM |
| :-----------: | :------------------------------: |
| Corstone-300  | `corstone300`                    |
| Corstone-310  | `corstone310`                    |

We recommend you pass this variable in the command line at CMake build
configuration time by adding
`-DARM_CORSTONE_BSP_TARGET_PLATFORM=<target_platform>` where
`<target_platform>` is one of the supported values mentioned in the table
above.

!!! example

    ```sh
    cmake -B __build -DARM_CORSTONE_BSP_TARGET_PLATFORM=corstone300
    ```

## Linking with other projects
As a CMake based project, we provide CMake target libraries for each supported
target platforms to integrate the BSP with any other project that also support
CMake.

Follow the steps below to link the BSP with your project:
* Add the project root directory as a subdirectory to your CMake build:
    ```cmake
    add_subdirectory(arm-corstone-platform-bsp)
    ```
* Link your CMake target (executable or library) with the
  `arm-corstone-platform-bsp` CMake target.

    !!! example

        ```cmake
        target_link_libraries(<your_cmake_target> PRIVATE arm-corstone-platform-bsp)
        ```
    Where <your_cmake_target> is the name of the CMake target you want to link the BSP library with.

## Adding new target support

The Corstone platform BSP is not developed as part of this project, but only
distributed what is already released in CMSIS pack format in a git repository.
Therefore, before adding a new target support, ensure that there is a CMSIS
pack available for that target.

* Download CMSIS pack for the target from [here](https://www.keil.arm.com/packs/)
* Create a new folder in thr root directory with the target name
    > For example, corstone310 for Corstone-310 target
* Extract the contents of the downloaded CMSIS pack into the newly created
  folder
* Add a `CMakeLists.txt` to that folder to define how to build the target support
* Update the root [CMakeLists.txt](CMakeLists.txt) to add the new target support as a subdirectory

### Validating new target support

Currently, there are no examples in this project. Therefore, the only possible
option is to validate new target support is by running the blinky example in
the [Arm Featured Reference Integration](https://gitlab.arm.com/iot/aws/freertos/iot-reference-arm-corstone3xx).

* Update the `arm-corstone-platform-bsp` submodule in Arm Featured Reference Integration to include new target support
* Follow the [instructions](https://gitlab.arm.com/iot/aws/freertos/iot-reference-arm-corstone3xx/-/blob/main/Docs/blinky.md) to
  build and run the blinky example

[cmsis-driver-documentation]: https://www.keil.com/pack/doc/CMSIS/Driver/html/index.html
[corstone-300-documentation]: https://developer.arm.com/Processors/Corstone-300
[corstone-310-documentation]: https://developer.arm.com/Processors/Corstone-310
[cmake-documentation]: https://cmake.org/documentation/
