/* SPDX-License-Identifier: GPL-3.0-or-later */

#ifndef _BRAMBLE_CANLINUX_H
#define _BRAMBLE_CANLINUX_H

#include "canmsg.h"

#define BRAMBLE_CAN_INTERFACE   "can0"

int can_open (const char *name);
int can_recv (int fd, struct canmsg *msg);
int can_send (int fd, struct canmsg *msg);
int can_recv_timeout (int fd, struct canmsg *msg, double timeout);

#endif /* !_BRAMBLE_CANLINUX_H */

/*
 * vi:ts=4 sw=4 expandtab
 */
