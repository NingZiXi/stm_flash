/** @file flash_nor_common.c @brief 可复用的同步 NOR 指令、状态和等待操作。 */
#include "stm_flash.h"
#define IO_TIMEOUT_MS 100U
static stm_err_t transfer(const flash_chip_io_t *io, uint8_t op, int addressed, uint32_t offset,
                          const void *tx, void *rx, size_t size, int quad)
{
    const flash_nor_profile_t *p = io->device->nor;
    flash_transfer_t t = {.opcode = op,
                          .instruction_lines = 1U,
                          .address_lines = addressed ? 1U : 0U,
                          .address_bytes = addressed ? p->address_bytes : 0U,
                          .data_lines = size ? (quad ? 4U : 1U) : 0U,
                          .dummy_cycles = quad ? p->quad_dummy : 0U,
                          .address = offset,
                          .tx_data = tx,
                          .rx_data = rx,
                          .size_bytes = size};
    return io->host.ops->exec(io->host.ctx, &t, IO_TIMEOUT_MS);
}
stm_err_t flash_nor_get_status(const flash_chip_io_t *io, flash_status_t *status)
{
    const flash_nor_profile_t *p = io->device->nor;
    uint8_t raw[3] = {0};
    for (unsigned i = 0U; i < p->status_count; ++i)
    {
        stm_err_t e = transfer(io, p->status_commands[i], 0, 0U, NULL, &raw[i], 1U, 0);
        if (e != STM_OK)
        {
            return e;
        }
    }
    flash_status_t s = {.valid_mask = FLASH_STATUS_BUSY | FLASH_STATUS_WRITE_ENABLED};
    if (raw[p->busy_index] & p->busy_mask)
    {
        s.flags |= FLASH_STATUS_BUSY;
    }
    if (raw[p->wel_index] & p->wel_mask)
    {
        s.flags |= FLASH_STATUS_WRITE_ENABLED;
    }
    if (p->qe_mask)
    {
        s.valid_mask |= FLASH_STATUS_QUAD_ENABLED;
        if (raw[p->qe_index] & p->qe_mask)
        {
            s.flags |= FLASH_STATUS_QUAD_ENABLED;
        }
    }
    if (p->suspend_mask)
    {
        s.valid_mask |= FLASH_STATUS_SUSPENDED;
        if (raw[p->suspend_index] & p->suspend_mask)
        {
            s.flags |= FLASH_STATUS_SUSPENDED;
        }
    }
    for (unsigned i = 0U; i < p->status_count; ++i)
    {
        if (p->protected_masks[i])
        {
            s.valid_mask |= FLASH_STATUS_PROTECTED;
        }
        if (raw[i] & p->protected_masks[i])
        {
            s.flags |= FLASH_STATUS_PROTECTED;
        }
    }
    *status = s;
    return STM_OK;
}
stm_err_t flash_nor_wait_ready(const flash_chip_io_t *io, uint32_t timeout)
{
    const flash_nor_profile_t *p = io->device->nor;
    uint32_t start = io->host.ops->get_tick_ms(io->host.ctx);
    for (;;)
    {
        uint8_t value;
        stm_err_t e = transfer(io, p->status_commands[p->busy_index], 0, 0U, NULL, &value, 1U, 0);
        if (e != STM_OK)
        {
            return e;
        }
        if (!(value & p->busy_mask))
        {
            return STM_OK;
        }
        if ((uint32_t)(io->host.ops->get_tick_ms(io->host.ctx) - start) >= timeout)
        {
            return STM_ERR_TIMEOUT;
        }
        io->host.ops->delay_ms(io->host.ctx, 1U);
    }
}
stm_err_t flash_nor_init(const flash_chip_io_t *io)
{
    const flash_nor_profile_t *p = io->device->nor;
    if (!p || (p->address_bytes != 3U && p->address_bytes != 4U) || !p->status_count ||
        p->status_count > 3U || p->busy_index >= p->status_count || p->wel_index >= p->status_count ||
        (p->qe_mask && p->qe_index >= p->status_count) ||
        (p->suspend_mask && p->suspend_index >= p->status_count) || !p->busy_mask || !p->wel_mask ||
        !p->read_single || !p->program || !p->erase || !p->write_enable ||
        (io->read_mode == FLASH_READ_QUAD && !p->read_quad) ||
        (p->address_bytes == 3U && io->device->size_bytes > 0x1000000U))
    {
        return STM_ERR_INVALID_CONFIG;
    }
    for (unsigned i = 0U; i < p->status_count; ++i)
    {
        if (!p->status_commands[i])
        {
            return STM_ERR_INVALID_CONFIG;
        }
    }
    stm_err_t e = flash_nor_wait_ready(io, io->device->erase_timeout_ms);
    if (e != STM_OK)
    {
        return e;
    }
    flash_status_t s;
    e = flash_nor_get_status(io, &s);
    if (e != STM_OK)
    {
        return e;
    }
    if (s.flags & FLASH_STATUS_SUSPENDED)
    {
        return FLASH_ERR_SUSPENDED;
    }
    if (io->read_mode == FLASH_READ_QUAD && (s.valid_mask & FLASH_STATUS_QUAD_ENABLED) &&
        !(s.flags & FLASH_STATUS_QUAD_ENABLED))
    {
        return STM_ERR_INVALID_CONFIG;
    }
    return STM_OK;
}
stm_err_t flash_nor_read(const flash_chip_io_t *io, uint32_t offset, void *data, size_t size)
{
    const flash_nor_profile_t *p = io->device->nor;
    int quad = io->read_mode == FLASH_READ_QUAD;
    return transfer(io, quad ? p->read_quad : p->read_single, 1, offset, NULL, data, size, quad);
}
static stm_err_t write_enable(const flash_chip_io_t *io)
{
    const flash_nor_profile_t *p = io->device->nor;
    stm_err_t e = transfer(io, p->write_enable, 0, 0U, NULL, NULL, 0U, 0);
    if (e != STM_OK)
    {
        return e;
    }
    uint8_t value;
    e = transfer(io, p->status_commands[p->wel_index], 0, 0U, NULL, &value, 1U, 0);
    return e != STM_OK ? e : ((value & p->wel_mask) ? STM_OK : FLASH_ERR_PROTECTED);
}
stm_err_t flash_nor_program_page(const flash_chip_io_t *io, uint32_t offset, const void *data, size_t size)
{
    stm_err_t e = write_enable(io);
    if (e != STM_OK)
    {
        return e;
    }
    e = transfer(io, io->device->nor->program, 1, offset, data, NULL, size, 0);
    return e == STM_OK ? flash_nor_wait_ready(io, io->device->program_timeout_ms) : e;
}
stm_err_t flash_nor_erase_sector(const flash_chip_io_t *io, uint32_t offset)
{
    stm_err_t e = write_enable(io);
    if (e != STM_OK)
    {
        return e;
    }
    e = transfer(io, io->device->nor->erase, 1, offset, NULL, NULL, 0U, 0);
    return e == STM_OK ? flash_nor_wait_ready(io, io->device->erase_timeout_ms) : e;
}
const flash_chip_ops_t flash_nor_ops = {.init = flash_nor_init,
                                        .get_status = flash_nor_get_status,
                                        .read = flash_nor_read,
                                        .program_page = flash_nor_program_page,
                                        .erase_sector = flash_nor_erase_sector,
                                        .wait_ready = flash_nor_wait_ready};
