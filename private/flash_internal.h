/**
 * @file    flash_internal.h
 * @brief   NOR 核心、器件描述与平台后端内部契约
 */
#ifndef FLASH_INTERNAL_H
#define FLASH_INTERNAL_H
#include "stm_flash.h"
typedef struct {
    uint8_t opcode, addressed, address_bytes, data_lines, dummy_cycles;
    uint32_t address, count;
} flash_command_t;

typedef struct {
    stm_err_t (*validate)(void *hal, uint32_t *clock_hz);
    stm_err_t (*geometry)(void *hal, uint32_t size_bytes, uint32_t max_clock_hz);
    const void *(*identity)(void *hal);
    HAL_StatusTypeDef (*command)(void *hal, const flash_command_t *cmd, uint32_t timeout);
    HAL_StatusTypeDef (*receive)(void *hal, uint8_t *data, uint32_t timeout);
    HAL_StatusTypeDef (*transmit)(void *hal, const uint8_t *data, uint32_t timeout);
} flash_bus_ops_t;
extern const flash_bus_ops_t flash_ospi_ops;

typedef struct {
    flash_chip_t chip;
    uint32_t jedec_id, size_bytes, page_size, erase_size, capabilities, max_clock_hz;
    uint32_t program_timeout_ms, erase_timeout_ms;
    uint8_t address_bytes, read_single, read_quad, quad_dummy, program, erase, write_enable;
    uint8_t status_commands[3], status_count;
    uint8_t busy_index, busy_mask, wel_index, wel_mask;
    uint8_t qe_index, qe_mask, suspend_index, suspend_mask;
    uint8_t protected_masks[3];
} flash_chip_desc_t;
const flash_chip_desc_t *flash_chip_find(uint32_t jedec_id, flash_chip_t requested);
void flash_chip_decode_status(const flash_chip_desc_t *chip, const uint8_t raw[3], flash_status_t *status);
#endif
