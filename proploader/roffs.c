#include <esp8266.h>
#include "espfsformat.h"
#include "roffs.h"

// initialize to an invalid address to indicate that no filesystem is mounted
#define BAD_FILESYSTEM_BASE 3
static uint32_t fsData = BAD_FILESYSTEM_BASE;

static int ICACHE_FLASH_ATTR readFlash(void *buf, uint32_t addr, int size)
{
    uint32_t alignedStart = (addr + 3) & ~3;
    uint32_t alignedStartOffset = alignedStart - addr;
    uint32_t alignedEnd = (addr + size) & ~3;
    uint32_t alignedEndOffset = alignedEnd - addr;
    uint32_t alignedSize = alignedEnd - alignedStart;
    uint32 longBuf;

    if (addr < alignedStart) {
        if (spi_flash_read(alignedStart - 4, &longBuf, 4) != 0)
            return -1;
        os_memcpy((uint8_t *)buf + 4 - alignedStartOffset, &longBuf, alignedStartOffset);
    }

    if (alignedSize > 0) {
        // this code assumes that spi_flash_read can read into a non-aligned buffer!
        if (spi_flash_read(alignedStart, (uint32 *)buf + alignedStartOffset, alignedSize) != 0)
            return -1;
    }

    if (addr + size > alignedEnd) {
        if (spi_flash_read(alignedEnd, &longBuf, 4) != 0)
            return -1;
        os_memcpy((uint8_t *)buf + alignedEndOffset, &longBuf, size - alignedEndOffset);
    }

    return 0;
}

int ICACHE_FLASH_ATTR roffs_mount(uint32_t flashAddress)
{
	EspFsHeader testHeader;

	// base address must be aligned to 4 bytes
	if ((flashAddress & 3) != 0)
		return -1;

	// read the filesystem header (first file header)
	if (readFlash(&testHeader, flashAddress, sizeof(EspFsHeader)) != 0)
        return -1;

    // check the magic number to make sure this is really a filesystem
	if (testHeader.magic != ESPFS_MAGIC)
		return -1;

	// filesystem is mounted successfully
    fsData = flashAddress;
    return 0;
}

int ICACHE_FLASH_ATTR roffs_open(ROFFS_FILE *file, const char *fileName)
{
	uint32_t p = fsData;
	char namebuf[256];
	EspFsHeader h;

	// make sure there is a filesystem mounted
    if (fsData == BAD_FILESYSTEM_BASE)
		return -1;

	// strip initial slashes
	while (fileName[0] == '/')
        fileName++;

	// find the file
	while(1) {

		// read the next file header
		if (readFlash(&h, p, sizeof(EspFsHeader)) != 0)
            return -1;
		p += sizeof(EspFsHeader);

        // check the magic number
		if (h.magic != ESPFS_MAGIC)
            return -1;

		// check for the end of image marker
        if (h.flags & FLAG_LASTFILE)
            return -1;

		// get the name of the file
		if (readFlash(namebuf, p, sizeof(namebuf)) != 0)
            return -1;
        p += h.nameLen;

		// check to see if this is the file we're looking for
        if (os_strcmp(namebuf, fileName) == 0) {
			file->start = p;
			file->offset = 0;
            file->size = h.fileLenComp;
			return 0;
		}

		// skip over the file data
		p += h.fileLenComp;

		// align to next 32 bit offset
        p = (p + 3) & ~3;
	}

    // file not found
    return -1;
}

int ICACHE_FLASH_ATTR roffs_read(ROFFS_FILE *file, uint8_t *buf, int len)
{
	int remaining = file->size - file->offset;

	// don't read beyond the end of the file
	if (len > remaining)
        len = remaining;

    // read from the flash
	if (readFlash(buf, file->start + file->offset, len) != 0)
        return -1;

	// update the file position
	file->offset += len;

	// return the number of bytes read
	return len;
}
