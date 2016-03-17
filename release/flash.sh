PORT=/dev/ttyUSB0
BAUD=115200
BOARD=none # none, ck, nodemcu, wifio
FLASH_SIZE=1M
FLASH_BLOCK_SIZE=1024
FLASH_SPEED=80
FLASH_INTERFACE=qio

esptool \
-cp $PORT \
-cd $BOARD \
-cb $BAUD \
-bz $FLASH_SIZE \
-bf $FLASH_SPEED \
-bm $FLASH_INTERFACE \
-ca 0x00000 -cf boot_v1.5.bin \
-ca 0x01000 -cf user1.bin \
-ca 0xFE000 -cf blank.bin
