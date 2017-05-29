# Library
ifndef ROOTDIR
	ROOTDIR=.
endif
ifndef MODULES
	MODULES=adc dac eic gloc i2c spi tc trng usart wdt
endif
ifndef UTILS_MODULES
	UTILS_MODULES=RingBuffer
endif
LIBNAME=libtungsten
CORE_MODULES=pins_$(CHIP_FAMILY)_$(PACKAGE) core dma flash scif bscif pm bpm ast gpio usb error
LIB_MODULES=$(CORE_MODULES) $(MODULES)
LIB_OBJS=$(addprefix $(ROOTDIR)/$(LIBNAME)/$(CHIP_FAMILY)/,$(addsuffix .o,$(LIB_MODULES)))
UTILS_OBJS=$(addprefix $(ROOTDIR)/$(LIBNAME)/utils/,$(addsuffix .o,$(UTILS_MODULES)))
EXT_OBJS=$(addsuffix .o,$(EXT_MODULES))

# Default options
ifndef CORTEX_M
	CORTEX_M=4
endif
ifndef CHIP_FAMILY
	CHIP_FAMILY=sam4l
endif
ifndef PACKAGE
	PACKAGE=64
endif
ifndef OPENOCD_CFG
	OPENOCD_CFG=$(ROOTDIR)/$(LIBNAME)/openocd.cfg
endif

# Serial port
ifndef SERIAL_PORT
	SERIAL_PORT=/dev/ttyACM0
endif

# Carbide
ifdef CARBIDE
	LIB_OBJS+=libtungsten/carbide.o
	PACKAGE=64
endif

# Compiler & linker
CXX=$(TOOLCHAIN_PATH)arm-none-eabi-g++
OBJCOPY=$(TOOLCHAIN_PATH)arm-none-eabi-objcopy
GDB=$(TOOLCHAIN_PATH)arm-none-eabi-gdb
SIZE=$(TOOLCHAIN_PATH)arm-none-eabi-size
OPENOCD=openocd

# Options for specific architecture
ARCH_FLAGS=-mthumb -mcpu=cortex-m$(CORTEX_M)

# Startup code
STARTUP=$(ROOTDIR)/$(LIBNAME)/startup.cpp

# Defines passed to the preprocessor using -D
ifdef BOOTLOADER
	PREPROC_DEFINE_BOOTLOADER=-DBOOTLOADER=$(BOOTLOADER)
endif
ifdef DEBUG
	PREPROC_DEFINE_DEBUG=-DDEBUG=$(DEBUG)
endif
PREPROC_DEFINES=-DPACKAGE=$(PACKAGE) $(PREPROC_DEFINE_BOOTLOADER) $(PREPROC_DEFINE_DEBUG)

# Compilation flags
# Note : do not use -O0, this might generate code too slow for some peripherals (notably the SPI controller)
ifdef DEBUG
	OPTFLAGS=-Og -g
else
	OPTFLAGS=-Os -flto -ffunction-sections -fdata-sections
endif
CXXFLAGS=$(ARCH_FLAGS) $(STARTUP_DEFS) \
	-I$(ROOTDIR)/$(LIBNAME) \
	-I$(ROOTDIR)/$(LIBNAME)/$(CHIP_FAMILY) \
	-I$(ROOTDIR)/$(LIBNAME)/utils \
	-std=c++11 -Wall $(OPTFLAGS) $(PREPROC_DEFINES) $(ADD_CXXFLAGS)

# Linking flags
ifndef LDSCRIPTNAME
	LDSCRIPTNAME=usercode.ld
endif
ifdef BOOTLOADER
	LDSCRIPTNAME=usercode_bootloader.ld
endif
LDSCRIPTS=-L$(ROOTDIR)/$(LIBNAME)/$(CHIP_FAMILY) -L$(ROOTDIR)/$(LIBNAME) -L. -T $(LDSCRIPTNAME)
ifdef CREATE_MAP
	MAP=-Wl,-Map=$(NAME).map
endif
LFLAGS=--specs=nano.specs --specs=nosys.specs $(LDSCRIPTS) -Wl,--gc-sections $(MAP)


### RULES

.PHONY: flash pause reset debug flash-debug codeuploader upload bootloader flash-bootloader debug-bootloader openocd clean clean-all _echo_comp_lib_objs _echo_comp_ext_objs

## Usercode-related rules

# Default rule, compile the firmware in Intel HEX format, ready to be flashed/uploaded
# https://en.wikipedia.org/wiki/Intel_HEX
all: $(NAME).hex
	@echo ""
	@echo "Binary size : " `$(SIZE) -d $(NAME).elf | tail -n 1 | cut -f 4` " bytes"
	@echo "== Compiled successfully!"

_echo_comp_lib_objs:
	@echo ""
	@echo "== Compiling library modules..."

_echo_comp_ext_objs:
	@echo ""
	@echo "== Compiling external modules..."

# Compile user code in the standard ELF format
$(NAME).elf: $(NAME).cpp $(STARTUP) $(STARTUP_DEP) _echo_comp_lib_objs $(LIB_OBJS) $(UTILS_OBJS) _echo_comp_ext_objs $(EXT_OBJS)
	@echo ""
	@echo "== Compiling ELF..."
	$(CXX) $(CXXFLAGS) $(LFLAGS) $(NAME).cpp $(STARTUP) $(STARTUP_DEP) $(LIB_OBJS) $(UTILS_OBJS) $(EXT_OBJS) -o $@

# Convert from ELF to iHEX format
$(NAME).hex: $(NAME).elf
	@echo ""
	@echo "== Converting ELF to Intel HEX"
	$(OBJCOPY) -O ihex $^ $@

# Compile library
$(ROOTDIR)/$(LIBNAME)/$(CHIP_FAMILY)/%.o: $(ROOTDIR)/$(LIBNAME)/$(CHIP_FAMILY)/%.cpp $(ROOTDIR)/$(LIBNAME)/$(CHIP_FAMILY)/%.h
	$(CXX) $(CXXFLAGS) $(LFLAGS) -c $< -o $@

# Compile other modules
%.o: %.cpp %.h
	$(CXX) $(CXXFLAGS) $(LFLAGS) -c $< -o $@

# Start OpenOCD, which is used to reset/flash the chip and as a remote target for GDB
openocd:
	$(OPENOCD) -f $(OPENOCD_CFG)

# Flash the firmware into the chip using OpenOCD
flash: $(NAME).hex
	@echo ""
	@echo "== Flashing into chip (make sure OpenOCD is started in background using 'make openocd')"
	echo "reset halt; flash write_image erase unlock $(NAME).hex; reset run; exit" | netcat localhost 4444

# Erase the chip's flash using OpenOCD
erase:
	@echo "== Erasing the chip's flash..."
	echo "reset halt; flash erase_sector 0 0 last; exit" | netcat localhost 4444

# Pause the chip execution using OpenOCD
pause:
	@echo "== Halting the chip execution..."
	echo "reset halt; exit" | netcat localhost 4444

# Reset the chip using OpenOCD
reset:
	@echo "== Resetting the chip..."
	echo "reset run; exit" | netcat localhost 4444

# Open GDB through OpenOCD to debug the firmware
debug: pause
	$(GDB) -ex "set print pretty on" -ex "target extended-remote localhost:3333" $(NAME).elf

flash-debug: flash debug

## Codeuploader-related rules

# Compile the codeuploader
codeuploader:
	make -C $(ROOTDIR)/$(LIBNAME)/codeuploader

# Upload user code via bootloader with the default channel : USB
upload: upload-usb

# Upload through USB
upload-usb: $(NAME).hex codeuploader
	$(ROOTDIR)/$(LIBNAME)/codeuploader/codeuploader $(NAME).hex

# Upload through serial port
upload-serial: $(NAME).hex codeuploader
	$(ROOTDIR)/$(LIBNAME)/codeuploader/codeuploader $(NAME).hex $(SERIAL_PORT)


## Bootloader-related rules

# Compile the bootloader
bootloader:
	make -C $(ROOTDIR)/$(LIBNAME)/bootloader
	@echo "Bootloader size : " `$(SIZE) -d libtungsten/bootloader/bootloader.elf | tail -n 1 | cut -f 4` " bytes"

# Flash the bootloader into the chip using OpenOCD
flash-bootloader: bootloader
	echo "reset halt; flash write_image erase unlock $(ROOTDIR)/$(LIBNAME)/bootloader/bootloader.hex; reset run; exit" | netcat localhost 4444

# Debug the bootloader
debug-bootloader: pause
	$(GDB) -ex "set print pretty on" -ex "target extended-remote localhost:3333" $(ROOTDIR)/$(LIBNAME)/bootloader/bootloader.elf

## Cleaning rules

clean: clean-all

clean-all: 
	rm -f $(NAME).elf $(NAME).map $(NAME).hex *.o $(ROOTDIR)/$(LIBNAME)/*.o $(ROOTDIR)/$(LIBNAME)/utils/*.o $(ROOTDIR)/$(LIBNAME)/$(CHIP_FAMILY)/*.o
	cd $(ROOTDIR)/$(LIBNAME)/bootloader; rm -f bootloader.elf bootloader.hex *.o
	cd $(ROOTDIR)/$(LIBNAME)/codeuploader; rm -f codeuploader *.o
