######################################################################
#  Project Makefile
######################################################################

BINARY		= main
SRCFILES	= main.c \
		  power.c \
		  address.c \
		  blink.c \
		  matrix.c \
		  trace.c \
		  canbus.c \
		  canservices.c \
		  serial.c \
		  i2c.c \
		  rtc.c \
		  backup.c \
		  rtos/heap_4.c \
		  rtos/list.c \
		  rtos/port.c \
		  rtos/queue.c \
		  rtos/tasks.c \
		  rtos/opencm3.c \
		  rtos/stream_buffer.c \
		  libwwg/miniprintf.c
LDSCRIPT	= stm32f103c8t6.ld
CFLAGS		+= -Ilibwwg
CFLAGS		+= -I../../bramble/src/libbramble

# Enable stack protection and FreeRTOS assertions
#CFLAGS	+= -DDEBUG_FIRMWARE

# DEPS		= 	# Any additional dependencies for your build
# CLOBBER	+= 	# Any additional files to be removed with "make clobber"

include ../Makefile.incl
include ../Makefile.rtos

######################################################################
#  NOTES:
#	1. remove any modules you don't need from SRCFILES
#	2. "make clean" will remove *.o etc., but leaves *.elf, *.bin
#	3. "make clobber" will "clean" and remove *.elf, *.bin etc.
#	4. "make flash" will perform:
#	   st-flash write main.bin 0x8000000
######################################################################

OPENOCD = openocd
OPENOCD_CFG = openocd_pi.cfg
#OPENOCD_CFG = openocd_stlink.cfg

flash:
	sudo $(OPENOCD) -f $(OPENOCD_CFG) \
	    -c "program main.elf verify reset exit"
reset:
	sudo $(OPENOCD) -f $(OPENOCD_CFG) \
	    -c "init" -c "targets" -c "reset run" -c "exit"
gdbserver:
	sudo $(OPENOCD) -f $(OPENOCD_CFG) \
	    -c "init" -c "targets" -c "reset halt"
debug:
	$(GDB) -ex "file main.elf" -ex "target extended-remote :3333"
