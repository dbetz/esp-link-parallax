#include <esp8266.h>
#include "roffs.h"

#ifdef SPIFFS

#include "spiffs.h"

static u8_t spiffs_work_buf[SPIFFS_CFG_LOG_PAGE_SZ(x)*2];
static u8_t spiffs_fds[4*32];
static spiffs fs;

int ICACHE_FLASH_ATTR roffs_mount(uint32_t flashAddress)
{
    spiffs_config cfg;
    s32_t spiffs_hal_read(u32_t addr, u32_t size, u8_t *dst);
    s32_t spiffs_hal_write(u32_t addr, u32_t size, u8_t *src);
    s32_t spiffs_hal_erase(u32_t addr, u32_t size);
    cfg.hal_read_f = spiffs_hal_read;
    cfg.hal_write_f = spiffs_hal_write;
    cfg.hal_erase_f = spiffs_hal_erase;
    int res = SPIFFS_mount(&fs,
                           &cfg,
                           spiffs_work_buf,
                           spiffs_fds, sizeof(spiffs_fds),
                           NULL, 0,
                           NULL);
    if (res != SPIFFS_OK) {
        os_printf("Formatting flash filesystem\n");
        res = SPIFFS_format(&fs);
        if (res == SPIFFS_OK) {
            res = SPIFFS_mount(&fs,
                              &cfg,
                              spiffs_work_buf,
                              spiffs_fds, sizeof(spiffs_fds),
                              NULL, 0,
                              NULL);
        }
    }

    return res == SPIFFS_OK ? 0 : -1;
}

ROFFS_FILE ICACHE_FLASH_ATTR *roffs_open(const char *fileName)
{
    ROFFS_FILE *file;
    spiffs_file fd;
    if ((fd = SPIFFS_open(&fs, fileName, SPIFFS_O_RDONLY, 0)) < 0)
        return NULL;
    if (!(file = (ROFFS_FILE *)os_malloc(sizeof(ROFFS_FILE)))) {
        SPIFFS_close(&fs, fd);
        return NULL;
    }
    file->fd = fd;
    return file;
}

int ICACHE_FLASH_ATTR roffs_close(ROFFS_FILE *file)
{
    SPIFFS_close(&fs, file->fd);
    os_free(file);
    return 0;
}

int ICACHE_FLASH_ATTR roffs_file_size(ROFFS_FILE *file)
{
    spiffs_stat stat;
    if (SPIFFS_fstat(&fs, file->fd, &stat) != SPIFFS_OK)
        return -1;
    return stat.size;
}

int ICACHE_FLASH_ATTR roffs_file_flags(ROFFS_FILE *file)
{
    return 0; // no place to store this metadata
}

int ICACHE_FLASH_ATTR roffs_read(ROFFS_FILE *file, char *buf, int len)
{
    return SPIFFS_read(&fs, file->fd, buf, len);
}

ROFFS_FILE ICACHE_FLASH_ATTR *roffs_create(const char *fileName)
{
    ROFFS_FILE *file;
    spiffs_file fd;
    if ((fd = SPIFFS_open(&fs, fileName, SPIFFS_O_CREAT | SPIFFS_O_WRONLY, 0)) < 0)
        return NULL;
    if (!(file = (ROFFS_FILE *)os_malloc(sizeof(ROFFS_FILE)))) {
        SPIFFS_close(&fs, fd);
        return NULL;
    }
    file->fd = fd;
    return file;
}

int ICACHE_FLASH_ATTR roffs_write(ROFFS_FILE *file, char *buf, int len)
{
    return SPIFFS_write(&fs, file->fd, buf, len);
}

#else

#include "roffsformat.h"

// open file structure
struct ROFFS_FILE_STRUCT {
    uint32_t start;
    uint32_t offset;
    uint32_t size;
    uint8_t flags;
};

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
	RoFsHeader testHeader;

	// base address must be aligned to 4 bytes
	if ((flashAddress & 3) != 0)
		return -1;

	// read the filesystem header (first file header)
	if (readFlash(&testHeader, flashAddress, sizeof(RoFsHeader)) != 0)
        return -1;

    // check the magic number to make sure this is really a filesystem
	if (testHeader.magic != ROFS_MAGIC)
		return -1;

	// filesystem is mounted successfully
    fsData = flashAddress;
    return 0;
}

ROFFS_FILE ICACHE_FLASH_ATTR *roffs_open(const char *fileName)
{
    uint32_t p = fsData;
	char namebuf[256];
	ROFFS_FILE *file;
	RoFsHeader h;

	// make sure there is a filesystem mounted
    if (fsData == BAD_FILESYSTEM_BASE)
		return NULL;

	// strip initial slashes
	while (fileName[0] == '/')
        fileName++;

	// find the file
	for (;;) {

		// read the next file header
		if (readFlash(&h, p, sizeof(RoFsHeader)) != 0)
            return NULL;
		p += sizeof(RoFsHeader);

		// check for the end of image marker
        if (h.flags & FLAG_LASTFILE)
            return NULL;

        // check the magic number
		if (h.magic != ROFS_MAGIC)
            return NULL;

		// only check active files that are not pending
        if ((h.flags & FLAG_ACTIVE) && !(h.flags & FLAG_PENDING)) {

            // get the name of the file
		    if (readFlash(namebuf, p, sizeof(namebuf)) != 0)
                return NULL;

		    // check to see if this is the file we're looking for
            if (os_strcmp(namebuf, fileName) == 0) {
                if (!(file = (ROFFS_FILE *)os_malloc(sizeof(ROFFS_FILE))))
                    return NULL;
			    file->start = p + h.nameLen;
			    file->offset = 0;
                file->size = h.fileLenComp;
                file->flags = h.flags;
			    return file;
		    }
        }

		// skip over the file data
		p += h.nameLen + h.fileLenComp;

		// align to next 32 bit offset
        p = (p + 3) & ~3;
	}

    // file not found
    return NULL;
}

int ICACHE_FLASH_ATTR roffs_close(ROFFS_FILE *file)
{
    if (!file)
        return -1;
    os_free(file);
    return 0;
}

int ICACHE_FLASH_ATTR roffs_file_size(ROFFS_FILE *file)
{
    if (!file)
        return -1;
    return (int)file->size;
}

int ICACHE_FLASH_ATTR roffs_file_flags(ROFFS_FILE *file)
{
    if (!file)
        return -1;
    return (int)file->flags;
}

int ICACHE_FLASH_ATTR roffs_read(ROFFS_FILE *file, char *buf, int len)
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

ROFFS_FILE ICACHE_FLASH_ATTR *roffs_create(const char *fileName)
{
    return NULL;
}

int ICACHE_FLASH_ATTR roffs_write(ROFFS_FILE *file, char *buf, int len)
{
    return -1;
}

#endif

