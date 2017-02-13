## CONFIG

NAME=bootloader

CORTEX_M=4
CHIP_FAMILY=sam4l
PACKAGE=64
#DEBUG=true

MODULES=tc usart flash wdt


# Include the main lib makefile
ROOTDIR=../..
LDSCRIPTNAME=bootloader.ld
ADD_CXXFLAGS=-fno-exceptions
include ../Makefile
