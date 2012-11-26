/*
 * common.c
 *
 * This file is part of libnetmd, a library for accessing Sony NetMD devices.
 *
 * Copyright (C) 2004 Bertrik Sikken
 * Copyright (C) 2011 Alexander Sulfrian
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <string.h>
#include <unistd.h>

#include "common.h"
#include "const.h"
#include "log.h"

#define NETMD_POLL_TIMEOUT 1000	/* miliseconds */
#define NETMD_SEND_TIMEOUT 1000
#define NETMD_RECV_TIMEOUT 1000
#define NETMD_RECV_TRIES 30

#ifdef WIN32
    #include <windows.h>
    #define msleep(x) Sleep(x)
#else
    #define msleep(x) usleep(1000*x)
#endif

/*
  polls to see if minidisc wants to send data

  @param dev USB device handle
  @param buf pointer to poll buffer
  @param tries maximum attempts to poll the minidisc
  @return if error <0, else number of bytes that md wants to send
*/
static int netmd_poll(libusb_device_handle *dev, unsigned char *buf, int tries)
{
    int i;

    for (i = 0; i < tries; i++) {
        /* send a poll message */
        memset(buf, 0, 4);

        if (libusb_control_transfer(dev, LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR |
                            LIBUSB_RECIPIENT_INTERFACE, 0x01, 0, 0, buf, 4,
                            NETMD_POLL_TIMEOUT) < 0) {
            netmd_log(NETMD_LOG_ERROR, "netmd_poll: libusb_control_transfer failed\n");
            return NETMDERR_USB;
        }

        if (buf[0] != 0) {
            break;
        }

        if (i > 0) {
            msleep(1000);
        }
    }

    return buf[2];
}


int netmd_exch_message(netmd_dev_handle *devh, unsigned char *cmd,
                       const size_t cmdlen, unsigned char *rsp)
{
    netmd_send_message(devh, cmd, cmdlen);
    return netmd_recv_message(devh, rsp);
}


int netmd_send_message(netmd_dev_handle *devh, unsigned char *cmd,
                       const size_t cmdlen)
{
    unsigned char pollbuf[4];
    int	len;
    libusb_device_handle *dev;

    dev = (libusb_device_handle *)devh;

    /* poll to see if we can send data */
    len = netmd_poll(dev, pollbuf, 1);
    if (len != 0) {
        netmd_log(NETMD_LOG_ERROR, "netmd_exch_message: netmd_poll failed\n");
        return (len > 0) ? NETMDERR_NOTREADY : len;
    }

    /* send data */
    netmd_log(NETMD_LOG_DEBUG, "Command:\n");
    netmd_log_hex(NETMD_LOG_DEBUG, cmd, cmdlen);
    if (libusb_control_transfer(dev, LIBUSB_ENDPOINT_OUT | LIBUSB_REQUEST_TYPE_VENDOR |
                        LIBUSB_RECIPIENT_INTERFACE, 0x80, 0, 0, cmd, (int)cmdlen,
                        NETMD_SEND_TIMEOUT) < 0) {
        netmd_log(NETMD_LOG_ERROR, "netmd_exch_message: libusb_control_transfer failed\n");
        return NETMDERR_USB;
    }

    return 0;
}

int netmd_recv_message(netmd_dev_handle *devh, unsigned char* rsp)
{
    int len;
    unsigned char pollbuf[4];
    libusb_device_handle *dev;

    dev = (libusb_device_handle *)devh;

    /* poll for data that minidisc wants to send */
    len = netmd_poll(dev, pollbuf, NETMD_RECV_TRIES);
    if (len <= 0) {
        netmd_log(NETMD_LOG_ERROR, "netmd_exch_message: netmd_poll failed\n");
        return (len == 0) ? NETMDERR_TIMEOUT : len;
    }

    /* receive data */
    if (libusb_control_transfer(dev, LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR |
                        LIBUSB_RECIPIENT_INTERFACE, pollbuf[1], 0, 0, rsp, len,
                        NETMD_RECV_TIMEOUT) < 0) {
        netmd_log(NETMD_LOG_ERROR, "netmd_exch_message: libusb_control_transfer failed\n");
        return NETMDERR_USB;
    }

    netmd_log(NETMD_LOG_DEBUG, "Response:\n");
    netmd_log_hex(NETMD_LOG_DEBUG, rsp, (size_t)len);

    /* return length */
    return len;
}
