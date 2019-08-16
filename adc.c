
/* POSIX Header files */
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <time.h>

#include <ti/drivers/ADC.h>
#include "uart_term.h"

/* Voltage written by the temperature thread and read by console thread */
volatile float voltage;

/* Mutex to protect the reading/writing of the temperature variables */
extern pthread_mutex_t voltageMutex;

/* ADC sample count */
#define ADC_SAMPLE_COUNT  (100)
uint16_t adcValue1[ADC_SAMPLE_COUNT];
uint32_t adcValue1MicroVolt[ADC_SAMPLE_COUNT];
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
    ADC_Params   params;
    int_fast16_t res;
    int32_t             status = 0;

    ADC_Params_init(&params);
    adc = ADC_open(Board_ADC1, &params);

    if (adc == NULL) {
        UART_PRINT("\rError initializing ADC1\n");
        while (1);
    }

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
    while (1) {
        for (i = 0; i < ADC_SAMPLE_COUNT; i++) {
            res = ADC_convert(adc, &adcValue1[i]);

            if (res == ADC_STATUS_SUCCESS) {
                adcValue1MicroVolt[i] = ADC_convertRawToMicroVolts(adc, adcValue1[i]);

                /*UART_PRINT("ADC1 raw result (%d): %d\n", 0, adcValue1[i]);
                UART_PRINT("\r ADC1 convert result (%d): %d uV\n", 0, adcValue1MicroVolt[i]);*/
            }
            else {
                UART_PRINT("\r ADC1 convert failed (%d)\n", i);
            }
            average += adcValue1MicroVolt[i];
        }

        average /= ADC_SAMPLE_COUNT;
        pthread_mutex_lock(&voltageMutex);
        voltage  = average;
        pthread_mutex_unlock(&voltageMutex);

        /* Block until the timer posts the semaphore. */
        retc = sem_wait(&semTimer);
        if (retc == -1) {
            while (1);
        }
        UART_PRINT("\rSem timer posted %d \n", voltage);
    }

    ADC_close(adc);
    return (NULL);
}
