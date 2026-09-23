/** @file flash_qspi.c @brief STM32 HAL QSPI 同步传输；无器件型号判断。 */
#include "flash_qspi.h"
static stm_err_t validate(void *context, flash_host_info_t *info)
{
    flash_qspi_context_t *c = context;
    QSPI_HandleTypeDef *hal = c->hal;
    if (!hal || !hal->Instance)
    {
        return STM_ERR_INVALID_ARG;
    }
#if defined(IS_QSPI_ALL_INSTANCE)
    if (!IS_QSPI_ALL_INSTANCE(hal->Instance))
    {
        return STM_ERR_INVALID_CONFIG;
    }
#endif
    uint32_t kernel = c->kernel_clock_hz;
    if (HAL_QSPI_GetState(hal) != HAL_QSPI_STATE_READY || hal->Init.DualFlash != QSPI_DUALFLASH_DISABLE ||
        (hal->Init.FlashID != QSPI_FLASH_ID_1 && hal->Init.FlashID != QSPI_FLASH_ID_2) ||
        (hal->Init.ClockMode != QSPI_CLOCK_MODE_0 && hal->Init.ClockMode != QSPI_CLOCK_MODE_3) ||
        (hal->Init.SampleShifting != QSPI_SAMPLE_SHIFTING_NONE &&
         hal->Init.SampleShifting != QSPI_SAMPLE_SHIFTING_HALFCYCLE) ||
        hal->Init.ClockPrescaler > 255U || hal->Init.FifoThreshold == 0U || hal->Init.FifoThreshold > 32U ||
        kernel == 0U || hal->Init.ChipSelectHighTime < QSPI_CS_HIGH_TIME_4_CYCLE ||
        hal->Init.ChipSelectHighTime > QSPI_CS_HIGH_TIME_8_CYCLE ||
        (uint64_t)kernel > (uint64_t)50000000U * (hal->Init.ClockPrescaler + 1U))
    {
        return STM_ERR_INVALID_CONFIG;
    }
    info->clock_hz = kernel / (hal->Init.ClockPrescaler + 1U);
    info->max_transfer_bytes = 4096U;
    info->data_line_mask = (1U << 1U) | (1U << 4U);
    return STM_OK;
}

static stm_err_t geometry(void *context, uint32_t size_bytes, uint32_t max_clock_hz)
{
    flash_qspi_context_t *c = context;
    QSPI_HandleTypeDef *hal = c->hal;
    if (!hal || !hal->Instance)
    {
        return STM_ERR_INVALID_ARG;
    }
    // QSPI 的 FSIZE = 地址位数 - 1，与 OSPI 的 DeviceSize 不同。
    if (hal->Init.FlashSize >= 31U || (1ULL << (hal->Init.FlashSize + 1U)) != size_bytes ||
        (uint64_t)c->kernel_clock_hz > (uint64_t)max_clock_hz * (hal->Init.ClockPrescaler + 1U))
    {
        return STM_ERR_INVALID_CONFIG;
    }
    return STM_OK;
}

static HAL_StatusTypeDef command(flash_qspi_context_t *c, const flash_transfer_t *in, uint32_t timeout)
{
    QSPI_CommandTypeDef cmd = {0};
    cmd.Instruction = in->opcode;
    cmd.InstructionMode = QSPI_INSTRUCTION_1_LINE;
    cmd.AddressMode = in->address_bytes ? QSPI_ADDRESS_1_LINE : QSPI_ADDRESS_NONE;
    cmd.AddressSize = in->address_bytes == 3U ? QSPI_ADDRESS_24_BITS : QSPI_ADDRESS_32_BITS;
    cmd.Address = in->address;
    cmd.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode = in->size_bytes == 0U   ? QSPI_DATA_NONE
                   : in->data_lines == 4U ? QSPI_DATA_4_LINES
                                          : QSPI_DATA_1_LINE;
    cmd.NbData = in->size_bytes;
    cmd.DummyCycles = in->dummy_cycles;
    cmd.DdrMode = QSPI_DDR_MODE_DISABLE;
    cmd.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
    cmd.SIOOMode = QSPI_SIOO_INST_EVERY_CMD;
    return HAL_QSPI_Command(c->hal, &cmd, timeout);
}

static const void *identity(void *p)
{
    flash_qspi_context_t *c = p;
    return c->hal ? c->hal->Instance : NULL;
}
static uint32_t tick(void *p)
{
    (void)p;
    return HAL_GetTick();
}
static void delay(void *p, uint32_t ms)
{
    (void)p;
    HAL_Delay(ms);
}
static stm_err_t context_ok(void *p)
{
    (void)p;
    return __get_IPSR() == 0U && __get_PRIMASK() == 0U && __get_BASEPRI() == 0U && __get_FAULTMASK() == 0U
               ? STM_OK
               : STM_ERR_INVALID_CONTEXT;
}
static int buffer_ok(void *p, const void *data, size_t n)
{
    (void)p;
    uintptr_t a = (uintptr_t)data;
    /* 拒绝串行存储映射区；其他缓冲区须由 BSP 确认可直接访问。 */
    return !(a < 0xB0000000UL && a + n > 0x90000000UL);
}
static stm_err_t exec_transfer(void *p, const flash_transfer_t *t, uint32_t timeout)
{
    flash_qspi_context_t *c = p;
    if (!t || !c->hal || !timeout || t->size_bytes > 4096U)
    {
        return STM_ERR_INVALID_ARG;
    }
    if (t->instruction_lines != 1U ||
        (t->address_bytes &&
         (t->address_lines != 1U || (t->address_bytes != 3U && t->address_bytes != 4U))) ||
        (!t->address_bytes && t->address_lines) || t->dummy_cycles > 31U ||
        (t->size_bytes && t->data_lines != 1U && t->data_lines != 4U))
    {
        return STM_ERR_NOT_SUPPORTED;
    }
    if ((t->size_bytes && ((!t->tx_data) == (!t->rx_data))) ||
        (!t->size_bytes && (t->tx_data || t->rx_data || t->data_lines)))
    {
        return STM_ERR_INVALID_ARG;
    }
    uint32_t start = HAL_GetTick();
    c->last_hal_status = command(c, t, timeout);
    if (c->last_hal_status == HAL_OK && t->size_bytes)
    {
        uint32_t spent = (uint32_t)(HAL_GetTick() - start);
        if (spent >= timeout)
        {
            c->last_hal_status = HAL_TIMEOUT;
        }
        else if (t->rx_data)
        {
            c->last_hal_status = HAL_QSPI_Receive(c->hal, t->rx_data, timeout - spent);
        }
        else
        {
            c->last_hal_status = HAL_QSPI_Transmit(c->hal, (uint8_t *)t->tx_data, timeout - spent);
        }
    }
    return c->last_hal_status == HAL_OK ? STM_OK
                                        : (c->last_hal_status == HAL_TIMEOUT ? STM_ERR_TIMEOUT : STM_ERR_IO);
}
const flash_host_ops_t flash_qspi_ops = {.name = "stm32_hal_qspi",
                                         .validate = validate,
                                         .check_device = geometry,
                                         .identity = identity,
                                         .exec = exec_transfer,
                                         .get_tick_ms = tick,
                                         .delay_ms = delay,
                                         .check_context = context_ok,
                                         .buffer_ok = buffer_ok};
flash_host_t flash_qspi_bind(flash_qspi_context_t *c, QSPI_HandleTypeDef *hal, uint32_t hz)
{
    if (c)
    {
        c->hal = hal;
        c->kernel_clock_hz = hz;
        c->last_hal_status = HAL_OK;
    }
    return (flash_host_t){.ops = &flash_qspi_ops, .ctx = c};
}
