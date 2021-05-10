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

//*****************************************************************************
// Includes
//*****************************************************************************
#ifdef CC32XX
#include <ti/devices/cc32xx/inc/hw_types.h>
#include <ti/devices/cc32xx/driverlib/prcm.h>
#include <ti/drivers/power/PowerCC32XX.h>
#endif

#include <ti/drivers/GPIO.h>

#ifndef NORTOS_SUPPORT
#include <pthread.h>
#else
#include <ti/drivers/dpl/ClockP.h>
#endif

#include <time.h>
#include <ti/drivers/net/wifi/simplelink.h>
#include "Board.h"
#include "platform.h"

//*****************************************************************************
// Defines
//*****************************************************************************
#define APPS_WDT_START_KEY          0xAE42DB15
#define HWREG(x) (*((volatile unsigned long *)(x)))

//*****************************************************************************
// Typedefs
//*****************************************************************************
typedef struct e_Timers
{
    unsigned long ePeripheral;
    unsigned long base;
    unsigned long timer;
}e_Timers;

typedef struct sBootInfo
{
    uint8_t ucActiveImg;
    uint32_t ulImgStatus;
    uint32_t ulStartWdtKey;
    uint32_t ulStartWdtTime;
}sBootInfo_t;

#ifdef __MSP432P401R__
//*****************************************************************************
//
//! Initiates a soft system reset.
//!
//! \return none
//
//*****************************************************************************
#include <ti/devices/msp432p4xx/driverlib/rom_map.h>
extern void MAP_ResetCtl_initiateSoftReset(void);
#endif

//*****************************************************************************
// Globals
//*****************************************************************************
#ifdef NORTOS_SUPPORT
ClockP_Handle gAsyncEvtTimerHandle;
#else
timer_t g_timerAsyncEvent;
#endif

extern void * InitTerm();

//*****************************************************************************
// Local Functions
//*****************************************************************************

//*****************************************************************************
//
//! \brief
//
//*****************************************************************************
void Platform_Reset()
{
#ifdef __MSP432P401R__
    MAP_ResetCtl_initiateSoftReset();
#elif CC32XX
    /* Reset the MCU in order to test the bundle */
    UART_PRINT("******* Rebooting *******\r\n");
    PRCMHibernateCycleTrigger();
#endif
}

void powerShutdown(uint32_t shutdownTime)
{
#ifdef CC32XX
    Power_NotifyObj hibSignal;
#endif

#ifdef __MSP432P401R__
#if 0
    Power_registerNotify(&hibSignal, PowerMSP432_ENTERING_SHUTDOWN,
                         preHibConfig,
                         (uintptr_t)NULL);
    if(shutdownTime != MAX_INT)
    {
        /* config gpio to wakeup from hib */
    }
    /*
     *    PowerMSP432_SHUTDOWN_0 => PCM_LPM35_VCORE0
     *    PowerMSP432_SHUTDOWN_1 => PCM_LPM45
     */
    Power_shutdown(PowerMSP432_SHUTDOWN_0,0);
#else
    if(shutdownTime != MAX_INT)
    {
        if(shutdownTime >= 1000)
        {
            sleep(shutdownTime / 1000);
        }
        usleep((shutdownTime % 1000) * 1000);
    }
#endif
#elif CC32XX
    Power_registerNotify(&hibSignal, PowerCC32XX_ENTERING_SHUTDOWN,
                         preHibConfig,
                         (uintptr_t)NULL);
    Power_shutdown(0,shutdownTime);
#endif
}

//*****************************************************************************
//
//! \brief
//
//*****************************************************************************
void Platform_Init()
{
    /* Enable Switch 2*/
    GPIO_setCallback(Board_GPIO_BUTTON0,
                     (GPIO_CallbackFxn)Platform_gpioButtonFxn0);

    /* Enable interrupts */
    GPIO_enableInt(Board_GPIO_BUTTON0);
}

//*****************************************************************************
//
//! \brief
//
//*****************************************************************************
void Platform_gpioButtonFxn0(unsigned int index)
{
    //UART_PRINT("******* OTA Switch Pressed - Run OTA Process *******\r\n");
    notifyOtaCommandArrived();

    GPIO_clearInt(Board_GPIO_BUTTON0);
}

//*****************************************************************************
//
//! \brief
//
//*****************************************************************************
int32_t Platform_CommitWdtConfig(int32_t TimeoutInSeconds)
{
#ifdef CC32XX
    int32_t lFileHandle;
    uint32_t ulToken = 0;
    sBootInfo_t sBootInfo;
    int32_t lRetVal;

    lFileHandle = sl_FsOpen(
        (unsigned char *)"/sys/mcubootinfo.bin",
        SL_FS_CREATE | SL_FS_OVERWRITE |
        SL_FS_CREATE_MAX_SIZE(
            sizeof(sBootInfo)) |
        SL_FS_CREATE_SECURE | SL_FS_CREATE_PUBLIC_WRITE |
        SL_FS_CREATE_NOSIGNATURE,
        (_u32 *)&ulToken);

    if(0 > lFileHandle)
    {
        //OTA_DBG_PRINT("OtaWatchDog: Error opening bootinfo file
      //        : %d\n\r",lFileHandle);
        return(-1);
    }

    memset(&sBootInfo,0,sizeof(sBootInfo_t));
    /* max 104 seconds */
    sBootInfo.ulStartWdtTime = 40000000 * TimeoutInSeconds; 
    sBootInfo.ulStartWdtKey = APPS_WDT_START_KEY;
    lRetVal =
        sl_FsWrite(lFileHandle, 0, (uint8_t*)&sBootInfo, sizeof(sBootInfo_t));
    lRetVal = sl_FsClose(lFileHandle, 0, 0, 0);
    if(0 != lRetVal)
    {
        //OTA_DBG_PRINT("OtaWatchDog: Failed to close the bootinfo file");
        return(-1);
    }
#endif
    //Enable WDT - done by PRCMCC3200MCUInit
    //HWREG(0x4402E188) |= 0x00000020;
    return(0);
}

//*****************************************************************************
//
//! \brief
//
//*****************************************************************************
void Platform_CommitWdtStop()
{
#ifdef CC32XX
    PowerCC32XX_reset(PowerCC32XX_PERIPH_WDT);
#endif
}

#ifdef NORTOS_SUPPORT

//*****************************************************************************
//
//! \brief
//
//*****************************************************************************
void Platform_TimerInit(void (*timerIntHandler)(int val))
{
    ClockP_Params params;

    /* Create a NonOs timer */
    ClockP_Params_init(&params);
    gAsyncEvtTimerHandle = ClockP_create((ClockP_Fxn)timerIntHandler, 0,
                                         &params);
}

//*****************************************************************************
//
//! \brief
//
//*****************************************************************************
void Platform_TimerStart(uint32_t asyncEvtTimeoutMsec)
{
    ClockP_setTimeout(gAsyncEvtTimerHandle,asyncEvtTimeoutMsec);
    ClockP_start(gAsyncEvtTimerHandle);
}

void Platform_TimerStop()
{
    ClockP_stop(gAsyncEvtTimerHandle);
}

#else

//*****************************************************************************
//
//! \brief
//
//*****************************************************************************
void Platform_TimerInit(void (*timerIntHandler)(sigval val))
{
    sigevent sev;

    /* Create Timer */
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_notify_function = timerIntHandler;

    timer_create(CLOCK_MONOTONIC, &sev, &g_timerAsyncEvent);
}

//*****************************************************************************
//
//! \brief
//
//*****************************************************************************
void Platform_TimerStart(uint32_t asyncEvtTimeoutMsec)
{
    struct itimerspec value;

    /* set as one shot timer */
    value.it_interval.tv_sec = 0;
    value.it_interval.tv_nsec = 0;

    /* set the timeout */
    value.it_value.tv_sec = (asyncEvtTimeoutMsec / 1000);
    value.it_value.tv_nsec = (asyncEvtTimeoutMsec % 1000) * 1000000;

    value.it_value.tv_sec += (value.it_value.tv_nsec / 1000000000);
    value.it_value.tv_nsec = value.it_value.tv_nsec % 1000000000;

    // kick the timer
    timer_settime(g_timerAsyncEvent, 0, &value, NULL);
}

//*****************************************************************************
//
//! \brief
//
//*****************************************************************************
void Platform_TimerStop()
{
    struct itimerspec value;

    /* stop timer */
    value.it_interval.tv_sec = 0;
    value.it_interval.tv_nsec = 0;
    value.it_value.tv_sec = 0;
    value.it_value.tv_nsec = 0;
    timer_settime(g_timerAsyncEvent, 0, &value, NULL);
}

#endif

//*****************************************************************************
//
//! \brief
//
//*****************************************************************************
void Platform_TimerInterruptClear()
{
    // Do nothing...
}

//*****************************************************************************
//
//! \brief
//
//*****************************************************************************
uint32_t Platform_MsecTimestamp()
{
    return(slcb_GetTimestamp());
}

//*****************************************************************************
//
//! \brief
//
//*****************************************************************************
void Platform_TimeoutStart(PlatformTimeout_t *pTimeoutInfo,
                           uint32_t TimeoutInMsec)
{
    pTimeoutInfo->Total10MSecUnits = TimeoutInMsec / 10;
    pTimeoutInfo->TSPrev = slcb_GetTimestamp();
    pTimeoutInfo->TSCurr = 0;
    pTimeoutInfo->DeltaTicks = 0;
    pTimeoutInfo->DeltaTicksReminder = 0;
}

//*****************************************************************************
//
//! \brief
//
//*****************************************************************************
uint8_t Platform_TimeoutIsExpired(PlatformTimeout_t *pTimeoutInfo)
{
    uint32_t TSCount;

    pTimeoutInfo->TSCurr = slcb_GetTimestamp();

    if(pTimeoutInfo->TSCurr >= pTimeoutInfo->TSPrev)
    {
        pTimeoutInfo->DeltaTicks = pTimeoutInfo->TSCurr - pTimeoutInfo->TSPrev;
    }
    else
    {
        pTimeoutInfo->DeltaTicks =
            (SL_TIMESTAMP_MAX_VALUE -
             pTimeoutInfo->TSPrev) + pTimeoutInfo->TSCurr;
    }

    TSCount = pTimeoutInfo->DeltaTicksReminder + pTimeoutInfo->DeltaTicks;

    if(TSCount > SL_TIMESTAMP_TICKS_IN_10_MILLISECONDS)
    {
        pTimeoutInfo->Total10MSecUnits -=
            (TSCount / SL_TIMESTAMP_TICKS_IN_10_MILLISECONDS);
        pTimeoutInfo->DeltaTicksReminder = TSCount %
                                        SL_TIMESTAMP_TICKS_IN_10_MILLISECONDS;

        if(pTimeoutInfo->Total10MSecUnits > 0)
        {
            pTimeoutInfo->TSPrev = pTimeoutInfo->TSCurr;
        }
        else
        {
            return(TRUE);
        }
    }

    return(FALSE);
}

//*****************************************************************************
//
//! \brief
//
//*****************************************************************************
void Platform_Sleep(uint32_t mSecDuration)
{
    PlatformTimeout_t SleepTimeout = {0};

    Platform_TimeoutStart(&SleepTimeout, mSecDuration);

    while(1)
    {
        if(Platform_TimeoutIsExpired(&SleepTimeout))
        {
            /* sleep expired */
            break;
        }
    }
}

//*****************************************************************************
//
//! \brief
//
//*****************************************************************************
void Platform_LedToggle()
{
    GPIO_toggle(Board_GPIO_LED0);
}

//*****************************************************************************
//
//! \brief
//
//*****************************************************************************
void Platform_FactoryDefaultIndication()
{
    uint32_t g_ledState = 0;
    uint32_t g_ledCount = 0;

    GPIO_write(Board_GPIO_LED0, 0);
    GPIO_write(Board_GPIO_LED1, 1);
    GPIO_write(Board_GPIO_LED2, 0);

    while(1)
    {
        g_ledCount++;
        g_ledCount &= 0x1FFFF;

        if(0 == g_ledCount)
        {
            if(g_ledState)
            {
                GPIO_toggle(Board_GPIO_LED1);
                GPIO_toggle(Board_GPIO_LED0);
                g_ledState = 0;
            }
            else
            {
                GPIO_toggle(Board_GPIO_LED1);
                GPIO_toggle(Board_GPIO_LED0);
                g_ledState = 1;
            }
        }
    }
}
