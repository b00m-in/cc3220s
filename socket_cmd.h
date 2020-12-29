/*
 * Copyright (c) 2016, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __SOCKET_CMD_H__
#define __SOCKET_CMD_H__

#include <stdint.h>
#include <ti/drivers/net/wifi/simplelink.h>

/* Check NWP generation includes*/
#include <ti/devices/cc32xx/inc/hw_gprcm.h>
#include <ti/devices/cc32xx/inc/hw_memmap.h>
#include <ti/devices/cc32xx/inc/hw_types.h>

#include "semaphore.h"
#include "uart_term.h"

#define BSD_SOCKET_ERROR        ( \
        "BSD Socket error, please refer \"BSD SOCKET ERRORS CODES\" section in errors.h")
#define SL_SOCKET_ERROR         ( \
        "Socket error, please refer \"SOCKET ERRORS CODES\" section in errors.h")
#define NETAPP_ERROR            ( \
        "Netapp error, please refer \"NETAPP ERRORS CODES\" section in errors.h")
#define DEVICE_ERROR            ( \
        "Device error, please refer \"NETAPP ERRORS CODES\" section in errors.h")

#define MAX_BUF_SIZE            (1400)
#define MAX_TEXT_PAD_SIZE       (256)
#define WLAN_SCAN_COUNT         (30)

// check the error code and handle it
#define ASSERT_ON_ERROR(error_code) \
    { \
        if(error_code < 0) \
        { \
            UART_PRINT("Error code %d \n", error_code); \
            return error_code; \
        } \
    }

#define ASSERT_ON_ERROR1(ret, errortype)\
        {\
            if(ret < 0)\
            {\
                SHOW_WARNING(ret, errortype);\
                return -1;\
            }\
        }
#define SHOW_WARNING(ret, errortype)        UART_PRINT( \
        "\n\r[line:%d, error code:%d] %s\n\r", __LINE__, ret, errortype);
typedef union
{
    uint32_t ipv4;          /* Ipv4 Address */
    uint8_t ipv6[16];       /* Ipv6 Address */
}ip_t;
typedef union
{
    SlSockAddrIn6_t in6;       /* Socket info for Ipv6 */
    SlSockAddrIn_t in4;        /* Socket info for Ipv4 */
}sockAddr_t;
typedef union
{
    uint8_t                    nwData[MAX_BUF_SIZE];
    int8_t                     textPad[MAX_TEXT_PAD_SIZE];
    SlWlanNetworkEntry_t       netEntries[WLAN_SCAN_COUNT];
    SlWlanExtNetworkEntry_t    extNetEntries[WLAN_SCAN_COUNT];
}gDataBuffer_t;
typedef struct connectionControlBlock_t
{
    sem_t    connectEventSyncObj;
    sem_t    ip4acquireEventSyncObj;
    sem_t    ip6acquireEventSyncObj;
    sem_t    eventCompletedSyncObj;
    uint32_t GatewayIP;
    uint8_t  ConnectionSSID[SL_WLAN_SSID_MAX_LENGTH +1];
    uint8_t  ConnectionBSSID[SL_WLAN_BSSID_LENGTH];
    uint32_t DestinationIp;
    uint32_t IpAddr;
    uint32_t StaIp;
    uint32_t Ipv6Addr[4];
}connection_CB;
typedef struct appControlBlock_t
{
    /* Status Variables */
    /* This bit-wise status variable shows the state of the NWP */
    uint32_t Status;                    
     /* This field keeps the device's role (STA, P2P or AP) */
    uint32_t Role;                     
    /* This flag lets the application to exit */
    uint32_t Exit;                      
    /* Sets the number of Ping attempts to send */
    uint32_t PingAttempts;                  
    /* Data & Network entry Union */
    gDataBuffer_t gDataBuffer;

    /* STA/AP mode CB */
    connection_CB CON_CB;

    /* WoWLAN semaphore */
    sem_t WowlanSleepSem;

}appControlBlock;

extern appControlBlock app_CB;
char *ssidName;
//char *deviceName;
_u8 *myDeviceName;
char *email; //_u32
char *coords; //_u32
char *temp;
uint8_t *slaveAddress;

void PrintIPAddress(unsigned char ipv6,
                    void *ip);

#endif /* __SOCKET_CMD_H__ */
