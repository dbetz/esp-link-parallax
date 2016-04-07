#ifndef __ROFFS_H__
#define __ROFFS_H__

#include <esp8266.h>

typedef struct {
    uint32_t start;
    uint32_t offset;
    uint32_t size;
} ROFFS_FILE;

int roffs_mount(uint32_t flashAddress);
int roffs_open(ROFFS_FILE *file, const char *fileName);
int roffs_read(ROFFS_FILE *file, uint8_t *buf, int len);

#endif


