/* SPDX-License-Identifier: GPL-3.0-or-later */

/* backup.c - bytewise access to 42 16-bit backup registers (84 bytes)
 */

#include "FreeRTOS.h"
#include "task.h"

#include <libopencm3/cm3/cortex.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/f1/bkp.h>
#include <libopencm3/stm32/f1/pwr.h>

#include "backup.h"

static void backup_unlock (void)
{
    PWR_CR |= PWR_CR_DBP;
}

static void backup_lock (void)
{
    PWR_CR &= ~PWR_CR_DBP;
}

static uint16_t backup_get_u16 (uint8_t addr)
{
    uint16_t v;

    switch (addr) {
        case 0: v = BKP_DR1; break;
        case 1: v = BKP_DR2; break;
        case 2: v = BKP_DR3; break;
        case 3: v = BKP_DR4; break;
        case 4: v = BKP_DR5; break;
        case 5: v = BKP_DR6; break;
        case 6: v = BKP_DR7; break;
        case 7: v = BKP_DR8; break;
        case 8: v = BKP_DR9; break;
        case 9: v = BKP_DR10; break;
        case 10: v = BKP_DR11; break;
        case 11: v = BKP_DR12; break;
        case 12: v = BKP_DR13; break;
        case 13: v = BKP_DR14; break;
        case 14: v = BKP_DR15; break;
        case 15: v = BKP_DR16; break;
        case 16: v = BKP_DR17; break;
        case 17: v = BKP_DR18; break;
        case 18: v = BKP_DR19; break;
        case 19: v = BKP_DR20; break;
        case 20: v = BKP_DR21; break;
        case 21: v = BKP_DR22; break;
        case 22: v = BKP_DR23; break;
        case 23: v = BKP_DR24; break;
        case 24: v = BKP_DR25; break;
        case 25: v = BKP_DR26; break;
        case 26: v = BKP_DR27; break;
        case 27: v = BKP_DR28; break;
        case 28: v = BKP_DR29; break;
        case 29: v = BKP_DR30; break;
        case 30: v = BKP_DR31; break;
        case 31: v = BKP_DR32; break;
        case 32: v = BKP_DR33; break;
        case 33: v = BKP_DR34; break;
        case 34: v = BKP_DR35; break;
        case 35: v = BKP_DR36; break;
        case 36: v = BKP_DR37; break;
        case 37: v = BKP_DR38; break;
        case 38: v = BKP_DR39; break;
        case 39: v = BKP_DR40; break;
        case 40: v = BKP_DR41; break;
        case 41: v = BKP_DR42; break;
        default: v = 0; break;
    }
    return v;
}

static void backup_put_u16 (uint8_t addr, uint16_t v)
{
    switch (addr) {
        case 0: BKP_DR1 = v; break;
        case 1: BKP_DR2 = v; break;
        case 2: BKP_DR3 = v; break;
        case 3: BKP_DR4 = v; break;
        case 4: BKP_DR5 = v; break;
        case 5: BKP_DR6 = v; break;
        case 6: BKP_DR7 = v; break;
        case 7: BKP_DR8 = v; break;
        case 8: BKP_DR9 = v; break;
        case 9: BKP_DR10 = v; break;
        case 10: BKP_DR11 = v; break;
        case 11: BKP_DR12 = v; break;
        case 12: BKP_DR13 = v; break;
        case 13: BKP_DR14 = v; break;
        case 14: BKP_DR15 = v; break;
        case 15: BKP_DR16 = v; break;
        case 16: BKP_DR17 = v; break;
        case 17: BKP_DR18 = v; break;
        case 18: BKP_DR19 = v; break;
        case 19: BKP_DR20 = v; break;
        case 20: BKP_DR21 = v; break;
        case 21: BKP_DR22 = v; break;
        case 22: BKP_DR23 = v; break;
        case 23: BKP_DR24 = v; break;
        case 24: BKP_DR25 = v; break;
        case 25: BKP_DR26 = v; break;
        case 26: BKP_DR27 = v; break;
        case 27: BKP_DR28 = v; break;
        case 28: BKP_DR29 = v; break;
        case 29: BKP_DR30 = v; break;
        case 30: BKP_DR31 = v; break;
        case 31: BKP_DR32 = v; break;
        case 32: BKP_DR33 = v; break;
        case 33: BKP_DR34 = v; break;
        case 34: BKP_DR35 = v; break;
        case 35: BKP_DR36 = v; break;
        case 36: BKP_DR37 = v; break;
        case 37: BKP_DR38 = v; break;
        case 38: BKP_DR39 = v; break;
        case 39: BKP_DR40 = v; break;
        case 40: BKP_DR41 = v; break;
        case 41: BKP_DR42 = v; break;
    }
}

static void backup_put_u8 (uint8_t addr, uint8_t v)
{
    uint16_t r;

    r = backup_get_u16 (addr / 2);
    if (addr % 2 == 0) {
        r &= ~0x00ff;
        r |= v;
    }
    else {
        r &= ~0xff00;
        r |= (uint16_t)v << 8;
    }
    backup_put_u16 (addr / 2, r);
}

static uint8_t backup_get_u8 (uint8_t addr)
{
    uint16_t r;

    r = backup_get_u16 (addr / 2);
    if (addr % 2 == 0)
        return r & 0xff;
    return (r >> 8);
}

uint8_t backup_get (uint8_t addr)
{
    uint8_t val;

    taskENTER_CRITICAL ();

    val = backup_get_u8 (addr);

    taskEXIT_CRITICAL ();
    return val;
}

uint8_t backup_get_from_isr (uint8_t addr)
{
    uint8_t val;

    UBaseType_t status = taskENTER_CRITICAL_FROM_ISR ();

    val = backup_get_u8 (addr);

    taskEXIT_CRITICAL_FROM_ISR (status);
    return val;
}

void backup_put (uint8_t addr, uint8_t val)
{
    taskENTER_CRITICAL ();

    backup_unlock ();
    backup_put_u8 (addr, val);
    backup_lock ();

    taskEXIT_CRITICAL ();
}

void backup_put_from_isr (uint8_t addr, uint8_t val)
{
    UBaseType_t status = taskENTER_CRITICAL_FROM_ISR ();

    backup_unlock ();
    backup_put_u8 (addr, val);
    backup_lock ();

    taskEXIT_CRITICAL_FROM_ISR (status);
}

void backup_init (void)
{
    rcc_peripheral_enable_clock (&RCC_APB1ENR, RCC_APB1ENR_PWREN);
    rcc_peripheral_enable_clock (&RCC_APB1ENR, RCC_APB1ENR_BKPEN);
}

/*
 * vi:ts=4 sw=4 expandtab
 */
