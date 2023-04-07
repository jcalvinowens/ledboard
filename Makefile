OBJCOPY = arm-none-eabi-objcopy
CC = arm-none-eabi-gcc

INCLUDES = -I./include

CFLAGS = -Wall -Wextra -Wdeclaration-after-statement -Wstrict-prototypes \
	-g -Os -fno-strict-aliasing -nostdlib -mcpu=cortex-m0 -mthumb \
	-march=armv6-m -mlittle-endian -DSTM32F030 -DNR_ROWS=6 -DNR_COLS=64 \
	-DSYSTEM_CLOCK_FREQUENCY=24000000 -DSCREEN_REFRESH_HZ=1250

LDFLAGS = -Tlink.ld

binary = ledboard.bin
elfout = ledboard.elf
obj = firmware.o start.o
asm = $(obj:.o=.s)

all: $(binary)

disasm: $(asm)
disasm: CFLAGS += -fverbose-asm

$(binary): $(elfout)
	$(OBJCOPY) -O binary $< $@

$(elfout): $(obj)
	$(CC) $(CFLAGS) $(LDFLAGS) $(obj) -o $@

%.o: %.c
	$(CC) $< $(CFLAGS) $(INCLUDES) -c -o $@

%.o: %.s
	$(CC) $< $(CFLAGS) $(INCLUDES) -c -o $@

%.s: %.c
	$(CC) $< $(CFLAGS) $(INCLUDES) -c -S -o $@

clean:
	rm -f firmware.s firmware.o start.o ledboard.bin ledboard.elf
