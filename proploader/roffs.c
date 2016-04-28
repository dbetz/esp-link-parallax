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

ROFFS_FILE ICACHE_FLASH_ATTR *roffs_create(const char *fileName, int size)
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
    uint32_t header;
    uint32_t start;
    uint32_t offset;
    uint32_t size;
    uint8_t flags;
};

// initialize to an invalid address to indicate that no filesystem is mounted
#define BAD_FILESYSTEM_BASE 3
static uint32_t fsData = BAD_FILESYSTEM_BASE;

static int ICACHE_FLASH_ATTR readFlash(uint32_t addr, void *buf, int size)
{
    uint32_t alignedStart = (addr + 3) & ~3;
    uint32_t alignedStartOffset = alignedStart - addr;
    uint32_t alignedEnd = (addr + size) & ~3;
    uint32_t alignedEndOffset = alignedEnd - addr;
    uint32_t alignedSize = alignedEnd - alignedStart;
    uint32 longBuf;

    if (addr < alignedStart) {
        if (spi_flash_read(alignedStart - 4, &longBuf, 4) != SPI_FLASH_RESULT_OK)
            return -1;
        os_memcpy((uint8_t *)buf + 4 - alignedStartOffset, &longBuf, alignedStartOffset);
    }

    if (alignedSize > 0) {
        // this code assumes that spi_flash_read can read into a non-aligned buffer!
        if (spi_flash_read(alignedStart, (uint32 *)buf + alignedStartOffset, alignedSize) != SPI_FLASH_RESULT_OK)
            return -1;
    }

    if (addr + size > alignedEnd) {
        if (spi_flash_read(alignedEnd, &longBuf, 4) != SPI_FLASH_RESULT_OK)
            return -1;
        os_memcpy((uint8_t *)buf + alignedEndOffset, &longBuf, size - alignedEndOffset);
    }

    return 0;
}

static int writeFlash(uint32_t addr, void *buf, int size)
{
os_printf("writeFlash: %08lx %d\n", addr, size);
    if (spi_flash_write(addr, (uint32 *)buf, size) != SPI_FLASH_RESULT_OK) {
os_printf("writeFlash: failed\n");
        return SPI_FLASH_RESULT_ERR;
    }
    return SPI_FLASH_RESULT_OK;
}

static int updateFlash(uint32_t addr, void *buf, int size)
{
os_printf("updateFlash: %08lx %d\n", addr, size);
    if (spi_flash_write(addr, (uint32 *)buf, size) != SPI_FLASH_RESULT_OK) {
os_printf("updateFlash: failed\n");
        return SPI_FLASH_RESULT_ERR;
    }
    return SPI_FLASH_RESULT_OK;
}

int ICACHE_FLASH_ATTR roffs_mount(uint32_t flashAddress)
{
	RoFsHeader testHeader;

	// base address must be aligned to 4 bytes
	if ((flashAddress & 3) != 0)
		return -1;

	// read the filesystem header (first file header)
	if (readFlash(flashAddress, &testHeader, sizeof(RoFsHeader)) != 0)
        return -2;

    // check the magic number to make sure this is really a filesystem
	if (testHeader.magic != ROFS_MAGIC)
		return -3;

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
		if (readFlash(p, &h, sizeof(RoFsHeader)) != 0)
            return NULL;

        // check the magic number
		if (h.magic != ROFS_MAGIC)
            return NULL;

		// check for the end of image marker
        if (h.flags & FLAG_LASTFILE)
            return NULL;

		// only check active files that are not pending
        if ((h.flags & FLAG_ACTIVE) && !(h.flags & FLAG_PENDING)) {

            // get the name of the file
		    if (readFlash(p + sizeof(RoFsHeader), namebuf, sizeof(namebuf)) != 0)
                return NULL;

		    // check to see if this is the file we're looking for
            if (os_strcmp(namebuf, fileName) == 0) {
                if (!(file = (ROFFS_FILE *)os_malloc(sizeof(ROFFS_FILE))))
                    return NULL;
                file->header = p;
			    file->start = p + sizeof(RoFsHeader) + h.nameLen;
			    file->offset = 0;
                file->size = h.fileLenComp;
                file->flags = h.flags;
			    return file;
		    }
        }

		// skip over the file data
		p += sizeof(RoFsHeader) + h.nameLen + h.fileLenComp;

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

    if (file->flags & FLAG_LASTFILE) {
	    RoFsHeader h;
	    
        if (spi_flash_read(file->header, (uint32 *)&h, sizeof(RoFsHeader)) != SPI_FLASH_RESULT_OK) {
os_printf("close: error reading new file header\n");
            return -1;
        }
        h.flags &= ~FLAG_PENDING;
	    if (updateFlash(file->header, (uint32 *)&h, sizeof(RoFsHeader)) != SPI_FLASH_RESULT_OK) {
os_printf("close: error updating new file header\n");
            return -1;
        }

        os_memset(&h, 0xff, sizeof(RoFsHeader));
	    h.magic = ROFS_MAGIC;
        file->offset = (file->offset + 3) & ~3;
	    if (writeFlash(file->start + file->offset, (uint32 *)&h, sizeof(RoFsHeader)) != SPI_FLASH_RESULT_OK) {
os_printf("close: error writing new terminator\n");
            return -1;
        }
    }

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
	if (readFlash(file->start + file->offset, buf, len) != 0)
        return -1;

	// update the file position
	file->offset += len;

	// return the number of bytes read
	return len;
}

#define NOT_FOUND   0xffffffff

static int ICACHE_FLASH_ATTR find_file_and_insertion_point(const char *fileName, uint32_t *pFileOffset, uint32_t *pInsertionOffset)
{
    uint32_t p = fsData;
	char namebuf[256];
	RoFsHeader h;

    // assume file won't be found
    *pFileOffset = NOT_FOUND;

	// make sure there is a filesystem mounted
    if (fsData == BAD_FILESYSTEM_BASE) {
os_printf("roffs: filesystem not mounted\n");
		return -1;
	}

	// strip initial slashes
	while (fileName[0] == '/')
        fileName++;

	// find the file
	for (;;) {

		// read the next file header
		if (readFlash(p, &h, sizeof(RoFsHeader)) != 0) {
os_printf("roffs: %08lx error reading file header\n", p);
            return -1;
        }

        // check the magic number
		if (h.magic != ROFS_MAGIC) {
os_printf("roffs: %08lx bad magic number\n", p);
            return -1;
        }

		// check for the end of image marker
        if (h.flags & FLAG_LASTFILE) {
os_printf("roffs: %08lx insertion point\n", p);
            *pInsertionOffset = p;
            return 0;
        }

		// only check active files that are not pending
        if ((h.flags & FLAG_ACTIVE) && !(h.flags & FLAG_PENDING)) {

            // get the name of the file
		    if (readFlash(p + sizeof(RoFsHeader), namebuf, sizeof(namebuf)) != 0) {
os_printf("roffs: %08lx error reading file name\n", p);
                return -1;
            }

os_printf("roffs: %08lx checking '%s'\n", p, namebuf);
		    // check to see if this is the file we're looking for
            if (os_strcmp(namebuf, fileName) == 0)
                *pFileOffset = p;
        }
        else {
os_printf("roffs: %08lx skipping %s file\n", p, h.flags & FLAG_PENDING ? "pending" : "deleted");
        }

		// skip over the file data
		p += sizeof(RoFsHeader) + h.nameLen + h.fileLenComp;

		// align to next 32 bit offset
        p = (p + 3) & ~3;
	}

    // never reached
os_printf("roffs: internal error\n");
    return -1;
}

ROFFS_FILE ICACHE_FLASH_ATTR *roffs_create(const char *fileName, int size)
{
    uint32_t fileOffset, insertionOffset;
	ROFFS_FILE *file;
	RoFsHeader h;

    if (find_file_and_insertion_point(fileName, &fileOffset, &insertionOffset) != 0) {
os_printf("create: can't find insertion point\n");
        return NULL;
}

    if (!(file = (ROFFS_FILE *)os_malloc(sizeof(ROFFS_FILE)))) {
os_printf("create: insufficient memory\n");
        return NULL;
}

	// delete the old version of the file if one was found
    if (fileOffset != NOT_FOUND) {
        if (spi_flash_read(fileOffset, (uint32 *)&h, sizeof(RoFsHeader)) != SPI_FLASH_RESULT_OK) {
os_printf("create: error reading old file header\n");
            os_free(file);
            return NULL;
        }
        h.flags &= ~FLAG_ACTIVE;
	    if (updateFlash(fileOffset, (uint32 *)&h, sizeof(RoFsHeader)) != SPI_FLASH_RESULT_OK) {
os_printf("create: error writing old file header\n");
            os_free(file);
            return NULL;
        }
    }

	h.magic = ROFS_MAGIC;
	h.flags = FLAG_ACTIVE | FLAG_PENDING;
	h.compression = COMPRESS_NONE;
	h.nameLen = (os_strlen(fileName) + 1 + 3) & ~3;
	h.fileLenComp = size;
	h.fileLenDecomp = size;

    file->header = insertionOffset;
    file->start = insertionOffset + sizeof(RoFsHeader) + h.nameLen;
    file->offset = 0;
    file->size = size;
    file->flags = FLAG_LASTFILE;

	if (writeFlash(insertionOffset, (uint32 *)&h, sizeof(RoFsHeader)) != SPI_FLASH_RESULT_OK) {
os_printf("create: error writing new file header\n");
        os_free(file);
        return NULL;
    }
	if (writeFlash(insertionOffset + sizeof(RoFsHeader), (uint32 *)fileName, h.nameLen) != SPI_FLASH_RESULT_OK) {
os_printf("create: error reading new file name\n");
        os_free(file);
        return NULL;
    }
    
    return file;
}

int ICACHE_FLASH_ATTR roffs_write(ROFFS_FILE *file, char *buf, int len)
{
    if (writeFlash(file->start + file->offset, (uint32 *)buf, len) != SPI_FLASH_RESULT_OK) {
os_printf("write: error writing to file\n");
        return -1;
    }
    file->offset += len;
    return len;
}

#endif

