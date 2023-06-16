#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

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
#include "backup.h"

/* Perform initialization:
 * - show the card address on the matrix display
 */
static void init_task (void *args __attribute((unused)))
{
    uint8_t addr = address_get ();

    vTaskDelay (pdMS_TO_TICKS (200));

    matrix_set_char ('0' + addr / 10); // tens
    vTaskDelay (pdMS_TO_TICKS (1000));

    matrix_set_char ('0' + addr % 10); // ones
    vTaskDelay (pdMS_TO_TICKS (1000));

    matrix_set_char (' '); // clear

    /* init complete - block forever
     */
    ulTaskNotifyTake (pdTRUE, portMAX_DELAY);
}


int main (void)
{
    bool por_flag = false;

    rcc_clock_setup_in_hse_8mhz_out_72mhz ();    // Use this for "blue pill"

    /* Pi power control subsystem needs to know if this is a board power-up
     * vs other reset so it can set GLOBAL_EN to an appropriate initial state.
     * N.B. power flags must be cleared since they persist across reset.
     */
    if ((RCC_CSR & RCC_CSR_PORRSTF))
        por_flag = true;
    RCC_CSR |= RCC_CSR_RMVF;

    backup_init (); // used by power
    blink_init ();
    matrix_init ();
    address_init ();
    power_init (por_flag);
    serial_init ();
    canbus_init ();
    canservices_init ();
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
