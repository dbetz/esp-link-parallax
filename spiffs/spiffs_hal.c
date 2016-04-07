/*
 spiffs_hal.c - SPI read/write/erase functions for SPIFFS.
 This file is part of the SPIFFS support for ESP-LINK.
 Modified from spiffs_hal.cpp by David Betz.
*/

/*
 spiffs_hal.cpp - SPI read/write/erase functions for SPIFFS.
 Copyright (c) 2015 Ivan Grokhotkov. All rights reserved.
 This file is part of the esp8266 core for Arduino environment.

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <stdlib.h>
#include "spiffs/spiffs.h"
#include "c_types.h"
#include "spi_flash.h"

/*
 spi_flash_read function requires flash address to be aligned on word boundary.
 We take care of this by reading first and last words separately and memcpy
 relevant bytes into result buffer.

alignment:       012301230123012301230123
bytes requested: -------***********------
read directly:   --------xxxxxxxx--------
read pre:        ----aaaa----------------
read post:       ----------------bbbb----
alignedBegin:            ^
alignedEnd:                      ^
*/

s32_t ICACHE_FLASH_ATTR spiffs_hal_read(u32_t addr, u32_t size, u8_t *dst) {
//    optimistic_yield(10000);

    u32_t result = SPIFFS_OK;
    u32_t alignedBegin = (addr + 3) & (~3);
    u32_t alignedEnd = (addr + size) & (~3);
    if (alignedEnd < alignedBegin) {
        alignedEnd = alignedBegin;
    }

    if (addr < alignedBegin) {
        u32_t nb = alignedBegin - addr;
        u32_t tmp;
        if (spi_flash_read(alignedBegin - 4, &tmp, 4) != SPIFFS_OK) {
//            DEBUGV("_spif_read(%d) addr=%x size=%x ab=%x ae=%x\r\n",
//                __LINE__, addr, size, alignedBegin, alignedEnd);
            return SPIFFS_ERR_INTERNAL;
        }
        memcpy(dst, &tmp + 4 - nb, nb);
    }

    if (alignedEnd != alignedBegin) {
        if (spi_flash_read(alignedBegin, (u32_t*) (dst + alignedBegin - addr),
                alignedEnd - alignedBegin) != SPIFFS_OK) {
//            DEBUGV("_spif_read(%d) addr=%x size=%x ab=%x ae=%x\r\n",
//                __LINE__, addr, size, alignedBegin, alignedEnd);
            return SPIFFS_ERR_INTERNAL;
        }
    }

    if (addr + size > alignedEnd) {
        u32_t nb = addr + size - alignedEnd;
        u32_t tmp;
        if (spi_flash_read(alignedEnd, &tmp, 4) != SPIFFS_OK) {
//            DEBUGV("_spif_read(%d) addr=%x size=%x ab=%x ae=%x\r\n",
//                __LINE__, addr, size, alignedBegin, alignedEnd);
            return SPIFFS_ERR_INTERNAL;
        }

        memcpy(dst + size - nb, &tmp, nb);
    }

    return result;
}

/*
 Like spi_flash_read, spi_flash_write has a requirement for flash address to be
 aligned. However it also requires RAM address to be aligned as it reads data
 in 32-bit words. Flash address (mis-)alignment is handled much the same way
 as for reads, but for RAM alignment we have to copy data into a temporary
 buffer. The size of this buffer is a tradeoff between number of writes required
 and amount of stack required. This is chosen to be 512 bytes here, but might
 be adjusted in the future if there are good reasons to do so.
*/

static const int UNALIGNED_WRITE_BUFFER_SIZE = 512;

s32_t ICACHE_FLASH_ATTR spiffs_hal_write(u32_t addr, u32_t size, u8_t *src) {
//    optimistic_yield(10000);

    u32_t alignedBegin = (addr + 3) & (~3);
    u32_t alignedEnd = (addr + size) & (~3);
    if (alignedEnd < alignedBegin) {
        alignedEnd = alignedBegin;
    }

    if (addr < alignedBegin) {
        u32_t ofs = alignedBegin - addr;
        u32_t nb = (size < ofs) ? size : ofs;
        u8_t tmp[4] __attribute__((aligned(4))) = {0xff, 0xff, 0xff, 0xff};
        memcpy(tmp + 4 - ofs, src, nb);
        if (spi_flash_write(alignedBegin - 4, (u32_t*) tmp, 4) != SPIFFS_OK) {
//            DEBUGV("_spif_write(%d) addr=%x size=%x ab=%x ae=%x\r\n",
//                __LINE__, addr, size, alignedBegin, alignedEnd);
            return SPIFFS_ERR_INTERNAL;
        }
    }

    if (alignedEnd != alignedBegin) {
        u32_t* srcLeftover = (u32_t*) (src + alignedBegin - addr);
        u32_t srcAlign = ((u32_t) srcLeftover) & 3;
        if (!srcAlign) {
            if (spi_flash_write(alignedBegin, (u32_t*) srcLeftover,
                    alignedEnd - alignedBegin) != SPIFFS_OK) {
//                DEBUGV("_spif_write(%d) addr=%x size=%x ab=%x ae=%x\r\n",
//                    __LINE__, addr, size, alignedBegin, alignedEnd);
                return SPIFFS_ERR_INTERNAL;
            }
        }
        else {
            u8_t buf[UNALIGNED_WRITE_BUFFER_SIZE];
            for (u32_t sizeLeft = alignedEnd - alignedBegin; sizeLeft; ) {
                size_t willCopy = sizeLeft < sizeof(buf) ? sizeLeft : sizeof(buf);
                memcpy(buf, srcLeftover, willCopy);

                if (spi_flash_write(alignedBegin, (u32_t*) buf, willCopy) != SPIFFS_OK) {
//                    DEBUGV("_spif_write(%d) addr=%x size=%x ab=%x ae=%x\r\n",
//                        __LINE__, addr, size, alignedBegin, alignedEnd);
                    return SPIFFS_ERR_INTERNAL;
                }

                sizeLeft -= willCopy;
                srcLeftover += willCopy;
                alignedBegin += willCopy;
            }
        }
    }

    if (addr + size > alignedEnd) {
        u32_t nb = addr + size - alignedEnd;
        u32_t tmp = 0xffffffff;
        memcpy(&tmp, src + size - nb, nb);

        if (spi_flash_write(alignedEnd, &tmp, 4) != SPIFFS_OK) {
//            DEBUGV("_spif_write(%d) addr=%x size=%x ab=%x ae=%x\r\n",
//                __LINE__, addr, size, alignedBegin, alignedEnd);
            return SPIFFS_ERR_INTERNAL;
        }
    }

    return SPIFFS_OK;
}

s32_t ICACHE_FLASH_ATTR spiffs_hal_erase(u32_t addr, u32_t size) {
    if ((size & (SPI_FLASH_SEC_SIZE - 1)) != 0 ||
        (addr & (SPI_FLASH_SEC_SIZE - 1)) != 0) {
//        DEBUGV("_spif_erase called with addr=%x, size=%d\r\n", addr, size);
        return SPIFFS_ERR_INTERNAL;
    }
    const u32_t sector = addr / SPI_FLASH_SEC_SIZE;
    const u32_t sectorCount = size / SPI_FLASH_SEC_SIZE;
    for (u32_t i = 0; i < sectorCount; ++i) {
//        optimistic_yield(10000);
        if (spi_flash_erase_sector(sector + i) != SPIFFS_OK) {
//            DEBUGV("_spif_erase addr=%x size=%d i=%d\r\n", addr, size, i);
            return SPIFFS_ERR_INTERNAL;
        }
    }
    return SPIFFS_OK;
}
