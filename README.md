Title: TI CC3220
Category: manual
Tags: hardware, software, tronix
Date: 3rd November 2017
Format: markdown

## Contents ##
- [Brief](#brief)
    - [OOTB](#ootb)
    - [Errata](#err)
- [Download](#brief)
- [Components](#components)
    - [Empty](#empty)
    - [Provisioning](#prov)
    - [MOV](#mov)
    - [ADC](#adc)
    - [Networking](#networking)
        - [Wifi](#wifi)
        - [sntp](#sntp)
    - [Modbus](#modbus)
    - [Cloud-ota](#cloud-ota)
    - [Combined](#combined)
- [Minicom](#minicom)
- [Uniflash](#uniflash)
- [Power](#power)
- [Layout](#layout)
- [Gotchas](#gotchas)
- [Todo](#todo)
- [Manual](#manual)
- [References](#references)


## [Brief](#brief){#brief}

Notes on provisioning/adc/cloud-ota

`TI_RTOS` is bundled in `Simplelink SDK`. No need to download separately.

Arm compiler at:
```
~/ti/ccsv8/ccsv8/tools/compiler/ti-cgt-arm_18.1.7.LTS/bin
```

### [OOTB](#ootb){#ootb}

swru473.pdf

### [Errata](#errata){#errata}

6.1.1 Provisioning: Provisioning mode is reflected by a flashing red LED (D10, not D7). 
6.1.2 CC3220 as AP: AP mode is applied by pressing the SW3 (not SW2) switch on the CC3220 Launchpad.

## [Download](#download){#download}

+ [Simplelink SDK]() 
+ [Uniflash]()
+ [CCStudio]()

## [Components](#components){#components}

### [Empty](#empty){#empty}

Provisioning using external confirmation/feedback

From swru455g (16.8.1):

To use a cloud-based feedback, the external confirmation bit should be set in the provisioning host command flags parameter when the provisioning process is started.

```
sl_WlanProvisioning(SL_WLAN_PROVISIONING_CMD_START_MODE_APSC, ROLE_AP, 600, NULL, SL_WLAN_PROVISIONING_CMD_FLAG_EXTERNAL_CONFIRMATION);
```

Can only use the above with `ROLE_STA` as second argument because it needs to be in station mode and connect to an external server to confirm connection. 


### [Provisioning](#provisioning){#provisioning}

Each of 7 states has 12 event handlers (function pointers) and corresponding next state defined by lookup/transition table `const Provisioning_TableEntry_t gTransitionTable[7][12]` 

8 AppState(s): 
```
0 STARTING 
1 WAITING_FOR_CONNECTION
2 WAIT_FOR_IP
3 PINGING_GW  
4 PROVISIONING_IN_PROGRESS 
5 PROVISIONING_WAIT_COMPLETE
6 ERROR
7 MAX
```
13 AppEvent(s):
```
0 STARTED 
1 CONNECTED
2 IP_ACQUIRED 
3 DISCONNECT
4 PING_COMPLETE 
5 PROVISIONING_STARTED 
6 PROVISIONING_SUCCESS 
7 PROVISIONING_STOPPED 
8 PROVISIONING_WAIT_CONN
9 TIMEOUT 
10 ERROR 
11 RESTART
12 MAX
```
17 event handlers (function pointers) of the form: 
```
typedef int32_t (*fptr_EventHandler)(void); 
``` 
For instance: 
```
int32_t StartConnection(void);                 // SC
int32_t HandleWaitForIp(void);                 // HWI
int32_t ProvisioningStart(void);               // PS
int32_t ProcessRestartRequest(void);           // PRR
int32_t ReportError(void);                     // RE
int32_t DoNothing(void);                       // DN
int32_t CheckLanConnection(void);              // CLC
int32_t CheckInternetConnection(void);         // CIC
int32_t HandleProvisioningComplete(void);      // HPC
int32_t HandleProvisioningTimeout(void);       // HPT
int32_t HandleUserApplication(void);           // HUA
int32_t SendPingToGW(void);                    // SPG
int32_t HandleDiscnctEvt(void);                // HD 
```

|State/Event| 0    | 1    | 2    | 3    | 4    | 5    | 6    | 7    | 8    | 9    | 10   | 11   |
|-----------|------|------|------|------|------|------|------|------|------|------|------|------|
| 0         |SC->1 |HWI->2|RE->6 |RE->6 |RE->6 |RE->6 |RE->6 |RE->6 |RE->6 |RE->6 |RE->6 |PS->4 |
| 1         |SC->1 |HWI->2|RE->6 |SC->1 |HPC->3|PS->4 |DN->1 |CLC->1|DN->1 |PS->4 |PS->4 |PRR->1|
| 2         |PS->4 |RE->6 |CLC->3|SC->1 |DN->2 |PS->4 |DN->2 |HPC->3|DN->2 |RE->6 |RE->6 |PRR->1|
| 3         |DN->3 |HWI->2|CIC->3|HD->1 |DN->3 |PS->4 |DN->3 |HUA->3|DN->3 |SPG->3|RE->6 |PRR->1|
| 4         |PSR->4|HC->4 |CIC->4|HD->4 |DN->4 |DN->4 |HPC->5|HPC->3|DN->1 |PRR->1|RE->6 |PRR->1|
| 5         |DN->5 |DN-->5|DN->5 |HD->5 |DN->5 |DN->5 |DN->5 |HUA->3|DN->1 |HPT->5|RE->6 |PRR->5|
| 6         |RE->1 |RE-->1|RE->1 |RE->1 |RE->1 |RE->1 |RE->1 |RE->1 |RE->1 |RE->1 |RE->1 |PS->1 |

```
S0->E0->SC->S1
S1->E1->HWI->S2
S2->E2->CLC->S3->E7(from CLC)->HUA->3
OR 
S2->E7->HPC->S3
S3->E9->SPG->S3->E9 ... repeat
```

4 threads are created:
```
RetVal = pthread_create(&gSpawnThread, &pAttrs_spawn, sl_Task, NULL);
RetVal = pthread_create(&gProvisioningThread, &pAttrs, ProvisioningTask, NULL);
RetVal = pthread_create(&gDisplayThread, &pAttrs_display, UpdateLedDisplay, NULL);
RetVal = pthread_create(&gAdcThread, &pAttrs_adc, adcThread, NULL);
```

The `ProvioningTask` thread creates a message queue `gProvisioningSMQueue` and receives from this queue:
```
gProvisioningSMQueue = mq_open("SMQueue", O_CREAT, 0, &attr);
...
while(1) {
    mq_receive(gProvisioningSMQueue, (char *)&event, 1, NULL);
    ...
}
```
The SL event handlers `SimpleLinkWlanEventHandler/SimpleLinkNetAppEventHandler` and the application event handlers send to this queue using function `SignalEvent`. 
```
int16_t SignalEvent(AppEvent event) {
    ...
    mq_send(gProvisioningSMQueue, &msg, 1, 0);
    ...
}
```

Both `ProvisioningTask & adcThread` create timers using sigevent.

The `adcThread` also blocks on a semaphore which gets posted to when the timer expires.

### [MOV](#mov){#mov}

The SPD that the CC3220S will connect has a mechanical relay that gets triggered when the thermal fuse burns out. So the normally open (NO) becomes closed and the normally closed (NC) becomes open. 

A simple continuity check circuit is used to check the state of NO. When the thermal fuse hasn't burned out, the transistor doesn't conduct (as theres no base current as the circuit is open) and the comparator ouputs low as its inverting input is lower. 

When the thermal fuse burns out and NO is now closed, the transistor conducts and the comparator ouputs high as its inverting input is higher. 

The output of the comparator can be fed to either the ADC or GPIO of the CC3220S. 

### [ADC](#adc){#adc}

ADC0 = pin 59
ADC1 = pin 60

The ADC is designed to be connected to the NC pin on the SPD (with COM on SPD sharing ground with the CC3220S). The read value on the ADC should be > 0.1V to qualify as "closed" which will result in a protection status of `true` sent to the server. Else the circuit is considered "open" and considered unprotected. Leave the NO pin on the SPD unconnected.

Pin 59 (GPIO04 module pin 49) is ADC_CH2. On the custom CC3220MODAS board it's connected to ANA_IN3. But on pwrcon_1 the analog input from the continuity circuit lines up with ANA_IN2. So short ANA_IN2 & ANA_IN3 and leave ANA_IN2 unconnected (don't populate the 0 resistor). 

### [Networking](#networking){#networking}

#### [Wifi](#wifi){#wifi}

When starting with the `empty` project and adding in Simplelink, add the `Library Path` and library`ccs/rtos/simplelink.a` in the project properties in C/C++ General -> Paths and Symbols. 

Then implement all the required callback functions (`SimpleLinkNetAppRequestMemFreeEventHandler`, `SimpleLinkNetAppRequestEventHandler`, `SimpleLinkWlanEventHandler`, `SimpleLinkGeneralEventHandler`, etc. etc.).

The host controls the provisioning process using one command: 
```
_i16 sl_WlanProvisioning(_u8 ProvisioningCmd, _u8 RequestedRoleAfterSuccess, _u16 InactivityTimeoutSec, char *pSmartConfigKey, _u32 Flags);
```
RequestedRoleAfterSucess: The desired role (AP or STA) to which the device should switch if provisioning is successful (relevant only if the value of the ProvisioningCmd is 0, 1, 2, or 3).

InactivityTimeoutSec: Defines the period of time (in seconds) the system waits before it automatically stops the provisioning process when no user activity is detected. Relevant only if the value of the
ProvisioningCmd command is 0, 1, 2, or 3.

Flags: Optional configuration conducted by a bitmap.

| Command        |         Value                |                            Action                                  |
|:----------- --:|:----------------------------:|:------------------------------------------------------------------:|
| BIT_0          | ENABLE_EXTERNAL_CONFIRMATION | Defines whether to use external confirmation or not. Relevant only |
|                |                              | if the value of the ProvisioningCmd command is 0, 1, 2, or 3.      |

Example usage: `retVal = sl_WlanProvisioning(provisioningCmd, ROLE_STA, PROVISIONING_INACTIVITY_TIMEOUT, NULL, SL_WLAN_PROVISIONING_CMD_FLAG_EXTERNAL_CONFIRMATION)`

#### [sntp](#sntp){#sntp}

As per the problem related in [sntp], the service pack needs to be included while using Uniflash to flash the image otherwise a -202 (sntp module error) is encountered.

Network time issue fixed in 94fb658

```
struct tm netTime;
...
status = ClockSync_get(&netTime); // tm's months run from 0->11
SlDateTime_t dateTime = {0};
...
dateTime.tm_mon = netTime.tm_mon + 1; // but SlDateTime's months run from 1->12
```

### [Modbus](#modbus){#modbus}

```
github.com/Jacajack/liblightmodbus
```

### [Cloud-OTA](#cloud-ota){#cloud-ota}

In the TI cloud-ota example each of 8 states has 12 event handlers (function pointers) and corresponding next state defined by lookup/transition table `const s_TableEntry gTransitionTable[8][17]` 

8 AppState(s): ignoring MAX
```
0 STARTING 
1 WAIT_FOR_CONNECTION
2 WAIT_FOR_IP
3 PROVISIONING_IN_PROGRESS 
4 PROVISIONING_WAIT_COMPLETE
5 PINGING_GW  
6 OTA_RUN
7 ERROR
8 MAX
```
17 AppEvent(s): ignoring MAX
```
0 NULL
1 STARTED 
2 CONNECTED
3 IP_ACQUIRED 
4 DISCONNECT
5 PROVISIONING_STARTED 
6 PROVISIONING_SUCCESS 
7 PROVISIONING_STOPPED 
8 PING_COMPLETE 
9 OTA_START
10 CONTINUE
11 OTA_CHECK_DONE
12 OTA_DOWNLOAD_DONE
13 OTA_ERROR
14 TIMEOUT 
15 ERROR 
16 RESTART
17 MAX
```
|St/Evt| 0    | 1    | 2    | 3    | 4    | 5    | 6    | 7    | 8    | 9    | 10   | 11   | 12   | 13  | 14  | 15  | 16  |
|------|------|------|------|------|------|------|------|------|------|------|------|------|------|-----|-----|-----|-----|
| 0    |TL->0 |HSC->1|SET->2|TL->0 |RM->0 |RE->7 |RE->7 |TL->0 |RE->7 |RE->7 |RE->7 |RE->7 |RE->7 |RE->7|RE->7|RE->7|RM->0|
| 1    |TL->1 |HSC->1|SET->2|RE->7 |PR->0 |PS->3 |RE->7 |SET->1|RE->7 |RE->7 |RE->7 |RE->7 |RE->7 |RE->7|PE->3|RE->7|RM->0|
| 2    |TL->2 |PE->3 |RE->7 |CLC->5|RM->0 |PE->3 |TL->2 |HPC->5|RE->7 |RE->7 |RE->7 |RE->7 |RE->7 |RE->7|RM->0|RE->7|RM->0|
| 3    |TL->3 |RE->7 |RE->7 |RE->7 |RM->0 |TL->3 |HPC->4|HPS->5|RE->7 |RE->7 |RE->7 |RE->7 |RE->7 |RE->7|RM->0|RE->7|RM->0|
| 4    |TL->4 |RE->7 |RE->7 |RE->7 |RM->0 |TL->4 |TL->4 |HPS->5|RE->7 |RE->7 |RE->7 |RE->7 |RE->7 |RE->7|RM->0|RE->7|RM->0|
| 5    |TL->5 |TL-->5|RE->7 |RE->7 |RM->0 |RE->7 |RE->7 |RE->7 |PC->5 |OI->6 |RE->7 |RE->7 |RE->7 |RE->7|RM->0|RE->7|RM->0|
| 6    |TL->6 |RE-->7|RE->7 |RE->7 |RM->0 |RE->7 |RE->7 |RE->7 |RE->7 |RE->7 |ORS->6|CLC->5|OIT->0|RE->7|PE->3|RE->7|RM->0|
| 7    |RE->7 |RE-->7|RE->7 |RE->7 |RE->7 |RE->7 |RE->7 |RE->7 |RE->7 |RE->7 |RE->7 |RE->7 |RE->7 |RE->7|RE->7|RE->7|RE->7|

16 event handlers (function pointers) of the form: 
```
typedef int32_t (*fptr_EventHandler)(void); 
``` 
For instance: 
```
int32_t HandleStartConnect(void);              // SC
int32_t CheckLanConnecttion(void);             // CLC
int32_t ProvisioningExample();                 // PE
int32_t ReportError(void);                     // RE
int32_t ToggleLED(void);                       // TL
int32_t CheckLanConnection(void);              // CLC
int32_t HandleProvisioningComplete(void);      // HPC
int32_t HandleProvisioningStop();              // HPS
int32_t OtaInit();                             // OI   <---
int32_t HandlePingComplete();                  // PC
int32_t ProcessRestartMcu();                   // RM   <---
int32_t OtaImageTestingAndReset();             // OIT  <---
int32_t OtaRunStep();                          // ORS  <---
static int32_t StartAsyncEvtTimer();           // SET
static int32_t StopAsyncEvtTimer();
static int32_t ProcessRestartRequest();
```

### [Combined](#combined){#combined}
------------------

8 AppState(s): ignoring MAX
```
0 STARTING 
1 WAITING_FOR_CONNECTION
2 WAIT_FOR_IP
3 PINGING_GW  
4 PROVISIONING_IN_PROGRESS 
5 PROVISIONING_WAIT_COMPLETE
6 OTA_RUN  <--- new state
7 ERROR
8 MAX
```
17 AppEvent(s): ignoring MAX
```
0 STARTED 
1 CONNECTED
2 IP_ACQUIRED 
3 DISCONNECT
4 PING_COMPLETE 
5 PROVISIONING_STARTED 
6 PROVISIONING_SUCCESS 
7 PROVISIONING_STOPPED 
8 PROVISIONING_WAIT_CONN
9 TIMEOUT 
10 ERROR 
11 RESTART
12 OTA_START <-- start new events 
13 CONTINUE
14 OTA_CHECK_DONE
15 OTA_DOWNLOAD_DONE
16 OTA_ERROR  <--- end new events
17 MAX
```
```
|St/Evt| 0    | 1    | 2    | 3    | 4    | 5    | 6    | 7    | 8    | 9    | 10   | 11   | 12N | 13N  | 14N  | 15N  | 16N |
|------|------|------|------|------|------|------|------|------|------|------|------|------|-----|------|------|------|-----|
| 0    |SC->1 |HWI->2|RE->7 |RE->7 |RE->7 |RE->7 |RE->7 |RE->7 |RE->7 |RE->7 |RE->7 |PS->4 |DN->0|DN->0 |DN->0 |DN->0 |DN->0|
| 1    |SC->1 |HWI->2|RE->7 |SC->1 |HPC->3|PS->4 |DN->1 |CLC->1|DN->1 |PS->4 |PS->4 |PRR->1|DN->1|DN->1 |DN->1 |DN->1 |DN->1|
| 2    |PS->4 |RE->7 |CLC->3|SC->1 |DN->2 |PS->4 |DN->2 |HPC->3|DN->2 |RE->7 |RE->7 |PRR->1|DN->2|DN->2 |DN->2 |DN->2 |DN->2|
| 3    |DN->3 |HWI->2|CIC->3|HD->1 |DN->3 |PS->4 |DN->3 |HUA->3|DN->3 |SPG->3|RE->7 |PRR->1|OI->6|DN->3 |DN->3 |DN->3 |DN->3|
| 4    |PSR->4|HC->4 |CIC->4|HD->4 |DN->4 |DN->4 |HPC->5|HPC->3|DN->1 |PRR->1|RE->7 |PRR->1|DN->4|DN->4 |DN->4 |DN->4 |DN->4|
| 5    |DN->5 |DN-->5|DN->5 |HD->5 |DN->5 |DN->5 |DN->5 |HUA->3|DN->1 |HPT->5|RE->7 |PRR->5|DN->5|DN->5 |DN->5 |DN->5 |DN->5|
| 6NEW |DN->6 |DN-->6|DN->6 |HD->1 |RE->7 |RE->7 |RE->7 |RE->7 |RE->7 |PE->3 |RE->7 |RM->1 |DN->6|ORS->6|CLC->3|OIT->0|RE->7|
| 7    |RE->1 |RE-->1|RE->1 |RE->1 |RE->1 |RE->1 |RE->1 |RE->1 |RE->1 |RE->1 |RE->1 |PS->1 |RE->1|RE->1 |RE->1 |RE->1 |RE->1|
```

20 event handlers (function pointers) of the form: 
```
typedef int32_t (*fptr_EventHandler)(void); 
```
```
int32_t StartConnection(void);                 // SC
int32_t HandleWaitForIp(void);                 // HWI
int32_t ProvisioningStart(void);               // PS
int32_t ProcessRestartRequest(void);           // PRR
int32_t ReportError(void);                     // RE
int32_t DoNothing(void);                       // DN
int32_t CheckLanConnection(void);              // CLC
int32_t CheckInternetConnection(void);         // CIC
int32_t HandleProvisioningComplete(void);      // HPC
int32_t HandleProvisioningStop();              // HPS
int32_t HandleProvisioningTimeout(void);       // HPT
int32_t HandleUserApplication(void);           // HUA
int32_t SendPingToGW(void);                    // SPG
int32_t HandleDiscnctEvt(void);                // HD 
int32_t OtaInit();                             // OI   <---
int32_t HandlePingComplete();                  // PC
int32_t ProcessRestartMcu();                   // RM   <---
int32_t OtaImageTestingAndReset();             // OIT  <---
int32_t OtaRunStep();                          // ORS  <---
int32_t StartAsyncEvtTimer();                  // SET
```

## minicom ##

minicom -b 115200 -8 -D /dev/ttyACM0

To run minicom as a non-root user, user has to be part of dialout group. 

Also for CC3200, user has to be added to plugdev group. 

## openocd

cd ~/ti/cc3200_sdk_1_3_0/CC3200SDK_1.3.0/cc3200-sdk/tools/gcc_scripts
openocd -f cc3200.cfg

Open On-Chip Debugger 0.9.0 (2015-09-02-10:42)
Licensed under GNU GPL v2
For bug reports, read
  http://openocd.org/doc/doxygen/bugs.html
adapter speed: 1000 kHz
Info : auto-selecting first available session transport "jtag". To override use 'transport select <transport>'.
cc3200_dbginit
Info : clock speed 1000 kHz
Info : JTAG tap: cc3200.jrc tap/device found: 0x0b97c02f (mfg: 0x017, part: 0xb97c, ver: 0x0)
Info : JTAG tap: cc3200.dap enabled
Info : cc3200.cpu: hardware has 6 breakpoints, 4 watchpoints

## cc3200

cc3200_sdk_1_3_0/CC3200SDK_1.3.0/tools/gcc_scripts/makedefs_ti_rtos
cc3200_sdk_1_3_0/CC3200SDK_1.3.0/ti_rtos/ti_rtos_config/gcc/configPkg/compiler.opt
cc3200_sdk_1_3_0/CC3200SDK_1.3.0/oslib/gcc/Makefile_oslib_tirtos

malloc (nbytes=76) at ../../../../../../newlib/libc/stdlib/malloc.c:215
215 ../../../../../../newlib/libc/stdlib/malloc.c: No such file or directory.

## Uniflash

+ [Product page](http://www.ti.com/tool/Uniflash)
+ [Download](http://processors.wiki.ti.com/index.php/Category:CCS_UniFlash)

Install as root:

```
sudo ./uniflash_sl.4.6.0.2176.run // install to /opt/ti
```

Start Uniflash:

```
cd /opt/ti/uniflash_4.6.0
sudo ./node-webkit/nw
```

### Uniflash Gotchas


In client mode (`TCPClient` in `socket_cmd.c`) if verifying server certificate like so:
```
sl_SetSockOpt(sock,SL_SOL_SOCKET,SL_SO_SECURE_FILES_CA_FILE_NAME, ROOT_CA_CERT_FILE, strlen(ROOT_CA_CERT_FILE));
```
then may run into the following error: 688: `SL_ERROR_BSD_ESEC_ASN_NO_SIGNER_E`

To work around, remove the server cert verification and receive this error (warning) instead: 453: `SL_ERROR_BSD_ESECSNOVERIFY`

Timestamp issue with CC32XX: the following code doesn't work on CC32XX because of missing time.h implementations.
```
time_t rawtime;
struct tm * timeinfo;
time(&rawtime); // time(NULL);
rawtime = mktime(timeinfo);
```

### [Networking](#networking){#networking}

+ source/ti/driver/net/wifi/errors.h

##TIRTOS

~/ti/tirtos_simplelink_2_13_01_09

Corresponding xdctools installed at:
~/ti/xdctools_3_31_01_33_core

This can be found by checking the install log which strangely has Windows paths:
./tirtossimplelink_2_13_01_09_install.log:Unpacking C:\ti\xdctools_3_31_01_33_core\jre\bin\dtplugin\deployJava1.dll
 
TIRTOS can be rebuit after changing Windows paths to Linux paths in tirtos.mak:
../xdctools_3_31_01_33_core/gmake -f tirtos.mak all

Also can choose compiler/linker options (CCS, GCC or IAR) in tirtos.mak

## Demos

wlanconnect -s "M0V" -t WPA/WPA2 -p "53606808"
send -c 192.168.1.100 -n 1

## OOB

provisioningTask
linkLocalTask: 
controlTask: handles button presses etc.
otaTask

## Drivers

### GPIO

### PWM

System clock 80MHz
Timer tick = 1/80Mhz = 12.5ns
16 bit registers + 8 bit prescalar
Maximum tick = 2^24-1 = 16777215 
Max time = (2^24-1) x 12.5ns = 0.209715s

### I2C

Standard bus frequencies are 100/400KHz

## BIOS Kernel


## [Power](#power){#power}

The board can be powered from an external power supply (i.e. not USB) using the instructions in section 2.5.2 in SWRU463.pdf (Simplelink CC3220 Wi-Fi LaunchPad Development Kit Hardware). 

1. Remove the USB cable.
2. Plug in the battery pack on J20 with the correct polarity.
3. Connect the jumper across J12 and J13.

The UART can still be used via USB to monitor output from the board even though the USB is not providing the power to the board. 

## [Layout](#layout){#layout}

The following GPIO ports were left unconnected due to space constraints on board:
```
P05_GPIO_14
P06_GPIO_15
P07_GPIO_16
P08_GPIO_17
P18_GPIO_28
P45_GPIO_31 / DCDC_ANA2_SW_P - not connected on dev board either
DCDC_ANA2_SW_N (P46) 
```

CC3220S/F <-> CC3220MODAS
```
Pin55 GPIO1 UART0_TX <-> Pin46
Pin57 GPIO2 UART0_RX <-> Pin47

Pin07 GPIO16 UART1_TX <-> Pin07
Pin08 GPIO17 UART1_RX <-> Pin08
```
~/ti/ccsv8/simplelink_cc32xx_sdk_3_10_00_04/source/ti/boards/CC3220S_LAUNCHXL/CC3220S_LAUNCHXL.c
.rxPin = UARTCC32XX_PIN_08_UART1_RX --> .rxPin = UARTCC32XX_PIN_45_UART1_RX

## [Gotchas](#gotchas){#gotchas}

Operation failed: fs_programming error: ret: -10289, ex_err: 2633 - FS_WRONG_SIGNATURE
Operation failed: fs_programming error: ret: -10287, ex_err: 2633 - FS_ERR_ROOT_CA_IS_UNKOWN

`SL_ERROR_BSD_ESEC_ASN_NO_SIGNER_E` caused due to incorrect root ca cert on server. 

Ensure thread stack size is sufficient for NWP. 
```
#define THREADSTACKSIZE    4096
```
-708
-111

## [Todo](#todo){#todo}

+ Intermittently errors and LED2 (10) stays on - fixed in 5e8e53d3

```
int32_t SendPingToGW(void) {
    ...
    ret = TCPClient(...);
    if (ret != 0) {
        // ocassionally arrives here, i.e. LED2 doesn't go off and stays on and errors with either -708 or -111
    }
    else {
        GPIO_write(Board_GPIO_LED2, Board_GPIO_LED_OFF);
    }

}
```

## [Manual](#manual){#manual}

Provisioning LED sequence on custom CC3220MODASF:

Red LED (D10) flashing: device in Access Point mode
Red LED (D10) solid: device in Station mode and connected to another AP
Yellow LED (D9) flashes briefly when device changes from Access Point to Station. Solid D9 indicates device needs a restart. 
Green LED (D8) flashes briefly when data is sent to the server

12 pins need to be connected between a CC3220S-LAUNCHXL/LAUNCHCC3220MODAS and the custom CC3220MODASF for flashing with Uniflash: VBAT, BRD, GND, TX, RX, RST, TMS, TCLK, TDO, TDI, VS/BRD and SOP1. 
VBAT & VBRD need to be jumped in production on the custom CC3220MODASF


## References ## 
+ [](https://www.embedded.com/electronics-blogs/say-what-/4441829/2/Your-very-own-IoT--Digging-deep-on-sensors)
+ [Simplelink SDK](http://www.ti.com/tool/SIMPLELINK-CC32XX-SDK) 
+ [Simplelink SDK Download](http://www.ti.com/tool/download/SIMPLELINK-CC32XX-SDK)
+ [CC3220S Description](https://www.ti.com/product/CC3220S/description)
+ [CC3220S Software](https://www.ti.com/product/CC3220S/toolssoftware)
+ [SYS/BIOS Downloads](http://software-dl.ti.com/dsps/dsps_public_sw/sdo_sb/targetcontent/bios/sysbios/index.html)
+ [TI-RTOS](http://software-dl.ti.com/dsps/dsps_public_sw/sdo_sb/targetcontent/tirtos/index.html)
+ [CC3x20 Simplelink Wi-Fi IoC Solution Device Provisioning (Rev. A)](swra513a.pdf)

## [Useful](#useful){#useful}
+ [tls certs](https://gist.github.com/laher/5795578)
+ [sntp](https://e2e.ti.com/support/wireless-connectivity/wifi/f/968/t/829406?tisearch=e2e-quicksearch&keymatch=sntp)

dummy-root-ca-cert
dummy-trusted-ca-cert
dummy-trusted-cert

binary signed with private key dummy-trusted-cert-key and cert file dummy-trusted-cert

Replace with

dst rootca ca x3
let's encrypt authority x3
b00m-trusted-ca-cert
b00m-trusted-cert

In `Trusted Root-Certificate Catalog` replace 

```
certificate-playground/certcatalogPlayGround20160911.lst
certificate-playground/certcatalogPlayGround20160911.lst.signed_3220.bin

certificate-catalog/certcatalog20190217.lst
certificate-catalog/certcatalog20190217.lst.signed_3220.bin

```
Sign the mcuimg.bin with b00m-trusted-cert-key / b00m-trusted-cert


turns out, the name of the root ca and trusted ca certs *on the simplelink device* need to have the file name match the certificate name (all lowercase and spaces are ok). to find the name of your cert, use
openssl x509 -text -noout -in name_of_cert

and look for CN in the subject field.

when adding the mcu image, the -cert option points to the simplelink filename.

So, if you upload your code signing cert with SL_filename 'my cool cert', then the --cert option would look like
--cert "my cool cert"



To resolve an error like the following:

Error connecting to the target:
(Error -1170 @ 0x0)
Unable to access the DAP. Reset the device, and retry the operation. If error persists, confirm configuration, power-cycle the board, and/or try more reliable JTAG settings (e.g. lower TCLK).
(Emulation package 8.4.0.00006)

From `https://dev.ti.com/tirex/explore/node?node=ABEoqU9o3snoxDcmIpW0EA__fc2e6sr__LATEST`

To enable flash programming on the CC32xx LaunchPad, the Sense-On-Power (SOP) jumpers on the board need to be configured correctly. The CC32xx device implements a SOP scheme to determine the device operation mode upon power up. Ensure the SOP jumpers are set to 010, as shown in the image below. 
