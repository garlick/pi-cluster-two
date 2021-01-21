/* SPDX-License-Identifier: GPL-3.0-or-later */

#if HAVE_CONFIG_H
# include "config.h"
#endif
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>

#include "src/libbramble/bramble.h"

static int usage (void)
{
    fprintf (stderr, "Usage: bramble firmware-version\n");
    return 1;
}

int firmware_version_main (int argc, char *argva[])
{
    int fd;
    uint8_t version;

    if (argc != 1)
        return usage ();
    fd = i2c_open (BRAMBLE_I2C_DEVICE, I2C_ADDRESS);
    if (fd < 0) {
        fprintf (stderr, "%s:0x%x: %s",
                 BRAMBLE_I2C_DEVICE, I2C_ADDRESS, strerror (errno));
        return 1;
    }
    if (i2c_read (fd, I2C_REG_VERSION, &version, 1) < 0) {
        fprintf (stderr, "%s:0x%x: could not read register %d: %s",
                 BRAMBLE_I2C_DEVICE, I2C_ADDRESS, I2C_REG_VERSION,
                 strerror (errno));
        return 1;
    }
    printf ("%d\n", version);
    close (fd);
    return 0;
}

/*
 * vi:ts=4 sw=4 expandtab
 */

