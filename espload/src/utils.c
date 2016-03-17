#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "propimage.h"
#include "fastproploader.h"
#include "utils.h"
#include "sock.h"

#ifndef TRUE
#define TRUE    1
#define FALSE   0
#endif

uint8_t *readEntireFile(const char *fileName, int *pSize)
{
    uint8_t *image;
    FILE *fp;

    /* open the image file */
    if (!(fp = fopen(fileName, "rb"))) {
        printf("error: can't open '%s'\n", fileName);
        return NULL;
    }
    
    /* get the size of the binary file */
    fseek(fp, 0, SEEK_END);
    *pSize = (int)ftell(fp);
    fseek(fp, 0, SEEK_SET);

    /* allocate space for the file */
    if (!(image = (uint8_t *)malloc(*pSize))) {
        printf("error: insufficient memory\n");
        return NULL;
    }

    /* read the entire image into memory */
    if ((int)fread(image, 1, *pSize, fp) != *pSize) {
        printf("error: reading '%s'\n", fileName);
        free(image);
        return NULL;
    }
    
    /* close the file */
    fclose(fp);
    
    /* return the image */
    return image;
}

int setBaudRate(const char *hostName, int baudRate)
{
    uint8_t buffer[1024];
    int hdrCnt, result;
    SOCKADDR_IN addr;

    if (GetInternetAddress(hostName, 80, &addr) != 0) {
        printf("error: invalid host name or IP address '%s'\n", hostName);
        return -1;
    }

    hdrCnt = snprintf((char *)buffer, sizeof(buffer), "\
POST /propeller/set-baud-rate?baud-rate=%d HTTP/1.1\r\n\
\r\n", baudRate);

    if (sendRequest(&addr, buffer, hdrCnt, buffer, sizeof(buffer), &result) == -1) {
        printf("error: set-baud-rate request failed\n");
        return -1;
    }
    else if (result != 200) {
        printf("error: set-baud-rate returned %d\n", result);
        return -1;
    }
    
    return 0;
}

int slowLoad(const char *hostName, PropellerImage *image, LoadType loadType, int ackSize)
{
    uint8_t buffer[1024], *packet;
    int hdrCnt, result;
    SOCKADDR_IN addr;

    if (GetInternetAddress(hostName, 80, &addr) != 0) {
        printf("error: invalid host name or IP address '%s'\n", hostName);
        return -1;
    }

    hdrCnt = snprintf((char *)buffer, sizeof(buffer), "\
POST /propeller/load?reset-pin=%d&baud-rate=%d HTTP/1.1\r\n\
Content-Length: %d\r\n\
\r\n", resetPin, initialBaudRate, image->imageSize);

    if (!(packet = (uint8_t *)malloc(hdrCnt + image->imageSize)))
        return -1;

    memcpy(packet,  buffer, hdrCnt);
    memcpy(&packet[hdrCnt], image->imageData, image->imageSize);
    
    if (sendRequest(&addr, packet, hdrCnt + image->imageSize, buffer, sizeof(buffer), &result) == -1) {
        printf("error: load request failed\n");
        return -1;
    }
    else if (result != 200) {
        printf("error: load returned %d\n", result);
        return -1;
    }
    
    return 0;
}

int sendRequest(SOCKADDR_IN *addr, uint8_t *req, int reqSize, uint8_t *res, int resMax, int *pResult)
{
    char buf[80];
    SOCKET sock;
    int cnt;
    
    if (ConnectSocket(addr, &sock) != 0) {
        printf("error: connect failed\n");
        return -1;
    }
    
    if (verbose) {
        printf("REQ: %d\n", reqSize);
        dumpHdr(req, reqSize);
    }
    
    if (SendSocketData(sock, req, reqSize) != reqSize) {
        printf("error: send request failed\n");
        return -1;
    }
    
    if ((cnt = ReceiveSocketDataTimeout(sock, res, resMax, 10000)) == -1) {
        printf("error: receive response failed\n");
        return -1;
    }
    
    if (verbose) {
        printf("RES: %d\n", cnt);
        dumpResponse(res, cnt);
    }
    
    if (sscanf((char *)res, "%s %d", buf, pResult) != 2)
        return -1;
        
    CloseSocket(sock);
    
    return cnt;
}
    
void dumpHdr(const uint8_t *buf, int size)
{
    int startOfLine = TRUE;
    const uint8_t *p = buf;
    while (p < buf + size) {
        if (*p == '\r') {
            if (startOfLine)
                break;
            startOfLine = TRUE;
            putchar('\n');
        }
        else if (*p != '\n') {
            startOfLine = FALSE;
            putchar(*p);
        }
        ++p;
    }
    putchar('\n');
}

void dumpResponse(const uint8_t *buf, int size)
{
    int startOfLine = TRUE;
    const uint8_t *p = buf;
    const uint8_t *save;
    int cnt;
    
    while (p < buf + size) {
        if (*p == '\r') {
            if (startOfLine) {
                ++p;
                if (*p == '\n')
                    ++p;
                break;
            }
            startOfLine = TRUE;
            putchar('\n');
        }
        else if (*p != '\n') {
            startOfLine = FALSE;
            putchar(*p);
        }
        ++p;
    }
    putchar('\n');
    
    save = p;
    while (p < buf + size) {
        if (*p == '\r')
            putchar('\n');
        else if (*p != '\n')
            putchar(*p);
        ++p;
    }

    p = save;
    cnt = 0;
    while (p < buf + size) {
        printf("%02x ", *p++);
        if ((++cnt % 16) == 0)
            putchar('\n');
    }
    if ((cnt % 16) != 0)
        putchar('\n');
}
