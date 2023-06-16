/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef _FIRMWARE_BACKUP_H
#define _FIRMWARE_BACKUP_H

/* Access backup regs like a block of memory with addr range of [0:83]
 * An out-of-bounds get returns 0.  An out-of-bounds put is a no-op.
 */

void backup_init (void);

uint8_t backup_get (uint8_t addr);
void backup_put (uint8_t addr, uint8_t val);

uint8_t backup_get_from_isr (uint8_t addr);
void backup_put_from_isr (uint8_t addr, uint8_t val);

#endif /* !_FIRMWARE_BACKUP_H */

/*
 * vi:ts=4 sw=4 expandtab
 */
