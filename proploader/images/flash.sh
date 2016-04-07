esptool -v -cp /dev/ttyUSB0 -cd none -cb 115200 -bm qio -bf 80 -bz 4M -ca 0x100000 -cf out.espfs
