/**
 * @file gd25q128.c
 * @brief GD25Q128C/E 的保守单线公共子集，三字节寻址。
 */
#include "stm_flash_device.h"

static const flash_nor_profile_t profile = {
    .address_bytes = 3U,
    .read_single = 0x03U,
    .program = 0x02U,
    .erase = 0x20U,
    .write_enable = 0x06U,
    .status_commands = {0x05U, 0x35U, 0x15U},
    .status_count = 3U,
    .busy_index = 0U,
    .busy_mask = 1U,
    .wel_index = 0U,
    .wel_mask = 2U,
    .qe_index = 1U,
    .qe_mask = 2U,
    .suspend_index = 1U,
    .suspend_mask = 0x84U,
    // BP4:0、CMP 或 C 型号 WPS 生效时拒绝全部擦写，不自动解除保护。
    .protected_masks = {0x7CU, 0x40U, 0x04U},
};

const flash_device_t flash_device_gd25q128 = {
    .name = "GD25Q128C/E (single SPI)",
    .jedec_id = 0xC84018U,
    .jedec_mask = 0xFFFFFFU,
    .size_bytes = 16UL * 1024UL * 1024UL,
    .page_size = 256U,
    .erase_size = 4096U,
    .capabilities = FLASH_CAP_READ_SINGLE | FLASH_CAP_PROGRAM | FLASH_CAP_ERASE,
    .max_clock_hz = 50000000U,
    .program_timeout_ms = 10U,
    .erase_timeout_ms = 1000U,
    .ops = &flash_nor_ops,
    .nor = &profile,
};
