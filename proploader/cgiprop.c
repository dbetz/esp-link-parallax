#include <esp8266.h>
#include <osapi.h>
#include "cgi.h"
#include "cgiprop.h"
#include "config.h"
#include "serbridge.h"
#include "proploader.h"
#include "uart.h"
#include "serled.h"
#include "espfs.h"

//#define STATE_DEBUG

static void startLoading(PropellerConnection *connection, const uint8_t *image, int imageSize);
static void finishLoading(PropellerConnection *connection);
static void abortLoading(PropellerConnection *connection);
static void httpdSendResponse(HttpdConnData *connData, int code, char *message);
static void timerCallback(void *data);
static void readCallback(char *buf, short length);

/* the order here must match the definition of LoadState in proploader.h */
static const char *stateNames[] = {
    "Idle",
    "Reset1",
    "Reset2",
    "TxHandshake",
    "RxHandshake",
    "VerifyChecksum"
};

static const ICACHE_FLASH_ATTR char *stateName(LoadState state)
{
    return state >= 0 && state < stMAX ? stateNames[state] : "Unknown";
}

int8_t ICACHE_FLASH_ATTR getIntArg(HttpdConnData *connData, char *name, int *pValue)
{
  char buf[16];
  int len = httpdFindArg(connData->getArgs, name, buf, sizeof(buf));
  if (len < 0) return 0; // not found, skip
  *pValue = atoi(buf);
  return 1;
}

// this is statically allocated because the serial read callback has no context parameter
PropellerConnection myConnection;

int ICACHE_FLASH_ATTR cgiPropInit()
{
    memset(&myConnection, 0, sizeof(PropellerConnection));
    myConnection.state = stIdle;
    return 1;
}

int ICACHE_FLASH_ATTR cgiPropSetBaudRate(HttpdConnData *connData)
{
    int baudRate;
    
    // check for the cleanup call
    if (connData->conn == NULL)
        return HTTPD_CGI_DONE;

    if (!getIntArg(connData, "baud-rate", &baudRate)) {
        errorResponse(connData, 400, "No baud-rate specified\r\n");
        return HTTPD_CGI_DONE;
    }

    DBG("set-baud-rate: baud-rate %d\n", baudRate);

    uart0_baud(baudRate);

    noCacheHeaders(connData, 200);
    httpdEndHeaders(connData);
    httpdSend(connData, "", 0);

    return HTTPD_CGI_DONE;
}

int ICACHE_FLASH_ATTR cgiPropLoad(HttpdConnData *connData)
{
    PropellerConnection *connection = &myConnection;
    
    // check for the cleanup call
    if (connData->conn == NULL)
        return HTTPD_CGI_DONE;

    if (connection->state != stIdle) {
        char buf[128];
        os_sprintf(buf, "Transfer already in progress: state %s\r\n", stateName(connection->state));
        errorResponse(connData, 400, buf);
        return HTTPD_CGI_DONE;
    }
    connData->cgiPrivData = connection;
    connection->connData = connData;
    
    if (connData->post->len == 0) {
        errorResponse(connData, 400, "No data\r\n");
        abortLoading(connection);
        return HTTPD_CGI_DONE;
    }
    else if (connData->post->buffLen != connData->post->len) {
        errorResponse(connData, 400, "Data too large\r\n");
        return HTTPD_CGI_DONE;
    }
    
    if (!getIntArg(connData, "baud-rate", &connection->baudRate))
        connection->baudRate = flashConfig.baud_rate;
    if (!getIntArg(connData, "final-baud-rate", &connection->finalBaudRate))
        connection->finalBaudRate = connection->baudRate;
    if (!getIntArg(connData, "reset-pin", &connection->resetPin))
        connection->resetPin = flashConfig.reset_pin;
    
    DBG("load: size %d, baud-rate %d, final-baud-rate %d, reset-pin %d\n", connData->post->buffLen, connection->baudRate, connection->finalBaudRate, connection->resetPin);

    startLoading(connection, (uint8_t *)connData->post->buff, connData->post->buffLen);

    return HTTPD_CGI_MORE;
}

int ICACHE_FLASH_ATTR cgiPropReset(HttpdConnData *connData)
{
    PropellerConnection *connection = &myConnection;
    
    // check for the cleanup call
    if (connData->conn == NULL)
        return HTTPD_CGI_DONE;

    if (connection->state != stIdle) {
        char buf[128];
        os_sprintf(buf, "Transfer already in progress: state %s\r\n", stateName(connection->state));
        errorResponse(connData, 400, buf);
        return HTTPD_CGI_DONE;
    }
    connData->cgiPrivData = connection;
    connection->connData = connData;

    if (!getIntArg(connData, "reset-pin", &connection->resetPin))
        connection->resetPin = flashConfig.reset_pin;

    DBG("reset: reset-pin %d\n", connection->resetPin);

    connection->image = NULL;

    makeGpio(connection->resetPin);
    GPIO_OUTPUT_SET(connection->resetPin, 1);
    connection->state = stReset1;
    
    os_timer_disarm(&connection->timer);
    os_timer_setfn(&connection->timer, timerCallback, connection);
    os_timer_arm(&connection->timer, RESET_DELAY_1, 0);

    return HTTPD_CGI_MORE;
}

static void ICACHE_FLASH_ATTR startLoading(PropellerConnection *connection, const uint8_t *image, int imageSize)
{
    connection->image = image;
    connection->imageSize = imageSize;
    
    uart0_baud(connection->baudRate);
    programmingCB = readCallback;

    makeGpio(connection->resetPin);
    GPIO_OUTPUT_SET(connection->resetPin, 1);
    connection->state = stReset1;
    
    os_timer_disarm(&connection->timer);
    os_timer_setfn(&connection->timer, timerCallback, connection);
    os_timer_arm(&connection->timer, RESET_DELAY_1, 0);
}

static void ICACHE_FLASH_ATTR finishLoading(PropellerConnection *connection)
{
    if (connection->finalBaudRate != connection->baudRate);
        uart0_baud(connection->finalBaudRate);
    programmingCB = NULL;
    myConnection.state = stIdle;
}

static void ICACHE_FLASH_ATTR abortLoading(PropellerConnection *connection)
{
    programmingCB = NULL;
    myConnection.state = stIdle;
}

#define MAX_SENDBUFF_LEN 2600

static void ICACHE_FLASH_ATTR httpdSendResponse(HttpdConnData *connData, int code, char *message)
{
    char sendBuff[MAX_SENDBUFF_LEN];
    httpdSetOutputBuffer(connData, sendBuff, sizeof(sendBuff));
    
    errorResponse(connData, code, message);
    httpdFlush(connData);
    connData->cgi = NULL;
}

static void ICACHE_FLASH_ATTR timerCallback(void *data)
{
    PropellerConnection *connection = (PropellerConnection *)data;
    
    os_timer_disarm(&connection->timer);
    
#ifdef STATE_DEBUG
    DBG("TIMER %s", stateName(connection->state));
#endif

    switch (connection->state) {
    case stIdle:
        // shouldn't happen
        break;
    case stReset1:
        connection->state = stReset2;
        GPIO_OUTPUT_SET(connection->resetPin, 0);
        os_timer_arm(&connection->timer, RESET_DELAY_2, 0);
        break;
    case stReset2:
        GPIO_OUTPUT_SET(connection->resetPin, 1);
        os_timer_arm(&connection->timer, RESET_DELAY_3, 0);
        if (connection->image)
            connection->state = stTxHandshake;
        else {
            httpdSendResponse(connection->connData, 200, "");
            connection->state = stIdle;
        }
        break;
    case stTxHandshake:
        connection->state = stRxHandshake;
        ploadInitiateHandshake(connection);
        os_timer_arm(&connection->timer, RX_HANDSHAKE_TIMEOUT, 0);
        break;
    case stRxHandshake:
        httpdSendResponse(connection->connData, 400, "RX handshake timeout\r\n");
        abortLoading(connection);
        break;
    case stVerifyChecksum:
        if (connection->retriesRemaining > 0) {
            uart_tx_one_char(UART0, 0xF9);
            os_timer_arm(&connection->timer, connection->retryDelay, 0);
            --connection->retriesRemaining;
        }
        else {
            httpdSendResponse(connection->connData, 400, "Checksum timeout\r\n");
            abortLoading(connection);
        }
        break;
    default:
        break;
    }
    
#ifdef STATE_DEBUG
    DBG(" -> %s\n", stateName(connection->state));
#endif
}

static void ICACHE_FLASH_ATTR readCallback(char *buf, short length)
{
    PropellerConnection *connection = &myConnection;
    int cnt, version;
    
    os_timer_disarm(&connection->timer);
    
#ifdef STATE_DEBUG
    DBG("READ: length %d, state %s", length, stateName(connection->state));
#endif

    switch (connection->state) {
    case stIdle:
    case stReset1:
    case stReset2:
    case stTxHandshake:
        // just ignore data received when we're not expecting it
        break;
    case stRxHandshake:
        if ((cnt = length) > connection->bytesRemaining)
            cnt = connection->bytesRemaining;
        memcpy(&connection->buffer[connection->bytesReceived], buf, cnt);
        connection->bytesReceived += cnt;
        if ((connection->bytesRemaining -= cnt) == 0) {
            if (ploadVerifyHandshakeResponse(connection, &version) == 0) {
                if (ploadLoadImage(connection, ltDownloadAndRun) == 0) {
                    os_timer_arm(&connection->timer, connection->retryDelay, 0);
                    connection->state = stVerifyChecksum;
                }
                else {
                    httpdSendResponse(connection->connData, 400, "Load image failed\r\n");
                    abortLoading(connection);
                }
            }
            else {
                httpdSendResponse(connection->connData, 400, "RX handshake failed\r\n");
                abortLoading(connection);
            }
        }
        break;
    case stVerifyChecksum:
        if (buf[0] == 0xFE) {
            httpdSendResponse(connection->connData, 200, "");
            finishLoading(connection);
        }
        else {
            httpdSendResponse(connection->connData, 400, "Checksum error\r\n");
            abortLoading(connection);
        }
        break;
    default:
        break;
    }
    
#ifdef STATE_DEBUG
    DBG(" -> %s\n", stateName(connection->state));
#endif
}




