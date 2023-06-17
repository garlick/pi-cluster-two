/* SPDX-License-Identifier: GPL-3.0-or-later */

/* power.c - control power of raspberry pi
 *
 * pi pmic
 * - write PB14 (0=off, 1=on), connected to pi GLOBAL_EN
 * - read PA8 (0=off, 1=on), connecting to pi RUN_PG
 *
 * soft shutdown
 * - pulse PA4 low and then high to trigger gpio-shutdown overlay on pi
 * - wait for PA5 to transition high to low indicating shutdown complete
 *
 * N.B. RUN_PG is 3V3, but GLOBAL_EN is pulled externally to 5V.
 * PB14 is a 5V tolerant pin.
 */

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/adc.h>

#include "trace.h"
#include "power.h"
#include "matrix.h"
#include "backup.h"

static TaskHandle_t shutdown_task_handle = NULL;

static bool pi_run_pg_get (void)
{
    if (gpio_get (GPIOA, GPIO8))
        return true;
    return false;
}

static void pi_global_en_set (bool val)
{
    if (val)
        gpio_set (GPIOB, GPIO14);
    else
        gpio_clear (GPIOB, GPIO14);
}

/* Read from port configured as output should return the last value written.
 */
static bool pi_global_en_get (void)
{
    if (gpio_get (GPIOB, GPIO14))
        return true;
    return false;
}

bool power_get_state (void)
{
    return pi_run_pg_get ();
}

void power_set_state (bool val)
{
    /* If GLOBAL_EN is high and RUN_PG is low, the pi may have turned off
     * its own PMIC (e.g. shutdown -h).  In that case, GLOBAL_EN needs to be
     * pulsed low for >= 1ms.
     */
    if (val && pi_global_en_get () && !pi_run_pg_get ()) {
        pi_global_en_set (false);
        vTaskDelay (pdMS_TO_TICKS (1));
    }

    /* Set GLOBAL_EN to desired state, then wait for RUN_PG to change before
     * returning.
     */
    pi_global_en_set (val);
    while (pi_run_pg_get () != val)
        vTaskDelay (pdMS_TO_TICKS (1));
}

static void power_task (void *args __attribute((unused)))
{
    const int loop_msec = 10;
    const int button_short_msec = 30; // debounce limit
    const int button_long_msec = 2000;
    bool button_press = false;
    int button_msec = 0;
    uint8_t prev_run_pg = 0xff; // impossible value to force update

    for (;;) {
        /* Ensure that the red LED and backup register tracks RUN_PG.
         * N.B. avoid updating the backup register on every loop iteration
         * since doing so may be a bit costly.
         */
        uint8_t run_pg = pi_run_pg_get () ? 1 : 0;
        if (run_pg != prev_run_pg) {
            matrix_set_red (run_pg);
            backup_put (0, run_pg);
            prev_run_pg = run_pg;
        }

        /* Perform OS friendly shutdown on short press of power button,
         * or hard power off on long press.  Or if the power is off, turn it
         * on with a short press.
         */
        if (!gpio_get (GPIOA, GPIO3)) { // button depressed
            if (button_press)
                button_msec += loop_msec;
            else {
                button_press = true;
                button_msec = 0;
            }
            if (button_msec >= button_long_msec) {
                power_set_state (false);
            }
            else if (button_msec >= button_short_msec) {
                if (power_get_state ()) {
                    power_shutdown ();
                }
                else {
                    power_set_state (true);
                }
            }
        }
        else { // button released
            button_press = false;
        }

        vTaskDelay (pdMS_TO_TICKS (loop_msec));
    }
}

/* Pulse the PI_HALT line low momentarily when notified by power_shutdown ().
 */
static void shutdown_task (void *args __attribute((unused)))
{
    for (;;) {
        ulTaskNotifyTake (pdTRUE, portMAX_DELAY);

        gpio_clear (GPIOA, GPIO4);
        vTaskDelay (pdMS_TO_TICKS (1000));
        gpio_set (GPIOA, GPIO4);
    }
}

void power_shutdown (void)
{
    xTaskNotifyGive (shutdown_task_handle);
}

bool power_is_shutdown (void)
{
    return gpio_get (GPIOA, GPIO5) ? false : true;
}

void power_init (bool por_flag)
{
    rcc_periph_clock_enable (RCC_GPIOA);
    rcc_periph_clock_enable (RCC_GPIOB);

    /* Configure inputs
     */
    gpio_set_mode (GPIOA,
                   GPIO_MODE_INPUT,
                   GPIO_CNF_INPUT_FLOAT,
                   GPIO8); // RUN_PG (pulled to +3V3 with 10K on the pi)
    gpio_set_mode (GPIOA,
                   GPIO_MODE_INPUT,
                   GPIO_CNF_INPUT_FLOAT,
                   GPIO5); // RUNNING
    gpio_set_mode (GPIOA,
                   GPIO_MODE_INPUT,
                   GPIO_CNF_INPUT_PULL_UPDOWN,
                   GPIO3); // BUTTON
    gpio_set (GPIOA, GPIO3); // pull up

    /* Configure outputs
     * N.B. the GLOBAL_EN output will revert to an input during reset,
     * possibly allowing the pi PMIC to turn on briefly until we resolve
     * it below.
     */
    gpio_set (GPIOA, GPIO4);
    gpio_set_mode (GPIOA,
                   GPIO_MODE_OUTPUT_2_MHZ,
                   GPIO_CNF_OUTPUT_OPENDRAIN,
                   GPIO4); // SHUTDOWN (active low)

    gpio_set (GPIOB, GPIO14);
    gpio_set_mode (GPIOB,
                   GPIO_MODE_OUTPUT_2_MHZ,
                   GPIO_CNF_OUTPUT_OPENDRAIN,
                   GPIO14); // GLOBAL_EN (pulled to +5V with 100K on the pi)

    /* Lower GLOBAL_EN if this is a power on reset (vs warm reset)
     * OR if previous state from backup register is off.
     * Backup register preserves last power state across a reset
     * (only a warm reset if battery is not attached).
     */
    if (por_flag || backup_get (0) == 0)
        gpio_clear (GPIOB, GPIO14);

    if (por_flag)
        backup_put (0, 0);

    xTaskCreate (power_task,
                "power",
                 100,
                 NULL,
                 configMAX_PRIORITIES - 1,
                 NULL);

    xTaskCreate (shutdown_task,
                 "shutdown",
                 200,
                 NULL,
                 configMAX_PRIORITIES - 1,
                 &shutdown_task_handle);
}

/*
 * vi:ts=4 sw=4 expandtab
 */
