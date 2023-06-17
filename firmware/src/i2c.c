/* SPDX-License-Identifier: GPL-3.0-or-later */

/* i2c.c - I2C2 (follower) connected to pi (leader)
 *
 * Emulate DS1307 real time clock + nvram for linux rtc-ds1307 driver.
 *
 * The memory map for 56 bytes of nvram (0x08 - 0x3f) is repurposed so that
 * the pi can communicate with the firmware by mmaping the nvram special file
 * at /sys/bus/nvmem/devices/ds1307_nvram0/nvmem and reading and writing
 * various locations.
 *
 * The RTC registers (0x00 - 0x06) track the firmware wall clock.
 * hwclock(8) may be used to get and put the time/date via the linux driver.
 *
 * PB10 - SCL (connected to pi GPIO3)
 * PB11 - SDA (connected to pi GPIO2)
 *
 * Notes:
 * - See note in matrix.c about SMBA conflict on PB12 (worked around there).
 * - the DS1307 control register (0x07) is unimplemented
 */

#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/i2c.h>
#include <libopencm3/stm32/f1/nvic.h>

#include "trace.h"
#include "i2c.h"
#include "address.h"
#include "matrix.h"
#include "rtc.h"
#include "version.h"    // libbramble, for current git version
#include "nvram.h"      // libbramble, for nvram region mappings

#define FOLLOWER_ADDRESS 0x68 // DS1307 address from data sheet

/* Get/put function prototypes for special nvram regions.
 * These functions must be callable from the i2c ISR.
 */
typedef void (*nvram_put_f)(const void *buf, uint8_t size);
typedef void (*nvram_get_f)(void *buf, uint8_t size);

struct nvram_region {
    uint8_t addr;
    uint8_t size;
    nvram_put_f put;
    nvram_get_f get;
    uint8_t valid:1;
    uint8_t dirty:1;
};

#define NVRAM_RTC_OFFSET 8

#define NVRAM_SIZE 64
#define NVRAM_ADDR_MASK 0x3f

static uint8_t nvram[NVRAM_SIZE];


static void matrix_put_columns_from_isr (const void *buf, uint8_t size)
{
    if (size == 5)
        matrix_set_from_isr (buf);
}

static void matrix_put_char_from_isr (const void *buf, uint8_t size)
{
    if (size == 1)
        matrix_set_char_from_isr (*(const char *)buf);
}

static void address_get_from_isr (void *buf, uint8_t size)
{
    if (size == 1)
        *(char *)buf = address_get ();
}

static void version_get_from_isr (void *buf, uint8_t size)
{
    memset (buf, 0, size);
    strncpy (buf, BRAMBLE_VERSION_STRING, size - 1);
}

static uint8_t int_to_bcd (uint8_t i)
{
    uint8_t bcd = i % 10;

    bcd |= (i / 10) << 4;

    return bcd;
}

static uint8_t bcd_to_int (uint8_t bcd)
{
    uint8_t i = bcd & 0xf;

    i += (bcd >> 4) * 10;

    return i;
}

/* RTC registers are mapped read-only to nvmem for debug from linux.
 * The 8th byte is a count of the number of times the clock has been set
 * so we can infer how linux is managing the hwclock by watching nvmem.
 */
static void ds1307_plus_count_get_from_isr (uint8_t *buf, uint8_t size)
{
    if (size == 8) {
        struct rtc_time time;

        rtc_get_from_isr (&time);

        buf[0] = int_to_bcd (time.sec);
        buf[1] = int_to_bcd (time.min);
        buf[2] = int_to_bcd (time.hour);
        buf[3] = time.day;
        buf[4] = int_to_bcd (time.date);
        buf[5] = int_to_bcd (time.month);
        buf[6] = int_to_bcd (time.year);
        buf[7] = time.set_count;
    }
}

static void ds1307_get_from_isr (uint8_t *buf, uint8_t size)
{
    if (size == 7) {
        struct rtc_time time;

        rtc_get_from_isr (&time);

        buf[0] = int_to_bcd (time.sec);
        buf[1] = int_to_bcd (time.min);
        buf[2] = int_to_bcd (time.hour);
        buf[3] = time.day;
        buf[4] = int_to_bcd (time.date);
        buf[5] = int_to_bcd (time.month);
        buf[6] = int_to_bcd (time.year);
    }
}

static void ds1307_put_from_isr (const uint8_t *buf, uint8_t size)
{
    if (size == 7) {
        struct rtc_time time;

        time.sec = bcd_to_int (buf[0]);
        time.min = bcd_to_int (buf[1]);
        time.hour = bcd_to_int (buf[2]);
        time.day = buf[3];
        time.date = bcd_to_int (buf[4]);
        time.month = bcd_to_int (buf[5]);
        time.year = bcd_to_int (buf[6]);

        rtc_set_from_isr (&time);
    }
}

static void get_zeroes (void *buf, uint8_t size)
{
    memset (buf, 0, size);
}

/* Special nvram regions with accessors.
 */
static struct nvram_region nvtab[] = {
    {   .addr = 0,
        .size = 7,
        .put = (nvram_put_f)ds1307_put_from_isr,
        .get = (nvram_get_f)ds1307_get_from_isr,
    },
    {   .addr = 7,
        .size = 1,
        .put = NULL,
        .get = get_zeroes,
    }, // reserved DS1307
    {   .addr = NVRAM_MATRIX_COLUMNS_ADDR + NVRAM_RTC_OFFSET,
        .size = NVRAM_MATRIX_COLUMNS_SIZE,
        .put = matrix_put_columns_from_isr,
        .get = get_zeroes,
    },
    {   .addr = NVRAM_MATRIX_CHAR_ADDR + NVRAM_RTC_OFFSET,
        .size = NVRAM_MATRIX_CHAR_SIZE,
        .put = matrix_put_char_from_isr,
        .get = get_zeroes,
    },
    {   .addr = NVRAM_SLOT_ADDR + NVRAM_RTC_OFFSET,
        .size = NVRAM_SLOT_SIZE,
        .put = NULL,
        .get = address_get_from_isr,
    },
    {   .addr = NVRAM_VERSION_ADDR + NVRAM_RTC_OFFSET,
        .size = NVRAM_VERSION_SIZE,
        .put = NULL,
        .get = version_get_from_isr,
    },
    {   .addr = 48, // debug
        .size = 8,
        .put = NULL,
        .get = (nvram_get_f)ds1307_plus_count_get_from_isr,
    },
};

static void cache_invalidate (void)
{
    for (uint8_t i = 0; i < sizeof (nvtab) / sizeof (nvtab[0]); i++)
        nvtab[i].valid = 0;
}

static void cache_flush (void)
{
    for (uint8_t i = 0; i < sizeof (nvtab) / sizeof (nvtab[0]); i++) {
        if (nvtab[i].dirty) {
            if (nvtab[i].put)
                nvtab[i].put (&nvram[nvtab[i].addr], nvtab[i].size);
            nvtab[i].dirty = 0;
        }
    }
}

static struct nvram_region *find_region_containing (uint8_t addr)
{
    for (uint8_t i = 0; i < sizeof (nvtab) / sizeof (nvtab[0]); i++) {
        if (addr >= nvtab[i].addr && addr < nvtab[i].addr + nvtab[i].size)
            return &nvtab[i];
    }
    return NULL;
}

/* Read a byte from from nvram[] at base + offset.
 * If address exceeds nvram size, wrap around to 0.
 * If region needs to be "faulted in" (because invalid flag is set),
 * call its get() callback.  Region remains valid for the I2C transaction.
 */
static void nvram_get (uint8_t base, uint8_t offset, uint8_t *val)
{
    uint8_t addr = (base + offset) & NVRAM_ADDR_MASK;
    struct nvram_region *region = find_region_containing (addr);

    if (region && !region->valid) {
        if (region->get)
            region->get (&nvram[region->addr], region->size);
        region->valid = 1;
    }

    *val = nvram[addr];
}

/* Write a byte to nvram[] at base + offset.
 * If address exceeds nvram size, wrap around to 0.
 * Fault in region if it is not valid first.  Then make the change and
 * mark the region dirty so we can call its put() callback at the end
 * of the I2C transaction.
 */
static void nvram_put (uint8_t base, uint8_t offset, uint8_t value)
{
    uint8_t addr = (base + offset) & NVRAM_ADDR_MASK;
    struct nvram_region *region = find_region_containing (addr);

    if (region && !region->valid) {
        if (region->get)
            region->get (&nvram[region->addr], region->size);
        region->valid = 1;
    }

    nvram[addr] = value;

    if (region && region->put)
        region->dirty = 1;
}

void i2c2_ev_isr (void)
{
    static uint8_t base = 0;
    static uint8_t offset = 0;
    static uint8_t write_count = 0;
    uint32_t sr1 = I2C_SR1 (I2C2);

    /* Pi sends start + stm32 address.
     * N.B. in a "repeated start condition", leader may send another start in
     * lieu of a stop to perform multiple operations without releasing the bus.
     */
    if ((sr1 & I2C_SR1_ADDR)) {
        (void)I2C_SR2 (I2C2);
        write_count = 0;
        cache_invalidate ();
    }
    /* Pi writes to stm32.
     * The first byte after the follower has been addressed sets the base
     * address to be used for subsequent reads or writes.
     */
    else if ((sr1 & I2C_SR1_RxNE)) { // recv buf not empty
        uint8_t val = i2c_get_data (I2C2);

        if (write_count == 0) {
            base = val;
            offset = 0;
        }
        else {
            nvram_put (base, offset++, val);
        }
        write_count++;
    }
    /* Pi reads from stm32.
     * Assume the base + offset is valid on a read.
     */
    else if ((sr1 & I2C_SR1_TxE) && !(sr1 & I2C_SR1_BTF)) { // xmit buf empty
        uint8_t val;

        nvram_get (base, offset++, &val);
        i2c_send_data (I2C2, val);
    }
    else if ((sr1 & I2C_SR1_STOPF)) {
        i2c_peripheral_enable (I2C2);
        cache_flush ();
    }
    else if ((sr1 & I2C_SR1_AF)) {
        I2C_SR1 (I2C2) &= ~(I2C_SR1_AF);
        cache_flush ();
    }
}

void i2c_init (void)
{
    rcc_periph_clock_enable (RCC_AFIO);
    rcc_periph_clock_enable (RCC_GPIOB);
    rcc_periph_clock_enable (RCC_I2C2);

    nvic_set_priority (NVIC_I2C2_EV_IRQ, 12<<4);
    nvic_enable_irq (NVIC_I2C2_EV_IRQ);

    gpio_set_mode (GPIOB,
                   GPIO_MODE_OUTPUT_50_MHZ,
                   GPIO_CNF_OUTPUT_ALTFN_OPENDRAIN,
                   GPIO_I2C2_SCL | GPIO_I2C2_SDA); // PB10, PB11


    i2c_reset (I2C2);

    i2c_peripheral_disable (I2C2);
    i2c_set_speed (I2C2, i2c_speed_sm_100k, I2C_CR2_FREQ_36MHZ);
    i2c_set_own_7bit_slave_address (I2C2, FOLLOWER_ADDRESS);
    i2c_enable_interrupt (I2C2, I2C_CR2_ITEVTEN | I2C_CR2_ITBUFEN);
    i2c_peripheral_enable (I2C2);

    i2c_enable_ack (I2C2);
}

/*
 * vi:ts=4 sw=4 expandtab
 */
