#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "fastproploader.h"
#include "propimage.h"
#include "utils.h"
#include "sock.h"

#define FAILSAFE_TIMEOUT    2.0         /* Number of seconds to wait for a packet from the host */
#define MAX_RX_SENSE_ERROR  23          /* Maximum number of cycles by which the detection of a start bit could be off (as affected by the Loader code) */
#define MAX_PACKET_SIZE     1024        /* size of data buffer in the second-stage loader */

// Offset (in bytes) from end of Loader Image pointing to where most host-initialized values exist.
// Host-Initialized values are: Initial Bit Time, Final Bit Time, 1.5x Bit Time, Failsafe timeout,
// End of Packet timeout, and ExpectedID.  In addition, the image checksum at word 5 needs to be
// updated.  All these values need to be updated before the download stream is generated.
// NOTE: DAT block data is always placed before the first Spin method
#define RAW_LOADER_INIT_OFFSET_FROM_END (-(10 * 4) - 8)

// Raw loader image.  This is a memory image of a Propeller Application written in PASM that fits into our initial
// download packet.  Once started, it assists with the remainder of the download (at a faster speed and with more
// relaxed interstitial timing conducive of Internet Protocol delivery. This memory image isn't used as-is; before
// download, it is first adjusted to contain special values assigned by this host (communication timing and
// synchronization values) and then is translated into an optimized Propeller Download Stream understandable by the
// Propeller ROM-based boot loader.
#include "IP_Loader.h"

static uint8_t initCallFrame[] = {0xFF, 0xFF, 0xF9, 0xFF, 0xFF, 0xFF, 0xF9, 0xFF};

double ClockSpeed = 80000000.0;

static void generateInitialLoaderImage(PropellerImage *image, int packetID, int initialBaudRate, int finalBaudRate);
static int transmitPacket(SOCKET sock, int id, uint8_t *payload, int payloadSize, int *pResult, int timeout);
static int32_t getLong(const uint8_t *buf);
static void setLong(uint8_t *buf, uint32_t value);

int fastLoad(const char *hostName, const char *fileName, LoadType loadType)
{
    PropellerImage loaderImage;
    int32_t packetID, checksum;
    uint8_t response[8];
    int imageSize, result, cnt, i;
    SOCKADDR_IN addr;
    uint8_t *image;
    SOCKET sock;

    if ((image = readEntireFile(fileName, &imageSize)) == NULL)
        return -1;

    /* compute the packet ID (number of packets to be sent) */
    packetID = (imageSize + MAX_PACKET_SIZE - 1) / MAX_PACKET_SIZE;

    /* generate a loader packet */
    generateInitialLoaderImage(&loaderImage, packetID, initialBaudRate, finalBaudRate);

    /* compute the image checksum */
    checksum = 0;
    for (i = 0; i < imageSize; ++i)
        checksum += image[i];
    for (i = 0; i < (int)sizeof(initCallFrame); ++i)
        checksum += initCallFrame[i];

    if (GetInternetAddress(hostName, 23, &addr) != 0) {
        printf("error: invalid host name or IP address '%s'\n", hostName);
        return -1;
    }
    
    if (ConnectSocket(&addr, &sock) != 0) {
        printf("error: connect failed\n");
        return -1;
    }
    
    /* load the second-stage loader using the propeller ROM protocol */
    if (slowLoad(hostName, &loaderImage, ltDownloadAndRun, 0/*sizeof(response)*/) != 0)
        return -1;

    /* wait for the second-stage loader to start */
    cnt = ReceiveSocketDataTimeout(sock, response, sizeof(response), 2000);
    if (cnt != 8) {
        printf("error: second-stage loader failed to start - cnt %d\n", cnt);
        return -1;
    }
    result = getLong(&response[0]);
    if (result != packetID) {
        printf("error: second-stage loader failed to start - packetID %d, result %d\n", packetID, result);
        return -1;
    }
    
    /* switch to the final baud rate */
    if (setBaudRate(hostName, finalBaudRate) != 0)
        return -1;

    /* transmit the image */
    uint8_t *p = image;
    int remaining = imageSize;
    while (remaining > 0) {
        int size;
        if ((size = remaining) > MAX_PACKET_SIZE)
            size = MAX_PACKET_SIZE;
        if (transmitPacket(sock, packetID, p, size, &result, 2000) != 0) {
            printf("error: transmitPacket failed\n");
            return -1;
        }
        if (result != packetID - 1) {
            printf("error: unexpected result: expected %d, received %d\n", packetID - 1, result);
            return -1;
        }
        remaining -= size;
        p += size;
        --packetID;
    }

    /* transmit the RAM verify packet and verify the checksum */
    if (transmitPacket(sock, packetID, verifyRAM, sizeof(verifyRAM), &result, 2000) != 0) {
        printf("error: transmitPacket failed\n");
        return -1;
    }
    if (result != -checksum) {
        printf("error: bad checksum\n");
        return -1;
    }
    packetID = -checksum;

    /* program the eeprom if requested */
    if (loadType & ltDownloadAndProgram) {
        if (transmitPacket(sock, packetID, programVerifyEEPROM, sizeof(programVerifyEEPROM), &result, 8000) != 0) {
            printf("error: transmitPacket failed\n");
            return -1;
        }
        if (result != -checksum*2) {
            printf("error: bad checksum\n");
            return -1;
        }
        packetID = -checksum*2;
    }

    /* transmit the readyToLaunch packet */
    if (transmitPacket(sock, packetID, readyToLaunch, sizeof(readyToLaunch), &result, 2000) != 0) {
        printf("error: transmitPacket failed\n");
        return -1;
    }
    if (result != packetID - 1) {
        printf("error: readyToLaunch failed\n");
        return -1;
    }
    --packetID;

    /* transmit the launchNow packet which actually starts the downloaded program */
    if (transmitPacket(sock, packetID, launchNow, sizeof(launchNow), NULL, 2000) != 0) {
        printf("error: transmitPacket failed\n");
        return -1;
    }

    CloseSocket(sock);

    return 0;
}

#define SPACE_FOR_HEADER    1024

static void generateInitialLoaderImage(PropellerImage *image, int packetID, int initialBaudRate, int finalBaudRate)
{
    int initAreaOffset = sizeof(rawLoaderImage) + RAW_LOADER_INIT_OFFSET_FROM_END;
    
    // Make an image from the loader template
    pimageSetImage(image, rawLoaderImage, sizeof(rawLoaderImage));
 
    // Clock mode
    //pimageSetLong(image, initAreaOffset +  0, 0);

    // Initial Bit Time.
    pimageSetLong(image, initAreaOffset +  4, (int)trunc(80000000.0 / initialBaudRate + 0.5));

    // Final Bit Time.
    pimageSetLong(image, initAreaOffset +  8, (int)trunc(80000000.0 / finalBaudRate + 0.5));

    // 1.5x Final Bit Time minus maximum start bit sense error.
    pimageSetLong(image, initAreaOffset + 12, (int)trunc(1.5 * ClockSpeed / finalBaudRate - MAX_RX_SENSE_ERROR + 0.5));

    // Failsafe Timeout (seconds-worth of Loader's Receive loop iterations).
    pimageSetLong(image, initAreaOffset + 16, (int)trunc(FAILSAFE_TIMEOUT * ClockSpeed / (3 * 4) + 0.5));

    // EndOfPacket Timeout (2 bytes worth of Loader's Receive loop iterations).
    pimageSetLong(image, initAreaOffset + 20, (int)trunc((2.0 * ClockSpeed / finalBaudRate) * (10.0 / 12.0) + 0.5));

    // PatchLoaderLongValue(RawSize*4+RawLoaderInitOffset + 24, Max(Round(ClockSpeed * SSSHTime), 14));
    // PatchLoaderLongValue(RawSize*4+RawLoaderInitOffset + 28, Max(Round(ClockSpeed * SCLHighTime), 14));
    // PatchLoaderLongValue(RawSize*4+RawLoaderInitOffset + 32, Max(Round(ClockSpeed * SCLLowTime), 26));

    // Minimum EEPROM Start/Stop Condition setup/hold time (400 KHz = 1/0.6 µS); Minimum 14 cycles
    //pimageSetLong(image, initAreaOffset + 24, 14);

    // Minimum EEPROM SCL high time (400 KHz = 1/0.6 µS); Minimum 14 cycles
    //pimageSetLong(image, initAreaOffset + 28, 14);

    // Minimum EEPROM SCL low time (400 KHz = 1/1.3 µS); Minimum 26 cycles
    //pimageSetLong(image, initAreaOffset + 32, 26);

    // First Expected Packet ID; total packet count.
    pimageSetLong(image, initAreaOffset + 36, packetID);

    // Recalculate and update checksum so low byte of checksum calculates to 0.
    pimageUpdateChecksum(image);
}

static int transmitPacket(SOCKET sock, int id, uint8_t *payload, int payloadSize, int *pResult, int timeout)
{
    int packetSize = 2*sizeof(uint32_t) + payloadSize;
    uint8_t *packet, response[8];
    int retries, result, cnt;
    int32_t tag;

    /* build the packet to transmit */
    if (!(packet = (uint8_t *)malloc(packetSize)))
        return -1;
    setLong(&packet[0], id);
    memcpy(&packet[8], payload, payloadSize);

    /* send the packet */
    retries = 3;
    while (--retries >= 0) {

        /* setup the packet header */
        tag = (int32_t)rand();
        setLong(&packet[4], tag);
        if (SendSocketData(sock, packet, packetSize) != packetSize)
            return -1;

        /* receive the response */
        if (pResult) {
            cnt = ReceiveSocketDataTimeout(sock, response, sizeof(response), timeout);
            result = getLong(&response[0]);
            if (cnt == 8 && getLong(&response[4]) == tag && result != id) {
                free(packet);
                *pResult = result;
                return 0;
            }
        }

        /* don't wait for a result */
        else {
            free(packet);
            return 0;
        }
    }

    /* free the packet */
    free(packet);

    /* return timeout */
    return -1;
}

static int32_t getLong(const uint8_t *buf)
{
     return (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
}

static void setLong(uint8_t *buf, uint32_t value)
{
     buf[3] = value >> 24;
     buf[2] = value >> 16;
     buf[1] = value >>  8;
     buf[0] = value;
}

