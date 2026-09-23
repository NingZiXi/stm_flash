/** @file flash_ospi.c @brief STM32 HAL OSPI 同步传输；无器件型号判断。 */
#include "flash_ospi.h"
static stm_err_t validate(void *context, flash_host_info_t *info)
{
    flash_ospi_context_t *c = context;
    OSPI_HandleTypeDef *hal = c->hal;
    if (!hal || !hal->Instance)
    {
        return STM_ERR_INVALID_ARG;
    }
#if defined(IS_OSPI_ALL_INSTANCE)
    if (!IS_OSPI_ALL_INSTANCE(hal->Instance))
    {
        return STM_ERR_INVALID_CONFIG;
    }
#endif
    uint32_t kernel = c->kernel_clock_hz;
    if (HAL_OSPI_GetState(hal) != HAL_OSPI_STATE_READY || hal->Init.DualQuad != HAL_OSPI_DUALQUAD_DISABLE ||
        hal->Init.MemoryType != HAL_OSPI_MEMTYPE_MICRON || hal->Init.ClockMode != HAL_OSPI_CLOCK_MODE_0 ||
        hal->Init.WrapSize != HAL_OSPI_WRAP_NOT_SUPPORTED || hal->Init.ChipSelectBoundary != 0U ||
        hal->Init.FreeRunningClock != HAL_OSPI_FREERUNCLK_DISABLE || hal->Init.ClockPrescaler == 0U ||
        hal->Init.ClockPrescaler > 256U || hal->Init.ChipSelectHighTime < 4U || kernel == 0U ||
        (uint64_t)kernel > (uint64_t)50000000U * hal->Init.ClockPrescaler)
    {
        return STM_ERR_INVALID_CONFIG;
    }
    info->clock_hz = kernel / hal->Init.ClockPrescaler;
    info->max_transfer_bytes = 4096U;
    info->data_line_mask = (1U << 1U) | (1U << 4U);
    return STM_OK;
}

static stm_err_t geometry(void *context, uint32_t size_bytes, uint32_t max_clock_hz)
{
    flash_ospi_context_t *c = context;
    OSPI_HandleTypeDef *hal = c->hal;
    if (!hal || !hal->Instance)
    {
        return STM_ERR_INVALID_ARG;
    }
    if (hal->Init.DeviceSize >= 32U || (1ULL << hal->Init.DeviceSize) != size_bytes ||
        (uint64_t)c->kernel_clock_hz > (uint64_t)max_clock_hz * hal->Init.ClockPrescaler)
    {
        return STM_ERR_INVALID_CONFIG;
    }
    return STM_OK;
}

static HAL_StatusTypeDef command(flash_ospi_context_t *c, const flash_transfer_t *in, uint32_t timeout)
{
    OSPI_RegularCmdTypeDef cmd = {0};
    cmd.OperationType = HAL_OSPI_OPTYPE_COMMON_CFG;
    cmd.FlashId = HAL_OSPI_FLASH_ID_1;
    cmd.Instruction = in->opcode;
    cmd.InstructionMode = HAL_OSPI_INSTRUCTION_1_LINE;
    cmd.InstructionSize = HAL_OSPI_INSTRUCTION_8_BITS;
    cmd.AddressMode = in->address_bytes ? HAL_OSPI_ADDRESS_1_LINE : HAL_OSPI_ADDRESS_NONE;
    cmd.AddressSize = in->address_bytes == 3U ? HAL_OSPI_ADDRESS_24_BITS : HAL_OSPI_ADDRESS_32_BITS;
    cmd.Address = in->address;
    cmd.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode = in->size_bytes == 0U   ? HAL_OSPI_DATA_NONE
                   : in->data_lines == 4U ? HAL_OSPI_DATA_4_LINES
                                          : HAL_OSPI_DATA_1_LINE;
    cmd.NbData = in->size_bytes;
    cmd.DummyCycles = in->dummy_cycles;
    cmd.DQSMode = HAL_OSPI_DQS_DISABLE;
    cmd.SIOOMode = HAL_OSPI_SIOO_INST_EVERY_CMD;
    return HAL_OSPI_Command(c->hal, &cmd, timeout);
}

static const void *identity(void *p)
{
    flash_ospi_context_t *c = p;
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
    flash_ospi_context_t *c = p;
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
            c->last_hal_status = HAL_OSPI_Receive(c->hal, t->rx_data, timeout - spent);
        }
        else
        {
            c->last_hal_status = HAL_OSPI_Transmit(c->hal, (uint8_t *)t->tx_data, timeout - spent);
        }
    }
    return c->last_hal_status == HAL_OK ? STM_OK
                                        : (c->last_hal_status == HAL_TIMEOUT ? STM_ERR_TIMEOUT : STM_ERR_IO);
}
const flash_host_ops_t flash_ospi_ops = {.name = "stm32_hal_ospi",
                                         .validate = validate,
                                         .check_device = geometry,
                                         .identity = identity,
                                         .exec = exec_transfer,
                                         .get_tick_ms = tick,
                                         .delay_ms = delay,
                                         .check_context = context_ok,
                                         .buffer_ok = buffer_ok};
flash_host_t flash_ospi_bind(flash_ospi_context_t *c, OSPI_HandleTypeDef *hal, uint32_t hz)
{
    if (c)
    {
        c->hal = hal;
        c->kernel_clock_hz = hz;
        c->last_hal_status = HAL_OK;
    }
    return (flash_host_t){.ops = &flash_ospi_ops, .ctx = c};
}
