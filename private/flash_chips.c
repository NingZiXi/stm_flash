/**
 * @file    flash_chips.c
 * @brief   已适配 NOR 的参数和寄存器布局；新增型号需单独验证
 */
#include "flash_internal.h"

static const flash_chip_desc_t chips[] = {
    {
        .chip = FLASH_CHIP_W25Q256JV_IQ, .jedec_id = 0xEF4019U,
        .size_bytes = 32UL * 1024UL * 1024UL, .page_size = 256U, .erase_size = 4096U,
        .capabilities = FLASH_CAP_READ_SINGLE | FLASH_CAP_READ_QUAD | FLASH_CAP_PROGRAM | FLASH_CAP_ERASE,
        .max_clock_hz = 50000000U, .program_timeout_ms = 10U, .erase_timeout_ms = 1000U,
        .address_bytes = 4U, .read_single = 0x13U, .read_quad = 0x6CU, .quad_dummy = 8U,
        .program = 0x12U, .erase = 0x21U, .write_enable = 0x06U,
        .status_commands = {0x05U, 0x35U, 0x15U}, .status_count = 3U,
        .busy_index = 0U, .busy_mask = 1U, .wel_index = 0U, .wel_mask = 2U,
        .qe_index = 1U, .qe_mask = 2U, .suspend_index = 1U, .suspend_mask = 0x80U,
        .protected_masks = {0x3CU, 0x40U, 0x04U},
    },
};

const flash_chip_desc_t *flash_chip_find(uint32_t jedec_id, flash_chip_t requested)
{
    for (size_t i = 0U; i < sizeof(chips) / sizeof(chips[0]); ++i) {
        if (chips[i].jedec_id == jedec_id &&
            (requested == FLASH_CHIP_AUTO || requested == chips[i].chip)) { return &chips[i]; }
    }
    return NULL;
}

void flash_chip_decode_status(const flash_chip_desc_t *chip, const uint8_t raw[3], flash_status_t *status)
{
    status->valid_mask = FLASH_STATUS_BUSY | FLASH_STATUS_WRITE_ENABLED;
    status->flags = 0U;
    if (raw[chip->busy_index] & chip->busy_mask) { status->flags |= FLASH_STATUS_BUSY; }
    if (raw[chip->wel_index] & chip->wel_mask) { status->flags |= FLASH_STATUS_WRITE_ENABLED; }
    if (chip->qe_mask) {
        status->valid_mask |= FLASH_STATUS_QUAD_ENABLED;
        if (raw[chip->qe_index] & chip->qe_mask) { status->flags |= FLASH_STATUS_QUAD_ENABLED; }
    }
    if (chip->suspend_mask) {
        status->valid_mask |= FLASH_STATUS_SUSPENDED;
        if (raw[chip->suspend_index] & chip->suspend_mask) { status->flags |= FLASH_STATUS_SUSPENDED; }
    }
    for (unsigned i = 0U; i < chip->status_count; ++i) {
        if (chip->protected_masks[i]) { status->valid_mask |= FLASH_STATUS_PROTECTED; }
        if (raw[i] & chip->protected_masks[i]) { status->flags |= FLASH_STATUS_PROTECTED; }
    }
}
