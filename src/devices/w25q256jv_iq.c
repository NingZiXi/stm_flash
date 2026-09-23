/** @file w25q256jv_iq.c
 *  @brief W25Q256JV_IQ 器件参数；共享通用 NOR 行为。
 */
#include "stm_flash_device.h"
static const flash_nor_profile_t profile = {
    .address_bytes = 4U,
    .read_single = 0x13U,
    .read_quad = 0x6CU,
    .quad_dummy = 8U,
    .program = 0x12U,
    .erase = 0x21U,
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
    .suspend_mask = 0x80U,
    .protected_masks = {0x3CU, 0x40U, 0x04U},
};
const flash_device_t flash_device_w25q256jv_iq = {
    .name = "W25Q256JV_IQ",
    .jedec_id = 0xEF4019U,
    .jedec_mask = 0xFFFFFFU,
    .size_bytes = 32UL * 1024UL * 1024UL,
    .page_size = 256U,
    .erase_size = 4096U,
    .capabilities = FLASH_CAP_READ_SINGLE | FLASH_CAP_READ_QUAD | FLASH_CAP_PROGRAM | FLASH_CAP_ERASE,
    .max_clock_hz = 50000000U,
    .program_timeout_ms = 10U,
    .erase_timeout_ms = 1000U,
    .ops = &flash_nor_ops,
    .nor = &profile,
};
