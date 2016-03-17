#ifndef __UTILS_H__
#define __UTILS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "propimage.h"
#include "fastproploader.h"
#include "sock.h"

extern int initialBaudRate;
extern int finalBaudRate;
extern int resetPin;
extern int verbose;

uint8_t *readEntireFile(const char *fileName, int *pSize);
int setBaudRate(const char *hostName, int baudRate);
int slowLoad(const char *hostName, PropellerImage *image, LoadType loadType, int ackSize);
int sendRequest(SOCKADDR_IN *addr, uint8_t *req, int reqSize, uint8_t *res, int resMax, int *pResult);
void dumpHdr(const uint8_t *buf, int size);
void dumpResponse(const uint8_t *buf, int size);

#ifdef __cplusplus
}
#endif

#endif
