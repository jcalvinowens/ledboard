#!/bin/bash -e

echo "Flashing: $(sha256sum ledboard.bin)"
openocd -f openocd.cfg -c 'stm32f0xx_flash ledboard.bin'
