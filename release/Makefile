IMAGES=\
release/boot_v1.5.bin \
release/user1.bin \
release/user2.bin \
release/blank.bin

FLASH_BLOCK_SIZE=1024

# this rule doesn't seem to work but running build followed by zip does
all:
	$(MAKE) build
	$(MAKE) zip

zip:	padded
	cd release; zip ../esp-link *

build:
	$(MAKE) -C .. STA_SSID= STA_PASS= clean
	$(MAKE) -C .. STA_SSID= STA_PASS=

padded:	release $(IMAGES)
	cp flash.sh release

release/%:	../firmware/% pad
	./pad $(FLASH_BLOCK_SIZE) <$< >$@

release/%:	../../esp_iot_sdk_v1.5.2/bin/% pad
	./pad $(FLASH_BLOCK_SIZE) <$< >$@

release:
	mkdir -p release

pad:	pad.c
	cc -o pad pad.c

clean:
	rm -rf release pad esp-link.zip
