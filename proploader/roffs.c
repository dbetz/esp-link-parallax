#include <esp8266.h>
#include "roffs.h"

// WARNING!!
// This code assumes that buffers passed in for reading/writing are long aligned.
// It also always reads/writes an integral number of longs even if the size parameter
// is not long aligned. Make sure buffers have enough space to account for this.
// This was done to simplify the code and it causes no problems with the way the
// code is used by httpdroffs.c.

#ifndef SPIFFS

#include "roffsformat.h"

// open file structure
struct ROFFS_FILE_STRUCT {
    uint32_t header;
    uint32_t start;
    uint32_t offset;
    uint32_t size;
    uint8_t flags;
};

#define BAD_FILESYSTEM_BASE 3
#define NOT_FOUND           0xffffffff

// initialize to an invalid address to indicate that no filesystem is mounted
static uint32_t fsData = BAD_FILESYSTEM_BASE;

static int readFlash(uint32_t addr, void *buf, int size);
static int writeFlash(uint32_t addr, void *buf, int size);
static int updateFlash(uint32_t addr, void *buf, int size);

int ICACHE_FLASH_ATTR roffs_mount(uint32_t flashAddress)
{
	RoFsHeader testHeader;

	// base address must be aligned to 4 bytes
	if ((flashAddress & 3) != 0)
		return -1;

	// read the filesystem header (first file header)
	if (readFlash(flashAddress, &testHeader, sizeof(RoFsHeader)) != SPI_FLASH_RESULT_OK)
        return -2;

    // check the magic number to make sure this is really a filesystem
	if (testHeader.magic != ROFS_MAGIC)
		return -3;

	// filesystem is mounted successfully
    fsData = flashAddress;
    return 0;
}

int ICACHE_FLASH_ATTR roffs_format(uint32_t flashAddress)
{
	RoFsHeader h;
    os_memset(&h, 0xff, sizeof(RoFsHeader));
    h.magic = ROFS_MAGIC;
    if (writeFlash(flashAddress, (uint32 *)&h, sizeof(RoFsHeader)) != SPI_FLASH_RESULT_OK) {
os_printf("format: error writing terminator\n");
        return -1;
    }
    return 0;
}

ROFFS_FILE ICACHE_FLASH_ATTR *roffs_open(const char *fileName)
{
    uint32_t p = fsData;
	char namebuf[256];
	ROFFS_FILE *file;
	RoFsHeader h;

	// make sure there is a filesystem mounted
    if (fsData == BAD_FILESYSTEM_BASE) {
os_printf("open: filesystem not mounted\n");
		return NULL;
	}

	// strip initial slashes
	while (fileName[0] == '/')
        fileName++;

	// find the file
	for (;;) {

		// read the next file header
		if (readFlash(p, &h, sizeof(RoFsHeader)) != SPI_FLASH_RESULT_OK)
            return NULL;

		// read the next file header
		if (readFlash(p, &h, sizeof(RoFsHeader)) != SPI_FLASH_RESULT_OK) {
os_printf("open: %08lx error reading file header\n", p);
            return NULL;
        }

        // check the magic number
		if (h.magic != ROFS_MAGIC) {
os_printf("open: %08lx bad magic number\n", p);
            return NULL;
        }

		// check for the end of image marker
        if (h.flags & FLAG_LASTFILE)
            return NULL;

		// terminate on a leftover pending file
		else if (h.flags & FLAG_PENDING) {
os_printf("open: terminate on a leftover pending file\n");
            return NULL;
        }
        
		// only check active files
        else if (h.flags & FLAG_ACTIVE) {

            // get the name of the file
		    if (readFlash(p + sizeof(RoFsHeader), namebuf, sizeof(namebuf)) != SPI_FLASH_RESULT_OK) {
os_printf("open: %08lx error reading file name\n", p);
                return NULL;
            }

os_printf("open: %08lx checking '%s'\n", p, namebuf);
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

        // deleted file
        else {
os_printf("open: %08lx skipping deleted file\n", p);
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
	    
        if (readFlash(file->header, (uint32 *)&h, sizeof(RoFsHeader)) != SPI_FLASH_RESULT_OK) {
os_printf("close: error reading new file header\n");
            return -1;
        }
        h.flags &= ~FLAG_PENDING;
	    h.fileLenComp = file->size;
	    h.fileLenDecomp = file->size;
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
	if (readFlash(file->start + file->offset, buf, len) != SPI_FLASH_RESULT_OK)
        return -1;

	// update the file position
	file->offset += len;

	// return the number of bytes read
	return len;
}

static int ICACHE_FLASH_ATTR find_file_and_insertion_point(const char *fileName, uint32_t *pFileOffset, uint32_t *pInsertionOffset)
{
    uint32_t p = fsData;
	char namebuf[256];
	RoFsHeader h;

    // assume file won't be found
    *pFileOffset = NOT_FOUND;

	// make sure there is a filesystem mounted
    if (fsData == BAD_FILESYSTEM_BASE) {
os_printf("find: filesystem not mounted\n");
		return -1;
	}

	// strip initial slashes
	while (fileName[0] == '/')
        fileName++;

	// find the file
	for (;;) {

		// read the next file header
		if (readFlash(p, &h, sizeof(RoFsHeader)) != SPI_FLASH_RESULT_OK) {
os_printf("find: %08lx error reading file header\n", p);
            return -1;
        }

        // check the magic number
		if (h.magic != ROFS_MAGIC) {
os_printf("find: %08lx bad magic number\n", p);
            return -1;
        }

		// check for the end of image marker
        if (h.flags & FLAG_LASTFILE) {
os_printf("find: %08lx insertion point\n", p);
            *pInsertionOffset = p;
            return 0;
        }

		// remove a leftover pending file
		else if (h.flags & FLAG_PENDING) {
		    uint32_t pending = p;
os_printf("find: remove a leftover pending file\n");
		    
		    // move ahead to next sector boundary
		    p = (p + SPI_FLASH_SEC_SIZE - 1) & (SPI_FLASH_SEC_SIZE - 1);
		    
		    // convert the pending file header to deleted
		    h.flags &= ~FLAG_PENDING;
		    h.nameLen = 0;
		    h.fileLenComp = p - pending - sizeof(RoFsHeader);
		    if (updateFlash(pending, (uint32_t *)&h, sizeof(RoFsHeader)) != SPI_FLASH_RESULT_OK) {
os_printf("find: error updating pending file header\n");
                return -1;
		    }
		    
		    // write a new terminator
		    memset(&h, 0xff, sizeof(RoFsHeader));
		    h.magic = ROFS_MAGIC;
		    if (writeFlash(p, (uint32_t *)&h, sizeof(RoFsHeader)) != SPI_FLASH_RESULT_OK) {
os_printf("find: error writing terminator\n");
		    }
		    
		    *pInsertionOffset = p;
		    return 0;
		}
		
		// only check active files
        else if (h.flags & FLAG_ACTIVE) {

            // get the name of the file
		    if (readFlash(p + sizeof(RoFsHeader), namebuf, sizeof(namebuf)) != SPI_FLASH_RESULT_OK) {
os_printf("find: %08lx error reading file name\n", p);
                return -1;
            }

os_printf("find: %08lx checking '%s'\n", p, namebuf);
		    // check to see if this is the file we're looking for
            if (os_strcmp(namebuf, fileName) == 0)
                *pFileOffset = p;
        }
        
        // deleted file
        else {
os_printf("find: %08lx skipping deleted file\n", p);
        }

		// skip over the file data
		p += sizeof(RoFsHeader) + h.nameLen + h.fileLenComp;

		// align to next 32 bit offset
        p = (p + 3) & ~3;
	}

    // never reached
os_printf("find: internal error\n");
    return -1;
}

ROFFS_FILE ICACHE_FLASH_ATTR *roffs_create(const char *fileName)
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
        if (readFlash(fileOffset, (uint32 *)&h, sizeof(RoFsHeader)) != SPI_FLASH_RESULT_OK) {
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
	h.fileLenComp = 0xffffffff;
	h.fileLenDecomp = 0xffffffff;

    file->header = insertionOffset;
    file->start = insertionOffset + sizeof(RoFsHeader) + h.nameLen;
    file->offset = 0;
    file->size = 0;
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
    int roundedLen = (len + 3) & ~3;
    if (writeFlash(file->start + file->size, (uint32 *)buf, roundedLen) != SPI_FLASH_RESULT_OK) {
os_printf("write: error writing to file\n");
        return -1;
    }
    file->offset += len;
    file->size += len;
    return len;
}

static int ICACHE_FLASH_ATTR readFlash(uint32_t addr, void *buf, int size)
{
    size = (size + 3) & ~3;
    return spi_flash_read(addr, buf, size);
}

static int ICACHE_FLASH_ATTR writeFlash(uint32_t addr, void *buf, int size)
{
    uint32_t sectorMask = SPI_FLASH_SEC_SIZE - 1;
    uint32_t sectorAddr = addr & ~sectorMask;
    uint8_t *p = buf;
os_printf("writeFlash: %08lx %d\n", addr, size);

    // erase the sector if the write begins on a sector boundary
    if (addr == sectorAddr) {
os_printf("writeFlash: erase %08lx\n", sectorAddr);
        if (spi_flash_erase_sector(sectorAddr / SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK) {
os_printf("writeFlash: erase failed\n");
            return SPI_FLASH_RESULT_ERR;
        }
    }
    
    // write each sector or partial sector
    while (sectorAddr + SPI_FLASH_SEC_SIZE < addr + size) {
        int writeSize = sectorAddr + SPI_FLASH_SEC_SIZE - addr;
        
        // write the next sector or partial sector
os_printf("writeFlash: write %08lx %d\n", addr, writeSize);
        if (spi_flash_write(addr, (uint32 *)p, writeSize) != SPI_FLASH_RESULT_OK) {
os_printf("writeFlash: write failed\n");
            return SPI_FLASH_RESULT_ERR;
        }
        
        // move ahead to the next sector
        addr += writeSize;
        size -= writeSize;
        p += writeSize;
        
        // erase the next sector
        sectorAddr = addr & ~sectorMask;
os_printf("writeFlash: erase %08lx\n", sectorAddr);
        if (spi_flash_erase_sector(sectorAddr / SPI_FLASH_SEC_SIZE) != SPI_FLASH_RESULT_OK) {
os_printf("writeFlash: erase failed\n");
            return SPI_FLASH_RESULT_ERR;
        }
    }
    
    // write the last partial sector
    if (size > 0) {
os_printf("writeFlash: write %08lx %d\n", addr, size);
        if (spi_flash_write(addr, (uint32 *)p, size) != SPI_FLASH_RESULT_OK) {
os_printf("writeFlash: write failed\n");
            return SPI_FLASH_RESULT_ERR;
        }
    }
    
    return SPI_FLASH_RESULT_OK;
}

static int ICACHE_FLASH_ATTR updateFlash(uint32_t addr, void *buf, int size)
{
os_printf("updateFlash: %08lx %d\n", addr, size);
    if (spi_flash_write(addr, (uint32 *)buf, size) != SPI_FLASH_RESULT_OK) {
os_printf("updateFlash: failed\n");
        return SPI_FLASH_RESULT_ERR;
    }
    return SPI_FLASH_RESULT_OK;
}

#endif

