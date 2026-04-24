/*
 * Copyright (c) 2023 Arm Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdint.h>
#include "stdio.h"
#include "timeout.h"
#include "cmsis_vio.h"

extern int stdout_init (void);

extern void vioSetValue(uint32_t id, int32_t value);
extern int32_t vioGetValue(uint32_t id);

static struct timeout_t timeout = {false, NULL};

/*
 * Semihosting is a mechanism that enables code running on an ARM target
 * to communicate and use the Input/Output facilities of a host computer
 * that is running a debugger.
 * There is an issue where if you use armclang at -O0 optimisation with
 * no parameters specified in the main function, the initialisation code
 * contains a breakpoint for semihosting by default. This will stop the
 * code from running before main is reached.
 * Semihosting can be disabled by defining __ARM_use_no_argv symbol
 * (or using higher optimization level).
 */
#if defined(__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050)
__asm("  .global __ARM_use_no_argv\n");
#endif

int main(void)
{
    /* Run Vio with 1s interval */
    uint32_t delay_ms = 1000;
    int32_t i = 0;
    /* Initialize timeout structure */
    timeout_init(&timeout, delay_ms);

    stdout_init();

    for (;;) {
        if (timeout_delay_is_elapsed(&timeout)) {
            char buffer[32] = {0};
            vioSetValue(0, i);
            snprintf(buffer, sizeof(buffer), "Vio value is set to %d\r\n", vioGetValue(0));
            printf("%s", buffer);
            i++;
        }
    }
}
