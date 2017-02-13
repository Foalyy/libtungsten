#include "flash.h"

namespace Flash {

    bool isReady() {
        return (*(volatile uint32_t*)(FLASH_BASE + OFFSET_FSR)) & (1 << FSR_FRDY);
    }

    uint32_t read(uint32_t address) {
        // Wait for the flash to be ready
        while (!isReady());

        // Return the word at the specified address
        return (*(volatile uint32_t*)(FLASH_ARRAY_BASE + address));
    }

    void readPage(int page, uint32_t data[]) {
        // Wait for the flash to be ready
        while (!isReady());

        // Copy the buffer to the page buffer
        for (int i = 0; i < FLASH_PAGE_SIZE_WORDS; i++) {
            data[i] = (*(volatile uint32_t*)(FLASH_ARRAY_BASE + page * FLASH_PAGE_SIZE_BYTES + i * 4));
        }
    }

    void erasePage(int page) {
        // Wait for the flash to be ready
        while (!isReady());

        // FCMD (Flash Command Register) : issue an Erase Page command
        (*(volatile uint32_t*)(FLASH_BASE + OFFSET_FCMD))
            = FCMD_CMD_EP << FCMD_CMD   // CMD : command code to issue (EP = Erase Page)
            | page << FCMD_PAGEN        // PAGEN : page number
            | FCMD_KEY;                 // KEY : write protection key
    }

    void clearPageBuffer() {
        // Wait for the flash to be ready
        while (!isReady());

        // FCMD (Flash Command Register) : issue a Clear Page Buffer command
        (*(volatile uint32_t*)(FLASH_BASE + OFFSET_FCMD))
            = FCMD_CMD_CPB << FCMD_CMD  // CMD : command code to issue (CPB = Clear Page Buffer)
            | FCMD_KEY;                 // KEY : write protection key
    }

    void writePage(int page, const uint32_t data[]) {
        // The flash technology only allows 1-to-0 transitions, so the 
        // page and the buffer must first be cleared (set to 1)
        erasePage(page);
        clearPageBuffer();

        // Wait for the flash to be ready
        while (!isReady());

        // Copy the buffer to the page buffer
        for (int i = 0; i < FLASH_PAGE_SIZE_WORDS; i++) {
            (*(volatile uint32_t*)(FLASH_ARRAY_BASE + page * FLASH_PAGE_SIZE_BYTES + i * 4)) = data[i];
        }

        // FCMD (Flash Command Register) : issue a Write Page command
        (*(volatile uint32_t*)(FLASH_BASE + OFFSET_FCMD))
            = FCMD_CMD_WP << FCMD_CMD   // CMD : command code to issue (WP = Write Page)
            | page << FCMD_PAGEN        // PAGEN : page number
            | FCMD_KEY;                 // KEY : write protection key
    }

    void readUserPage(uint32_t data[]) {
        // Wait for the flash to be ready
        while (!isReady());

        // Copy the buffer to the page buffer
        for (int i = 0; i < FLASH_PAGE_SIZE_WORDS; i++) {
            data[i] = (*(volatile uint32_t*)(USER_PAGE_BASE + i * 4));
        }
    }

    void eraseUserPage() {
        // Wait for the flash to be ready
        while (!isReady());

        // FCMD (Flash Command Register) : issue an Erase User Page command
        (*(volatile uint32_t*)(FLASH_BASE + OFFSET_FCMD))
            = FCMD_CMD_EUP << FCMD_CMD  // CMD : command code to issue (EUP = Erase User Page)
            | FCMD_KEY;                 // KEY : write protection key
    }

    void writeUserPage(const uint32_t data[]) {
        // The flash technology only allows 1-to-0 transitions, so the 
        // page and the buffer must first be cleared (set to 1)
        eraseUserPage();
        clearPageBuffer();

        // Wait for the flash to be ready
        while (!isReady());

        // Copy the buffer to the page buffer
        for (int i = 0; i < FLASH_PAGE_SIZE_WORDS; i++) {
            (*(volatile uint32_t*)(USER_PAGE_BASE + i * 4)) = data[i];
        }

        // FCMD (Flash Command Register) : issue a Write User Page command
        (*(volatile uint32_t*)(FLASH_BASE + OFFSET_FCMD))
            = FCMD_CMD_WUP << FCMD_CMD  // CMD : command code to issue (WUP = Write User Page)
            | FCMD_KEY;                 // KEY : write protection key
    }

}