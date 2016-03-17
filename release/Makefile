IMAGES=\
release/boot_v1.5.bin \
release/user1.bin \
release/blank.bin

FLASH_BLOCK_SIZE=1024

all:	padded
	cd release; zip ../esp-link *

padded:	release $(IMAGES)
	cp flash.sh release

release/%:	../../esp_iot_sdk_v1.5.2/bin/% pad
	./pad $(FLASH_BLOCK_SIZE) <$< >$@

release/%:	../firmware/% pad
	./pad $(FLASH_BLOCK_SIZE) <$< >$@

release:
	mkdir -p release

pad:	pad.c
	cc -o pad pad.c

clean:
	rm -rf release pad esp-link.zip
