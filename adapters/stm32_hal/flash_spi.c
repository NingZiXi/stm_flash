/**
 * @file flash_spi.c
 * @brief 普通 SPI 事务与片选管理；不包含器件协议或 DMA。
 */
#include "flash_spi.h"
#include <string.h>

#define SPI_TRANSFER_LIMIT 4096U
#define SPI_RX_CHUNK 64U

static stm_err_t context_ok(void *p)
{
    (void)p;
    return __get_IPSR() == 0U && __get_PRIMASK() == 0U && __get_BASEPRI() == 0U &&
                   __get_FAULTMASK() == 0U
               ? STM_OK
               : STM_ERR_INVALID_CONTEXT;
}

static uint32_t prescaler_divisor(uint32_t setting)
{
    switch (setting)
    {
    case SPI_BAUDRATEPRESCALER_2:
        return 2U;
    case SPI_BAUDRATEPRESCALER_4:
        return 4U;
    case SPI_BAUDRATEPRESCALER_8:
        return 8U;
    case SPI_BAUDRATEPRESCALER_16:
        return 16U;
    case SPI_BAUDRATEPRESCALER_32:
        return 32U;
    case SPI_BAUDRATEPRESCALER_64:
        return 64U;
    case SPI_BAUDRATEPRESCALER_128:
        return 128U;
    case SPI_BAUDRATEPRESCALER_256:
        return 256U;
    default:
        return 0U;
    }
}

static stm_err_t validate(void *p, flash_host_info_t *info)
{
    flash_spi_context_t *c = p;
    if (!c || !info || !c->hal || !c->hal->Instance || !c->cs_port || !c->cs_pin ||
        (c->cs_pin & (c->cs_pin - 1U)))
    {
        return STM_ERR_INVALID_ARG;
    }
    SPI_HandleTypeDef *h = c->hal;
#if defined(IS_SPI_ALL_INSTANCE)
    if (!IS_SPI_ALL_INSTANCE(h->Instance))
    {
        return STM_ERR_INVALID_CONFIG;
    }
#endif
    if (c->faulted)
    {
        return STM_ERR_INVALID_STATE;
    }
    uint32_t divisor = prescaler_divisor(h->Init.BaudRatePrescaler);
    int mode_ok =
        (h->Init.CLKPolarity == SPI_POLARITY_LOW && h->Init.CLKPhase == SPI_PHASE_1EDGE) ||
        (h->Init.CLKPolarity == SPI_POLARITY_HIGH && h->Init.CLKPhase == SPI_PHASE_2EDGE);
    if (!divisor || c->kernel_clock_hz < divisor ||
        (uint64_t)c->kernel_clock_hz > (uint64_t)50000000U * divisor || !mode_ok ||
        HAL_SPI_GetState(h) != HAL_SPI_STATE_READY || h->Lock != HAL_UNLOCKED ||
        h->Init.Mode != SPI_MODE_MASTER || h->Init.Direction != SPI_DIRECTION_2LINES ||
        h->Init.DataSize != SPI_DATASIZE_8BIT || h->Init.NSS != SPI_NSS_SOFT ||
        h->Init.FirstBit != SPI_FIRSTBIT_MSB || h->Init.TIMode != SPI_TIMODE_DISABLE ||
        h->Init.CRCCalculation != SPI_CRCCALCULATION_DISABLE)
    {
        return STM_ERR_INVALID_CONFIG;
    }
    info->clock_hz = c->kernel_clock_hz / divisor;
    info->max_transfer_bytes = SPI_TRANSFER_LIMIT;
    info->data_line_mask = 1U << 1U;
    return STM_OK;
}

static stm_err_t geometry(void *p, uint32_t size, uint32_t max_hz)
{
    flash_spi_context_t *c = p;
    flash_host_info_t info;
    stm_err_t result = validate(p, &info);
    if (result != STM_OK)
    {
        return result;
    }
    uint32_t divisor = prescaler_divisor(c->hal->Init.BaudRatePrescaler);
    if (!size || !max_hz || (uint64_t)c->kernel_clock_hz > (uint64_t)max_hz * divisor)
    {
        return STM_ERR_INVALID_CONFIG;
    }
    return STM_OK;
}

static const void *identity(void *p)
{
    flash_spi_context_t *c = p;
    return c && c->hal ? c->hal->Instance : NULL;
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

static int buffer_ok(void *p, const void *data, size_t size)
{
    (void)p;
    uintptr_t address = (uintptr_t)data;
    if (!data || size > UINTPTR_MAX - address)
    {
        return 0;
    }
    // 不从串行存储映射区发起事务；其他缓冲的可访问性由板级保证。
    return !(address < 0xB0000000UL && address + size > 0x90000000UL);
}

static uint32_t remaining(uint32_t start, uint32_t timeout)
{
    uint32_t elapsed = (uint32_t)(HAL_GetTick() - start);
    return elapsed < timeout ? timeout - elapsed : 0U;
}

static stm_err_t exec_transfer(void *p, const flash_transfer_t *t, uint32_t timeout)
{
    flash_spi_context_t *c = p;
    if (!c || !t || !timeout || timeout == HAL_MAX_DELAY || t->size_bytes > SPI_TRANSFER_LIMIT ||
        (t->size_bytes && ((!t->tx_data) == (!t->rx_data))) ||
        (!t->size_bytes && (t->tx_data || t->rx_data || t->data_lines)))
    {
        return STM_ERR_INVALID_ARG;
    }
    if (t->instruction_lines != 1U || t->dummy_cycles > 32U || t->dummy_cycles % 8U ||
        (t->address_bytes &&
         (t->address_lines != 1U || (t->address_bytes != 3U && t->address_bytes != 4U))) ||
        (!t->address_bytes && t->address_lines) || (t->size_bytes && t->data_lines != 1U))
    {
        return STM_ERR_NOT_SUPPORTED;
    }
    if (t->address_bytes == 3U && t->address > 0xFFFFFFU)
    {
        return STM_ERR_OUT_OF_RANGE;
    }
    stm_err_t result = context_ok(p);
    if (result != STM_OK)
    {
        return result;
    }
    flash_host_info_t info;
    result = validate(p, &info);
    if (result != STM_OK)
    {
        return result;
    }
    if (t->size_bytes && !buffer_ok(p, t->tx_data ? t->tx_data : t->rx_data, t->size_bytes))
    {
        return STM_ERR_INVALID_ARG;
    }
    uint8_t header[9] = {t->opcode};
    unsigned n = 1U;
    for (unsigned i = t->address_bytes; i > 0U; --i)
    {
        header[n++] = (uint8_t)(t->address >> ((i - 1U) * 8U));
    }
    for (unsigned i = 0U; i < t->dummy_cycles / 8U; ++i)
    {
        header[n++] = 0xFFU;
    }
    uint32_t start = HAL_GetTick();
    HAL_GPIO_WritePin(c->cs_port, c->cs_pin, GPIO_PIN_RESET);
    uint32_t left = remaining(start, timeout);
    c->last_hal_status = left ? HAL_SPI_Transmit(c->hal, header, (uint16_t)n, left) : HAL_TIMEOUT;
    if (c->last_hal_status == HAL_OK && t->tx_data)
    {
        left = remaining(start, timeout);
        c->last_hal_status =
            left ? HAL_SPI_Transmit(c->hal, (uint8_t *)t->tx_data, (uint16_t)t->size_bytes, left)
                 : HAL_TIMEOUT;
    }
    if (c->last_hal_status == HAL_OK && t->rx_data)
    {
        // 固定发送 FF，不将未初始化的接收缓冲当作 MOSI 数据。
        uint8_t dummy[SPI_RX_CHUNK];
        memset(dummy, 0xFF, sizeof(dummy));
        size_t done = 0U;
        while (done < t->size_bytes && c->last_hal_status == HAL_OK)
        {
            size_t count = t->size_bytes - done;
            if (count > sizeof(dummy))
            {
                count = sizeof(dummy);
            }
            left = remaining(start, timeout);
            c->last_hal_status =
                left ? HAL_SPI_TransmitReceive(c->hal, dummy, (uint8_t *)t->rx_data + done,
                                               (uint16_t)count, left)
                     : HAL_TIMEOUT;
            done += count;
        }
    }
    if (c->last_hal_status == HAL_OK && !remaining(start, timeout))
    {
        c->last_hal_status = HAL_TIMEOUT;
    }
    if (c->last_hal_status != HAL_OK)
    {
        // 终止本次阻塞事务，不自动重试可能已部分生效的写命令。
        c->faulted = 1U;
        c->last_abort_status = HAL_SPI_Abort(c->hal);
    }
    HAL_GPIO_WritePin(c->cs_port, c->cs_pin, GPIO_PIN_SET);
    return c->last_hal_status == HAL_OK
               ? STM_OK
               : (c->last_hal_status == HAL_TIMEOUT ? STM_ERR_TIMEOUT : STM_ERR_IO);
}

const flash_host_ops_t flash_spi_ops = {
    .name = "stm32_hal_spi",
    .validate = validate,
    .check_device = geometry,
    .identity = identity,
    .exec = exec_transfer,
    .get_tick_ms = tick,
    .delay_ms = delay,
    .check_context = context_ok,
    .buffer_ok = buffer_ok,
};

flash_host_t flash_spi_bind(flash_spi_context_t *c, SPI_HandleTypeDef *hal, GPIO_TypeDef *cs_port,
                            uint16_t cs_pin, uint32_t hz)
{
    if (c)
    {
        *c = (flash_spi_context_t){
            .hal = hal,
            .cs_port = cs_port,
            .cs_pin = cs_pin,
            .kernel_clock_hz = hz,
            .last_hal_status = HAL_OK,
            .last_abort_status = HAL_OK,
        };
    }
    return (flash_host_t){.ops = &flash_spi_ops, .ctx = c};
}
