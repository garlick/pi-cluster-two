/************************************************************\
 * Copyright 2023 Jim Garlick <garlick.jim@gmail.com>
 *
 * This file is part of the pi cluster II project
 * https://github.com/worlickwerx/pi-cluster-two
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
\************************************************************/

#include <string.h>

#include "librtos/FreeRTOS.h"
#include "librtos/task.h"

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/spi.h>
#include <libopencm3/cm3/nvic.h>

#include "blink.h"
#include "address.h"
#include "matrix.h"
#include "power.h"
#include "trace.h"
#include "canbus.h"
#include "canservices.h"
#include "serial.h"
#include "i2c.h"
#include "rtc.h"

bool por_flag = false;

/* Continue initialization after a short delay if por_flag is true.
 * The delay makes it more likely that hotplug insertion is complete
 * before we read the bus address pins or initialize the CAN peripheral.
 */
static void init_task (void *args __attribute((unused)))
{
    if (por_flag)
        vTaskDelay (pdMS_TO_TICKS (1000));

    address_init ();
    canbus_init ();
    canservices_init ();

    /* Display slot address
     */
    uint8_t addr = address_get ();
    char addrchr[] = { '0', '1', '2', '3',
                       '4', '5', '6', '7',
                       '8', '9', 'A', 'B',
                       'C', 'D', 'E', 'F' };

    matrix_set_char (addrchr[addr % 16]);
    vTaskDelay (pdMS_TO_TICKS (2000));
    matrix_set_char (' '); // clear

    /* init complete - block forever
     */
    ulTaskNotifyTake (pdTRUE, portMAX_DELAY);
}


int main (void)
{
    rcc_clock_setup_in_hse_8mhz_out_72mhz ();    // Use this for "blue pill"

    /* Pi power control subsystem needs to know if this is a board power-up
     * vs other reset so it can set GLOBAL_EN to an appropriate initial state.
     * N.B. power flags must be cleared since they persist across reset.
     */
    if ((RCC_CSR & RCC_CSR_PORRSTF))
        por_flag = true;
    RCC_CSR |= RCC_CSR_RMVF;

    blink_init ();
    matrix_init ();
    power_init (por_flag);
    serial_init ();
    i2c_init ();
    rtc_init ();

    xTaskCreate (init_task,
                 "init",
                 200,
                 NULL,
                 configMAX_PRIORITIES - 1,
                 NULL);

    trace_printf ("Hello world!\n");

    vTaskStartScheduler ();
    /*NOTREACHED*/
    for (;;)
        ;
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */
