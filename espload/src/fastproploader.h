#ifndef FASTPROPLOADER_H
#define FASTPROPLOADER_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ltShutdown = 0,
    ltDownloadAndRun = (1 << 0),
    ltDownloadAndProgram = (1 << 1),
    ltDownloadAndProgramAndRun = ltDownloadAndRun | ltDownloadAndProgram
} LoadType;

int fastLoad(const char *hostName, const char *fileName, LoadType loadType);

#ifdef __cplusplus
}
#endif

#endif
