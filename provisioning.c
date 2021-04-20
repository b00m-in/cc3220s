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

/*****************************************************************************

   Application Name     - Provisioning application
   Application Overview - This application demonstrates how to use 
                          the provisioning method
                        in order to establish connection to the AP.
                        The application
                        connects to an AP and ping's the gateway to
                        verify the connection.

   Application Details  - Refer to 'Provisioning' README.html

*****************************************************************************/
//****************************************************************************
//
//! \addtogroup
//! @{
//
//****************************************************************************

/* Standard Include */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* TI-DRIVERS Header files */
#include <ti/drivers/net/wifi/simplelink.h>
#include <ti/drivers/SPI.h>
#include <ti/drivers/GPIO.h>
#include <ti/drivers/ADC.h>
#include <ti/net/utils/clock_sync.h>
#include <ti/drivers/net/wifi/slnetifwifi.h>
#include <uart_term.h>

#include <pthread.h>
#include <unistd.h>
#include "mqueue.h"
#include <time.h>
#include "external_provisioning_conf.h"
#include "socket_cmd.h"

/* Application Version and Naming*/
#define APPLICATION_NAME         "PROVISIONING"
#define APPLICATION_VERSION "01.00.00.14"

/* USER's defines */
#define SPAWN_TASK_PRIORITY                     (9)
#define TASK_STACK_SIZE                         (4096)
#define TIMER_TASK_STACK_SIZE                   (1024)
#define DISPLAY_TASK_STACK_SIZE                 (512)
#define SL_STOP_TIMEOUT                         (200)
/* Provisioning inactivity timeout in seconds (20 min)*/
#define PROVISIONING_INACTIVITY_TIMEOUT         (1200)     
/* 15 seconds */
#define RECONNECTION_ESTABLISHED_TIMEOUT_SEC    (15)  
/* 2 seconds */ 
#define CONNECTION_PHASE_TIMEOUT_SEC            (2) 
/* 1 second */   
#define PING_TIMEOUT_SEC                        (1)    

/* Enable UART Log */
#define LOG_MESSAGE_ENABLE
#ifdef LOG_MESSAGE_ENABLE
#define LOG_MESSAGE UART_PRINT
#else
#define LOG_MESSAGE NADA
#endif

#define SC_KEY_ENABLE       (0)
#define SECURED_AP_ENABLE   (0)


#define AP_SEC_TYPE         (SL_WLAN_SEC_TYPE_WPA_WPA2)
#define AP_SEC_PASSWORD     "1234567890"
#define SC_KEY              "1234567890123456"
#define DEST_IP_ADDR                SL_IPV4_VAL(192,168,1,100)

/* For Debug - Start Provisioning for each reset, or stay connected */
/* Used mainly in case of provisioning robustness tests             */
#define FORCE_PROVISIONING   (0)

/*#define ASSERT_ON_ERROR(error_code) \
            {\*/
            /* Handling the error-codes is specific to the application */ /* \
            if (error_code < 0) {LOG_MESSAGE("ERROR! %d \n\r",error_code); \
            gLedDisplayState = LedState_ERROR; } \*/
            /* else, continue w/ execution */ /*\
            }\*/

#define ROLE_STA 0
#define ROLE_AP  2

/* Application's states */
typedef enum
{
    AppState_STARTING,
    AppState_WAIT_FOR_CONNECTION,
    AppState_WAIT_FOR_IP,

    AppState_PINGING_GW,

    AppState_PROVISIONING_IN_PROGRESS,
    AppState_PROVISIONING_WAIT_COMPLETE,

    AppState_ERROR,
    AppState_MAX

}AppState;

/* Application's events */
typedef enum
{
    AppEvent_STARTED,
    AppEvent_CONNECTED,
    AppEvent_IP_ACQUIRED,
    AppEvent_DISCONNECT,

    AppEvent_PING_COMPLETE,

    AppEvent_PROVISIONING_STARTED,
    AppEvent_PROVISIONING_SUCCESS,
    AppEvent_PROVISIONING_STOPPED,
    AppEvent_PROVISIONING_WAIT_CONN,

    AppEvent_TIMEOUT,
    AppEvent_ERROR,
    AppEvent_RESTART,
    AppEvent_STUCK,
    AppEvent_MAX

}AppEvent;

 /* Function pointer to the event handler */
typedef int32_t (*fptr_EventHandler)(void);

/* Entry in the lookup table */
typedef struct _Provisioning_TableEntry_t_
{
    fptr_EventHandler   p_evtHndl;  /* Pointer to the event handler */
    AppState            nextState;  /* Next state of the application */
}Provisioning_TableEntry_t;

appControlBlock     app_CB;

/* LED State */
typedef enum
{
    LedState_CONNECTION,
    LedState_FACTORY_DEFAULT,
    LedState_ERROR
}LedState;

/****************************************************************************
                      LOCAL FUNCTION PROTOTYPES
****************************************************************************/
int32_t ReportError(void);
int32_t HandleConnection(void);
int32_t HandleDiscnctEvt(void);
int32_t DoNothing(void);
int32_t HandleProvisioningComplete(void);
int32_t HandleProvisioningTimeOut(void);
int32_t CheckLanConnection(void);
int32_t CheckInternetConnection(void);
int32_t HandleUserApplication(void);
int32_t HandleWaitForIp(void);
int32_t ProcessRestartRequest(void);
int32_t ProcessStartRequest(void);
int32_t ProvisioningStart(void);
int32_t SendPingToGW(void);
int32_t StartConnection(void);
int16_t SignalEvent(AppEvent event);
int32_t returnToFactoryDefault(void);
void    StopAsyncEvtTimer(void);
int32_t wlanConnect(void);

/****************************************************************************
                      GLOBAL VARIABLES
****************************************************************************/
pthread_t gProvisioningThread = (pthread_t)NULL;
pthread_t gDisplayThread = (pthread_t)NULL;
pthread_t gSpawnThread = (pthread_t)NULL;
pthread_t gAdcThread = (pthread_t)NULL;
//pthread_t gUart1Thread = (pthread_t)NULL;

extern void *adcThread(void *arg0);
//extern void *uart1Thread(void *arg0);
pthread_mutex_t voltageMutex;

uint8_t  gIsWlanConnected = 0;
uint8_t  gStopInProgress = 0;
uint32_t gPingSent = 0;
uint32_t gPingSuccess = 0;
uint8_t  gRole = ROLE_STA;
mqd_t    gProvisioningSMQueue;

uint16_t gLedCount = 0;
uint8_t  gLedState = 0;
uint32_t gErrledCount = 0;
uint8_t  gErrLedState = 0;
uint8_t  gWaitForConn = 0;
LedState gLedDisplayState = LedState_CONNECTION;

timer_t gTimer;
volatile bool forget = false;
bool once = false;
bool stuck = false;

const char *Roles[] = {"STA","STA","AP","P2P"};
const char *WlanStatus[] = {"DISCONNECTED","SCANING","CONNECTING","CONNECTED"};

/* Application lookup/transition table */
const Provisioning_TableEntry_t gTransitionTable[AppState_MAX][AppEvent_MAX] =
{
    /* AppState_STARTING */
    {
        /* Event: AppEvent_STARTED */ {StartConnection,
                                       AppState_WAIT_FOR_CONNECTION     },
        /* Event: AppEvent_CONNECTED */ {HandleWaitForIp, AppState_WAIT_FOR_IP },
        /* Event: AppEvent_IP_ACQUIRED */ {ReportError, AppState_ERROR      },
        /* Event: AppEvent_DISCONNECT */ {ReportError, AppState_ERROR       },
        /* Event: AppEvent_PING_COMPLETE */ {ReportError, AppState_ERROR    },
        /* AppEvent_PROVISIONING_STARTED */ {ReportError, AppState_ERROR    },
        /* AppEvent_PROVISIONING_SUCCESS */ {ReportError, AppState_ERROR    },
        /* AppEvent_PROVISIONING_STOPPED */ {ReportError, AppState_ERROR    },
        /* AppEvent_PROVISIONING_WAIT_CONN */ {ReportError, AppState_ERROR  },
        /* Event: AppEvent_TIMEOUT */ {ReportError, AppState_ERROR          },
        /* Event: AppEvent_ERROR */ {ReportError, AppState_ERROR            },
        /* Event: AppEvent_RESTART */ {ProvisioningStart,
                                       AppState_PROVISIONING_IN_PROGRESS},
        /* Event: AppEvent_STUCK */ {DoNothing, AppState_STARTING           }
    },
    /* AppState_WAIT_FOR_CONNECTION */
    {
        /* Event: AppEvent_STARTED */ 
        {StartConnection, AppState_WAIT_FOR_CONNECTION            },
        /** Event: AppEvent_CONNECTED */ 
        {HandleWaitForIp, AppState_WAIT_FOR_IP                    },
        /* Event: AppEvent_IP_ACQUIRED */
         {ReportError, AppState_ERROR                             },
        /* Event: AppEvent_DISCONNECT */ 
        {StartConnection, AppState_WAIT_FOR_CONNECTION            },
        /* Event: AppEvent_PING_COMPLETE */
         {HandleProvisioningComplete, AppState_PINGING_GW         },
        /* AppEvent_PROVISIONING_STARTED */ 
        {ProvisioningStart, AppState_PROVISIONING_IN_PROGRESS     },
        /* AppEvent_PROVISIONING_SUCCESS */ 
        {DoNothing, AppState_WAIT_FOR_CONNECTION                  },
        /* AppEvent_PROVISIONING_STOPPED */ 
        {CheckLanConnection, AppState_WAIT_FOR_CONNECTION         },
        /* AppEvent_PROVISIONING_WAIT_CONN */ 
        {DoNothing, AppState_WAIT_FOR_CONNECTION                  },
        /* Event: AppEvent_TIMEOUT */
        {ProvisioningStart, AppState_PROVISIONING_IN_PROGRESS     },
        /* Event: AppEvent_ERROR */ 
        {ProvisioningStart, AppState_PROVISIONING_IN_PROGRESS     },
        /* Event: AppEvent_RESTART */ 
        {ProcessRestartRequest, AppState_WAIT_FOR_CONNECTION      },
        /* Event: AppEvent_STUCK */ 
        {DoNothing, AppState_STARTING           }
    },
    /* AppState_WAIT_FOR_IP */
    {
        /* Event: AppEvent_STARTED */
         {ProvisioningStart, AppState_PROVISIONING_IN_PROGRESS          },
        /* Event: AppEvent_CONNECTED */ {ReportError, AppState_ERROR    },
        /* Event: AppEvent_IP_ACQUIRED */
         {CheckLanConnection, AppState_PINGING_GW                       },
        /* Event: AppEvent_DISCONNECT */ 
        {StartConnection, AppState_WAIT_FOR_CONNECTION                  },
        /* Event: AppEvent_PING_COMPLETE */ 
        {DoNothing, AppState_WAIT_FOR_IP                                },
        /* AppEvent_PROVISIONING_STARTED */
         {ProvisioningStart, AppState_PROVISIONING_IN_PROGRESS          },
        /* AppEvent_PROVISIONING_SUCCESS */ 
        {DoNothing, AppState_WAIT_FOR_IP                                },
        /* AppEvent_PROVISIONING_STOPPED */ 
        {HandleProvisioningComplete, AppState_PINGING_GW                },
        /* AppEvent_PROVISIONING_WAIT_CONN */ 
        {DoNothing, AppState_WAIT_FOR_IP                                },
        /* Event: AppEvent_TIMEOUT */ 
        {ReportError, AppState_ERROR                                    },
        /* Event: AppEvent_ERROR */
         {ReportError, AppState_ERROR                                   },
        /* Event: AppEvent_RESTART */ 
        {ProcessRestartRequest, AppState_WAIT_FOR_CONNECTION     },
        /* Event: AppEvent_STUCK */ 
        {DoNothing, AppState_STARTING           }
    },
    /* AppState_PINGING_GW */
    {
        /* Event: AppEvent_STARTED */ 
        {DoNothing, AppState_PINGING_GW                                  },
        /* Event: AppEvent_CONNECTED */ 
        {HandleWaitForIp, AppState_WAIT_FOR_IP                           },
        /* Event: AppEvent_IP_ACQUIRED */
        {CheckInternetConnection, AppState_PINGING_GW                    },
        /* Event: AppEvent_DISCONNECT */ 
        {HandleDiscnctEvt, AppState_WAIT_FOR_CONNECTION                  },
        /* Event: AppEvent_PING_COMPLETE */ 
        {DoNothing, AppState_PINGING_GW                                  },
        /* AppEvent_PROVISIONING_STARTED */ 
        {ProvisioningStart, AppState_PROVISIONING_IN_PROGRESS            },
        /* AppEvent_PROVISIONING_SUCCESS */
         {DoNothing, AppState_PINGING_GW                                 },
        /* AppEvent_PROVISIONING_STOPPED */ 
        {HandleUserApplication, AppState_PINGING_GW                      },
        /* AppEvent_PROVISIONING_WAIT_CONN */ 
        {DoNothing, AppState_PINGING_GW                                  },
        /*  Event: AppEvent_TIMEOUT */
        {SendPingToGW, AppState_PINGING_GW                               },
        /* Event: AppEvent_ERROR */ 
        {ReportError, AppState_ERROR                                     },
        /* Event: AppEvent_RESTART */ 
        {ProcessRestartRequest, AppState_WAIT_FOR_CONNECTION             },
        /* Event: AppEvent_STUCK */ 
        {DoNothing, AppState_STARTING             }
    },
    /* AppState_PROVISIONING_IN_PROGRESS  */
    {
        /* Event: AppEvent_STARTED */ 
        {ProcessStartRequest, AppState_PROVISIONING_IN_PROGRESS    },
        /* Event: AppEvent_CONNECTED */
         {HandleConnection, AppState_PROVISIONING_IN_PROGRESS            },
        /* Event: AppEvent_IP_ACQUIRED */
        {CheckInternetConnection, AppState_PROVISIONING_IN_PROGRESS      },
        /* Event: AppEvent_DISCONNECT */ 
        {HandleDiscnctEvt, AppState_PROVISIONING_IN_PROGRESS             },
        /* Event: AppEvent_PING_COMPLETE */ 
        {DoNothing, AppState_PROVISIONING_IN_PROGRESS                    },
        /* AppEvent_PROVISIONING_STARTED */
         {DoNothing, AppState_PROVISIONING_IN_PROGRESS                   },
        /* AppEvent_PROVISIONING_SUCCESS */ 
        {HandleProvisioningComplete, AppState_PROVISIONING_WAIT_COMPLETE },
        /* AppEvent_PROVISIONING_STOPPED */
        {HandleProvisioningComplete, AppState_PINGING_GW                },
        /* AppEvent_PROVISIONING_WAIT_CONN */
        {DoNothing, AppState_WAIT_FOR_CONNECTION                        },
        /* Event: AppEvent_TIMEOUT */ 
        {ProcessRestartRequest, AppState_WAIT_FOR_CONNECTION            },
        /* Event: AppEvent_ERROR */
        {ReportError, AppState_ERROR                                    },
        /* Event: AppEvent_RESTART */ 
        {ProcessRestartRequest, AppState_WAIT_FOR_CONNECTION            },
        /* Event: AppEvent_STUCK */ 
        {DoNothing, AppState_STARTING           }
    
    },
    /* AppState_PROVISIONING_WAIT_COMPLETE */
    {
        /* Event: AppEvent_STARTED */
         {DoNothing, AppState_PROVISIONING_WAIT_COMPLETE },
        /* Event: AppEvent_CONNECTED */ 
        {DoNothing, AppState_PROVISIONING_WAIT_COMPLETE  },
        /* Event: AppEvent_IP_ACQUIRED */
         {DoNothing, AppState_PROVISIONING_WAIT_COMPLETE },
        /* Event: AppEvent_DISCONNECT */ 
        {HandleDiscnctEvt, AppState_PROVISIONING_WAIT_COMPLETE },
        /* Event: AppEvent_PING_COMPLETE */ 
        {DoNothing, AppState_PROVISIONING_WAIT_COMPLETE },
        /* AppEvent_PROVISIONING_STARTED */ 
        {DoNothing, AppState_PROVISIONING_WAIT_COMPLETE },
        /* AppEvent_PROVISIONING_SUCCESS */ 
        {DoNothing, AppState_PROVISIONING_WAIT_COMPLETE },
        /* AppEvent_PROVISIONING_STOPPED */ 
        {HandleUserApplication, AppState_PINGING_GW     },
        /* AppEvent_PROVISIONING_WAIT_CONN */ 
        {DoNothing, AppState_WAIT_FOR_CONNECTION        },
        /* Event: AppEvent_TIMEOUT */ 
        {HandleProvisioningTimeOut, AppState_PROVISIONING_WAIT_COMPLETE },
        /* Event: AppEvent_ERROR */
        {ReportError, AppState_ERROR },
        /* Event: AppEvent_RESTART */ 
        {ProcessRestartRequest, AppState_PROVISIONING_WAIT_COMPLETE },
        /* Event: AppEvent_STUCK */ 
        {DoNothing, AppState_STARTING           }
    },
    /* AppState_ERROR */
    /* we will restart connection for all errors */
    {
        /* Event: AppEvent_STARTED */
         {ReportError, AppState_WAIT_FOR_CONNECTION     },
        /* Event: AppEvent_CONNECTED */ 
        {ReportError, AppState_WAIT_FOR_CONNECTION     },
        /* Event: AppEvent_IP_ACQUIRED */ 
        {ReportError, AppState_WAIT_FOR_CONNECTION     },
        /* Event: AppEvent_DISCONNECT */ 
        {ReportError, AppState_WAIT_FOR_CONNECTION     },
        /* Event: AppEvent_PING_COMPLETE */ 
        {ReportError, AppState_WAIT_FOR_CONNECTION     },
        /* AppEvent_PROVISIONING_STARTED */
        {ReportError, AppState_WAIT_FOR_CONNECTION     },
        /* AppEvent_PROVISIONING_SUCCESS */ 
        {ReportError, AppState_WAIT_FOR_CONNECTION     },
        /* AppEvent_PROVISIONING_STOPPED */ 
        {ReportError, AppState_WAIT_FOR_CONNECTION     },
        /* AppEvent_PROVISIONING_WAIT_CONN */ 
        {ReportError, AppState_WAIT_FOR_CONNECTION     },
        /* Event: AppEvent_TIMEOUT */
         {ReportError, AppState_WAIT_FOR_CONNECTION    },
        /* Event: AppEvent_ERROR */
         {ReportError,AppState_WAIT_FOR_CONNECTION     },
        /* Event: AppEvent_RESTART */ 
        {ReportError, AppState_WAIT_FOR_CONNECTION     },
        /* Event: AppEvent_STUCK */ 
        {DoNothing, AppState_STARTING           }
    }
};

/* Application state's context */
AppState g_CurrentState;

/*****************************************************************************
                  SimpleLink Callback Functions
*****************************************************************************/

//*****************************************************************************
//
//! \brief The Function Handles WLAN Events
//!
//! \param[in]  pWlanEvent - Pointer to WLAN Event Info
//!
//! \return None
//!
//*****************************************************************************
void SimpleLinkWlanEventHandler(SlWlanEvent_t *pWlanEvent)
{
    switch(pWlanEvent->Id)
    {
        case SL_WLAN_EVENT_CONNECT:
            LOG_MESSAGE("\r [Event] STA connected to AP "
                "- BSSID:%.2x:%.2x:%.2x:%.2x:%.2x:%.2x, ",
                pWlanEvent->Data.Connect.Bssid[0],
                pWlanEvent->Data.Connect.Bssid[1],
                pWlanEvent->Data.Connect.Bssid[2],
                pWlanEvent->Data.Connect.Bssid[3],
                pWlanEvent->Data.Connect.Bssid[4],
                pWlanEvent->Data.Connect.Bssid[5]);

            /* set the string terminate */
            pWlanEvent->Data.Connect.SsidName[pWlanEvent->Data.Connect.SsidLen] =
                '\0';

            LOG_MESSAGE("\r SSID:%s CurrentState: %d \n", pWlanEvent->Data.Connect.SsidName, g_CurrentState);
            SignalEvent(AppEvent_CONNECTED);
            break;

        case SL_WLAN_EVENT_DISCONNECT:
            LOG_MESSAGE("\r [Event] STA disconnected from AP (Reason Code = %d) CurrentState: %d \n", pWlanEvent->Data.Disconnect.ReasonCode, g_CurrentState);
            SignalEvent(AppEvent_DISCONNECT);
            break;

        case SL_WLAN_EVENT_STA_ADDED:
            LOG_MESSAGE("\r [Event] New STA Added CurrentState: %d \n", g_CurrentState);
            LOG_MESSAGE(
                "\r [Event] New STA Added (MAC Address:"
                " %.2x:%.2x:%.2x:%.2x:%.2x)\r\n",
                pWlanEvent->Data.STAAdded.Mac[0],
                pWlanEvent->Data.STAAdded.Mac[1],
                pWlanEvent->Data.STAAdded.Mac[2],
                pWlanEvent->Data.STAAdded.Mac[3],
                pWlanEvent->Data.STAAdded.Mac[4],
                pWlanEvent->Data.STAAdded.Mac[5]);
            break;

        case SL_WLAN_EVENT_STA_REMOVED:
            LOG_MESSAGE("\r [Event] STA Removed CurrentState: %d \n", g_CurrentState);
            LOG_MESSAGE(
                "\r [Event] STA Removed (MAC Address: %.2x:%.2x:%.2x:%.2x:%.2x)\r\n",
                pWlanEvent->Data.STAAdded.Mac[0],
                pWlanEvent->Data.STAAdded.Mac[1],
                pWlanEvent->Data.STAAdded.Mac[2],
                pWlanEvent->Data.STAAdded.Mac[3],
                pWlanEvent->Data.STAAdded.Mac[4],
                pWlanEvent->Data.STAAdded.Mac[5]);
            break;

        case SL_WLAN_EVENT_PROVISIONING_PROFILE_ADDED:
            LOG_MESSAGE("\r [Provisioning] Profile Added CurrentState: %d \n", g_CurrentState);
            LOG_MESSAGE("\r [Provisioning] Profile Added: SSID: %s sizeof: %d strlen: %d \n", pWlanEvent->Data.ProvisioningProfileAdded.Ssid, sizeof(pWlanEvent->Data.ProvisioningProfileAdded.Ssid), strlen(pWlanEvent->Data.ProvisioningProfileAdded.Ssid));
            uint8_t len = strlen(pWlanEvent->Data.ProvisioningProfileAdded.Ssid);
            free(ssidName);
            ssidName = malloc(len+1);
            memcpy(ssidName, pWlanEvent->Data.ProvisioningProfileAdded.Ssid, len);
            ssidName[len] = '\0';
            LOG_MESSAGE("\r [Provisioning] Profile Added: SSID: %s - %d - %d \n", ssidName, strlen(ssidName), sizeof(ssidName));
            if(pWlanEvent->Data.ProvisioningProfileAdded.ReservedLen > 0)
            {
                LOG_MESSAGE(" [Provisioning] Profile Added: PrivateToken:%s\r\n",
                            pWlanEvent->Data.ProvisioningProfileAdded.Reserved);
            }
            /*_i32 writeStatus, closeStatus;
            _i32 FileHdl;
            unsigned char *DeviceFileName = "m0vNV";
            _u32 MaxSize, MasterToken;
            FileHdl = sl_FsOpen((unsigned char *)DeviceFileName,
            SL_FS_CREATE|SL_FS_CREATE_SECURE|SL_FS_OVERWRITE|
            SL_FS_CREATE_NOSIGNATURE|SL_FS_CREATE_MAX_SIZE(MaxSize), &MasterToken);
            if(FileHdl < 0 )
            {
                LOG_MESSAGE(" [Provisioning] Error opening file for write %s \r\n", DeviceFileName);
            } else {
                writeStatus = sl_FsWrite(FileHdl, 0, (_u8 *)ssidName, strlen(ssidName));
                if (writeStatus < 0) {
                    LOG_MESSAGE(" [Provisioning] Error writing file %s \r\n", ssidName);
                }
                closeStatus = sl_FsClose(FileHdl,0,0,0);
                if (closeStatus < 0 ) {
                    LOG_MESSAGE(" [Provisioning] Error closing file %s \r\n", DeviceFileName);
                    sl_FsClose(FileHdl, 0, 'A', 1);
                } 
            }*/
            break;

        case SL_WLAN_EVENT_PROVISIONING_STATUS:
        {
            switch(pWlanEvent->Data.ProvisioningStatus.ProvisioningStatus)
            {
            case SL_WLAN_PROVISIONING_GENERAL_ERROR:
                LOG_MESSAGE("\r [Provisioning] Provisioning_general_error status=%d\r\n",
                            pWlanEvent->Data.ProvisioningStatus.ProvisioningStatus);
            case SL_WLAN_PROVISIONING_ERROR_ABORT:
                LOG_MESSAGE("\r [Provisioning] Provisioning_error_abort status=%d\r\n",
                            pWlanEvent->Data.ProvisioningStatus.ProvisioningStatus);
            case SL_WLAN_PROVISIONING_ERROR_ABORT_INVALID_PARAM:
                LOG_MESSAGE("\r [Provisioning] Provisioning_error_invalid_param status=%d\r\n",
                            pWlanEvent->Data.ProvisioningStatus.ProvisioningStatus);
            case SL_WLAN_PROVISIONING_ERROR_ABORT_HTTP_SERVER_DISABLED:
                LOG_MESSAGE("\r [Provisioning] Provisioning_error_abort_http_server_disabled status=%d\r\n", pWlanEvent->Data.ProvisioningStatus.ProvisioningStatus);
            case SL_WLAN_PROVISIONING_ERROR_ABORT_PROFILE_LIST_FULL:
                LOG_MESSAGE("\r [Provisioning] Provisioning_error_abort_profile_list_full status=%d\r\n", pWlanEvent->Data.ProvisioningStatus.ProvisioningStatus);
            case SL_WLAN_PROVISIONING_ERROR_ABORT_PROVISIONING_ALREADY_STARTED:
                LOG_MESSAGE("\r [Provisioning] Provisioning_error_abort_provisioning_already_started status=%d\r\n", pWlanEvent->Data.ProvisioningStatus.ProvisioningStatus);
                SignalEvent(AppEvent_ERROR);
                break;

            case SL_WLAN_PROVISIONING_CONFIRMATION_STATUS_FAIL_NETWORK_NOT_FOUND:
                LOG_MESSAGE("\r [Provisioning] Profile confirmation failed: network not found\r\n");
                SignalEvent(AppEvent_PROVISIONING_STARTED);
                break;

            case SL_WLAN_PROVISIONING_CONFIRMATION_STATUS_FAIL_CONNECTION_FAILED:
                LOG_MESSAGE("\r [Provisioning] Profile Confirmation failed CurrentState: %d \n", g_CurrentState);
                LOG_MESSAGE(" \r [Provisioning] Profile confirmation failed:"
                    " Connection failed for %s w/ status %d \r\n", 
                    pWlanEvent->Data.ProvisioningStatus.Ssid, 
                    pWlanEvent->Data.ProvisioningStatus.WlanStatus);
                SignalEvent(AppEvent_PROVISIONING_STARTED);
                break;

            case SL_WLAN_PROVISIONING_CONFIRMATION_STATUS_CONNECTION_SUCCESS_IP_NOT_ACQUIRED:
                LOG_MESSAGE("\r [Provisioning] Profile confirmation failed:"
                    " IP address not acquired\r\n");
                SignalEvent(AppEvent_PROVISIONING_STARTED);
                break;

            case SL_WLAN_PROVISIONING_CONFIRMATION_STATUS_SUCCESS_FEEDBACK_FAILED:
                LOG_MESSAGE("\r [Provisioning] Profile Confirmation failed "
                    "(Connection Success, feedback to Smartphone app failed)\r\n");
                SignalEvent(AppEvent_PROVISIONING_STARTED);
                break;

            case SL_WLAN_PROVISIONING_CONFIRMATION_STATUS_SUCCESS:
                LOG_MESSAGE("\r [Provisioning] Profile Confirmation Success! ProvisioningStatus=%d, WlanStatus=%d \r\n", pWlanEvent->Data.ProvisioningStatus.ProvisioningStatus, pWlanEvent->Data.ProvisioningStatus.WlanStatus);
                SignalEvent(AppEvent_PROVISIONING_SUCCESS);
                break;

            case SL_WLAN_PROVISIONING_AUTO_STARTED:
                LOG_MESSAGE("\r [Provisioning] Auto-Provisioning Started \n");
                SignalEvent(AppEvent_RESTART);
                break;

            case SL_WLAN_PROVISIONING_STOPPED:
                LOG_MESSAGE("\r [Provisioning] Provisioning stopped: CurrentState: %d \n", g_CurrentState);
                LOG_MESSAGE("\r [Provisioning Stopped] Current Role: %s \n",
                            Roles[pWlanEvent->Data.ProvisioningStatus.Role]);
                if(ROLE_STA == pWlanEvent->Data.ProvisioningStatus.Role)
                {
                    LOG_MESSAGE("\r [Provisioning Stopped] WLAN Status: %s \n",
                                WlanStatus[pWlanEvent->Data.ProvisioningStatus.
                                           WlanStatus]);

                    if(SL_WLAN_STATUS_CONNECTED ==
                       pWlanEvent->Data.ProvisioningStatus.WlanStatus)
                    {
                        LOG_MESSAGE("\r [Provisioning Stopped] Connected to SSID: %s \n",
                                    pWlanEvent->Data.ProvisioningStatus.Ssid);
                        SignalEvent(AppEvent_PROVISIONING_STOPPED);
                    }
                    else if(SL_WLAN_STATUS_SCANING ==
                            pWlanEvent->Data.ProvisioningStatus.WlanStatus)
                    {
                        LOG_MESSAGE("\r [Provisioning Stopped] Scanning: %s \n", pWlanEvent->Data.ProvisioningStatus.WlanStatus);
                        gWaitForConn = 1;
                        SignalEvent(AppEvent_PROVISIONING_WAIT_CONN);
                    }
                }
                else
                {
                    /* [External configuration]: In case external configuration is currently active and PROVISIONING STOP event arrived,
                     * Signal the SM to stop */
                    if (TRUE == IsActiveExternalConfiguration())
                    {
                        SignalEvent(AppEvent_PROVISIONING_STOPPED);
                        /*UART_PRINT("\r [ExtProv] Starting TCP server to listen for credentials \n");

                        // Open a server socket and listen for 2 sem posts. 
                        int32_t ret = 0;
                        uint8_t nb = 1; 
                        int16_t port = 8888;
                        ret = TCPServer(nb, port, FALSE, 1, FALSE);

                        if(ret < 0)
                        {
                            UART_PRINT("\r [ExtProv] Couldn't start TCP server so reverting .. \n");
                            ret = sl_WlanProvisioning(SL_WLAN_PROVISIONING_CMD_START_MODE_APSC, ROLE_STA,
                                                    1200,
                                                    NULL,
                                                    SL_WLAN_PROVISIONING_CMD_FLAG_EXTERNAL_CONFIRMATION);
                                                    //(uint32_t)NULL);
                        } 
                        else {
                            UART_PRINT("\r [ExtProv] Started TCP server on port %d to listen for credentials \n", port);
                        }*/
                    }
                    else
                    {
                        SignalEvent(AppEvent_RESTART);
                    }
                }
                break;

            case SL_WLAN_PROVISIONING_SMART_CONFIG_SYNCED:
                LOG_MESSAGE(" [Provisioning] Smart Config Synced!\r\n");
                break;

            case SL_WLAN_PROVISIONING_SMART_CONFIG_SYNC_TIMEOUT:
                LOG_MESSAGE(" [Provisioning] Smart Config Sync Timeout!\r\n");
                break;

            case SL_WLAN_PROVISIONING_CONFIRMATION_WLAN_CONNECT:
                LOG_MESSAGE(
                    "\r [Provisioning] Profile confirmation: WLAN Connected! ProvisioningStatus=%d, WlanStatus=%d\r\n", pWlanEvent->Data.ProvisioningStatus.ProvisioningStatus, pWlanEvent->Data.ProvisioningStatus.WlanStatus);
                break;

            case SL_WLAN_PROVISIONING_CONFIRMATION_IP_ACQUIRED:
                LOG_MESSAGE(
                    "\r [Provisioning] Profile confirmation: IP Acquired!\r\n");
                break;

            case SL_WLAN_PROVISIONING_EXTERNAL_CONFIGURATION_READY:
                LOG_MESSAGE("\r [Provisioning] External "
                                "configuration is ready! \r\n");
                /* [External configuration]: External configuration is ready,
                start the external configuration process.
                In case of using the external provisioning
                enable the function below which will trigger StartExternalProvisioning() */
                /* SignalEvent(AppEvent_STARTED); */
                break;

            default:
                LOG_MESSAGE(" [Provisioning] "
                                "Unknown Provisioning Status: %d\r\n",
                                pWlanEvent->Data.ProvisioningStatus.ProvisioningStatus);
                break;
            }
        }
        break;

        default:
            LOG_MESSAGE(" [Event] - WlanEventHandler has received %d !!!!\r\n",
                    pWlanEvent->Id);
            break;
    }
}

//*****************************************************************************
//
//! \brief The Function Handles the Fatal errors
//!
//! \param[in]  slFatalErrorEvent - Pointer to Fatal Error Event info
//!
//! \return None
//!
//*****************************************************************************
void SimpleLinkFatalErrorEventHandler(SlDeviceFatal_t *slFatalErrorEvent)
{
    switch (slFatalErrorEvent->Id)
    {
    case SL_DEVICE_EVENT_FATAL_DEVICE_ABORT:
        {
            LOG_MESSAGE("[ERROR] - FATAL ERROR: Abort NWP event detected: "
                "AbortType=%d, AbortData=0x%x\n\r",
                slFatalErrorEvent->Data.DeviceAssert.Code,
                slFatalErrorEvent->Data.DeviceAssert.Value);
        }
        break;

    case SL_DEVICE_EVENT_FATAL_DRIVER_ABORT:
        {
            LOG_MESSAGE("[ERROR] - FATAL ERROR: Driver Abort detected. \n\r");
        }
        break;

    case SL_DEVICE_EVENT_FATAL_NO_CMD_ACK:
        {
            LOG_MESSAGE("[ERROR] - FATAL ERROR: No Cmd Ack detected "
                "[cmd opcode = 0x%x] \n\r",
                slFatalErrorEvent->Data.NoCmdAck.Code);
        }
        break;

    case SL_DEVICE_EVENT_FATAL_SYNC_LOSS:
        {
            LOG_MESSAGE("[ERROR] - FATAL ERROR: Sync loss detected n\r");
        }
        break;

    case SL_DEVICE_EVENT_FATAL_CMD_TIMEOUT:
        {
            LOG_MESSAGE("[ERROR] - FATAL ERROR: Async event timeout detected "
                "[event opcode =0x%x]  \n\r",
                slFatalErrorEvent->Data.CmdTimeout.Code);
        }
        break;

    default:
        LOG_MESSAGE("[ERROR] - FATAL ERROR: Unspecified error detected \n\r");
        break;
    }
}

//*****************************************************************************
//
//! \brief This function handles network events such as IP acquisition, IP
//!           leased, IP released etc.
//!
//! \param[in]  pNetAppEvent - Pointer to NetApp Event Info
//!
//! \return None
//!
//*****************************************************************************
void SimpleLinkNetAppEventHandler(SlNetAppEvent_t *pNetAppEvent)
{
    switch(pNetAppEvent->Id)
    {
    case SL_NETAPP_EVENT_IPV4_ACQUIRED:
        LOG_MESSAGE("\r [NETAPP EVENT] IP Acquired: IP=%d.%d.%d.%d , "
            "Gateway=%d.%d.%d.%d\n\r",
            SL_IPV4_BYTE(pNetAppEvent->Data.IpAcquiredV4.Ip,3),
            SL_IPV4_BYTE(pNetAppEvent->Data.IpAcquiredV4.Ip,2),
            SL_IPV4_BYTE(pNetAppEvent->Data.IpAcquiredV4.Ip,1),
            SL_IPV4_BYTE(pNetAppEvent->Data.IpAcquiredV4.Ip,0),
            SL_IPV4_BYTE(pNetAppEvent->Data.IpAcquiredV4.Gateway,3),
            SL_IPV4_BYTE(pNetAppEvent->Data.IpAcquiredV4.Gateway,2),
            SL_IPV4_BYTE(pNetAppEvent->Data.IpAcquiredV4.Gateway,1),
            SL_IPV4_BYTE(pNetAppEvent->Data.IpAcquiredV4.Gateway,0));
        SignalEvent(AppEvent_IP_ACQUIRED);
        break;

    case SL_NETAPP_EVENT_DHCPV4_LEASED:

        LOG_MESSAGE("[NETAPP Event] IP Leased: %d.%d.%d.%d\r\n",
            SL_IPV4_BYTE(pNetAppEvent->Data.IpLeased.IpAddress,3),
            SL_IPV4_BYTE(pNetAppEvent->Data.IpLeased.IpAddress,2),
            SL_IPV4_BYTE(pNetAppEvent->Data.IpLeased.IpAddress,1),
            SL_IPV4_BYTE(pNetAppEvent->Data.IpLeased.IpAddress,0));
        SignalEvent(AppEvent_IP_ACQUIRED);
        break;

    case SL_NETAPP_EVENT_DHCP_IPV4_ACQUIRE_TIMEOUT:
        LOG_MESSAGE("[NETAPP Event] IP acquired timeout \r\n");
        break;


    default:
        LOG_MESSAGE("[NETAPP EVENT] Unhandled event [0x%x] \n\r",
                    pNetAppEvent->Id);
        break;
    }
}

//*****************************************************************************
//
//! \brief This function handles HTTP server events
//!
//! \param[in]  pServerEvent    - Contains the relevant event information
//! \param[in]  pServerResponse - Should be filled by the user with the
//!                               relevant response information
//!
//! \return None
//!
//****************************************************************************
void SimpleLinkHttpServerEventHandler(
    SlNetAppHttpServerEvent_t *pHttpEvent,
    SlNetAppHttpServerResponse_t *
    pHttpResponse)
{
    /* Unused in this application */
    LOG_MESSAGE("[HttpServerEvent]: ");
}

//*****************************************************************************
//
//! \brief This function handles General Events
//!
//! \param[in]  pDevEvent - Pointer to General Event Info
//!
//! \return None
//!
//*****************************************************************************
void SimpleLinkGeneralEventHandler(SlDeviceEvent_t *pDevEvent)
{
    /* Most of the general errors are not FATAL are are to be handled
    appropriately by the application */
    LOG_MESSAGE("[GENERAL EVENT] - ID=[%d] Sender=[%d]\n\n",
        pDevEvent->Data.Error.Code,
        pDevEvent->Data.Error.Source);
}

//*****************************************************************************
//
//! \brief This function handles socket events indication
//!
//! \param[in]  pSock - Pointer to Socket Event Info
//!
//! \return None
//!
//*****************************************************************************
void SimpleLinkSockEventHandler(SlSockEvent_t *pSock)
{
    /* This application doesn't work with socket - Events are not expected */
    switch( pSock->Event )
    {
    case SL_SOCKET_TX_FAILED_EVENT:
        switch( pSock->SocketAsyncEvent.SockTxFailData.Status)
        {
        case SL_ERROR_BSD_ECLOSE:
            LOG_MESSAGE("[SOCK ERROR] - close socket (%d) operation "
                "failed to transmit all queued packets\n\r",
                pSock->SocketAsyncEvent.SockTxFailData.Sd);
            break;
        default:
            LOG_MESSAGE("[SOCK ERROR] - TX FAILED  :  socket %d , "
                "reason (%d) \n\n",
                pSock->SocketAsyncEvent.SockTxFailData.Sd,
                pSock->SocketAsyncEvent.SockTxFailData.Status);
            break;
        }
        break;

    default:
        LOG_MESSAGE("[SOCK EVENT] - Unexpected Event [%x0x]\n\n",pSock->Event);
        break;
    }
}

//*****************************************************************************
//
//! \brief  This function handles init-complete event from SL
//!
//! \param  status - Mode the device is configured or error if initialization
//!                     failed
//!
//! \return None
//!
//*****************************************************************************
void SimpleLinkInitCallback(uint32_t status,
                            SlDeviceInitInfo_t *DeviceInitInfo)
{
    if ((int32_t)status == SL_ERROR_RESTORE_IMAGE_COMPLETE)
    {
        LOG_MESSAGE(
            "\r\n**********************************\r\nReturn to"
            " Factory Default been Completed\r\nPlease RESET the Board\r\n"
            "**********************************\r\n");
        gLedDisplayState = LedState_FACTORY_DEFAULT;
        while(1)
        {
            ;
        }
    }

    ASSERT_ON_ERROR((int32_t)status);


    LOG_MESSAGE("Device started in %s role\n\r", (0 == status) ? "Station" :\
        (2 == status) ? "AP" : ((3 == status) ? "P2P" : "Start Role error"));

    if(ROLE_STA == status)
    {
        gRole = ROLE_STA;
        SignalEvent(AppEvent_STARTED);
    }
    else if (ROLE_AP == status)
    {
        gRole = ROLE_AP;
        SignalEvent(AppEvent_RESTART);
    }
    else
    {
        SignalEvent(AppEvent_ERROR);
    }
}

void SimpleLinkNetAppRequestEventHandler(SlNetAppRequest_t *pNetAppRequest,
                                         SlNetAppResponse_t *pNetAppResponse)
{
    /* Unused in this application */
    LOG_MESSAGE("\r [EventHandler] received non API post \n");
    extern _u16 gHandle;
    switch(pNetAppRequest->Type)
    {
    case SL_NETAPP_REQUEST_HTTP_POST:
    {
        _u32 RequestFlags;
        _u32 ContentLength;
        RequestFlags = pNetAppRequest->requestData.Flags;
        /* Prepare pending response */
        pNetAppResponse->ResponseData.pMetadata = NULL;
        pNetAppResponse->ResponseData.MetadataLen = 0;
        pNetAppResponse->ResponseData.pPayload = NULL;
        pNetAppResponse->ResponseData.PayloadLen = 0;
        pNetAppResponse->ResponseData.Flags = 0;
        if (pNetAppRequest->requestData.MetadataLen > 0)
        {
            /* Process the meta data in
             * pNetAppRequest->requestData.pMetadata */
            ContentLength = ExtractLengthFromMetaData(
                    pNetAppRequest->requestData.pMetadata,
                    pNetAppRequest->requestData.MetadataLen);
            LOG_MESSAGE("\r [POST EventHandler] header - (%d) content length \r\n ", ContentLength);
            /* Allocate buffer to receive the entire content if needed */
        }
        pNetAppResponse->Status = SL_NETAPP_HTTP_RESPONSE_200_OK;
        /*
        pNetAppResponse->ResponseData.pMetadata = NULL;
        pNetAppResponse->ResponseData.MetadataLen = 0;
        pNetAppResponse->ResponseData.pPayload = NULL;
        pNetAppResponse->ResponseData.PayloadLen = 0;
        pNetAppResponse->ResponseData.Flags = 0;
        */
    }
    break;
    default:
    /* GET/PUT/DELETE requests will reach here. */
    break;
}
}

void SimpleLinkNetAppRequestMemFreeEventHandler(uint8_t *buffer)
{
    /* Unused in this application */
}

//*****************************************************************************
//                 Local Functions
//*****************************************************************************

//*****************************************************************************
//
//! \brief Return to Factory defaults indication Led
//!
//! \param  none
//!
//! \return none
//!
//*****************************************************************************
void FactoryIndicationLed(void)
{
    GPIO_write(Board_GPIO_LED2, Board_GPIO_LED_OFF);
    GPIO_write(Board_GPIO_LED1, Board_GPIO_LED_OFF);
    GPIO_write(Board_GPIO_LED0, Board_GPIO_LED_ON);
    gLedState = 0;

    while(1)
    {
        gLedCount++;
        gLedCount &=  0x1FFFF;

        if (0 == gLedCount)
        {
            if(gLedState)
            {
                GPIO_write(Board_GPIO_LED1, Board_GPIO_LED_OFF);
                GPIO_write(Board_GPIO_LED0, Board_GPIO_LED_ON);
                gLedState = 0;
            }
            else
            {
                GPIO_write(Board_GPIO_LED1, Board_GPIO_LED_ON);
                GPIO_write(Board_GPIO_LED0, Board_GPIO_LED_OFF);
                gLedState = 1;
            }
        }
    }
}

//*****************************************************************************
//
//! \brief Error indication Led
//!
//! \param  none
//!
//! \return none
//!
//*****************************************************************************
void ErrorLedDisplay(void)
{
    GPIO_write(Board_GPIO_LED2, Board_GPIO_LED_OFF);
    GPIO_write(Board_GPIO_LED1, Board_GPIO_LED_OFF);
    GPIO_write(Board_GPIO_LED0, Board_GPIO_LED_OFF);
    gErrLedState = 0;

    while(1)
    {
        gErrledCount++;
        gErrledCount &=  0xFFFFF;

        if (0 == gErrledCount)
        {
            if(gErrLedState)
            {
                GPIO_write(Board_GPIO_LED2, Board_GPIO_LED_OFF);
                gErrLedState = 0;
            }
            else
            {
                GPIO_write(Board_GPIO_LED2, Board_GPIO_LED_ON);
                gErrLedState = 1;
            }
        }
    }
}

//*****************************************************************************
//
//! \brief Update status for indication Led
//!
//! \param  none
//!
//! \return none
//!
//*****************************************************************************
void * UpdateLedDisplay(void *arg)
{
    while(1)
    {
        if(LedState_ERROR == gLedDisplayState)
        {
            ErrorLedDisplay();
        }
        else if (LedState_FACTORY_DEFAULT == gLedDisplayState)
        {
            FactoryIndicationLed();
        }
        else
        {
            if(gIsWlanConnected) /* connected (led ON) */
            {
                if(0 == gLedState)
                {
                    GPIO_write(Board_GPIO_LED0, Board_GPIO_LED_ON);
                    gLedState = 1;
                    GPIO_write(Board_GPIO_LED1, Board_GPIO_LED_OFF);
                    GPIO_write(Board_GPIO_LED2, Board_GPIO_LED_OFF);
                }
            }
            else /* disconnected (blink led) */
            {
                if(gLedState)
                {
                    GPIO_write(Board_GPIO_LED0, Board_GPIO_LED_OFF);
                    gLedState = 0;
                }
                else
                {
                    GPIO_write(Board_GPIO_LED0, Board_GPIO_LED_ON);
                    gLedState = 1;
                }
            }
        }

        usleep(500000);
    }
}

//*****************************************************************************
//
//! \brief The interrupt handler for the async-evt timer
//!
//! \param  none
//!
//! \return none
//!
//*****************************************************************************
void AsyncEvtTimerHandler(sigval arg)
{
    SignalEvent(AppEvent_TIMEOUT);
    LOG_MESSAGE("\r [Event] - Async event timer handler has received %x !!!!\r\n", arg);
}

//*****************************************************************************
//
//! \brief Call Host Timer
//!
//! \param  Duration in mSec
//!
//! \return none
//!
//*****************************************************************************
void StartAsyncEvtTimer(uint32_t duration)
{
    struct itimerspec value;

    /* Start timer */
    value.it_interval.tv_sec = 0;
    value.it_interval.tv_nsec = 0;
    value.it_value.tv_sec = duration;
    value.it_value.tv_nsec = 0;

    timer_settime(gTimer, 0, &value, NULL);
}

//*****************************************************************************
//
//! \brief Stop Timer
//!
//! \param  None
//!
//! \return none
//!
//*****************************************************************************
void StopAsyncEvtTimer(void)
{
    struct itimerspec value;

    /* Stop timer */
    value.it_interval.tv_sec = 0;
    value.it_interval.tv_nsec = 0;
    value.it_value.tv_sec = 0;
    value.it_value.tv_nsec = 0;

    timer_settime(gTimer, 0, &value, NULL);
}

//*****************************************************************************
//
//! \brief Error Handler - Print and restart connection
//!
//! \param  None
//!
//! \return none
//!
//*****************************************************************************
int32_t ReportError(void)
{
    LOG_MESSAGE(
        "\r [Provisioning] Provisioning Application Error - Restarting \r\n ");
    gIsWlanConnected = 0;
    sl_WlanProvisioning(SL_WLAN_PROVISIONING_CMD_STOP, ROLE_AP, 0, NULL,
                        (uint32_t)NULL);


    StartAsyncEvtTimer(CONNECTION_PHASE_TIMEOUT_SEC);

    return(0);
}

//*****************************************************************************
//
//! \brief Restart request - disconnected and open timer to re-open
//!  provisioning for non connection
//!
//! \param  None
//!
//! \return None
//!
//*****************************************************************************
int32_t ProcessRestartRequest(void)
{
    LOG_MESSAGE("\r ProcessRestartRequest stuck=%d ... \n", stuck);
    if (stuck) 
    {
        LOG_MESSAGE("\r ProcessRestartRequest stopping asynceventtimer \n");
        StopAsyncEvtTimer();
        LOG_MESSAGE("\r ProcessRestartRequest stopped asynceventtimer \n");
        int32_t            iRetVal = 0;
        /* Re-initialize Simple Link */
        LOG_MESSAGE("\r ProcessRestartRequest stopping simplelink \n");
        sl_Stop(SL_STOP_TIMEOUT);
        LOG_MESSAGE("\r ProcessRestartRequest stopped simplelink \n");
        if((iRetVal =
                sl_Start(NULL, NULL, (P_INIT_CALLBACK)SimpleLinkInitCallback)) < 0)
        {
            LOG_MESSAGE("sl_Start Failed\r\n");
            if (iRetVal == SL_ERROR_RESTORE_IMAGE_COMPLETE)
            {
                LOG_MESSAGE(
                    "\r\n**********************************\r\n"
                    "Return to Factory Default been Completed\r\n"
                    "Please RESET the Board\r\n"
                    "**********************************\r\n");
                gLedDisplayState = LedState_FACTORY_DEFAULT;
            }
            while(1)
            {
                ;
            }
        }
        stuck = false;
        gIsWlanConnected = 0;
        return(0);
    }
    gIsWlanConnected = 0;
    StartAsyncEvtTimer(CONNECTION_PHASE_TIMEOUT_SEC);
    
    /* [External configuration] - 
	 * Stop the external provisioning process (if running).
     * Start will be executed after 
	 * SL_WLAN_PROVISIONING_EXTERNAL_CONFIGURATION_READY is received */
    if (TRUE == IsActiveExternalConfiguration())
    {
        StopExternalProvisioning();
    }

    return (0);
}

//*****************************************************************************
//
//! \brief Start request -
//!   in case of external provisioning, it means that the nwp is now
//!   ready to start the process and the user's actions can be applied
//!
//! \param  None
//!
//! \return None
//!
//*****************************************************************************
int32_t ProcessStartRequest(void)
{
    /* [External configuration] -
     * Start the external provisioning process. The function bellow
     * Needs to be enabled and implemented in order to receive the profile
     * Credentials. This can happen since we got the following event
     * from the device:
     * SL_WLAN_PROVISIONING_EXTERNAL_CONFIGURATION_READY */
   // StartExternalProvisioning();

    return (0);
}
//*****************************************************************************
//
//! \brief Get AP security parameters
//!
//! \param  None
//!
//! \return None
//!
//*****************************************************************************
int32_t GetSecureStatus(void)
{
    uint8_t sec_type;
    uint16_t len = 1;
    uint16_t  config_opt = SL_WLAN_AP_OPT_SECURITY_TYPE;
    sl_WlanGet(SL_WLAN_CFG_AP_ID, &config_opt, &len, (_u8* )&sec_type);
    if (sec_type == AP_SEC_TYPE)
    {
        return(1);
    }
    return(0);
}

//*****************************************************************************
//
//! \brief Set AP security parameters
//!
//! \param  None
//!
//! \return None
//!
//*****************************************************************************
int32_t SetSecuredAP(const uint8_t sec_en)
{
    //set secured AP parameters
    uint8_t val = AP_SEC_TYPE;
    uint8_t open = SL_WLAN_SEC_TYPE_OPEN;
    uint8_t password[65];
    uint16_t len = strlen((char *)AP_SEC_PASSWORD);
    if(sec_en)
    {
        sl_WlanSet(SL_WLAN_CFG_AP_ID, SL_WLAN_AP_OPT_SECURITY_TYPE, 1,
                   (uint8_t *)&val);
        memset(password, 0, sizeof(password));
        memcpy(password, (char *)AP_SEC_PASSWORD, len);
        sl_WlanSet(SL_WLAN_CFG_AP_ID, SL_WLAN_AP_OPT_PASSWORD, len,
                   (uint8_t *)password);
        LOG_MESSAGE("\r Setting AP secured parameters\n");
    }
    else
    {
        sl_WlanSet(SL_WLAN_CFG_AP_ID, SL_WLAN_AP_OPT_SECURITY_TYPE, 1,
                   (uint8_t *)&open);
    }
    return(0);
}

//*****************************************************************************
//
//! \brief  Start connection - wait for connection
//!
//! \param  None
//!
//! \return None
//!
//*****************************************************************************
int32_t StartConnection(void)
{
    gIsWlanConnected = 0;

    GPIO_write(Board_GPIO_LED2, Board_GPIO_LED_OFF);
    LOG_MESSAGE("\r [App] StartConnection CurrentState: %d \n", g_CurrentState);
    StartAsyncEvtTimer(CONNECTION_PHASE_TIMEOUT_SEC);

    return(0);
}

//*****************************************************************************
//
//! \brief  Connection event arriver, open timer for IP ACQUIRED
//!
//! \param  None
//!
//! \return None
//!
//*****************************************************************************
int32_t HandleConnection(void)
{
    gIsWlanConnected = 1;

    LOG_MESSAGE("\r [App] HandleConnection %d \n", g_CurrentState);
    return(0);
}

//*****************************************************************************
//
//! \brief  Provisioning completed succesfully, connection established
//!         Open timer to make sure application nagotiation completed as well
//!
//! \param  None
//!
//! \return None
//!
//*****************************************************************************
int32_t HandleProvisioningComplete(void)
{
    gStopInProgress = 0;
    gIsWlanConnected = 1;
    LOG_MESSAGE("\r [Provisioning] Provisioning Application Ended Successfully current=%d \n ", g_CurrentState);

    /* [External configuration] - 
	Stop the external provisioning process (if running) */
    if (TRUE == IsActiveExternalConfiguration())
    {
        StopExternalProvisioning();
    }

    StartAsyncEvtTimer(RECONNECTION_ESTABLISHED_TIMEOUT_SEC);

    if(!once) {
        LOG_MESSAGE("\r [ProvisioningComplete] Sending confo current=%d  \r\n", g_CurrentState);
        GPIO_write(Board_GPIO_LED1, Board_GPIO_LED_ON);
        //int32_t             status = 0;
        //status = wlanConnect();
        //if (status < 0) {
        //    LOG_MESSAGE("\r\n wlanConnect error \r\n");
        //}
        //LOG_MESSAGE(" [TCPClient] Sending confo status=%d \r\n", status);
        updateTime();
        int32_t ret = 0;
        ip_t dest;
        //memset(&dest, 0x0, sizeof(dest));
        dest.ipv4 = DEST_IP_ADDR;
        uint8_t nb = 1; // doesn't seem to make a difference.
        int16_t port = 38979;
        uint32_t numPackets = 1;
        ret = TCPClient(nb, port, dest, FALSE /*ipv6*/, numPackets, TRUE);
        if (ret != 0) {
            LOG_MESSAGE("[TCPClient] [line:%d, error:%d] \n\r", __LINE__, ret);
            //Display_printf(display, 0, 0, "TCPClient failed");
        }
        else {
            GPIO_write(Board_GPIO_LED1, Board_GPIO_LED_OFF);
        }
        once=true;

        //port = 38981;
        //ret = TCPClient(nb, port, dest, FALSE /*ipv6*/, numPackets, TRUE);
        /*if (ret != 0) {
            LOG_MESSAGE("[TCPClient] [line:%d, error:%d] \n\r", __LINE__, ret);
            //Display_printf(display, 0, 0, "TCPClient failed");
        }*/
    }

    return(0);
}

//*****************************************************************************
//
//! \brief  Application side function
//!         Ping to Gateway and open timer for next ping
//!
//! \param  None
//!
//! \return None
//!
//*****************************************************************************
int32_t SendPingToGW(void)
{
    LOG_MESSAGE("[App] SendingPingtoGW %d \r\n", g_CurrentState);
    /* Get IP and Gateway information */
    uint16_t len = sizeof(SlNetCfgIpV4Args_t);
    uint16_t ConfigOpt = 0;   /* return value could be one of the following: 
    SL_NETCFG_ADDR_DHCP / SL_NETCFG_ADDR_DHCP_LLA / SL_NETCFG_ADDR_STATIC  */
    SlNetCfgIpV4Args_t ipV4 = {0};
    SlNetAppPingReport_t report;
    SlNetAppPingCommand_t pingCommand;

    sl_NetCfgGet(SL_NETCFG_IPV4_STA_ADDR_MODE,&ConfigOpt,&len,(uint8_t *)&ipV4);

    /* destination IP of gateway                */
    pingCommand.Ip = ipV4.IpGateway; 

    /* size of ping, in bytes                   */    
    pingCommand.PingSize = 150;           

    /* delay between pings, in milliseconds     */    
    pingCommand.PingIntervalTime = 120000;    

    /* timeout for every ping in milliseconds   */    
    pingCommand.PingRequestTimeout = 1000; 

    /* max number of ping requests. 0 - forever */    
    pingCommand.TotalNumberOfAttempts = 1; 

    /* report only when finished                */    
    pingCommand.Flags = 0;                          

    /* Ping Gateway */
    sl_NetAppPing( &pingCommand, SL_AF_INET, &report, NULL );

    /* Set Over all ping Statistics */
    gPingSent++;
    if (report.PacketsSent == report.PacketsReceived)
    {
        gPingSuccess++;
    }
    else
    {
        LOG_MESSAGE("\r [App] Ping error ... reconnecting ... \n");
        //sl_Stop(SL_STOP_TIMEOUT);
        //gRole = sl_Start(NULL, NULL, NULL);
        stuck = true;
        //SignalEvent(AppEvent_STUCK);
        GPIO_write(Board_GPIO_LED2, Board_GPIO_LED_ON);
        SignalEvent(AppEvent_RESTART);
        //SignalEvent(AppEvent_STARTED);
        return(0); // can't return -1 as the event hander will fail
    }
    LOG_MESSAGE("\r [App] "
        "Reply from %d.%d.%d.%d: %s, "
        "Time=%dms, \tOverall Stat Success (%d/%d)\r\n",
        SL_IPV4_BYTE(ipV4.IpGateway,3),SL_IPV4_BYTE(ipV4.IpGateway,
                                                    2),
        SL_IPV4_BYTE(ipV4.IpGateway,1),SL_IPV4_BYTE(ipV4.IpGateway,
                                                    0),
        (report.PacketsSent ==
         report.PacketsReceived) ? "SUCCESS" : "FAIL",
        (report.PacketsSent ==
         report.PacketsReceived) ? report.MinRoundTime : 0,
        gPingSuccess, gPingSent);
    

    // stop ping process
    pingCommand.Ip = 0; 
    sl_NetAppPing( &pingCommand, SL_AF_INET, &report, NULL );

    /* Get time from ntps */
    LOG_MESSAGE("\r [App] Timezone = %d.\n\r",ClockSync_getTimeZone());
    struct tm netTime;
    int32_t status = 0;
    status = ClockSync_get(&netTime);
    if ((status == 0) || (status == CLOCKSYNC_ERROR_INTERVAL))
    {
        LOG_MESSAGE("\r [SendPingToGW] Localtime: %s/r/n", asctime(&netTime));
    }
    else
    {
        LOG_MESSAGE("\r [SendPingToGW] Error = %d\n\r",status);
    }

    int32_t ret = 0;
    /* Set the time on the device - this needs to be done everytime to ensure the device doesn't lose track of the time on a power cycle. */
    SlDateTime_t dateTime= {0};
    dateTime.tm_sec = netTime.tm_sec;
    dateTime.tm_min = netTime.tm_min;
    dateTime.tm_hour = netTime.tm_hour;
    dateTime.tm_day = netTime.tm_mday;
    dateTime.tm_mon = netTime.tm_mon + 1;
    dateTime.tm_year = netTime.tm_year + 1900;
    //LOG_MESSAGE("\r [SendPingToGW] year: %d, month: %d, day: %d \n", dateTime.tm_year, dateTime.tm_mon, dateTime.tm_day);
    sl_DeviceSet(SL_DEVICE_GENERAL, SL_DEVICE_GENERAL_DATE_TIME, sizeof(SlDateTime_t), (uint8_t *)(&dateTime));
    //LOG_MESSAGE("\r [SendPingToGW] SlDateTime_t: %d-%d-%d\n", dateTime.tm_year, dateTime.tm_mon, dateTime.tm_day);
    //SlDateTime_t dateTime1= {0};
    //_i8 configOpt = SL_DEVICE_GENERAL_DATE_TIME;
    //ret = sl_DeviceGet(SL_DEVICE_GENERAL, &configOpt, sizeof(SlDateTime_t), (uint8_t *)(&dateTime1));
    //LOG_MESSAGE("\r [SendPingToGW] SlDateTime_t1: %d-%d-%d\n", dateTime1.tm_year, dateTime1.tm_mon, dateTime1.tm_day);
    //ASSERT_ON_ERROR1(ret, DEVICE_ERROR);
    /*if(clock_settime(CLOCK_REALTIME, &netTime) != 0) 
    {
        LOG_MESSAGE("settime error \n\r");
    }*/

    GPIO_write(Board_GPIO_LED2, Board_GPIO_LED_ON);
    ip_t dest;
    //memset(&dest, 0x0, sizeof(dest));
    dest.ipv4 = DEST_IP_ADDR;
    uint8_t nb = 1; // doesn't seem to make a difference.
    int16_t port = 38981;
    uint32_t numPackets = 1;
    ret = TCPClient(nb, port, dest, FALSE /*ipv6*/, numPackets, TRUE);
    if (ret != 0) {
        LOG_MESSAGE("\r [TCPClient] [line:%d, error:%d] \n\r", __LINE__, ret);
        LOG_MESSAGE("\r [App] TCPClient error ... reconnecting ... \n");
        //sl_Stop(100);
        //gRole = sl_Start(NULL, NULL, NULL);
        //SignalEvent(AppEvent_STUCK);
        stuck = true;
        SignalEvent(AppEvent_RESTART);
        //SignalEvent(AppEvent_STARTED);
        return(0);
        //Display_printf(display, 0, 0, "TCPClient failed");
    }
    else {
        GPIO_write(Board_GPIO_LED2, Board_GPIO_LED_OFF);
    }
    StartAsyncEvtTimer(PING_TIMEOUT_SEC);

    return(0);
}

//*****************************************************************************
//
//! \brief  Application side function
//!          Print AP parameters, and open timer to start real application
//!
//! \param  None
//!
//! \return None
//!
//*****************************************************************************
int32_t HandleUserApplication(void)
{
    LOG_MESSAGE("\r [App] User Application Started %d \r\n ", g_CurrentState);
    gIsWlanConnected = 1;
    gWaitForConn = 0;
    /* as long as the device connected, run user application */

    /* Get IP and Gateway information */
    uint16_t len = sizeof(SlNetCfgIpV4Args_t);
    uint16_t ConfigOpt = 0;   /* return value could be one of the following: 
                                 SL_NETCFG_ADDR_DHCP / SL_NETCFG_ADDR_DHCP_LLA
                                                    / SL_NETCFG_ADDR_STATIC  */
    SlNetCfgIpV4Args_t ipV4 = {0};

    sl_NetCfgGet(SL_NETCFG_IPV4_STA_ADDR_MODE,&ConfigOpt,&len,(uint8_t *)&ipV4);

    LOG_MESSAGE(
        "\tDHCP is %s \r\n\tIP \t%d.%d.%d.%d \r\n\tMASK \t%d.%d.%d.%d \r\n\tGW"
        " \t%d.%d.%d.%d \r\n\tDNS \t%d.%d.%d.%d\n\r",
        (ConfigOpt == SL_NETCFG_ADDR_DHCP) ? "ON" : "OFF",
        SL_IPV4_BYTE(ipV4.Ip,
                     3),SL_IPV4_BYTE(ipV4.Ip,2),SL_IPV4_BYTE(ipV4.Ip,
                                                             1),
        SL_IPV4_BYTE(ipV4.Ip,0),
        SL_IPV4_BYTE(ipV4.IpMask,3),SL_IPV4_BYTE(ipV4.IpMask,
                                                 2),SL_IPV4_BYTE(ipV4.IpMask,
                                                                 1),
        SL_IPV4_BYTE(ipV4.IpMask,0),
        SL_IPV4_BYTE(ipV4.IpGateway,3),SL_IPV4_BYTE(ipV4.IpGateway,
                                                    2),
        SL_IPV4_BYTE(ipV4.IpGateway,1),SL_IPV4_BYTE(ipV4.IpGateway,0),
        SL_IPV4_BYTE(ipV4.IpDnsServer,
                     3),
        SL_IPV4_BYTE(ipV4.IpDnsServer,2),SL_IPV4_BYTE(ipV4.IpDnsServer,
                                                      1),
        SL_IPV4_BYTE(ipV4.IpDnsServer,0));

    /* Reset Over All Ping Statistics*/
    gPingSent = 0;
    gPingSuccess = 0;


    /* Get time from ntps */
    LOG_MESSAGE("\r [App] Timezone = %d.\n\r",ClockSync_getTimeZone());
    struct tm netTime;
    int32_t status = 0;
    status = ClockSync_get(&netTime);
    if ((status == 0) || (status == CLOCKSYNC_ERROR_INTERVAL))
    {
        LOG_MESSAGE("\r [App] Localtime: %s/r/n", asctime(&netTime));
    }
    else
    {
        LOG_MESSAGE("\r [App] Error = %d\n\r",status);
    }

    StartAsyncEvtTimer(PING_TIMEOUT_SEC);

    return(0);
}

//*****************************************************************************
//
//! \brief   Wait for IP, open timer for error
//!
//! \param  None
//!
//! \return None
//!
//*****************************************************************************
int32_t HandleWaitForIp(void)
{
    LOG_MESSAGE("\r [Provisioning] HandleWaitForIp: %d \n", g_CurrentState);
    StartAsyncEvtTimer(CONNECTION_PHASE_TIMEOUT_SEC * 3);
    return(0);
}

//*****************************************************************************
//
//! \brief  Disconnect received, disconnect and open timer for re-connect
//!
//! \param  None
//!
//! \return None
//!
//*****************************************************************************
int32_t HandleDiscnctEvt(void)
{
    gIsWlanConnected = 0;
    LOG_MESSAGE("\r [Provisioning] HandleDiscnctEv: %d \n\r", g_CurrentState);
    StartAsyncEvtTimer(RECONNECTION_ESTABLISHED_TIMEOUT_SEC);
    return(0);
}

//*****************************************************************************
//
//! \brief  Connection established, check if force provisioning (robastnes test) 
//! or move to
//!         user application.
//!
//! \param  None
//!
//! \return None
//!
//*****************************************************************************
int32_t CheckLanConnection(void)
{
    LOG_MESSAGE("\r [App] CheckLanConnection %d \r\n", g_CurrentState);
    if (forget) { //&& !gWaitForConn) {
        LOG_MESSAGE("\r [App] Force Provisioning Start\r\n");
        returnToFactoryDefault();
        SignalEvent(AppEvent_PROVISIONING_STARTED);
        forget = false;
        once = false;
        return(0);
    }
    /* Force Provisioning Application to run */
    if ((FORCE_PROVISIONING)&&(!gWaitForConn))
    {
        LOG_MESSAGE("\r [App] Force Provisioning Start\r\n");
        returnToFactoryDefault();
        SignalEvent(AppEvent_PROVISIONING_STARTED);
    }
    else
    {
        /* Handle User application */
        SignalEvent(AppEvent_PROVISIONING_STOPPED);
    }
    return(0);
}

//*****************************************************************************
//
//! \brief   IP Acquired, open timer to make sure process completed
//!         user application.
//!
//! \param  None
//!
//! \return None
//!
//*****************************************************************************
int32_t CheckInternetConnection(void)
{
    LOG_MESSAGE("\r [Provisioning] CheckInternetConnection %d \n\r", g_CurrentState);
    /* Connect to cloud if enabled */
    // Occassionally when in AppState_PINGING_GW, an AppEvent_IP_ACQUIRED is encountered
    if (g_CurrentState == 3)
    {
        /* Handle User application */
        SignalEvent(AppEvent_PROVISIONING_STOPPED);
    }
    
    return(0);
}

//*****************************************************************************
//
//! \brief   Do Nothing
//!
//! \param  None
//!
//! \return None
//!
//*****************************************************************************
int32_t DoNothing(void)
{
    LOG_MESSAGE("\r [Provisioning] DoNothing: %d \n", g_CurrentState);
    /*if (forget) {
        StartAsyncEvtTimer(CONNECTION_PHASE_TIMEOUT_SEC);
        forget = false;
    }*/
    return(0);
}

//*****************************************************************************
//
//! \brief   Handle Provisioning timeout, check if connection success
//!          Continue if success, or start over if fail
//!
//! \param  None
//!
//! \return None
//!
//*****************************************************************************
int32_t HandleProvisioningTimeOut(void)
{
    LOG_MESSAGE("[Provisioning] HandleProvisioningTimeout: %d \n\r", g_CurrentState);
    /* Connection failed, restart */
    SignalEvent(AppEvent_RESTART);
    return(0);
}

//*****************************************************************************
//
//! \brief  This function signals the application events
//!
//! \param  None
//!
//! \return None
//!
//*****************************************************************************
int16_t SignalEvent(AppEvent event)
{
    char msg = (char)event;

    //
    //signal provisioning task about SL Event
    //
    mq_send(gProvisioningSMQueue, &msg, 1, 0);
    
    LOG_MESSAGE("\r [Signal] CurrentState: %d Event: %d \n", g_CurrentState, event);

    return(0);
}


//*****************************************************************************
//
//! \brief  In case external confirmation (cloud) failed, need to abort
//!
//! \param  None
//!
//! \return None
//!
//*****************************************************************************
void _AbortProvExternalConfirmation(void)
{
    int16_t retVal;

    LOG_MESSAGE("Aborting provisioning external confirmation...\r\n");
    retVal = sl_WlanProvisioning(
        SL_WLAN_PROVISIONING_CMD_ABORT_EXTERNAL_CONFIRMATION, 0, 0, NULL, 0);
    LOG_MESSAGE("Abort retVal=%d\r\n", retVal);
}

//*****************************************************************************
//
//! \brief  Delete all profiles and return to factory default
//!
//! \param  None
//!
//! \return None
//!
//*****************************************************************************
int32_t returnToFactoryDefault(void)
{
    int32_t slRetVal;
    SlFsRetToFactoryCommand_t RetToFactoryCommand;
    int32_t ExtendedError;
    unsigned char event;
    Provisioning_TableEntry_t   *pEntry = NULL;

    RetToFactoryCommand.Operation = SL_FS_FACTORY_RET_TO_DEFAULT;
    slRetVal =
        sl_FsCtl((SlFsCtl_e)SL_FS_CTL_RESTORE, 0, NULL,
                 (uint8_t *)&RetToFactoryCommand,
                 sizeof(SlFsRetToFactoryCommand_t), NULL, 0,
                 NULL);

    /* if command was not executed because provisioning is currently running,
     * send PRVISIONING_STOP command, and wait for the PROVISIONING_STOPPED 
     * event (which confirms that the provisioning process was successfully 
     * stopped) before trying again */

    if(SL_RET_CODE_PROVISIONING_IN_PROGRESS == slRetVal)
    {
        LOG_MESSAGE(
            " [ERROR] Provisioning is already running,"
            " stopping current session...\r\n");
        gStopInProgress = 1;
        slRetVal =
            sl_WlanProvisioning(SL_WLAN_PROVISIONING_CMD_STOP,0, 0, NULL,
                                (uint32_t)NULL);
        ASSERT_ON_ERROR(slRetVal);

        while(gStopInProgress)
        {
            mq_receive(gProvisioningSMQueue, (char *)&event, 1, NULL);

            StopAsyncEvtTimer();

            /* Find Next event entry */
            pEntry =
                (Provisioning_TableEntry_t *)
                &gTransitionTable[g_CurrentState][event];

            if (NULL != pEntry->p_evtHndl)
            {
                if (pEntry->p_evtHndl() < 0)
                {
                    LOG_MESSAGE("Event handler failed..!! \n\r");
                    while(1)
                    {
                        ;
                    }
                }
            }

            /* Change state according to event */
            if (pEntry->nextState != g_CurrentState)
            {
                g_CurrentState = pEntry->nextState;
            }
        }

        SignalEvent(AppEvent_RESTART);

        slRetVal =
            sl_FsCtl((SlFsCtl_e)SL_FS_CTL_RESTORE, 0, NULL,
                     (uint8_t *)&RetToFactoryCommand,
                     sizeof(SlFsRetToFactoryCommand_t), NULL, 0,
                     NULL);
        ASSERT_ON_ERROR(slRetVal);
    }
    if ((_i32)slRetVal < 0)
    {
        /* Pay attention, for this function the slRetVal is composed from 
                    Signed RetVal & extended error */
        slRetVal = (_i16)slRetVal >> 16;
        ExtendedError = (_u16)slRetVal & 0xFFFF;
        return(ExtendedError);
    }
    /* Reset */
    sl_Stop(SL_STOP_TIMEOUT);
    gRole = sl_Start(NULL, NULL, NULL);

    gIsWlanConnected = 0;

    return(slRetVal);
}

//*****************************************************************************
//
//! \brief  Start Provisioning example
//!
//! \param  None
//!
//! \return None
//!
//*****************************************************************************
int32_t ProvisioningStart(void)
{
    int32_t  retVal = 0;
    int32_t  status = 0;
    uint8_t  configOpt = 0;
    uint16_t configLen = 0;
    uint8_t  simpleLinkMac[SL_MAC_ADDR_LEN];
    uint16_t macAddressLen;
    uint8_t  provisioningCmd;
    SlDeviceVersion_t ver = {0};

    LOG_MESSAGE("\n\r\n\r\n\r==================================\n\r");
    LOG_MESSAGE(" Provisioning Example Ver. %s\n\r",APPLICATION_VERSION);
    LOG_MESSAGE("==================================\n\r");

    if (forget) 
    {
        _i16 Status;
        Status = sl_WlanProfileDel(SL_WLAN_DEL_ALL_PROFILES);
        if( Status < 0 )
        {
            LOG_MESSAGE("\r\n Unable to delete all profiles %d \n\r", Status);
        }
        else {
            LOG_MESSAGE("\r\n Deleted all profiles %d \n\r", Status);
            gIsWlanConnected = 0;
        }
        StopAsyncEvtTimer();
        forget = false;
    }

    status = GetSecureStatus();
    if ( (SECURED_AP_ENABLE) || (status != SECURED_AP_ENABLE))
    /*Will enter only if previous and present Secure AP are different or Secure AP is 1*/ 
    {
        SetSecuredAP(SECURED_AP_ENABLE);
        if (ROLE_AP == gRole)
        {
            /* Reset device to start secured AP */
            sl_Stop(SL_STOP_TIMEOUT);
            gRole = sl_Start(NULL, NULL, NULL);
        }
    }

    /* Get device's info */
    configOpt = SL_DEVICE_GENERAL_VERSION;
    configLen = sizeof(ver);
    retVal =
        sl_DeviceGet(SL_DEVICE_GENERAL, &configOpt, &configLen,
                     (uint8_t *)(&ver));

    if(SL_RET_CODE_PROVISIONING_IN_PROGRESS == retVal)
    {
        LOG_MESSAGE(
            " [ERROR] Provisioning is already running,"
            " stopping current session...\r\n");
        SignalEvent(AppEvent_ERROR);
        return 0;
    }

    LOG_MESSAGE(
        "\n\r CHIP 0x%x\r\n MAC  31.%d.%d.%d.%d\r\n PHY  %d.%d.%d.%d\r\n "
        "NWP  %d.%d.%d.%d\r\n ROM  %d\r\n HOST %d.%d.%d.%d\r\n",
        ver.ChipId,
        ver.FwVersion[0],ver.FwVersion[1],
        ver.FwVersion[2],ver.FwVersion[3],
        ver.PhyVersion[0],ver.PhyVersion[1],
        ver.PhyVersion[2],ver.PhyVersion[3],
        ver.NwpVersion[0],ver.NwpVersion[1],ver.NwpVersion[2],
        ver.NwpVersion[3],
        ver.RomVersion,
        SL_MAJOR_VERSION_NUM,SL_MINOR_VERSION_NUM,SL_VERSION_NUM,
        SL_SUB_VERSION_NUM);
    LOG_MESSAGE(" [App] Timezone = %d.\n\r",ClockSync_getTimeZone());

    macAddressLen = sizeof(simpleLinkMac);
    sl_NetCfgGet(SL_NETCFG_MAC_ADDRESS_GET,NULL,&macAddressLen,
                 (unsigned char *)simpleLinkMac);
    LOG_MESSAGE(" MAC address: %x:%x:%x:%x:%x:%x\r\n\r\n",
                simpleLinkMac[0],
                simpleLinkMac[1],
                simpleLinkMac[2],
                simpleLinkMac[3],
                simpleLinkMac[4],
                simpleLinkMac[5]                            );

    /* start provisioning */
    provisioningCmd = SL_WLAN_PROVISIONING_CMD_START_MODE_APSC;

    /* [External configuration] - change the provisioning command to bellow in case of external configuration */
    /* provisioningCmd = SL_WLAN_PROVISIONING_CMD_START_MODE_APSC_EXTERNAL_CONFIGURATION; */

    if(provisioningCmd <= 
	SL_WLAN_PROVISIONING_CMD_START_MODE_APSC_EXTERNAL_CONFIGURATION)
    {
        LOG_MESSAGE(
		"\r\n Starting Provisioning! "
		"mode=%d (0-AP, 1-SC, 2-AP+SC, 3-AP+SC+EXT)\r\n\r\n",provisioningCmd);
    }
    else
    {
        LOG_MESSAGE("\r\n Provisioning Command = %d \r\n\r\n",provisioningCmd);
    }

    gIsWlanConnected = 0;

    if( SC_KEY_ENABLE)
    {
        uint8_t key[SL_WLAN_SMART_CONFIG_KEY_LENGTH];

        memcpy(key, (char *)SC_KEY, SL_WLAN_SMART_CONFIG_KEY_LENGTH);
        retVal =
            sl_WlanProvisioning(provisioningCmd, ROLE_STA,
                                PROVISIONING_INACTIVITY_TIMEOUT,
                                (char *)&key,
                                (uint32_t)NULL);
    }
    else
    {
        retVal =
            sl_WlanProvisioning(provisioningCmd, ROLE_STA,
                                PROVISIONING_INACTIVITY_TIMEOUT,
                                NULL,
                                SL_WLAN_PROVISIONING_CMD_FLAG_EXTERNAL_CONFIRMATION);
                                //(uint32_t)NULL);
    }
    if(retVal < 0)
    {
        LOG_MESSAGE(" Provisioning Command Error, num:%d\r\n",retVal);
    }
    else 
    {
        LOG_MESSAGE(" Provisioning Command retval, num:%d\r\n",retVal);
    }

    return(0);
}

//*****************************************************************************
//
//! \brief  Start the main task, initialize SimpleLink device
//!
//! \param  None
//!
//! \return None
//!
//*****************************************************************************
void * ProvisioningTask(void *arg)
{
    int32_t            iRetVal = 0;
    unsigned char      event;
    Provisioning_TableEntry_t         *pEntry = NULL;
    mq_attr            attr;
    sigevent           sev;
    pthread_attr_t     timerThreadAttr;

    /* Create message queue */
    attr.mq_curmsgs = 0;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = sizeof( unsigned char );
    gProvisioningSMQueue = mq_open("SMQueue", O_CREAT, 0, &attr);

    pthread_attr_init(&timerThreadAttr);
    timerThreadAttr.stacksize = TIMER_TASK_STACK_SIZE;

    /* Create Timer */
    sev.sigev_notify = SIGEV_THREAD;
    sev.sigev_notify_function = &AsyncEvtTimerHandler;
    sev.sigev_notify_attributes = &timerThreadAttr;

    timer_create(CLOCK_MONOTONIC, &sev, &gTimer);

    /* Initialize Simple Link */
    if((iRetVal =
            sl_Start(NULL, NULL, (P_INIT_CALLBACK)SimpleLinkInitCallback)) < 0)
    {
        LOG_MESSAGE("sl_Start Failed\r\n");
        if (iRetVal == SL_ERROR_RESTORE_IMAGE_COMPLETE)
        {
            LOG_MESSAGE(
                "\r\n**********************************\r\n"
                "Return to Factory Default been Completed\r\n"
                "Please RESET the Board\r\n"
                "**********************************\r\n");
            gLedDisplayState = LedState_FACTORY_DEFAULT;
        }
        while(1)
        {
            ;
        }
    }

    while(1)
    {
        mq_receive(gProvisioningSMQueue, (char *)&event, 1, NULL);

        StopAsyncEvtTimer();

        /* Find Next event entry */
        pEntry =
            (Provisioning_TableEntry_t *)&gTransitionTable[g_CurrentState][
                event];

        if (NULL != pEntry->p_evtHndl)
        {
            if (pEntry->p_evtHndl() < 0)
            {
                LOG_MESSAGE("Event handler failed..!! current: %d event: %d \n\r", g_CurrentState, event);
                while(1)
                {
                    ;
                }
            }
        }

        /* Change state acording to event */
        if (pEntry->nextState != g_CurrentState)
        {
            g_CurrentState = pEntry->nextState;
        }
    }
}

//*****************************************************************************
//
//! \brief  Application startup display on UART
//!
//! \param  none
//!
//! \return none
//!
//*****************************************************************************
static void DisplayBanner(char * AppName)
{
    LOG_MESSAGE("\n\n\n\r");
    LOG_MESSAGE("\t\t *************************************************\n\r");
    LOG_MESSAGE("\t\t            %s Application       \n\r", AppName);
    LOG_MESSAGE("\t\t *************************************************\n\r");
    LOG_MESSAGE("\n\n\n\r");
    //uint8_t bytes[] = {0x7a, 0x04, 0x00, 0x00, 0x00, 0x02, 0x7B, 0x80};
    //WriteBytes(&bytes, 8);
}

_i32 ExtractLengthFromMetaData(_u8 *pMetaDataStart, _u16 MetaDataLen)
{
    _u8 *pTlv;
    _u8 *pEnd;
    _u8 Type;
    _u16 TlvLen;
    pTlv = pMetaDataStart;
    pEnd = pMetaDataStart + MetaDataLen;
    while (pTlv < pEnd)
    {
        Type = *pTlv; /* Type is one byte */
        pTlv++;
        TlvLen = *(_u16 *)pTlv; /* Length is two bytes */
        LOG_MESSAGE("\r [POST] header type (%d) length (%d) looking for (%d) or (%d) \n", Type, TlvLen, SL_NETAPP_REQUEST_METADATA_TYPE_HTTP_REFERER, SL_NETAPP_REQUEST_METADATA_TYPE_HTTP_CONTENT_LEN);
        pTlv+=2;
        if (Type == SL_NETAPP_REQUEST_METADATA_TYPE_HTTP_CONTENT_LEN)
        {
            _i32 LengthFieldValue=0;
            LOG_MESSAGE("\r [POST] header - (%d) contentlen \n", LengthFieldValue);
            /* Found the right type, extract its value and return. */
            memcpy(&LengthFieldValue, pTlv, TlvLen);
            pTlv += TlvLen;
        }
        else if (Type == SL_NETAPP_REQUEST_METADATA_TYPE_HTTP_REFERER) {
            // using the referer header to get owner credentials from the provisioning app
            free(email);
            email = malloc(TlvLen+1);
            memcpy(email, pTlv, TlvLen);
            //LOG_MESSAGE("\r [POST] header - configurer (%s) - len - (%d)\n ", email, TlvLen);
            email[TlvLen] = '\0';
            LOG_MESSAGE("\r [POST] header - configurer (%s) - len - (%d)\n ", email, TlvLen);
            pTlv += TlvLen;
        }
        else if (Type == SL_NETAPP_REQUEST_METADATA_TYPE_HTTP_ETAG) {
            // using the etag header to get gps coords from the provisioning app
            free(coords);
            coords = malloc(TlvLen+1);
            memcpy(coords, pTlv, TlvLen);
            coords[TlvLen] = '\0';
            LOG_MESSAGE("\r [POST] header - location (%s) - len - (%d)\n ", coords, TlvLen);
            pTlv += TlvLen;
        }
        else
        {
            /* Not the type we are looking for. Skip over the
             * value field to the next type. */
            free(temp);
            temp = malloc(TlvLen+1);
            memcpy(temp, pTlv, TlvLen);  
            temp[TlvLen] = '\0';
            LOG_MESSAGE("\r [POST] header - (%s)  unexpected type - len - (%d) \n", temp, TlvLen);
            pTlv += TlvLen;
        }
    }
    return -1;
}


/*!
    \brief          GPIO button function callback

    This routine gets called whenever board button (SW3 for CC32xx
    or S2 for MSP-432) gets pressed. It posts the sleep semaphore,
    in order to wake the device from LPDS.

    \param          index    -   Contains the board GPIO index who
                                 triggered this function.

    \return         void

    \sa             cmdEnableWoWLANCallback

*/
void gpioButtonFxn0(uint8_t index)
{
    if (forget) {
        LOG_MESSAGE("\n\r Ignoring GPIO button SW2 press .. \n\r");
        return;
    } 
    else {
        LOG_MESSAGE("\n\rGPIO button SW2 pressed .. \n\r");
    }
    forget = true;
    /*_i16 Status;
    Status = sl_WlanProfileDel(SL_WLAN_DEL_ALL_PROFILES);
    if( Status < 0 )
    {
        LOG_MESSAGE("\r\n Unable to delete all profiles %d \n\r", Status);
    }
    else {
        LOG_MESSAGE("\r\n Deleted all profiles %d \n\r", Status);
        gIsWlanConnected = 0;
    }*/
    LOG_MESSAGE("\r\n Restarting ... \n\r");
    SignalEvent(AppEvent_RESTART);
    //SignalEvent(AppEvent_STUCK);
    //StartAsyncEvtTimer(CONNECTION_PHASE_TIMEOUT_SEC);
    //StartAsyncEvtTimer(RECONNECTION_ESTABLISHED_TIMEOUT_SEC);  
    return;
}

void updateTime() {
    /* Get time from ntps */
    LOG_MESSAGE("\r [App] Timezone = %d.\n\r",ClockSync_getTimeZone());
    struct tm netTime;
    int32_t status = 0;
    status = ClockSync_get(&netTime);
    if ((status == 0) || (status == CLOCKSYNC_ERROR_INTERVAL))
    {
        LOG_MESSAGE("\r [App] Localtime: %s/r/n", asctime(&netTime));
    }
    else
    {
        LOG_MESSAGE("\r [App] Error = %d\n\r",status);
    }

    /* Set the time on the device - this needs to be done everytime to ensure the device doesn't lose track of the time on a power cycle. */
    SlDateTime_t dateTime= {0};
    dateTime.tm_sec = netTime.tm_sec;
    dateTime.tm_min = netTime.tm_min;
    dateTime.tm_hour = netTime.tm_hour;
    dateTime.tm_day = netTime.tm_mday;
    dateTime.tm_mon = netTime.tm_mon + 1;
    dateTime.tm_year = netTime.tm_year + 1900;
    sl_DeviceSet(SL_DEVICE_GENERAL, SL_DEVICE_GENERAL_DATE_TIME, sizeof(SlDateTime_t), (uint8_t *)(&dateTime));
    /*if(clock_settime(CLOCK_REALTIME, &netTime) != 0) */
}

int32_t wlanConnect(void)
{
    SlWlanSecParams_t secParams = {0};
    int32_t status = 0;

    secParams.Key = (signed char*)AP_SEC_PASSWORD;
    secParams.KeyLen = strlen(AP_SEC_PASSWORD);
    secParams.Type = AP_SEC_TYPE;

    status = sl_WlanConnect((const signed char*)ssidName, strlen(ssidName),  
                            0, &secParams, 0);
    ASSERT_ON_ERROR(status);
    
    //Display_printf(display, 0, 0, "Trying to connect to AP : %s\n\r", SSID_NAME);

    // Wait for WLAN Event
    /*while((!IS_CONNECTED(PowerMeasure_CB.slStatus)) ||
          (!IS_IP_ACQUIRED(PowerMeasure_CB.slStatus)))
    { 
        // Turn on user LED 
        GPIO_write(Board_GPIO_LED0, Board_GPIO_LED_ON);
        usleep(50000);
        // Turn off user LED 
        GPIO_write(Board_GPIO_LED0, Board_GPIO_LED_OFF);
        usleep(50000);
    }*/

    return(0);
   
}

void NADA(void) {


}

//*****************************************************************************
//
//! \brief  Main application thread
//!
//! \param  none
//!
//! \return none
//!
//*****************************************************************************
void * mainThread( void *arg )
{

    uint32_t             RetVal;
    pthread_attr_t      pAttrs;
    pthread_attr_t      pAttrs_spawn;
    pthread_attr_t      pAttrs_display;
    pthread_attr_t      pAttrs_adc;
    //pthread_attr_t      pAttrs_uart1;
    struct sched_param  priParam;

    /* Initialize SlNetSock layer with CC31xx/CC32xx interface */
    SlNetIf_init(0);
    SlNetIf_add(SLNETIF_ID_1, "CC32xx",
                (const SlNetIf_Config_t *)&SlNetIfConfigWifi,
                5);

    SlNetSock_init(0);
    SlNetUtil_init(0);

    GPIO_init();
    SPI_init();
    ADC_init();

    /* Create a mutex that will protect voltage variable */
    RetVal = pthread_mutex_init(&voltageMutex, NULL);
    if (RetVal != 0) {
        /* pthread_mutex_init() failed */
        UART_PRINT("\r m0v - Couldn't create mutex  - %d\n", RetVal);
        //LOG_MESSAGE("\r b00m - Couldn't create mutex  - %d\n", RetVal);
        while (1) {}
    }

    /* Initial Terminal, and print Application name */
    InitTerm();
    InitTerm1();
    //DisplayBanner(APPLICATION_NAME);

    /* Switch off all LEDs on boards */
    GPIO_write(Board_GPIO_LED0, Board_GPIO_LED_OFF);
    GPIO_write(Board_GPIO_LED1, Board_GPIO_LED_OFF);
    GPIO_write(Board_GPIO_LED2, Board_GPIO_LED_OFF);
    /* install Button callback  SW2 on CC3220S Launchpad */
    GPIO_setCallback(Board_GPIO_BUTTON0, gpioButtonFxn0);
    /* Enable interrupts */
    GPIO_enableInt(Board_GPIO_BUTTON0);

    /* create the sl_Task */
    pthread_attr_init(&pAttrs_spawn);
    priParam.sched_priority = SPAWN_TASK_PRIORITY;
    RetVal = pthread_attr_setschedparam(&pAttrs_spawn, &priParam);
    RetVal |= pthread_attr_setstacksize(&pAttrs_spawn, TASK_STACK_SIZE);

    RetVal = pthread_create(&gSpawnThread, &pAttrs_spawn, sl_Task, NULL);

    if(RetVal)
    {
        while(1)
        {
            ;
        }
    }

    /* create the provisioning thread */
    pthread_attr_init(&pAttrs);
    priParam.sched_priority = 1;
    RetVal = pthread_attr_setschedparam(&pAttrs, &priParam);
    RetVal |= pthread_attr_setstacksize(&pAttrs, TASK_STACK_SIZE);

    if(RetVal)
    {
        /* error handling */
        while(1)
        {
            ;
        }
    }

    RetVal = pthread_create(&gProvisioningThread, &pAttrs, ProvisioningTask, NULL);

    if(RetVal)
    {
        while(1)
        {
            ;
        }
    }

    /* create led display thread */
    pthread_attr_init(&pAttrs_display);
    priParam.sched_priority = 5;
    RetVal = pthread_attr_setschedparam(&pAttrs_display, &priParam);
    RetVal |= pthread_attr_setstacksize(&pAttrs_display,
                                        DISPLAY_TASK_STACK_SIZE);

    if(RetVal)
    {
        /* error handling */
        while(1)
        {
            ;
        }
    }

    RetVal = pthread_create(&gDisplayThread, &pAttrs_display, UpdateLedDisplay, NULL);

    if(RetVal)
    {
        while(1)
        {
            ;
        }
    }

    /* create the adc thread */
    /*
    pthread_attr_init(&pAttrs_adc);
    priParam.sched_priority = 1;
    RetVal = pthread_attr_setschedparam(&pAttrs_adc, &priParam);
    RetVal |= pthread_attr_setstacksize(&pAttrs_adc, TASK_STACK_SIZE);

    if(RetVal)
    {
        // error handling 
        while(1)
        {
            ;
        }
    }
    RetVal = pthread_create(&gAdcThread, &pAttrs_adc, adcThread, NULL);
    if (RetVal != 0) {
        // pthread_create() failed 
        while (1);
    }
    */

    /* create the uart1 read thread */
    /*pthread_attr_init(&pAttrs_uart1);
    priParam.sched_priority = 1;
    RetVal = pthread_attr_setschedparam(&pAttrs_uart1, &priParam);
    RetVal |= pthread_attr_setstacksize(&pAttrs_uart1, TASK_STACK_SIZE);

    if(RetVal)
    {
        // error handling 
        while(1)
        {
            ;
        }
    }
    RetVal = pthread_create(&gUart1Thread, &pAttrs_uart1, uart1Thread, NULL);
    if (RetVal != 0) {
        // pthread_create() failed 
        while (1);
    }*/
    return(0);
}
