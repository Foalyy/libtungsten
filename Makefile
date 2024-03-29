# Default config
ifndef NAME
	NAME=main
endif
ifndef ROOTDIR
	ROOTDIR=.
endif
ifndef CORTEX_M
	CORTEX_M=4
endif
ifndef CHIP_FAMILY
	CHIP_FAMILY=sam4l
endif
ifndef CHIP_MODEL
	# ls2x, ls4x or ls8x, indicates the quantity of memory (see datasheet 2.2 Configuration Summary)
	CHIP_MODEL=ls4x
endif
ifndef PACKAGE
	PACKAGE=64
endif
ifndef BUILD_PREFIX
	BUILD_PREFIX=$(NAME)
endif
ifndef BUILD_PATH
	BUILD_PATH=$(ROOTDIR)/build
endif
ifndef OPENOCD_CFG
	OPENOCD_CFG=$(ROOTDIR)/$(LIBNAME)/$(CHIP_FAMILY)/openocd.cfg
endif
ifndef JTAG_ADAPTER
	JTAG_ADAPTER=cmsis-dap
endif
ifndef CARBIDE
	CARBIDE=false
endif
ifndef DEBUG
	DEBUG=false
endif
ifndef BOOTLOADER
	BOOTLOADER=false
endif
ifndef CUSTOM_BOOTLOADER
	CUSTOM_BOOTLOADER=false
endif
ifndef CUSTOM_CODEUPLOADER
	CUSTOM_CODEUPLOADER=false
endif
ifndef CREATE_MAP
	CREATE_MAP=false
endif
ifndef SERIAL_PORT
	SERIAL_PORT=/dev/ttyACM0
endif

# Library
LIBNAME=libtungsten
CORE_MODULES=pins_$(CHIP_FAMILY)_$(PACKAGE) ast bpm bscif core dma error flash gpio interrupt_priorities pm scif usb wdt
LIB_MODULES=$(CORE_MODULES) $(MODULES)

# Compilation objects
LIB_OBJS=$(addprefix $(BUILD_PATH)/$(BUILD_PREFIX)/$(LIBNAME)/$(CHIP_FAMILY)/,$(addsuffix .o,$(LIB_MODULES)))
UTILS_OBJS=$(addprefix $(BUILD_PATH)/$(BUILD_PREFIX)/$(LIBNAME)/utils/,$(addsuffix .o,$(UTILS_MODULES)))
USER_OBJS=$(addprefix $(BUILD_PATH)/$(BUILD_PREFIX)/,$(addsuffix .o,$(USER_MODULES)))

# Carbide-specific options
ifeq ($(strip $(CARBIDE)), true)
	LIB_OBJS+=libtungsten/carbide/carbide.o
	PACKAGE=64
endif

# Toolchain
CXX=$(TOOLCHAIN_PATH)arm-none-eabi-g++
CC=$(TOOLCHAIN_PATH)arm-none-eabi-gcc
OBJCOPY=$(TOOLCHAIN_PATH)arm-none-eabi-objcopy
GDB=$(TOOLCHAIN_PATH)arm-none-eabi-gdb
SIZE=$(TOOLCHAIN_PATH)arm-none-eabi-size
OBJDUMP=$(TOOLCHAIN_PATH)arm-none-eabi-objdump
OPENOCD=openocd
OPENOCD_PORT=4444
NETCAT=netcat

# Architecture options
ARCH_FLAGS=-mthumb -mcpu=cortex-m$(CORTEX_M)

# Startup code
STARTUP=$(ROOTDIR)/$(LIBNAME)/$(CHIP_FAMILY)/startup.cpp

# Condition-compiled USART ports
USE_USARTS=
ifeq ($(strip $(USE_USART0)), true)
	USE_USARTS+=-DUSE_USART0
endif
ifeq ($(strip $(USE_USART1)), true)
	USE_USARTS+=-DUSE_USART1
endif
ifeq ($(strip $(USE_USART2)), true)
	USE_USARTS+=-DUSE_USART2
endif
ifeq ($(strip $(USE_USART3)), true)
	USE_USARTS+=-DUSE_USART3
endif

# Defines passed to the preprocessor using -D
ifeq ($(strip $(CHIP_MODEL)),ls2x)
	N_FLASH_PAGES=256
else ifeq ($(strip $(CHIP_MODEL)),ls4x)
	N_FLASH_PAGES=512
else ifeq ($(strip $(CHIP_MODEL)),ls8x)
	N_FLASH_PAGES=1024
else
$(error Unknown CHIP_MODEL $(CHIP_MODEL), please use ls2x, ls4x or ls8x)
endif
PREPROC_DEFINES=-DPACKAGE=$(PACKAGE) -DBOOTLOADER=$(BOOTLOADER) -DDEBUG=$(DEBUG) -DN_FLASH_PAGES=$(N_FLASH_PAGES) $(USE_USARTS) $(USER_DEFINES)

# Compilation flags
# Note : do not use -O0, as this might generate code too slow for some peripherals (notably the SPI controller)
ifeq ($(strip $(DEBUG)), true)
	OPTFLAGS=-Og -g
else
	OPTFLAGS=-Os -ffunction-sections -fdata-sections
endif
CXXFLAGS=$(ARCH_FLAGS) \
	-I$(ROOTDIR)/$(LIBNAME) \
	-I$(ROOTDIR)/$(LIBNAME)/$(CHIP_FAMILY) \
	-I$(ROOTDIR)/$(LIBNAME)/utils \
	-I$(ROOTDIR)/$(LIBNAME)/carbide \
	$(INCLUDE) \
	-std=c++11 -Wall $(OPTFLAGS) $(PREPROC_DEFINES) $(ADD_CXXFLAGS)
CFLAGS=$(ARCH_FLAGS) \
	-I$(ROOTDIR)/$(LIBNAME) \
	-I$(ROOTDIR)/$(LIBNAME)/$(CHIP_FAMILY) \
	-I$(ROOTDIR)/$(LIBNAME)/utils \
	-I$(ROOTDIR)/$(LIBNAME)/carbide \
	$(INCLUDE) \
	-std=c99 -Wall $(OPTFLAGS) $(PREPROC_DEFINES) $(ADD_CFLAGS)

# Linking flags
ifndef LD_SCRIPT_NAME
	ifeq ($(strip $(BOOTLOADER)), true)
		LD_SCRIPT_NAME=usercode_bootloader_$(CHIP_MODEL).ld
	else
		LD_SCRIPT_NAME=usercode_$(CHIP_MODEL).ld
	endif
endif
ifeq ($(strip $(CREATE_MAP)), true)
	MAP=-Wl,-Map=$(BUILD_PATH)/$(BUILD_PREFIX)/$(NAME).map
endif
LFLAGS=--specs=nano.specs --specs=nosys.specs -L. -L$(ROOTDIR)/$(LIBNAME) -L$(ROOTDIR)/$(LIBNAME)/$(CHIP_FAMILY) -L$(ROOTDIR)/$(LIBNAME)/carbide -L$(ROOTDIR)/$(LIBNAME)/$(CHIP_FAMILY)/ld_scripts -T $(LD_SCRIPT_NAME) -Wl,--gc-sections $(MAP)

# Custom bootloader
ifeq ($(strip $(CUSTOM_BOOTLOADER)), true)
	BOOTLOADER_ROOTDIR=$(ROOTDIR)/bootloader
else
	BOOTLOADER_ROOTDIR=$(ROOTDIR)/$(LIBNAME)/bootloader
endif

# Custom codeuploader
ifeq ($(strip $(CUSTOM_CODEUPLOADER)), true)
	CODEUPLOADER_ROOTDIR=$(ROOTDIR)/codeuploader
else
	CODEUPLOADER_ROOTDIR=$(ROOTDIR)/$(LIBNAME)/codeuploader
endif

OPENOCD_ARGS=-c "source [find interface/$(JTAG_ADAPTER).cfg]" -f $(OPENOCD_CFG)


### RULES

.PHONY: flash pause reset debug flash-debug objdump codeuploader upload bootloader flash-bootloader debug-bootloader objdump-bootloader openocd clean clean-all _create_build_path _echo_config _echo_comp_lib_objs _echo_comp_user_objs


## Usercode-related rules

# Default rule, compile the firmware in Intel HEX format, ready to be flashed/uploaded
# https://en.wikipedia.org/wiki/Intel_HEX
all: $(BUILD_PATH)/$(BUILD_PREFIX)/$(NAME).bin $(BUILD_PATH)/$(BUILD_PREFIX)/$(NAME).hex
	@echo ""
	@echo "== Finished compiling successfully!"

_create_build_path:
	@mkdir -p $(BUILD_PATH)/$(BUILD_PREFIX)

_echo_config:
	@echo ""
	@echo "== Configuration summary :"
	@echo ""
	@echo "    CARBIDE=$(CARBIDE)"
	@echo "    PACKAGE=$(PACKAGE)"
	@echo "    CHIP_MODEL=$(CHIP_MODEL)"
	@echo "    DEBUG=$(DEBUG)"
	@echo "    BOOTLOADER=$(BOOTLOADER)"
	@echo "    MODULES=$(MODULES)"
	@echo "    UTILS_MODULES=$(UTILS_MODULES)"
	@echo "    EXT_MODULES=$(EXT_MODULES)"
	@echo "    CREATE_MAP=$(CREATE_MAP)"
	@echo "    LD_SCRIPT_NAME=$(LD_SCRIPT_NAME)"
	@echo ""
	@echo "(if the configuration has changed since the last compilation, remember to perform 'make clean' first)"

_echo_comp_lib_objs:
	@echo ""
	@echo "== Compiling library modules..."

_echo_comp_user_objs:
	@echo ""
	@echo "== Compiling user-defined modules..."

# Compile user code in the standard ELF format
$(BUILD_PATH)/$(BUILD_PREFIX)/$(NAME).elf: _create_build_path _echo_config $(NAME).cpp $(STARTUP) $(STARTUP_DEP) _echo_comp_lib_objs $(LIB_OBJS) $(UTILS_OBJS) _echo_comp_user_objs $(USER_OBJS)
	@echo ""
	@echo "== Compiling ELF..."
	$(CXX) $(CXXFLAGS) $(LFLAGS) $(NAME).cpp $(STARTUP) $(STARTUP_DEP) $(LIB_OBJS) $(UTILS_OBJS) $(USER_OBJS) -o $@

# Convert from ELF to iHEX format
$(BUILD_PATH)/$(BUILD_PREFIX)/$(NAME).hex: $(BUILD_PATH)/$(BUILD_PREFIX)/$(NAME).elf $(BUILD_PATH)/$(BUILD_PREFIX)/$(NAME).bin
	@echo ""
	@echo "== Converting ELF to Intel HEX format"
	$(OBJCOPY) -O ihex $< $@
# 	@echo "Binary size :" `$(SIZE) -d $(NAME).elf | tail -n 1 | cut -f 4` "bytes"
	@echo "Binary size :" `ls -l $(BUILD_PATH)/$(BUILD_PREFIX)/$(NAME).bin | cut -d " " -f 5` "bytes"

# Convert from ELF to bin format
$(BUILD_PATH)/$(BUILD_PREFIX)/$(NAME).bin: $(BUILD_PATH)/$(BUILD_PREFIX)/$(NAME).elf
	$(OBJCOPY) -O binary $< $@

# Automatic generation of source dependencies
.SECONDEXPANSION:
$(BUILD_PATH)/$(BUILD_PREFIX)/%.d: $(ROOTDIR)/%.cpp $$(wildcard %.h)
	@echo "Updating dependencies of $<"
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) -MM -MT $(@:.d=.o) -c $< > $@
$(BUILD_PATH)/$(BUILD_PREFIX)/%.d: $(ROOTDIR)/%.c $$(wildcard %.h)
	@echo "Updating dependencies of $<"
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -MM -MT $(@:.d=.o) -c $< > $@
ifneq ($(MAKECMDGOALS),openocd)
ifneq ($(MAKECMDGOALS),erase)
ifneq ($(MAKECMDGOALS),pause)
ifneq ($(MAKECMDGOALS),reset)
ifneq ($(MAKECMDGOALS),debug)
ifneq ($(MAKECMDGOALS),objdump)
ifneq ($(MAKECMDGOALS),bootloader)
ifneq ($(MAKECMDGOALS),flash-bootloader)
ifneq ($(MAKECMDGOALS),autoflash-bootloader)
ifneq ($(MAKECMDGOALS),debug-bootloader)
ifneq ($(MAKECMDGOALS),debug-flash-bootloader)
ifneq ($(MAKECMDGOALS),clean)
include $(LIB_OBJS:.o=.d)
include $(UTILS_OBJS:.o=.d)
include $(USER_OBJS:.o=.d)
endif
endif
endif
endif
endif
endif
endif
endif
endif
endif
endif
endif

# Compile library
$(BUILD_PATH)/$(BUILD_PREFIX)/$(LIBNAME)/$(CHIP_FAMILY)/%.o: $(ROOTDIR)/$(LIBNAME)/$(CHIP_FAMILY)/%.cpp $(ROOTDIR)/$(LIBNAME)/$(CHIP_FAMILY)/pins_$(CHIP_FAMILY)_$(PACKAGE).cpp $(ROOTDIR)/$(LIBNAME)/$(CHIP_FAMILY)/interrupt_priorities.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LFLAGS) -c $< -o $@

# Compile other modules
$(BUILD_PATH)/$(BUILD_PREFIX)/%.o: $(ROOTDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(LFLAGS) -c $< -o $@

$(BUILD_PATH)/$(BUILD_PREFIX)/%.o: $(ROOTDIR)/%.c
	$(CC) $(CFLAGS) $(LFLAGS) -c $< -o $@

# Start OpenOCD, which is used to reset/flash the chip and as a remote target for GDB
openocd:
	$(OPENOCD) $(OPENOCD_ARGS)

# Flash the firmware into the chip using OpenOCD
flash: $(BUILD_PATH)/$(BUILD_PREFIX)/$(NAME).hex
	@echo ""
	@echo "== Flashing into chip (make sure OpenOCD is started in background using 'make openocd')"
	echo "reset halt; flash write_image erase unlock $(BUILD_PATH)/$(BUILD_PREFIX)/$(NAME).hex; reset run; exit" | $(NETCAT) localhost $(OPENOCD_PORT)

# Flash the firmware into the chip by automatically starting a temporary OpenOCD instance
autoflash: $(BUILD_PATH)/$(BUILD_PREFIX)/$(NAME).hex
	$(OPENOCD) $(OPENOCD_ARGS) & (sleep 1; echo "reset halt; flash write_image erase unlock $(BUILD_PATH)/$(BUILD_PREFIX)/$(NAME).hex; reset run; exit" | $(NETCAT) localhost $(OPENOCD_PORT)) > /dev/null; killall openocd

# Erase the chip's flash using OpenOCD
erase:
	@echo "== Erasing the chip's flash..."
	echo "reset halt; flash erase_sector 0 0 last; exit" | $(NETCAT) localhost $(OPENOCD_PORT)

# Pause the chip execution using OpenOCD
pause:
	@echo "== Halting the chip execution..."
	echo "reset halt; exit" | $(NETCAT) localhost $(OPENOCD_PORT)

# Reset the chip using OpenOCD
reset:
	@echo "== Resetting the chip..."
	echo "reset run; exit" | $(NETCAT) localhost $(OPENOCD_PORT)

# Open GDB through OpenOCD to debug the firmware
debug: pause
	$(GDB) -ex "set print pretty on" -ex "target extended-remote localhost:3333" $(BUILD_PATH)/$(BUILD_PREFIX)/$(NAME).elf

# Flash and debug the firmware
flash-debug: flash debug

# Show the disassembly of the compiled program
objdump: $(BUILD_PATH)/$(BUILD_PREFIX)/$(NAME).hex
	$(OBJDUMP) -dSC $(BUILD_PATH)/$(BUILD_PREFIX)/$(NAME).elf | less


## Codeuploader-related rules

# Compile the codeuploader
codeuploader:
	make -C $(CODEUPLOADER_ROOTDIR)

# Upload user code via bootloader with the default channel : USB
upload: upload-usb

# Upload through USB
upload-usb: $(BUILD_PATH)/$(BUILD_PREFIX)/$(NAME).hex codeuploader
	$(BUILD_PATH)/codeuploader/codeuploader $(BUILD_PATH)/$(BUILD_PREFIX)/$(NAME).hex

# Upload through serial port
upload-serial: $(BUILD_PATH)/$(BUILD_PREFIX)/$(NAME).hex codeuploader
	$(BUILD_PATH)/codeuploader/codeuploader $(BUILD_PATH)/$(BUILD_PREFIX)/$(NAME).hex $(SERIAL_PORT)


## Bootloader-related rules

# Compile the bootloader
bootloader: $(BOOTLOADER_ROOTDIR)/bootloader_config.h
	make -C $(BOOTLOADER_ROOTDIR)

# Flash the bootloader into the chip using OpenOCD
flash-bootloader: bootloader
	make -C $(BOOTLOADER_ROOTDIR) flash

# Flash the bootloader into the chip by automatically starting a temporary OpenOCD instance
autoflash-bootloader: bootloader
	make -C $(BOOTLOADER_ROOTDIR) autoflash

# Debug the bootloader
debug-bootloader: pause
	make -C $(BOOTLOADER_ROOTDIR) debug

# Flash and debug the bootloader
flash-debug-bootloader: flash-bootloader debug-bootloader

# Show the disassembly of the compiled bootloader
objdump-bootloader: bootloader
	make -C $(BOOTLOADER_ROOTDIR) objdump


## USBCom

usbcom_dump:
	make -C $(ROOTDIR)/$(LIBNAME)/usbcom usbcom_dump
	$(ROOTDIR)/$(LIBNAME)/usbcom/usbcom_dump

usbcom_write:
	make -C $(ROOTDIR)/$(LIBNAME)/usbcom usbcom_write
	$(ROOTDIR)/$(LIBNAME)/usbcom/usbcom_write


## Cleaning rules

# The 'clean' rule can be redefined in your Makefile to add your own logic, but remember :
# 1/ to define it AFTER the 'include libtungsten/Makefile' instruction
# 2/ to add the 'clean-all' dependency
clean: clean-all

clean-all:
	rm -r $(BUILD_PATH)
