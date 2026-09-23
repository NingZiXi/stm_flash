/** @file stm_flash.c @brief 平台无关的 NOR 生命周期、边界检查和逐页校验。 */
#include "flash_internal.h"
#include <stdlib.h>
static flash_handle_t g_devices;
#define FLASH_BUFFER_BYTES 256U
static stm_err_t flash_fail(flash_handle_t dev, stm_err_t e)
{
    dev->last_error = e;
    dev->ready = 0U;
    return e;
}
static stm_err_t flash_result(flash_handle_t dev, stm_err_t e)
{
    dev->last_error = e;
    if (e != STM_OK && e != FLASH_ERR_PROTECTED && e != FLASH_ERR_SUSPENDED)
    {
        dev->ready = 0U;
    }
    return e;
}
// 检查实例和缓冲区范围，禁止使用外部存储映射区作为缓冲区
static stm_err_t flash_check(flash_handle_t dev, uint32_t offset_bytes, const void *data, size_t size_bytes,
                             int buffer)
{
    if (dev == NULL)
    {
        return STM_ERR_INVALID_ARG;
    }
    if (!(dev->io.host.ops->check_context(dev->io.host.ctx) == STM_OK))
    {
        return STM_ERR_INVALID_CONTEXT;
    }
    if (dev->ready == 0U || dev->io.host.ctx == NULL)
    {
        return STM_ERR_INVALID_STATE;
    }
    if (offset_bytes > dev->io.device->size_bytes || size_bytes > dev->io.device->size_bytes - offset_bytes)
    {
        return STM_ERR_OUT_OF_RANGE;
    }
    if (buffer && size_bytes != 0U)
    {
        uintptr_t start = (uintptr_t)data;
        if (data == NULL || size_bytes > UINTPTR_MAX - start ||
            (dev->io.host.ops->buffer_ok != NULL &&
             !dev->io.host.ops->buffer_ok(dev->io.host.ctx, data, size_bytes)))
        {
            return STM_ERR_INVALID_ARG;
        }
    }
    return STM_OK;
}

stm_err_t flash_get_status(flash_handle_t dev, flash_status_t *status)
{
    stm_err_t e = flash_check(dev, 0U, status, sizeof(*status), 1);
    if (e != STM_OK)
    {
        return e;
    }
    flash_status_t local;
    e = flash_result(dev, dev->io.device->ops->get_status(&dev->io, &local));
    if (e == STM_OK)
    {
        *status = local;
    }
    return e;
}
static stm_err_t flash_write_allowed(flash_handle_t dev)
{
    stm_err_t e =
        flash_result(dev, dev->io.device->ops->wait_ready(&dev->io, dev->io.device->erase_timeout_ms));
    if (e != STM_OK)
    {
        return e;
    }
    flash_status_t s;
    e = flash_get_status(dev, &s);
    if (e != STM_OK)
    {
        return e;
    }
    if (s.flags & FLASH_STATUS_SUSPENDED)
    {
        return FLASH_ERR_SUSPENDED;
    }
    if (s.flags & FLASH_STATUS_PROTECTED)
    {
        return FLASH_ERR_PROTECTED;
    }
    return STM_OK;
}
stm_err_t flash_read(flash_handle_t dev, uint32_t offset, void *data, size_t size)
{
    stm_err_t e = flash_check(dev, offset, data, size, 1);
    if (e != STM_OK || !size)
    {
        return e;
    }
    e = flash_result(dev, dev->io.device->ops->wait_ready(&dev->io, dev->io.device->erase_timeout_ms));
    if (e != STM_OK)
    {
        return e;
    }
    flash_status_t status;
    e = flash_get_status(dev, &status);
    if (e != STM_OK)
    {
        return e;
    }
    if (status.flags & FLASH_STATUS_SUSPENDED)
    {
        return FLASH_ERR_SUSPENDED;
    }
    if (dev->io.read_mode == FLASH_READ_QUAD && (status.valid_mask & FLASH_STATUS_QUAD_ENABLED) &&
        !(status.flags & FLASH_STATUS_QUAD_ENABLED))
    {
        return flash_fail(dev, STM_ERR_INVALID_CONFIG);
    }
    uint8_t *out = data;
    while (size)
    {
        size_t count = size > 4096U ? 4096U : size;
        if (count > dev->host_info.max_transfer_bytes)
        {
            count = dev->host_info.max_transfer_bytes;
        }
        e = flash_result(dev, dev->io.device->ops->read(&dev->io, offset, out, count));
        if (e != STM_OK)
        {
            return e;
        }
        offset += (uint32_t)count;
        out += count;
        size -= count;
    }
    return STM_OK;
}
// 比较实际数据或检查是否需要擦除
static stm_err_t flash_compare(flash_handle_t dev, uint32_t offset_bytes, const uint8_t *data,
                               size_t size_bytes, int preflight)
{
    uint8_t buffer[FLASH_BUFFER_BYTES];
    while (size_bytes != 0U)
    {
        size_t count = size_bytes > sizeof(buffer) ? sizeof(buffer) : size_bytes;
        stm_err_t s = flash_read(dev, offset_bytes, buffer, count);
        if (s != STM_OK)
        {
            return s;
        }
        for (size_t i = 0U; i < count; ++i)
        {
            uint8_t expected = data != NULL ? data[i] : 0xFFU;
            if (preflight ? ((buffer[i] & expected) != expected) : (buffer[i] != expected))
            {
                return preflight ? FLASH_ERR_NEEDS_ERASE : flash_fail(dev, STM_ERR_VERIFY);
            }
        }
        offset_bytes += (uint32_t)count;
        size_bytes -= count;
        if (data != NULL)
        {
            data += count;
        }
    }
    return STM_OK;
}

// 校验 Flash 内容
stm_err_t flash_verify(flash_handle_t dev, uint32_t offset_bytes, const void *data, size_t size_bytes)
{
    stm_err_t s = flash_check(dev, offset_bytes, data, size_bytes, 1);
    return s == STM_OK ? flash_compare(dev, offset_bytes, data, size_bytes, 0) : s;
}

// 自动拆分页边界并校验每页
stm_err_t flash_write(flash_handle_t dev, uint32_t offset_bytes, const void *data, size_t size_bytes)
{
    stm_err_t s = flash_check(dev, offset_bytes, data, size_bytes, 1);
    if (s != STM_OK || size_bytes == 0U)
    {
        return s;
    }
    if (!(dev->io.device->capabilities & FLASH_CAP_PROGRAM))
    {
        return STM_ERR_NOT_SUPPORTED;
    }
    s = flash_write_allowed(dev);
    if (s != STM_OK)
    {
        return s;
    }
    s = flash_compare(dev, offset_bytes, data, size_bytes, 1);
    if (s != STM_OK)
    {
        return s;
    }
    const uint8_t *in = data;
    while (size_bytes != 0U)
    {
        uint32_t count = dev->io.device->page_size - offset_bytes % dev->io.device->page_size;
        if (count > dev->host_info.max_transfer_bytes)
        {
            count = dev->host_info.max_transfer_bytes;
        }
        if (size_bytes < count)
        {
            count = (uint32_t)size_bytes;
        }
        s = flash_result(dev, dev->io.device->ops->program_page(&dev->io, offset_bytes, in, count));
        if (s != STM_OK)
        {
            return s;
        }
        s = flash_compare(dev, offset_bytes, in, count, 0);
        if (s != STM_OK)
        {
            return s;
        }
        offset_bytes += count;
        in += count;
        size_bytes -= count;
    }
    return STM_OK;
}

// 按器件擦除粒度操作并检查结果
stm_err_t flash_erase(flash_handle_t dev, uint32_t offset_bytes, size_t size_bytes)
{
    stm_err_t s = flash_check(dev, offset_bytes, NULL, size_bytes, 0);
    if (s != STM_OK)
    {
        return s;
    }
    if (offset_bytes % dev->io.device->erase_size != 0U || size_bytes % dev->io.device->erase_size != 0U)
    {
        return STM_ERR_INVALID_ARG;
    }
    if (size_bytes == 0U)
    {
        return STM_OK;
    }
    if (!(dev->io.device->capabilities & FLASH_CAP_ERASE))
    {
        return STM_ERR_NOT_SUPPORTED;
    }
    s = flash_write_allowed(dev);
    if (s != STM_OK)
    {
        return s;
    }
    while (size_bytes != 0U)
    {
        s = flash_result(dev, dev->io.device->ops->erase_sector(&dev->io, offset_bytes));
        if (s != STM_OK)
        {
            return s;
        }
        s = flash_compare(dev, offset_bytes, NULL, dev->io.device->erase_size, 0);
        if (s != STM_OK)
        {
            return s;
        }
        offset_bytes += dev->io.device->erase_size;
        size_bytes -= dev->io.device->erase_size;
    }
    return STM_OK;
}

static int valid_device(const flash_device_t *d)
{
    return d && d->name && d->size_bytes && d->page_size && d->erase_size && d->page_size <= d->erase_size &&
           d->erase_size <= d->size_bytes && d->size_bytes % d->erase_size == 0U &&
           d->erase_size % d->page_size == 0U && d->max_clock_hz && d->program_timeout_ms &&
           d->erase_timeout_ms && d->ops && d->ops->init && d->ops->read && d->ops->get_status &&
           d->ops->wait_ready && d->ops->program_page && d->ops->erase_sector;
}
stm_err_t flash_create(const flash_config_t *c, flash_handle_t *out)
{
    if (!c || !out || !c->host.ctx)
    {
        return STM_ERR_INVALID_ARG;
    }
    if (*out)
    {
        return STM_ERR_INVALID_STATE;
    }
    const flash_host_ops_t *h = c->host.ops;
    if (!h || !h->name || !h->validate || !h->check_device || !h->identity || !h->exec || !h->get_tick_ms ||
        !h->delay_ms || !h->check_context)
    {
        return STM_ERR_INVALID_CONFIG;
    }
    if ((!c->candidates && c->candidate_count) || (c->candidates && !c->candidate_count))
    {
        return STM_ERR_INVALID_ARG;
    }
    stm_err_t e = h->check_context(c->host.ctx);
    if (e != STM_OK)
    {
        return e;
    }
    if (c->read_mode != FLASH_READ_SINGLE && c->read_mode != FLASH_READ_QUAD)
    {
        return STM_ERR_INVALID_CONFIG;
    }
    const void *key = h->identity(c->host.ctx);
    if (!key)
    {
        return STM_ERR_INVALID_CONFIG;
    }
    for (flash_handle_t it = g_devices; it; it = it->next)
    {
        if (it->identity == key)
        {
            return STM_ERR_INVALID_STATE;
        }
    }
    flash_handle_t dev = calloc(1U, sizeof(*dev));
    if (!dev)
    {
        return STM_ERR_NO_MEM;
    }
    dev->io.host = c->host;
    dev->io.read_mode = c->read_mode;
    dev->identity = key;
    e = h->validate(c->host.ctx, &dev->host_info);
    if (e == STM_OK && (!dev->host_info.clock_hz || dev->host_info.max_transfer_bytes < 3U ||
                        !(dev->host_info.data_line_mask & (1U << 1U)) ||
                        !(dev->host_info.data_line_mask & (1U << c->read_mode))))
    {
        e = STM_ERR_NOT_SUPPORTED;
    }
    if (e == STM_OK)
    {
        uint8_t id[3];
        flash_transfer_t t = {
            .opcode = 0x9FU, .instruction_lines = 1U, .data_lines = 1U, .rx_data = id, .size_bytes = 3U};
        e = h->exec(c->host.ctx, &t, 100U);
        if (e == STM_OK)
        {
            dev->jedec_id = ((uint32_t)id[0] << 16U) | ((uint32_t)id[1] << 8U) | id[2];
            e = flash_select_device(dev->jedec_id, c, &dev->io.device);
        }
    }
    if (e == STM_OK && !valid_device(dev->io.device))
    {
        e = STM_ERR_INVALID_CONFIG;
    }
    if (e == STM_OK)
    {
        const flash_device_t *d = dev->io.device;
        uint32_t cap = c->read_mode == FLASH_READ_QUAD ? FLASH_CAP_READ_QUAD : FLASH_CAP_READ_SINGLE;
        if (!(d->capabilities & cap))
        {
            e = STM_ERR_NOT_SUPPORTED;
        }
        else
        {
            e = h->check_device(c->host.ctx, d->size_bytes, d->max_clock_hz);
        }
    }
    if (e == STM_OK)
    {
        e = dev->io.device->ops->init(&dev->io);
    }
    if (e != STM_OK)
    {
        free(dev);
        return e;
    }
    dev->ready = 1U;
    dev->next = g_devices;
    g_devices = dev;
    *out = dev;
    return STM_OK;
}
stm_err_t flash_delete(flash_handle_t *handle)
{
    if (!handle)
    {
        return STM_ERR_INVALID_ARG;
    }
    if (!*handle)
    {
        return STM_OK;
    }
    flash_handle_t *link = &g_devices;
    while (*link && *link != *handle)
    {
        link = &(*link)->next;
    }
    if (!*link)
    {
        return STM_ERR_INVALID_ARG;
    }
    flash_handle_t dev = *link;
    stm_err_t e = dev->io.host.ops->check_context(dev->io.host.ctx);
    if (e != STM_OK)
    {
        return e;
    }
    *link = dev->next;
    free(dev);
    *handle = NULL;
    return STM_OK;
}
stm_err_t flash_get_info(flash_handle_t dev, flash_info_t *info)
{
    if (!dev || !info)
    {
        return STM_ERR_INVALID_ARG;
    }
    const flash_device_t *d = dev->io.device;
    *info = (flash_info_t){.device = d,
                           .host_name = dev->io.host.ops->name,
                           .size_bytes = d->size_bytes,
                           .page_size = d->page_size,
                           .erase_size = d->erase_size,
                           .capabilities = d->capabilities,
                           .jedec_id = dev->jedec_id,
                           .clock_hz = dev->host_info.clock_hz,
                           .last_error = dev->last_error,
                           .read_mode = dev->io.read_mode,
                           .ready = dev->ready};
    return STM_OK;
}
