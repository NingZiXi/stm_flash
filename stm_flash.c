/**
 * @file    stm_flash.c
 * @brief   W25Q256JV-IQ 四字节寻址、分页写入和扇区擦除
 */
#include "stm_flash.h"
#include <stdlib.h>

struct flash_context {
    OSPI_HandleTypeDef *hal;       // 调用者提供并独占的 HAL 句柄
    uint32_t jedec_id;             // 24 位 JEDEC ID
    uint32_t clock_hz;             // 根据 HAL 配置计算的串行时钟，Hz
    HAL_StatusTypeDef last_hal_status;    // 最近一次 HAL 返回值
    flash_read_mode_t read_mode;    // 读取数据线模式
    uint8_t ready;                 // 非零表示接口可用，不代表硬件已测试
    struct flash_context *next; // 当前组件的实例链表
};

static flash_handle_t g_devices = NULL;

#define FLASH_IO_TIMEOUT 100U
#define FLASH_PROGRAM_TIMEOUT 10U
#define FLASH_ERASE_TIMEOUT 1000U
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

// 发送单线指令和可选的四字节地址
static stm_err_t flash_command(flash_handle_t dev, uint8_t opcode,
                                        int addressed, uint32_t offset_bytes,
                                        uint32_t count, uint32_t lines, uint32_t dummy)
{
    OSPI_RegularCmdTypeDef cmd = {0};
    cmd.OperationType = HAL_OSPI_OPTYPE_COMMON_CFG;
    cmd.FlashId = HAL_OSPI_FLASH_ID_1;
    cmd.Instruction = opcode;
    cmd.InstructionMode = HAL_OSPI_INSTRUCTION_1_LINE;
    cmd.InstructionSize = HAL_OSPI_INSTRUCTION_8_BITS;
    cmd.AddressMode = addressed ? HAL_OSPI_ADDRESS_1_LINE : HAL_OSPI_ADDRESS_NONE;
    cmd.AddressSize = HAL_OSPI_ADDRESS_32_BITS;
    cmd.Address = offset_bytes;
    cmd.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode = count != 0U ? lines : HAL_OSPI_DATA_NONE;
    cmd.NbData = count;
    cmd.DummyCycles = dummy;
    cmd.DQSMode = HAL_OSPI_DQS_DISABLE;
    cmd.SIOOMode = HAL_OSPI_SIOO_INST_EVERY_CMD;
    return flash_hal_result(dev, HAL_OSPI_Command(dev->hal, &cmd, FLASH_IO_TIMEOUT));
}

// 读取指定状态寄存器
static stm_err_t flash_status_byte(flash_handle_t dev, uint8_t opcode, uint8_t *value)
{
    stm_err_t s = flash_command(dev, opcode, 0, 0U, 1U, HAL_OSPI_DATA_1_LINE, 0U);
    if (s != STM_OK) { return s; }
    return flash_hal_result(dev, HAL_OSPI_Receive(dev->hal, value, FLASH_IO_TIMEOUT));
}

// 等待 BUSY 清零，支持 tick 回绕
static stm_err_t flash_wait(flash_handle_t dev, uint32_t timeout)
{
    uint32_t start = HAL_GetTick();
    for (;;) {
        uint8_t value;
        stm_err_t s = flash_status_byte(dev, 0x05U, &value);
        if (s != STM_OK) { return s; }
        if ((value & 1U) == 0U) { return STM_OK; }
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
    if (offset_bytes > FLASH_SIZE_BYTES || size_bytes > FLASH_SIZE_BYTES - offset_bytes) {
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

// 读取三个状态字节
static stm_err_t flash_status_all(flash_handle_t dev, uint8_t status[3])
{
    const uint8_t commands[3] = {0x05U, 0x35U, 0x15U};
    for (unsigned i = 0U; i < 3U; ++i) {
        stm_err_t s = flash_status_byte(dev, commands[i], &status[i]);
        if (s != STM_OK) { return s; }
    }
    return STM_OK;
}

// 拒绝挂起操作及任何块保护或独立锁模式
static stm_err_t flash_write_allowed(flash_handle_t dev)
{
    uint8_t status[3];
    stm_err_t s = flash_wait(dev, FLASH_ERASE_TIMEOUT);
    if (s != STM_OK) { return s; }
    s = flash_status_all(dev, status);
    if (s != STM_OK) { return s; }
    if ((status[1] & 0x80U) != 0U) { return FLASH_ERR_SUSPENDED; }
    if ((status[0] & 0x3CU) != 0U || (status[1] & 0x40U) != 0U ||
        (status[2] & 0x04U) != 0U) { return FLASH_ERR_PROTECTED; }
    return STM_OK;
}

// 写使能并确认 WEL
static stm_err_t flash_write_enable(flash_handle_t dev)
{
    uint8_t status;
    stm_err_t s = flash_command(dev, 0x06U, 0, 0U, 0U, HAL_OSPI_DATA_NONE, 0U);
    if (s != STM_OK) { return s; }
    s = flash_status_byte(dev, 0x05U, &status);
    if (s != STM_OK) { return s; }
    return (status & 2U) != 0U ? STM_OK : FLASH_ERR_PROTECTED;
}

// 检查外设配置并识别器件
static stm_err_t flash_init_device(flash_handle_t dev, OSPI_HandleTypeDef *hal,
                             flash_read_mode_t mode)
{
    if (dev == NULL || hal == NULL) { return STM_ERR_INVALID_ARG; }
    if (!flash_context_ok()) { return STM_ERR_INVALID_CONTEXT; }
    if (dev->ready != 0U || (mode != FLASH_READ_SINGLE && mode != FLASH_READ_QUAD)) {
        return STM_ERR_INVALID_CONFIG;
    }
    uint32_t kernel = __HAL_RCC_GET_OSPI_SOURCE() == RCC_OSPICLKSOURCE_HCLK
                          ? HAL_RCC_GetHCLKFreq() : 0U;
    if ((hal->Instance != OCTOSPI1 && hal->Instance != OCTOSPI2) ||
        HAL_OSPI_GetState(hal) != HAL_OSPI_STATE_READY ||
        hal->Init.DeviceSize != 25U || hal->Init.DualQuad != HAL_OSPI_DUALQUAD_DISABLE ||
        hal->Init.MemoryType != HAL_OSPI_MEMTYPE_MICRON ||
        hal->Init.ClockMode != HAL_OSPI_CLOCK_MODE_0 ||
        hal->Init.WrapSize != HAL_OSPI_WRAP_NOT_SUPPORTED ||
        hal->Init.ChipSelectBoundary != 0U ||
        hal->Init.FreeRunningClock != HAL_OSPI_FREERUNCLK_DISABLE ||
        hal->Init.ClockPrescaler == 0U || hal->Init.ClockPrescaler > 256U ||
        hal->Init.ChipSelectHighTime < 4U || kernel == 0U ||
        (uint64_t)kernel > (uint64_t)50000000U * hal->Init.ClockPrescaler) {
        return STM_ERR_INVALID_CONFIG;
    }
    dev->hal = hal;
    dev->read_mode = mode;
    dev->jedec_id = 0U;
    dev->clock_hz = kernel / hal->Init.ClockPrescaler;
    dev->last_hal_status = HAL_OK;
    stm_err_t s = flash_wait(dev, FLASH_ERASE_TIMEOUT);
    if (s != STM_OK) { return s; }
    uint8_t id[3];
    s = flash_command(dev, 0x9FU, 0, 0U, 3U, HAL_OSPI_DATA_1_LINE, 0U);
    if (s != STM_OK) { return s; }
    s = flash_hal_result(dev, HAL_OSPI_Receive(hal, id, FLASH_IO_TIMEOUT));
    if (s != STM_OK) { return s; }
    dev->jedec_id = ((uint32_t)id[0] << 16U) | ((uint32_t)id[1] << 8U) | id[2];
    if (dev->jedec_id != FLASH_JEDEC_ID) { return STM_ERR_NOT_SUPPORTED; }
    uint8_t status[3];
    s = flash_status_all(dev, status);
    if (s != STM_OK) { return s; }
    if ((status[1] & 0x80U) != 0U) { return FLASH_ERR_SUSPENDED; }
    if (mode == FLASH_READ_QUAD && (status[1] & 2U) == 0U) {
        return STM_ERR_INVALID_CONFIG;
    }
    dev->ready = 1U;
    return STM_OK;
}

// 读取状态寄存器
stm_err_t flash_read_status(flash_handle_t dev, uint8_t status[3])
{
    stm_err_t s = flash_check(dev, 0U, status, 3U, 1);
    return s == STM_OK ? flash_status_all(dev, status) : s;
}

// 读取连续字节
stm_err_t flash_read(flash_handle_t dev, uint32_t offset_bytes, void *data, size_t size_bytes)
{
    stm_err_t s = flash_check(dev, offset_bytes, data, size_bytes, 1);
    if (s != STM_OK || size_bytes == 0U) { return s; }
    s = flash_wait(dev, FLASH_ERASE_TIMEOUT);
    if (s != STM_OK) { return s; }
    uint8_t status;
    s = flash_status_byte(dev, 0x35U, &status);
    if (s != STM_OK) { return s; }
    if ((status & 0x80U) != 0U) { return FLASH_ERR_SUSPENDED; }
    if (dev->read_mode == FLASH_READ_QUAD && (status & 2U) == 0U) {
        return flash_fail(dev, STM_ERR_INVALID_CONFIG);
    }
    uint8_t *out = data;
    while (size_bytes != 0U) {
        uint32_t count = size_bytes > 4096U ? 4096U : (uint32_t)size_bytes;
        int quad = dev->read_mode == FLASH_READ_QUAD;
        s = flash_command(dev, quad ? 0x6CU : 0x13U, 1, offset_bytes, count,
                          quad ? HAL_OSPI_DATA_4_LINES : HAL_OSPI_DATA_1_LINE, quad ? 8U : 0U);
        if (s != STM_OK) { return s; }
        s = flash_hal_result(dev, HAL_OSPI_Receive(dev->hal, out, FLASH_IO_TIMEOUT));
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
        uint32_t count = FLASH_PAGE_BYTES - offset_bytes % FLASH_PAGE_BYTES;
        if (size_bytes < count) { count = (uint32_t)size_bytes; }
        s = flash_write_enable(dev);
        if (s != STM_OK) { return s; }
        s = flash_command(dev, 0x12U, 1, offset_bytes, count, HAL_OSPI_DATA_1_LINE, 0U);
        if (s != STM_OK) { return s; }
        s = flash_hal_result(dev, HAL_OSPI_Transmit(dev->hal, (uint8_t *)in, FLASH_IO_TIMEOUT));
        if (s != STM_OK) { return s; }
        s = flash_wait(dev, FLASH_PROGRAM_TIMEOUT);
        if (s != STM_OK) { return s; }
        s = flash_compare(dev, offset_bytes, in, count, 0);
        if (s != STM_OK) { return s; }
        offset_bytes += count; in += count; size_bytes -= count;
    }
    return STM_OK;
}

// 按 4 KiB 擦除并检查擦除结果
stm_err_t flash_erase(flash_handle_t dev, uint32_t offset_bytes, size_t size_bytes)
{
    stm_err_t s = flash_check(dev, offset_bytes, NULL, size_bytes, 0);
    if (s != STM_OK) { return s; }
    if (offset_bytes % FLASH_SECTOR_BYTES != 0U || size_bytes % FLASH_SECTOR_BYTES != 0U) {
        return STM_ERR_INVALID_ARG;
    }
    if (size_bytes == 0U) { return STM_OK; }
    s = flash_write_allowed(dev);
    if (s != STM_OK) { return s; }
    while (size_bytes != 0U) {
        s = flash_write_enable(dev);
        if (s != STM_OK) { return s; }
        s = flash_command(dev, 0x21U, 1, offset_bytes, 0U, HAL_OSPI_DATA_NONE, 0U);
        if (s != STM_OK) { return s; }
        s = flash_wait(dev, FLASH_ERASE_TIMEOUT);
        if (s != STM_OK) { return s; }
        s = flash_compare(dev, offset_bytes, NULL, FLASH_SECTOR_BYTES, 0);
        if (s != STM_OK) { return s; }
        offset_bytes += FLASH_SECTOR_BYTES; size_bytes -= FLASH_SECTOR_BYTES;
    }
    return STM_OK;
}

// 创建对象并独占对应外设
stm_err_t flash_create(const flash_config_t *config, flash_handle_t *out_handle)
{
    if (config == NULL || out_handle == NULL || config->hal == NULL) { return STM_ERR_INVALID_ARG; }
    if (*out_handle != NULL) { return STM_ERR_INVALID_STATE; }
    if (__get_IPSR() != 0U || __get_PRIMASK() != 0U ||
        __get_BASEPRI() != 0U || __get_FAULTMASK() != 0U) { return STM_ERR_INVALID_CONTEXT; }
    for (flash_handle_t it = g_devices; it != NULL; it = it->next) {
        if (it->hal->Instance == config->hal->Instance) { return STM_ERR_INVALID_STATE; }
    }
    flash_handle_t dev = calloc(1U, sizeof(*dev));
    if (dev == NULL) { return STM_ERR_NO_MEM; }
    stm_err_t err = flash_init_device(dev, config->hal, config->read_mode);
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
    info->jedec_id = handle->jedec_id;
    info->clock_hz = handle->clock_hz;
    info->last_hal_status = handle->last_hal_status;
    info->read_mode = handle->read_mode;
    info->ready = handle->ready;
    return STM_OK;
}
