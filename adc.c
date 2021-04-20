
/* POSIX Header files */
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <time.h>

#include <ti/drivers/ADC.h>
#include "uart_term.h"
#include "modbus.h"
#include <lightmodbus/lightmodbus.h>
#include <lightmodbus/master.h>

ModbusMaster mstatus;
uint8_t lc; // counter to loop through all the meter's input registers
/* Voltage written by the temperature thread and read by console thread */
volatile float voltage;
volatile float latestMeterReads[13];

/* Mutex to protect the reading/writing of the temperature variables */
extern pthread_mutex_t voltageMutex;

/* ADC sample count */
#define ADC_SAMPLE_COUNT  (1000)
uint16_t adcValue1[ADC_SAMPLE_COUNT];
uint32_t adcValue1MicroVolt[ADC_SAMPLE_COUNT];
uint16_t adcValue59[ADC_SAMPLE_COUNT];
uint32_t adcValue59MicroVolt[ADC_SAMPLE_COUNT];
/*
 *  ======== postSem ========
 *  Function called when the timer (created in setupTimer) expires.
 */
static void postSem(union sigval val)
{
    sem_t *sem = (sem_t*)(val.sival_ptr);

    sem_post(sem);
}

/*
 *  ======== setupTimer ========
 *  Create a timer that will expire at the period specified by the
 *  time arguments. When the timer expires, the passed in semaphore
 *  will be posted by the postSem function.
 *
 *  A non-zero return indicates a failure.
 */
int setupTimer(sem_t *sem, timer_t *timerid, time_t sec, long nsec)
{
    struct sigevent   sev;
    struct itimerspec its;
    int               retc;

    retc = sem_init(sem, 0, 0);
    if (retc != 0) {
        return(retc);
    }

    /* Create the timer that wakes up the thread that will pend on the sem. */
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_value.sival_ptr = sem;
    sev.sigev_notify_function = &postSem;
    sev.sigev_notify_attributes = NULL;
    retc = timer_create(CLOCK_MONOTONIC, &sev, timerid);
    if (retc != 0) {
        return(retc);
    }

    /* Set the timer to go off at the specified period */
    its.it_interval.tv_sec = sec;
    its.it_interval.tv_nsec = nsec;
    its.it_value.tv_sec = sec;
    its.it_value.tv_nsec = nsec;
    retc = timer_settime(*timerid, 0, &its, NULL);
    if (retc != 0) {
        timer_delete(*timerid);
        return(retc);
    }

    return(0);
}

void *adcThread(void *arg0)
{
    int             retc;
    sem_t           semTimer;
    timer_t         timerid;

    uint16_t     i;
    ADC_Handle   adc;
    ADC_Handle   adc59;
    ADC_Params   params;
    int_fast16_t res;
    int_fast16_t res59;
    int32_t             status = 0;

    ADC_Params_init(&params);
    adc = ADC_open(Board_ADC1, &params);
    adc59 = ADC_open(Board_ADC0, &params);

    if (adc == NULL) {
        //UART_PRINT("\rError initializing ADC1\n");
        while (1);
    }
	modbusMasterInit( &mstatus );

    /*
     *  The adc thread blocks on the semTimer semaphore, which the
     *  timerId timer will post every second. The timer is created in the
     *  setupTimer function. It's returned so the thread could change the
     *  period or delete it if desired.
     */
    retc = setupTimer(&semTimer, &timerid, 10, 0);
    if (retc != 0) {
        while (1);
    }
    float average = 0.0;
    float average59 = 0.0;
    //uint8_t wb[] = {0x7a, 0x04, 0x00, 0x00, 0x00, 0x02, 0x7B, 0x80};
    //uint8_t wb[] = {0x11, 0x01, 0x01, 0x30, 0x00, 0x01, 0xFE, 0xA9};
    uint8_t wb[] = {0x7A, 0x04, 0x00, 0x00, 0x00, 0x02, 0x7B, 0x80};
    //uint8_t wbpwr[] = {0x7A, 0x04, 0x00, 0x0C, 0x00, 0x02, 0xBB, 0x83};
    uint8_t rb[] = {0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77, 0x77};
    while (1) {
        for (i = 0; i < ADC_SAMPLE_COUNT; i++) {
            res = ADC_convert(adc, &adcValue1[i]);
            res59 = ADC_convert(adc59, &adcValue59[i]);

            if (res == ADC_STATUS_SUCCESS) {
                adcValue1MicroVolt[i] = ADC_convertRawToMicroVolts(adc, adcValue1[i]);
                /*if (i % 100 == 0) {
                    UART_PRINT("ADC1 raw result (%d): %d\n", 0, adcValue1[i]);
                    UART_PRINT("\r ADC1 convert result (%d): %d uV\n", 0, adcValue1MicroVolt[i]);
                    UART_PRINT("\r ADC1 average (%.3f) uV\n", 0, average);
                }*/
            }
            else {
                //UART_PRINT("\r ADC1 convert failed (%d)\n", i);
            }
            average += adcValue1MicroVolt[i];

            if (res59 == ADC_STATUS_SUCCESS) {
                adcValue59MicroVolt[i] = ADC_convertRawToMicroVolts(adc59, adcValue59[i]);

                //UART_PRINT("ADC1 raw result (%d): %d\n", 0, adcValue1[i]);
                //UART_PRINT("\r ADC1 convert result (%d): %d uV\n", 0, adcValue1MicroVolt[i]);
            }
            else {
                UART_PRINT("\r ADC1 convert failed (%d)\n", i);
            }
            average59 += adcValue59MicroVolt[i];
        }

        average /= ADC_SAMPLE_COUNT;
        average59 /= ADC_SAMPLE_COUNT;
        //UART_PRINT("\r [ADC60] Sem timer posted %.2f \n", average);
        //UART_PRINT("\r [ADC59] Sem timer posted %d \n", average59);
        pthread_mutex_lock(&voltageMutex);
        voltage  = average59;
        pthread_mutex_unlock(&voltageMutex);
        average = 0.; //average59 = 0.;

        /* Block until the timer posts the semaphore. */
        retc = sem_wait(&semTimer);
        if (retc == -1) {
            while (1);
        }
	modbusBuildRequest0304( &mstatus, 4, 122, inputRegs[lc], 2 );
        //WriteBytes(mstatus.request.frame, 8);
        // EN up to enable TX
        GPIO_write(CC3220S_LAUNCHXL_GPIO_07, Board_GPIO_LED_ON);
        //WriteBytes1(&wb, 8); // blocks until all 8 bytes are written to uart1
        WriteBytes(mstatus.request.frame, 8); // blocks until all 8 bytes are written to uart1
        GPIO_write(CC3220S_LAUNCHXL_GPIO_07, Board_GPIO_LED_OFF); // EN down to disable TX / enable RX
        ReadBytes(&rb, 9);  // blocks for readTimeout seconds while reading from uart1
        //WriteBytes(&rb, 9);  // write the read bytes to uart0
        float vf;
        ieee754(&rb[3], &vf); // skip the first 3 bytes and convert the next 4 to float
        //UART_PRINT("\r %s: %.3f \n", parameters[lc], vf);
        if (vf > 1000000000.00 || vf < -10000000000.00 ) { //iee754 returns floats like 5.e+33 if rb doesn't have a valid meter reading in it, for instance when ReadBytes times out for whatever reason
            latestMeterReads[lc] = 987.65; // this is a dummy reading value - document it somewhere in a manual!
        } else {
            latestMeterReads[lc] = vf;
        }
        lc++;
        if (lc > 12) {
            lc = 0;
        }
    }

    ADC_close(adc);
    return (NULL);
}

// ieee754 converts the provided array of 4 bytes into a float pointed at by the second argument. 
// ieee754 first combines the 4 bytes into a uint32_t and then type casts into a float 
// saving the result at the destination of the float pointer provided as argument
void ieee754(uint8_t* bytes, float* f) {
    uint8_t i;
    uint32_t num = 0;
    uint32_t temp = 0;
    for (i = 0; i < 4; i++) {
        temp = bytes[i] << (24-8*i);
        num |= temp;
    }
    *f = *((float*)&num);
}
