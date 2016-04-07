#ifndef __ROFFS_H__
#define __ROFFS_H__

#include <esp8266.h>

typedef struct ROFFS_FILE ROFFS_FILE;

int roffs_mount(uint32_t flashAddress);
ROFFS_FILE *roffs_open(const char *fileName);
int roffs_file_size(ROFFS_FILE *file);
int roffs_file_flags(ROFFS_FILE *file);
int roffs_read(ROFFS_FILE *file, char *buf, int len);
int roffs_close(ROFFS_FILE *file);

#endif


