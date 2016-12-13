#include <WProgram.h>
#include "can.h"
#include "canmgr.h"
#include "activity.h"
#include "identify.h"
#include "target_power.h"
#include "target_reset.h"
#include "target_console.h"

static struct canmgr_payload canmgr_console_id;
static uint8_t canmgr_console_connected;

void console_recv (char *buf, int len, void *arg)
{
    if (canmgr_console_connected) {
        // send to connected id
    }
}

void canmgr_setup (uint32_t can_addr)
{
    int i;
    can0_begin (CANMGR_DST_MASK);
    // receive dst=can_addr
    for (i = 0; i < 8; i++)
        can0_setfilter (can_addr<<5, i);
    canmgr_console_connected = 0;
    target_console_set_receiver (console_recv, NULL);
}

// test: cansend 200#0000000001 to turn on
//       cansend 200#0000000000 to turn off
void canmgr_dispatch (struct canmgr_payload *pkt, uint8_t len)
{
    switch (pkt->object) {
        case CANOBJ_LED_IDENTIFY:
            if (len == 1)
                identify_set (pkt->data[0]);
            // FIXME: send ACK
            break;
        case CANOBJ_TARGET_POWER:
            if (len == 1)
                target_power_set (pkt->data[0]);
            // FIXME: send ACK
            break;
        case CANOBJ_TARGET_RESET:
            if (len == 1) {
                if (pkt->data[0] == 1 || pkt->data[0] == 0)
                    target_reset_set (pkt->data[0]);
                else
                    target_reset_pulse ();
            }
            // FIXME: send ACK
            break;
        case CANOBJ_TARGET_CONSOLECONN:
            if (!canmgr_console_connected) {
                canmgr_console_id.object = pkt->data[3]
                                         | ((pkt->data[2] & 3) << 8);
                canmgr_console_id.type = (pkt->data[2] >> 4) & 3;
                canmgr_console_id.node = ((pkt->data[2] >> 6) & 3)
                                       | (pkt->data[1] & 0x0F) << 2;
                canmgr_console_id.module = ((pkt->data[1] >> 4) & 0x0F)
                                         | (pkt->data[0] & 3) << 4;
                canmgr_console_id.cluster = pkt->data[0] >> 2;
                canmgr_console_connected = 1;
                // FIXME: send ACK
            } else  {
                // FIXME: send NAK
            }
            break;
        case CANOBJ_TARGET_CONSOLEDISC:
            // FIXME: check if "owner"
            canmgr_console_connected = 0;
            // FIXME: send ACK
            break;
    }
}

void canmgr_update (void)
{
    uint8_t buf[8];
    uint8_t len;
    uint32_t id;
    struct canmgr_payload pkt;

    if (can0_available ()) {
        activity_pulse ();
        if (can0_read (&id, &len, buf) && len >= 4) {
            if (len < 4)
                return; // runt
            pkt.object = buf[3];
            pkt.object |= (buf[2]&3)<<8;
            memcpy (pkt.data, &buf[4], 4);
            canmgr_dispatch (&pkt, len - 4);
        }
    }
}

void canmgr_finalize (void)
{
    can0_end ();
}

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */

