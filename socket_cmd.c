/* Standard includes */
#include <stdlib.h>
#include <ti/display/Display.h>
#include <time.h>
#include <pthread.h>

#include "socket_cmd.h"
//#include "common.h"
//#include "empty.h"
#include "cJSON.h"

#define SECURE_SOCKET
//#define CLIENT_AUTHENTICATION

#ifdef SECURE_SOCKET
#define TCP_PROTOCOL_FLAGS    SL_SEC_SOCKET
#define ROOT_CA_CERT_FILE     "dummy-root-ca-cert" //"dst root ca x3" //
#define PRIVATE_KEY_FILE      "dummy-trusted-cert-key" //"b00m-trusted-cert-key" //
#define TRUSTED_CERT_FILE     "dummy-trusted-cert" //"b00m-trusted-cert" //
#define TRUSTED_CERT_CHAIN    "trusted-chain.pem"

#define DEVICE_YEAR                 (2018)
#define DEVICE_MONTH                (3)
#define DEVICE_DATE                 (30)

#define BUF_LEN                (MAX_BUF_SIZE - 20)
#else
#define TCP_PROTOCOL_FLAGS      0
#define BUF_LEN                (MAX_BUF_SIZE)
#endif

//extern volatile float voltage;
//extern pthread_mutex_t voltageMutex;


int32_t TCPClient(uint8_t nb,
                  uint16_t portNumber,
                  ip_t ipAddress,
                  uint8_t ipv6,
                  uint32_t numberOfPackets,
                  uint8_t tx);

int32_t TCPClientTest(uint8_t nb,
                  uint16_t portNumber,
                  ip_t ipAddress,
                  uint8_t ipv6,
                  uint32_t numberOfPackets,
                  uint8_t tx);

int hashf(unsigned char *str); 

/*!
    \brief          TCP Client.

    This routine shows how to set up a simple TCP client.
    It shows sending and receiving packets as well.

    \param          nb              -   Sets the socket type: blocking or
										non-blocking.

    \param          portNumber      -   Decides which port is affiliated
										with the server's socket.

    \param          ipv6            -   Sets the version of the L3 IP
										protocol, IPv4 or IPv6.

    \param          numberOfPackets -   Sets the Number of packets
										to send \ receive.

    \param          tx              -   Decides if the function would
										transmit data. If this flag
                                        is set to false, 
										this function would receive.

    \return         Upon successful completion, the function shall return 0.
                    In case of failure, this function would return -1;

    \sa             cmdSendCallback, cmdRecvCallback

 */
int32_t TCPClient(uint8_t nb,
                  uint16_t portNumber,
                  ip_t ipAddress,
                  uint8_t ipv6,
                  uint32_t numberOfPackets,
                  uint8_t tx)
{
    int32_t sock;
    int32_t status;
    uint32_t i = 0;
    int32_t nonBlocking;
    SlSockAddr_t        *sa;
    int32_t addrSize;
    sockAddr_t sAddr;
    
    UART_PRINT("\r [TCPClient] Starting send \n");

    /* clear the global data buffer */
    //memset(app_CB.gDataBuffer.nwData, 0x0, MAX_BUF_SIZE);
    memset(&app_CB.gDataBuffer.nwData, 0x0 , sizeof(app_CB.gDataBuffer));

    /* filling the buffer with data */
    for(i = 0; i < MAX_BUF_SIZE; i++)
    {
        app_CB.gDataBuffer.nwData[i] = (char)(i % 10);
    }

    /* for response from server*/
    uint8_t recd_data[MAX_BUF_SIZE];

    /* prepare json packet*/
    cJSON *loco = NULL;
    struct timespec abstime;
    abstime.tv_sec = 0;
    abstime.tv_nsec = 0;
    clock_gettime(CLOCK_REALTIME, &abstime);
    
    uint8_t nameLen = SL_NETAPP_MAX_DEVICE_URN_LEN;
    int32_t ret;
    free(myDeviceName);
    myDeviceName = malloc(SL_NETAPP_MAX_DEVICE_URN_LEN);
    ret = sl_NetAppGet (SL_NETAPP_DEVICE_ID, SL_NETAPP_DEVICE_URN, &nameLen, (_u8 *)myDeviceName);
    ASSERT_ON_ERROR1(ret, NETAPP_ERROR);
    UART_PRINT("\r [TCPClient] Devicename: %s - %d - %d \n", myDeviceName, sizeof(myDeviceName), strlen(myDeviceName));
    //free(deviceName);
    //deviceName = malloc(strlen(my_device_name));
    //strcpy(deviceName, my_device_name);
    UART_PRINT("\r 1:%s 2:%s \r\n", myDeviceName, ssidName);
    //free(my_device_name);
    
    // calculate hash of 1st 4 of myDeviceName and 1st 3 of SSID
    char dn[5];
    char ss[4];
    _u8 *ch = malloc(9);

    strncpy(dn, myDeviceName, sizeof(dn));
    dn[4] = '\0';
    UART_PRINT("\r dn : %s \n", dn);
    strncpy(ss, ssidName, sizeof(ss));
    ss[3] = '\0';
    UART_PRINT("\r ss: %s \n", ss);
    strncpy(ch, dn, 5);
    strncat(ch, ss, 4);
    ch[7] = '\0';
    ch[8] = '\0';
    UART_PRINT("\r ch:%s \n", ch);
    int hashi;
    hashi = abs(hashf(ch));
    UART_PRINT("\r [TCPClient] unhashed: %s size: %d hashed: %d \n", ch, strlen(ch), hashi);
    loco = cJSON_CreateObject();
    switch(portNumber)
    {
        case 38979:
            cJSON_AddNumberToObject(loco, "timestamp", abstime.tv_sec);
            cJSON_AddStringToObject(loco, "devicename", myDeviceName);//"Movprov");
            cJSON_AddStringToObject(loco, "ssid", ssidName);
            cJSON_AddNumberToObject(loco, "hash", hashi);
            break;
        case 38981:
            //cJSON_AddNumberToObject(loco, "id", 43672934);
            cJSON_AddNumberToObject(loco, "id", hashi);
            cJSON_AddNumberToObject(loco, "timestamp", abstime.tv_sec);
            cJSON_AddBoolToObject(loco, "status", true);
            //pthread_mutex_lock(&voltageMutex);
            //cJSON_AddNumberToObject(loco, "voltage", voltage);
            //pthread_mutex_unlock(&voltageMutex);
            cJSON_AddNumberToObject(loco, "freq", 50.3);
            cJSON_AddNumberToObject(loco, "lat", 13.4538);
            cJSON_AddNumberToObject(loco, "lng", 77.6283);
            break;
        default:
            UART_PRINT("\r Unexpected port number %d \r\n", portNumber);
            break;
    }
    free(ch);

    char *out = NULL;
    out = cJSON_Print(loco);
    char *buf = NULL;
    size_t len = 0;
    len = strlen(out) + 5;
    buf = (char*)malloc(len);
    if (buf == NULL)
    {
        UART_PRINT("Failed to allocate memory.\n");
        return -1;
    }
    /* Print to buffer */
    if (!cJSON_PrintPreallocated(loco, buf, (int)len, 1)) {
        UART_PRINT("cJSON_PrintPreallocated failed!\n");
        if (strcmp(out, buf) != 0) {
            UART_PRINT("cJSON_PrintPreallocated not the same as cJSON_Print!\n");
            UART_PRINT("cJSON_Print result:\n%s\n", out);
            UART_PRINT("cJSON_PrintPreallocated result:\n%s\n", buf);
        }
        free(out);
        free(buf);
        return -1;
    }
    /* end prepare json packet*/

    if(ipv6)
    {
        sAddr.in6.sin6_family = SL_AF_INET6;
        sAddr.in6.sin6_port = sl_Htons(portNumber);
        sAddr.in6.sin6_flowinfo = 0;

        sAddr.in6.sin6_addr._S6_un._S6_u32[0] =
            ((unsigned long*)ipAddress.ipv6)[0];
        sAddr.in6.sin6_addr._S6_un._S6_u32[1] =
            ((unsigned long*)ipAddress.ipv6)[1];
        sAddr.in6.sin6_addr._S6_un._S6_u32[2] =
            ((unsigned long*)ipAddress.ipv6)[2];
        sAddr.in6.sin6_addr._S6_un._S6_u32[3] =
            ((unsigned long*)ipAddress.ipv6)[3];
        sa = (SlSockAddr_t*)&sAddr.in6;
        addrSize = sizeof(SlSockAddrIn6_t);
    }
    else
    {
        /* filling the TCP server socket address */
        sAddr.in4.sin_family = SL_AF_INET;

        /* Since this is the client's side, 
		 * we must know beforehand the IP address
         * and the port of the server wer'e trying to connect.
         */
        sAddr.in4.sin_port = sl_Htons((unsigned short)portNumber);
        sAddr.in4.sin_addr.s_addr = sl_Htonl((unsigned int)ipAddress.ipv4);

        sa = (SlSockAddr_t*)&sAddr.in4;
        addrSize = sizeof(SlSockAddrIn6_t);
    }

    if (sa == NULL) {
        UART_PRINT("\r [TCPClient] sa NULL****\n");
    }
    else {
        UART_PRINT("\r [TCPClient] sa !NULL  %d \n", sa->sa_family);
    }
    PrintIPAddress(FALSE, (void*)&ipAddress.ipv4);
    UART_PRINT("\r [TCPClient} Setting up socket ****\n");
    /* Get socket descriptor - this would be the
     * socket descriptor for the TCP session.
     */
    sock = sl_Socket(sa->sa_family, SL_SOCK_STREAM, TCP_PROTOCOL_FLAGS);
    //ASSERT_ON_ERROR1(sock, SL_SOCKET_ERROR);
    ASSERT_ON_ERROR(sock);
    UART_PRINT("\r [TCPClient] ENTER SSL****\n");

#ifdef SECURE_SOCKET

    SlDateTime_t dateTime;
    dateTime.tm_day = DEVICE_DATE;
    dateTime.tm_mon = DEVICE_MONTH;
    dateTime.tm_year = DEVICE_YEAR;

    sl_DeviceSet(SL_DEVICE_GENERAL, SL_DEVICE_GENERAL_DATE_TIME, sizeof(SlDateTime_t), (uint8_t *)(&dateTime));

    UART_PRINT("\r [TCPClient] *** SSL ****\n");
    /* Set the following to enable Server Authentication */
    //sl_SetSockOpt(sock,SL_SOL_SOCKET,SL_SO_SECURE_FILES_CA_FILE_NAME, ROOT_CA_CERT_FILE, strlen(ROOT_CA_CERT_FILE));
    // Following mask doesn't seem to make a difference
    
    SlSockSecureMask_t mask;
    mask.SecureMask = SL_SEC_MASK_TLS_RSA_WITH_AES_256_CBC_SHA | SL_SEC_MASK_TLS_RSA_WITH_AES_256_CBC_SHA;
    sl_SetSockOpt(sock,SL_SOL_SOCKET,SL_SO_SECURE_MASK,&mask,sizeof(SlSockSecureMask_t));

#ifdef CLIENT_AUTHENTICATION
    /* Set the following to pass Client Authentication */
    sl_SetSockOpt(sock,SL_SOL_SOCKET,SL_SO_SECURE_FILES_PRIVATE_KEY_FILE_NAME,
                  PRIVATE_KEY_FILE, strlen(
                      PRIVATE_KEY_FILE));
    sl_SetSockOpt(sock,SL_SOL_SOCKET,SL_SO_SECURE_FILES_CERTIFICATE_FILE_NAME,
                  TRUSTED_CERT_CHAIN, strlen(
                      TRUSTED_CERT_CHAIN));
#endif
#endif

    /* Set socket as non-blocking socket (if needed):
     * Non-blocking sockets allows user to handle 
	 * other tasks rather than block
     * on socket API calls. 
	 * If an API call using the Non-blocking socket descriptor
     * returns 'SL_ERROR_BSD_EAGAIN' - 
	 * this indicate that the user should try the API again later.
     */
    if(TRUE == nb)
    {
        UART_PRINT("\r [TCPClient] nonblocking****\n");
        nonBlocking = TRUE;
        status =
            sl_SetSockOpt(sock, SL_SOL_SOCKET, SL_SO_NONBLOCKING, &nonBlocking,
                          sizeof(nonBlocking));

        if(status < 0)
        {
            UART_PRINT("\r [line:%d, error:%d] %s \n", __LINE__, status,
                       SL_SOCKET_ERROR);
            sl_Close(sock);
            return(-1);
        }
    }

    status = -1;

    while(status < 0)
    {
        UART_PRINT("\r ***sl_Connect**** \n");
        /* Calling 'sl_Connect' followed by server's
         * 'sl_Accept' would start session with
         * the TCP server. */
        status = sl_Connect(sock, sa, addrSize);
        if((status == SL_ERROR_BSD_EALREADY)&& (TRUE == nb))
        {
            UART_PRINT("\r ***SL_ERROR_BSD_EALREADY**** \n");
            sleep(1);
            continue;
        }
        else if(status < 0)
        {
            UART_PRINT("\r [line:%d, error:%d] %s \r\n", __LINE__, status,
                       SL_SOCKET_ERROR);
            /*if ((int)status == -453) {
                continue; // ignore SL_ERROR_BSD_ESECSNOVERIFY error because we're not verifying server
            } else {
                sl_Close(sock);
                return(-1);
            }*/
        }
        break;
    }

    i = 0;

    if(tx)
    {
        int32_t buflen;
        uint32_t sent_bytes = 0;
        uint32_t bytes_to_send = (numberOfPackets * len);

        while(sent_bytes == 0)
        {
            if(bytes_to_send - sent_bytes >= BUF_LEN)
            {
                buflen = BUF_LEN;
            }
            else
            {
                buflen = bytes_to_send - sent_bytes;
            }

            /* Send packets to the server */
            UART_PRINT("\r Sending packet: %s \r\n", *buf);
            status = sl_Send(sock, buf, len, 0);
            if((status == SL_ERROR_BSD_EAGAIN) && (TRUE == nb))
            {
                sleep(1);
                continue;
            }
            else if(status < 0)
            {
                UART_PRINT("\r [line:%d, error:%d] %s \r\n", __LINE__, status,
                           SL_SOCKET_ERROR);
                sl_Close(sock);
                return(-1);
            }
            i++;
            sent_bytes += status;
        }

        UART_PRINT("\r Sent %u packets (%u bytes) successfully \r\n",
                   i,
                   sent_bytes);
    }
    else
    {
        uint32_t rcvd_bytes = 0;

        while(rcvd_bytes < (numberOfPackets * BUF_LEN))
        {
            status = sl_Recv(sock, &app_CB.gDataBuffer.nwData, MAX_BUF_SIZE, 0);
            if((status == SL_ERROR_BSD_EAGAIN) && (TRUE == nb))
            {
                sleep(1);
                continue;
            }
            else if(status < 0)
            {
                UART_PRINT("\r [line:%d, error:%d] %s\n\r", __LINE__, status, BSD_SOCKET_ERROR);
                sl_Close(sock);
                return(-1);
            }
            else if(status == 0)
            {
                UART_PRINT("TCP Server closed the connection\n\r");
                break;
            }
            rcvd_bytes += status;
        }

        UART_PRINT("Received %u packets (%u bytes) successfully\n\r", (rcvd_bytes / BUF_LEN), rcvd_bytes);
    }

    if(tx) //receive thank you response
    {
        uint32_t rcvd_bytes = 0;

        while(rcvd_bytes == 0 )
        {
            status = sl_Recv(sock, &recd_data, MAX_BUF_SIZE, 0);
            if((status == SL_ERROR_BSD_EAGAIN) && (TRUE == nb))
            {
                sleep(1);
                continue;
            }
            else if(status < 0)
            {
                UART_PRINT("\r [line:%d, error:%d] %s\n\r", __LINE__, status, BSD_SOCKET_ERROR);
                sl_Close(sock);
                return -1;
            }
            else if (status == 0)
            {
                UART_PRINT("\r TCP Server closed the connection\n\r");
                break;
            }
            rcvd_bytes += status;
        }

        UART_PRINT("\r Received %u packets (%u bytes) successfully\n\r",(rcvd_bytes/BUF_LEN), rcvd_bytes);
        uint8_t j = 0;
        for(j = 0; j < rcvd_bytes; ++j) {
            UART_PRINT("%c", recd_data[j]);
        }
        UART_PRINT("\n");
    }
    /* Calling 'close' with the socket descriptor,
     * once operation is finished. */
    status = sl_Close(sock);
    ASSERT_ON_ERROR1(status, SL_SOCKET_ERROR);

    free(out);
    free(buf);
    cJSON_Delete(loco);
    return(0);
}

int32_t TCPClientTest(uint8_t nb,
                  uint16_t portNumber,
                  ip_t ipAddress,
                  uint8_t ipv6,
                  uint32_t numberOfPackets,
                  uint8_t tx)
{

    UART_PRINT("[line:%d, error:%d] %d\n\r", __LINE__, nb, portNumber);
    //Display_printf(display, 0, 0, "Starting send %d %d %d %d %d\n", nb, portNumber, ipv6, numberOfPackets, tx );
    return(0);
}

/*!
    \brief          Prints IP address.

    This routine prints IP addresses in a dotted decimal
    notation (IPv4) or colon : notation (IPv6)

    \param          ip         -   Points to command line buffer.

    \param          ipv6       -   Flag that sets 
                                   if the address is IPv4 or IPv6.

    \return         void

 */
void PrintIPAddress(unsigned char ipv6,
                    void *ip)
{
    uint32_t        *pIPv4;
    uint8_t         *pIPv6;
    int32_t          i=0;

    if(!ip)
    {
        return;
    }

    if(ipv6)
    {
        pIPv6 = (uint8_t*) ip;

        for(i = 0; i < 14; i+=2)
        {
            UART_PRINT("%02x%02x:", pIPv6[i], pIPv6[i+1]);
        }

        UART_PRINT("%02x%02x", pIPv6[i], pIPv6[i+1]);
    }
    else
    {
        pIPv4 = (uint32_t*)ip;
        UART_PRINT("\r %d.%d.%d.%d", 
                    SL_IPV4_BYTE(*pIPv4,3), 
                    SL_IPV4_BYTE(*pIPv4,2),
                    SL_IPV4_BYTE(*pIPv4,1),
                    SL_IPV4_BYTE(*pIPv4,0));
    }
    return;
}

int hashf(unsigned char *str) 
{
    int hash = 5381;
    int c;

    while (c = *str++)
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

    return hash;
}
