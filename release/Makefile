IMAGES=\
release/boot_v1.5.bin \
release/user1.bin \
release/user2.bin \
release/blank.bin

CP=cp

# this rule doesn't seem to work but running build followed by zip does
all:
	$(MAKE) build
	$(MAKE) zip

zip:	padded
	cd release; zip ../esp-link *

build:
	$(MAKE) -C .. clean
	$(MAKE) -C .. STA_SSID= STA_PASS=

padded:	release $(IMAGES)
	cp flash.sh release

release/%:	../firmware/%
	$(CP) $< $@

release/boot_v1.5.bin:	../../esp_iot_sdk_v1.5.2/bin/boot_v1.5.bin patch
	./patch $< $@

release/blank.bin:	../../esp_iot_sdk_v1.5.2/bin/blank.bin
	$(CP) $< $@

release:
	mkdir -p release

patch:	patch.c
	cc -o $@ $<

clean:
	rm -rf release patch esp-link.zip
