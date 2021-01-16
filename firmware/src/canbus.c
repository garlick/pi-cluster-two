/* SPDX-License-Identifier: GPL-3.0-or-later */

/* canbus.c - handle CAN network
 *
 * Notes:
 * - All filters direct messages to FIFO 0
 *   FIFO 0 receiive ISR is enabled, FIFO 1 is not.
 * - FIFO 0 ISR stores messages in a FreeRTOS queue
 * - There is no s/w transmit queuing.  3 h/w transmit mailboxes sufficient?
 * - Error ISRs are not enabled.
 */

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/f1/nvic.h>
#include <libopencm3/stm32/can.h>

#include "FreeRTOS.h"
#include "queue.h"

#include "canbus.h"
#include "matrix.h"
#include "trace.h"
#include "canmsg.h"

static const uint32_t baudrate = 1000000;

#define CAN_RX_QUEUE_DEPTH  16
static QueueHandle_t canrxq;

int canbus_send (struct canmsg_raw *raw)
{
    if (can_transmit (CAN1,
                      raw->msgid,
                      raw->xmsgidf,
                      raw->rtrf,
                      raw->length,
                      (uint8_t *)raw->data) < 0)
        return -1;
    matrix_pulse_green (); // blink the activity LED
    return 0;
}

int canbus_recv (struct canmsg_raw *raw, int timeout)
{
    int ticks = timeout >= 0 ? pdMS_TO_TICKS (timeout) : portMAX_DELAY;

    if (xQueueReceive (canrxq, raw, ticks) == pdPASS) {
        matrix_pulse_green (); // blink the activity LED
        return 0;
    }
    return -1;
}

/* N.B. Left-justify 29 bit ID's in 32 bit registers.
 */
void canbus_filter_set (uint32_t nr, uint32_t id, uint32_t mask)
{
    can_filter_id_mask_32bit_init (nr, id << 3, mask << 3, 0, true);
}

void usb_lp_can_rx0_isr (void)
{
    if ((CAN_RF0R (CAN1) & CAN_RF0R_FMP0_MASK)) {
        struct canmsg_raw msg;
        uint8_t fmi;

        can_receive (CAN1,
                     0,
                     true,
                     &msg.msgid,
                     &msg.xmsgidf,
                     &msg.rtrf,
                     &fmi,
                     &msg.length,
                     msg.data,
                     NULL);

        xQueueSendToBackFromISR (canrxq, &msg, NULL);
    }
}

struct canbaud {
    uint32_t baud;
    uint32_t sjw;
    uint32_t ts1;
    uint32_t ts2;
    uint32_t prescaler;
};

// table generated using http://www.bittiming.can-wiki.info/
// assumes prescaler is clocked at 36 MHz (APB1/PCLK1)
static const struct canbaud btab[] = {
    {   10000, CAN_BTR_SJW_1TQ, CAN_BTR_TS1_13TQ, CAN_BTR_TS2_2TQ, 225 },
    {   50000, CAN_BTR_SJW_1TQ, CAN_BTR_TS1_13TQ, CAN_BTR_TS2_2TQ, 45 },
    {   83333, CAN_BTR_SJW_1TQ, CAN_BTR_TS1_13TQ, CAN_BTR_TS2_2TQ, 27 },
    {  125000, CAN_BTR_SJW_1TQ, CAN_BTR_TS1_13TQ, CAN_BTR_TS2_2TQ, 18 },
    {  250000, CAN_BTR_SJW_1TQ, CAN_BTR_TS1_13TQ, CAN_BTR_TS2_2TQ, 9 },
    {  500000, CAN_BTR_SJW_1TQ, CAN_BTR_TS1_10TQ, CAN_BTR_TS1_1TQ, 6 },
    {  800000, CAN_BTR_SJW_1TQ, CAN_BTR_TS1_12TQ, CAN_BTR_TS1_2TQ, 3 },
    { 1000000, CAN_BTR_SJW_1TQ, CAN_BTR_TS1_10TQ, CAN_BTR_TS1_1TQ, 3 },
};

static const struct canbaud *lookup_baud (uint32_t baud)
{
    unsigned int i;
    for (i = 0; i < sizeof (btab) / sizeof (btab[0]); i++)
        if (btab[i].baud == baud)
            return &btab[i];
    return NULL;
}

void canbus_init (void)
{
    const struct canbaud *baud;

    rcc_periph_clock_enable (RCC_AFIO);
    rcc_peripheral_enable_clock (&RCC_APB1ENR, RCC_APB1ENR_CAN1EN);
    rcc_periph_clock_enable (RCC_GPIOB);

    gpio_set_mode (GPIOB,
                   GPIO_MODE_OUTPUT_50_MHZ,
                   GPIO_CNF_OUTPUT_ALTFN_PUSHPULL,
                   GPIO_CAN_PB_TX); // PB9

    gpio_set_mode(GPIOB,
                  GPIO_MODE_INPUT,
                  GPIO_CNF_INPUT_FLOAT,
                  GPIO_CAN_PB_RX); // PB8

    gpio_primary_remap (AFIO_MAPR_SWJ_CFG_FULL_SWJ,
                        AFIO_MAPR_CAN1_REMAP_PORTB);

    can_reset (CAN1);

    if (!(baud = lookup_baud (baudrate)))
        FATAL ("unknown baud rate");

    can_init (CAN1,
              false,            // ttcm=off
              true,             // enable auto bus off management
              true,             // auto wakeup mode
              false,            // enable auto retransmission
              true,             // receive FIFO locked mode
              false,            // transmit FIFO priority (msg id)
              baud->sjw,        // baud rate params
              baud->ts1,
              baud->ts2,
              baud->prescaler,
              false,            // disable loopback mode
              false);           // disable silent mode

    canrxq = xQueueCreate (CAN_RX_QUEUE_DEPTH, sizeof (struct canmsg_raw));

    nvic_set_priority (NVIC_USB_LP_CAN_RX0_IRQ, 12<<4);
    nvic_enable_irq (NVIC_USB_LP_CAN_RX0_IRQ);
    can_enable_irq (CAN1, CAN_IER_FMPIE0);
}

/*
 * vi:ts=4 sw=4 expandtab
 */
