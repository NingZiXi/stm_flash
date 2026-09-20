/**
 * @file    stm_flash.c
 * @brief   外部 NOR 通用分页、擦除、状态和生命周期实现
 */
#include "private/flash_internal.h"
#include <stdlib.h>

struct flash_context {
    void *hal;       // 调用者提供并独占的 HAL 句柄
    const flash_bus_ops_t *bus;
    const flash_chip_desc_t *chip;
    flash_bus_type_t bus_type;
    uint32_t jedec_id;             // 24 位 JEDEC ID
    uint32_t clock_hz;             // 根据 HAL 配置计算的串行时钟，Hz
    HAL_StatusTypeDef last_hal_status;    // 最近一次 HAL 返回值
    flash_read_mode_t read_mode;    // 读取数据线模式
    uint8_t ready;                 // 非零表示接口可用，不代表硬件已测试
    struct flash_context *next; // 当前组件的实例链表
};

static flash_handle_t g_devices = NULL;

#define FLASH_IO_TIMEOUT 100U
#define FLASH_BUFFER_BYTES 256U

// 阻塞接口要求线程上下文和正常运行的 HAL tick
static int flash_context_ok(void)
{
    return __get_IPSR() == 0U && __get_PRIMASK() == 0U &&
           __get_BASEPRI() == 0U && __get_FAULTMASK() == 0U;
}

// 通信或校验故障后停止后续访问
static stm_err_t flash_fail(flash_handle_t dev, stm_err_t status)
{
    dev->ready = 0U;
    return status;
}

// 记录 HAL 返回值
static stm_err_t flash_hal_result(flash_handle_t dev, HAL_StatusTypeDef status)
{
    dev->last_hal_status = status;
    if (status == HAL_OK) { return STM_OK; }
    return flash_fail(dev, status == HAL_TIMEOUT ? STM_ERR_TIMEOUT : STM_ERR_IO);
}

// 发送单线指令；地址宽度来自器件描述。
static stm_err_t flash_command(flash_handle_t dev, uint8_t opcode,
                                        int addressed, uint32_t offset_bytes,
                                        uint32_t count, uint32_t lines, uint32_t dummy)
{
    flash_command_t cmd = {
        .opcode = opcode, .addressed = (uint8_t)addressed,
        .address_bytes = dev->chip != NULL ? dev->chip->address_bytes : 3U,
        .data_lines = (uint8_t)lines, .dummy_cycles = (uint8_t)dummy,
        .address = offset_bytes, .count = count,
    };
    return flash_hal_result(dev, dev->bus->command(dev->hal, &cmd, FLASH_IO_TIMEOUT));
}

// 读取指定状态寄存器
static stm_err_t flash_status_byte(flash_handle_t dev, uint8_t opcode, uint8_t *value)
{
    stm_err_t s = flash_command(dev, opcode, 0, 0U, 1U, 1U, 0U);
    if (s != STM_OK) { return s; }
    return flash_hal_result(dev, dev->bus->receive(dev->hal, value, FLASH_IO_TIMEOUT));
}

// 等待 BUSY 清零，支持 tick 回绕
static stm_err_t flash_wait(flash_handle_t dev, uint32_t timeout)
{
    uint32_t start = HAL_GetTick();
    for (;;) {
        uint8_t value;
        stm_err_t s = flash_status_byte(dev, dev->chip->status_commands[dev->chip->busy_index], &value);
        if (s != STM_OK) { return s; }
        if ((value & dev->chip->busy_mask) == 0U) { return STM_OK; }
        if ((uint32_t)(HAL_GetTick() - start) >= timeout) {
            return flash_fail(dev, STM_ERR_TIMEOUT);
        }
        HAL_Delay(1U);
    }
}

// 检查实例和缓冲区范围，禁止使用外部存储映射区作为缓冲区
static stm_err_t flash_check(flash_handle_t dev, uint32_t offset_bytes,
                                     const void *data, size_t size_bytes, int buffer)
{
    if (dev == NULL) { return STM_ERR_INVALID_ARG; }
    if (!flash_context_ok()) { return STM_ERR_INVALID_CONTEXT; }
    if (dev->ready == 0U || dev->hal == NULL) { return STM_ERR_INVALID_STATE; }
    if (offset_bytes > dev->chip->size_bytes || size_bytes > dev->chip->size_bytes - offset_bytes) {
        return STM_ERR_OUT_OF_RANGE;
    }
    if (buffer && size_bytes != 0U) {
        uintptr_t start = (uintptr_t)data;
        if (data == NULL || size_bytes > UINTPTR_MAX - start ||
            (start < 0xB0000000UL && start + size_bytes > 0x90000000UL)) {
            return STM_ERR_INVALID_ARG;
        }
    }
    return STM_OK;
}

// 按器件描述读取状态寄存器
static stm_err_t flash_status_all(flash_handle_t dev, uint8_t status[3])
{
    for (unsigned i = 0U; i < dev->chip->status_count; ++i) {
        stm_err_t s = flash_status_byte(dev, dev->chip->status_commands[i], &status[i]);
        if (s != STM_OK) { return s; }
    }
    return STM_OK;
}

// 拒绝挂起操作及任何块保护或独立锁模式
static stm_err_t flash_write_allowed(flash_handle_t dev)
{
    uint8_t status[3];
    stm_err_t s = flash_wait(dev, dev->chip->erase_timeout_ms);
    if (s != STM_OK) { return s; }
    s = flash_status_all(dev, status);
    if (s != STM_OK) { return s; }
    flash_status_t decoded;
    flash_chip_decode_status(dev->chip, status, &decoded);
    if (decoded.flags & FLASH_STATUS_SUSPENDED) { return FLASH_ERR_SUSPENDED; }
    if (decoded.flags & FLASH_STATUS_PROTECTED) { return FLASH_ERR_PROTECTED; }
    return STM_OK;
}

// 写使能并确认 WEL
static stm_err_t flash_write_enable(flash_handle_t dev)
{
    uint8_t status;
    stm_err_t s = flash_command(dev, dev->chip->write_enable, 0, 0U, 0U, 0U, 0U);
    if (s != STM_OK) { return s; }
    s = flash_status_byte(dev, dev->chip->status_commands[dev->chip->wel_index], &status);
    if (s != STM_OK) { return s; }
    return (status & dev->chip->wel_mask) != 0U ? STM_OK : FLASH_ERR_PROTECTED;
}

// 先识别型号，再按器件描述校验配置；未知芯片不执行厂商专用操作。
static stm_err_t flash_init_device(flash_handle_t dev, const flash_config_t *config)
{
    dev->hal = config->bus.handle.ospi;
    dev->bus = &flash_ospi_ops;
    dev->bus_type = config->bus.type;
    dev->read_mode = config->read_mode;
    if (config->read_mode != FLASH_READ_SINGLE && config->read_mode != FLASH_READ_QUAD) {
        return STM_ERR_INVALID_CONFIG;
    }
    stm_err_t s = dev->bus->validate(dev->hal, &dev->clock_hz);
    if (s != STM_OK) { return s; }
    uint8_t id[3];
    s = flash_command(dev, 0x9FU, 0, 0U, 3U, 1U, 0U);
    if (s != STM_OK) { return s; }
    s = flash_hal_result(dev, dev->bus->receive(dev->hal, id, FLASH_IO_TIMEOUT));
    if (s != STM_OK) { return s; }
    dev->jedec_id = ((uint32_t)id[0] << 16U) | ((uint32_t)id[1] << 8U) | id[2];
    dev->chip = flash_chip_find(dev->jedec_id, config->chip);
    if (dev->chip == NULL) { return STM_ERR_NOT_SUPPORTED; }
    s = dev->bus->geometry(dev->hal, dev->chip->size_bytes, dev->chip->max_clock_hz);
    if (s != STM_OK) { return s; }
    uint32_t capability = config->read_mode == FLASH_READ_QUAD ? FLASH_CAP_READ_QUAD : FLASH_CAP_READ_SINGLE;
    if (!(dev->chip->capabilities & capability)) { return STM_ERR_NOT_SUPPORTED; }
    s = flash_wait(dev, dev->chip->erase_timeout_ms);
    if (s != STM_OK) { return s; }
    uint8_t raw[3] = {0};
    s = flash_status_all(dev, raw);
    if (s != STM_OK) { return s; }
    flash_status_t status;
    flash_chip_decode_status(dev->chip, raw, &status);
    if (status.flags & FLASH_STATUS_SUSPENDED) { return FLASH_ERR_SUSPENDED; }
    if (config->read_mode == FLASH_READ_QUAD && (status.valid_mask & FLASH_STATUS_QUAD_ENABLED) &&
        !(status.flags & FLASH_STATUS_QUAD_ENABLED)) { return STM_ERR_INVALID_CONFIG; }
    dev->ready = 1U;
    return STM_OK;
}

stm_err_t flash_get_status(flash_handle_t dev, flash_status_t *status)
{
    stm_err_t s = flash_check(dev, 0U, status, sizeof(*status), 1);
    if (s != STM_OK) { return s; }
    uint8_t raw[3] = {0};
    s = flash_status_all(dev, raw);
    if (s == STM_OK) { flash_chip_decode_status(dev->chip, raw, status); }
    return s;
}

// 读取连续字节
stm_err_t flash_read(flash_handle_t dev, uint32_t offset_bytes, void *data, size_t size_bytes)
{
    stm_err_t s = flash_check(dev, offset_bytes, data, size_bytes, 1);
    if (s != STM_OK || size_bytes == 0U) { return s; }
    s = flash_wait(dev, dev->chip->erase_timeout_ms);
    if (s != STM_OK) { return s; }
    flash_status_t status;
    s = flash_get_status(dev, &status);
    if (s != STM_OK) { return s; }
    if (status.flags & FLASH_STATUS_SUSPENDED) { return FLASH_ERR_SUSPENDED; }
    if (dev->read_mode == FLASH_READ_QUAD && (status.valid_mask & FLASH_STATUS_QUAD_ENABLED) &&
        !(status.flags & FLASH_STATUS_QUAD_ENABLED)) { return flash_fail(dev, STM_ERR_INVALID_CONFIG); }
    uint8_t *out = data;
    while (size_bytes != 0U) {
        uint32_t count = size_bytes > 4096U ? 4096U : (uint32_t)size_bytes;
        int quad = dev->read_mode == FLASH_READ_QUAD;
        s = flash_command(dev, quad ? dev->chip->read_quad : dev->chip->read_single, 1, offset_bytes, count,
                          quad ? 4U : 1U, quad ? dev->chip->quad_dummy : 0U);
        if (s != STM_OK) { return s; }
        s = flash_hal_result(dev, dev->bus->receive(dev->hal, out, FLASH_IO_TIMEOUT));
        if (s != STM_OK) { return s; }
        offset_bytes += count; out += count; size_bytes -= count;
    }
    return STM_OK;
}

// 比较实际数据或检查是否需要擦除
static stm_err_t flash_compare(flash_handle_t dev, uint32_t offset_bytes,
                                       const uint8_t *data, size_t size_bytes, int preflight)
{
    uint8_t buffer[FLASH_BUFFER_BYTES];
    while (size_bytes != 0U) {
        size_t count = size_bytes > sizeof(buffer) ? sizeof(buffer) : size_bytes;
        stm_err_t s = flash_read(dev, offset_bytes, buffer, count);
        if (s != STM_OK) { return s; }
        for (size_t i = 0U; i < count; ++i) {
            uint8_t expected = data != NULL ? data[i] : 0xFFU;
            if (preflight ? ((buffer[i] & expected) != expected) : (buffer[i] != expected)) {
                return preflight ? FLASH_ERR_NEEDS_ERASE : flash_fail(dev, STM_ERR_VERIFY);
            }
        }
        offset_bytes += (uint32_t)count; size_bytes -= count;
        if (data != NULL) { data += count; }
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
    if (s != STM_OK || size_bytes == 0U) { return s; }
    s = flash_write_allowed(dev);
    if (s != STM_OK) { return s; }
    s = flash_compare(dev, offset_bytes, data, size_bytes, 1);
    if (s != STM_OK) { return s; }
    const uint8_t *in = data;
    while (size_bytes != 0U) {
        uint32_t count = dev->chip->page_size - offset_bytes % dev->chip->page_size;
        if (size_bytes < count) { count = (uint32_t)size_bytes; }
        s = flash_write_enable(dev);
        if (s != STM_OK) { return s; }
        s = flash_command(dev, dev->chip->program, 1, offset_bytes, count, 1U, 0U);
        if (s != STM_OK) { return s; }
        s = flash_hal_result(dev, dev->bus->transmit(dev->hal, (uint8_t *)in, FLASH_IO_TIMEOUT));
        if (s != STM_OK) { return s; }
        s = flash_wait(dev, dev->chip->program_timeout_ms);
        if (s != STM_OK) { return s; }
        s = flash_compare(dev, offset_bytes, in, count, 0);
        if (s != STM_OK) { return s; }
        offset_bytes += count; in += count; size_bytes -= count;
    }
    return STM_OK;
}

// 按器件擦除粒度操作并检查结果
stm_err_t flash_erase(flash_handle_t dev, uint32_t offset_bytes, size_t size_bytes)
{
    stm_err_t s = flash_check(dev, offset_bytes, NULL, size_bytes, 0);
    if (s != STM_OK) { return s; }
    if (offset_bytes % dev->chip->erase_size != 0U || size_bytes % dev->chip->erase_size != 0U) {
        return STM_ERR_INVALID_ARG;
    }
    if (size_bytes == 0U) { return STM_OK; }
    s = flash_write_allowed(dev);
    if (s != STM_OK) { return s; }
    while (size_bytes != 0U) {
        s = flash_write_enable(dev);
        if (s != STM_OK) { return s; }
        s = flash_command(dev, dev->chip->erase, 1, offset_bytes, 0U, 0U, 0U);
        if (s != STM_OK) { return s; }
        s = flash_wait(dev, dev->chip->erase_timeout_ms);
        if (s != STM_OK) { return s; }
        s = flash_compare(dev, offset_bytes, NULL, dev->chip->erase_size, 0);
        if (s != STM_OK) { return s; }
        offset_bytes += dev->chip->erase_size; size_bytes -= dev->chip->erase_size;
    }
    return STM_OK;
}

// 创建对象并独占对应外设
stm_err_t flash_create(const flash_config_t *config, flash_handle_t *out_handle)
{
    if (config == NULL || out_handle == NULL) { return STM_ERR_INVALID_ARG; }
    if (config->bus.type != FLASH_BUS_OSPI) { return STM_ERR_NOT_SUPPORTED; }
    if (config->bus.handle.ospi == NULL) { return STM_ERR_INVALID_ARG; }
    if (*out_handle != NULL) { return STM_ERR_INVALID_STATE; }
    if (__get_IPSR() != 0U || __get_PRIMASK() != 0U ||
        __get_BASEPRI() != 0U || __get_FAULTMASK() != 0U) { return STM_ERR_INVALID_CONTEXT; }
    for (flash_handle_t it = g_devices; it != NULL; it = it->next) {
        if (it->bus_type == config->bus.type && it->bus->identity(it->hal) == flash_ospi_ops.identity(config->bus.handle.ospi)) { return STM_ERR_INVALID_STATE; }
    }
    flash_handle_t dev = calloc(1U, sizeof(*dev));
    if (dev == NULL) { return STM_ERR_NO_MEM; }
    stm_err_t err = flash_init_device(dev, config);
    if (err != STM_OK) { free(dev); return err; }
    dev->next = g_devices;
    g_devices = dev;
    *out_handle = dev;
    return STM_OK;
}

// 释放软件对象，HAL 外设保持不变
stm_err_t flash_delete(flash_handle_t *handle)
{
    if (handle == NULL) { return STM_ERR_INVALID_ARG; }
    if (*handle == NULL) { return STM_OK; }
    if (__get_IPSR() != 0U || __get_PRIMASK() != 0U ||
        __get_BASEPRI() != 0U || __get_FAULTMASK() != 0U) { return STM_ERR_INVALID_CONTEXT; }
    flash_handle_t *link = &g_devices;
    while (*link != NULL && *link != *handle) { link = &(*link)->next; }
    if (*link == NULL) { return STM_ERR_INVALID_ARG; }
    flash_handle_t dev = *handle;
    *link = dev->next;
    free(dev);
    *handle = NULL;
    return STM_OK;
}

// 复制信息快照，不暴露可写的内部对象
stm_err_t flash_get_info(flash_handle_t handle, flash_info_t *info)
{
    if (handle == NULL || info == NULL) { return STM_ERR_INVALID_ARG; }
    info->chip = handle->chip->chip;
    info->bus_type = handle->bus_type;
    info->size_bytes = handle->chip->size_bytes;
    info->page_size = handle->chip->page_size;
    info->erase_size = handle->chip->erase_size;
    info->capabilities = handle->chip->capabilities;
    info->jedec_id = handle->jedec_id;
    info->clock_hz = handle->clock_hz;
    info->last_hal_status = handle->last_hal_status;
    info->read_mode = handle->read_mode;
    info->ready = handle->ready;
    return STM_OK;
}
