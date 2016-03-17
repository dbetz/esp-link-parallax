#include <stddef.h>
#include "propimage.h"

void pimageSetImage(PropellerImage *image, uint8_t *imageData, int imageSize)
{
    image->imageData = imageData;
    image->imageSize = imageSize;
}

uint32_t pimageClkFreq(PropellerImage *image)
{
    return pimageGetLong(image, offsetof(SpinHdr, clkfreq));
}

void pimageSetClkFreq(PropellerImage *image, uint32_t clkFreq)
{
    pimageSetLong(image, offsetof(SpinHdr, clkfreq), clkFreq);
}

uint8_t pimageClkMode(PropellerImage *image)
{
    return pimageGetByte(image, offsetof(SpinHdr, clkmode));
}

void pimageSetClkMode(PropellerImage *image, uint8_t clkMode)
{
    pimageSetByte(image, offsetof(SpinHdr, clkmode), clkMode);
}

uint8_t pimageUpdateChecksum(PropellerImage *image)
{
    SpinHdr *spinHdr = (SpinHdr *)image->imageData;
    uint8_t *p = image->imageData;
    int chksum, cnt;
    spinHdr->chksum = chksum = 0;
    for (cnt = image->imageSize; --cnt >= 0; )
        chksum += *p++;
    spinHdr->chksum = SPIN_TARGET_CHECKSUM - chksum;
    return chksum & 0xff;
}

uint8_t pimageGetByte(PropellerImage *image, int offset)
{
     uint8_t *buf = image->imageData + offset;
     return buf[0];
}

void pimageSetByte(PropellerImage *image, int offset, uint8_t value)
{
     uint8_t *buf = image->imageData + offset;
     buf[0] = value;
}

uint16_t pimageGetWord(PropellerImage *image, int offset)
{
     uint8_t *buf = image->imageData + offset;
     return (buf[1] << 8) | buf[0];
}

void pimageSetWord(PropellerImage *image, int offset, uint16_t value)
{
     uint8_t *buf = image->imageData + offset;
     buf[1] = value >>  8;
     buf[0] = value;
}

uint32_t pimageGetLong(PropellerImage *image, int offset)
{
     uint8_t *buf = image->imageData + offset;
     return (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
}

void pimageSetLong(PropellerImage *image, int offset, uint32_t value)
{
     uint8_t *buf = image->imageData + offset;
     buf[3] = value >> 24;
     buf[2] = value >> 16;
     buf[1] = value >>  8;
     buf[0] = value;
}

