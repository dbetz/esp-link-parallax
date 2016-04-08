GZIP_COMPRESSION ?= yes

ifeq ($(CROSS),win32)
PREFIX=i586-mingw32msvc-
CFLAGS=-D__MINGW32__
OS=Windows_NT
endif

ifeq ($(OS),Windows_NT)

TARGET = mkespfsimage.exe

CC = $(PREFIX)gcc
LD = $(CC)
CFLAGS=-c -I../../espfs
# -Imman-win32 -std=gnu99
# LDFLAGS=-Lmman-win32 -lmman 

ifeq ("$(GZIP_COMPRESSION)","yes")
CFLAGS += -DESPFS_GZIP
LDFLAGS += -lz
endif

OBJECTS = mkespfsimage.o

#all: libmman $(TARGET)
all: $(TARGET)

libmman:
	$(Q) make -C mman-win32

$(TARGET): $(OBJECTS)
	$(LD) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(OBJECTS) $(TARGET) ./mman-win32/libmman.a ./mman-win32/mman.o

.PHONY: all clean

else

CFLAGS=-I../../espfs -std=gnu99
ifeq ("$(GZIP_COMPRESSION)","yes")
CFLAGS		+= -DESPFS_GZIP
endif

OBJS=mkespfsimage.o
TARGET=mkespfsimage

$(TARGET): $(OBJS)
ifeq ("$(GZIP_COMPRESSION)","yes")
	$(CC) -o $@ $^ -lz
else
	$(CC) -o $@ $^
endif

clean:
	rm -f $(TARGET) $(OBJS)

endif
