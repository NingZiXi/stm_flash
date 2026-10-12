/**
 * @file test_spi.c
 * @brief HAL SPI 适配器的独立事务、错误和配置回归。
 */
#include "flash_spi.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
#define REQUIRE(x)                                                                                 \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
            abort();                                                                               \
    } while (0)
uint32_t test_ipsr, test_primask, test_basepri, test_faultmask;
static uint32_t now, advance, calls, fail_at, aborts, cs_calls;
static uint32_t timeouts[80];
static uint8_t transmitted[8192];
static size_t sent;
static GPIO_PinState cs = GPIO_PIN_SET;
static HAL_StatusTypeDef injected, abort_result;
static HAL_SPI_StateTypeDef state = HAL_SPI_STATE_READY;
uint32_t HAL_GetTick(void)
{
    return now;
}
void HAL_Delay(uint32_t n)
{
    now += n;
}
HAL_SPI_StateTypeDef HAL_SPI_GetState(SPI_HandleTypeDef *h)
{
    (void)h;
    return state;
}
void HAL_GPIO_WritePin(GPIO_TypeDef *p, uint16_t n, GPIO_PinState v)
{
    REQUIRE(p && n == 16U);
    cs = v;
    ++cs_calls;
}
static HAL_StatusTypeDef record(uint8_t *tx, uint16_t n, uint32_t timeout)
{
    REQUIRE(cs == GPIO_PIN_RESET && timeout && calls < 80U && sent + n <= sizeof(transmitted));
    timeouts[calls++] = timeout;
    memcpy(transmitted + sent, tx, n);
    sent += n;
    now += advance;
    return fail_at == calls ? injected : HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *h, uint8_t *p, uint16_t n, uint32_t timeout)
{
    REQUIRE(h != NULL);
    return record(p, n, timeout);
}
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *h, uint8_t *tx, uint8_t *rx,
                                          uint16_t n, uint32_t timeout)
{
    REQUIRE(h && rx);
    for (uint16_t i = 0U; i < n; ++i)
    {
        REQUIRE(tx[i] == 0xFFU);
    }
    HAL_StatusTypeDef result = record(tx, n, timeout);
    if (result == HAL_OK)
    {
        memset(rx, 0xA5, n);
    }
    return result;
}
HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *h)
{
    REQUIRE(h && cs == GPIO_PIN_RESET);
    ++aborts;
    return abort_result;
}
static void reset(void)
{
    calls = aborts = cs_calls = sent = fail_at = advance = 0U;
    injected = HAL_ERROR;
    abort_result = HAL_OK;
    cs = GPIO_PIN_SET;
}
int main(void)
{
    int instance;
    SPI_HandleTypeDef h = {.Instance = &instance,
                           .Init = {
                               .Mode = SPI_MODE_MASTER,
                               .Direction = SPI_DIRECTION_2LINES,
                               .DataSize = SPI_DATASIZE_8BIT,
                               .NSS = SPI_NSS_SOFT,
                               .BaudRatePrescaler = SPI_BAUDRATEPRESCALER_8,
                           }};
    GPIO_TypeDef gpio;
    flash_spi_context_t c;
    flash_host_t host = flash_spi_bind(&c, &h, &gpio, 16U, 84000000U);
    CHECK(!cs_calls && !calls && host.ops->identity(host.ctx) == &instance);
    flash_host_info_t info;
    CHECK(host.ops->validate(host.ctx, &info) == STM_OK && info.clock_hz == 10500000U &&
          info.data_line_mask == 2U && info.max_transfer_bytes == 4096U);
    CHECK(host.ops->check_device(host.ctx, 0x1000000U, 50000000U) == STM_OK);
    CHECK(host.ops->check_device(host.ctx, 0x1000000U, 10000000U) == STM_ERR_INVALID_CONFIG);
    c.kernel_clock_hz = 0U;
    CHECK(host.ops->validate(host.ctx, &info) == STM_ERR_INVALID_CONFIG);
    c.kernel_clock_hz = 400000000U; // 分频 8，边界 50 MHz。
    CHECK(host.ops->validate(host.ctx, &info) == STM_OK && info.clock_hz == 50000000U);
    ++c.kernel_clock_hz;
    CHECK(host.ops->validate(host.ctx, &info) == STM_ERR_INVALID_CONFIG && !cs_calls);
    c.kernel_clock_hz = 84000000U;
    h.Init.CLKPolarity = SPI_POLARITY_HIGH;
    h.Init.CLKPhase = SPI_PHASE_2EDGE;
    CHECK(host.ops->validate(host.ctx, &info) == STM_OK);
    h.Init.CLKPhase = SPI_PHASE_1EDGE;
    CHECK(host.ops->validate(host.ctx, &info) == STM_ERR_INVALID_CONFIG);
    h.Init.CLKPolarity = SPI_POLARITY_LOW;
    uint32_t *fields[] = {
        &h.Init.Mode,     &h.Init.Direction, &h.Init.DataSize,       &h.Init.NSS,
        &h.Init.FirstBit, &h.Init.TIMode,    &h.Init.CRCCalculation, &h.Init.BaudRatePrescaler};
    for (unsigned i = 0U; i < sizeof(fields) / sizeof(fields[0]); ++i)
    {
        uint32_t saved = *fields[i];
        *fields[i] = 0x1234U;
        CHECK(host.ops->validate(host.ctx, &info) == STM_ERR_INVALID_CONFIG);
        *fields[i] = saved;
    }
    c.cs_pin = 3U;
    CHECK(host.ops->validate(host.ctx, &info) == STM_ERR_INVALID_ARG);
    c.cs_pin = 16U;
    c.cs_port = NULL;
    CHECK(host.ops->validate(host.ctx, &info) == STM_ERR_INVALID_ARG);
    c.cs_port = &gpio;
    h.Lock = HAL_LOCKED;
    CHECK(host.ops->validate(host.ctx, &info) == STM_ERR_INVALID_CONFIG);
    h.Lock = HAL_UNLOCKED;
    state = HAL_SPI_STATE_BUSY;
    CHECK(host.ops->validate(host.ctx, &info) == STM_ERR_INVALID_CONFIG);
    state = HAL_SPI_STATE_READY;
    uint8_t rx[130], tx[5] = {1U, 2U, 3U, 4U, 5U};
    flash_transfer_t t = {.opcode = 0x03U,
                          .instruction_lines = 1U,
                          .address_lines = 1U,
                          .address_bytes = 3U,
                          .address = 0xABCDEFU,
                          .data_lines = 1U,
                          .rx_data = rx,
                          .size_bytes = sizeof(rx)};
    now = UINT32_MAX - 2U;
    advance = 1U;
    CHECK(host.ops->exec(host.ctx, &t, 10U) == STM_OK && cs_calls == 2U && cs == GPIO_PIN_SET &&
          calls == 4U);
    CHECK(sent == 134U && transmitted[0] == 3U && transmitted[1] == 0xABU &&
          transmitted[2] == 0xCDU && transmitted[3] == 0xEFU && rx[0] == 0xA5U && rx[129] == 0xA5U);
    CHECK(timeouts[0] == 10U && timeouts[1] == 9U && timeouts[2] == 8U && timeouts[3] == 7U);
    reset();
    t.opcode = 0x13U;
    t.address_bytes = 4U;
    t.address = 0x12345678U;
    t.dummy_cycles = 8U;
    CHECK(host.ops->exec(host.ctx, &t, 10U) == STM_OK && transmitted[1] == 0x12U &&
          transmitted[4] == 0x78U && transmitted[5] == 0xFFU);
    reset();
    t.opcode = 0x02U;
    t.address_bytes = 3U;
    t.address = 0xFFFFFAU;
    t.dummy_cycles = 0U;
    t.rx_data = NULL;
    t.tx_data = tx;
    t.size_bytes = sizeof(tx);
    CHECK(host.ops->exec(host.ctx, &t, 10U) == STM_OK && calls == 2U &&
          !memcmp(transmitted + 4U, tx, sizeof(tx)));
    reset();
    t.data_lines = 4U;
    CHECK(host.ops->exec(host.ctx, &t, 10U) == STM_ERR_NOT_SUPPORTED && !cs_calls);
    t.data_lines = 1U;
    t.dummy_cycles = 7U;
    CHECK(host.ops->exec(host.ctx, &t, 10U) == STM_ERR_NOT_SUPPORTED && !cs_calls);
    t.dummy_cycles = 0U;
    t.address = 0x1000000U;
    CHECK(host.ops->exec(host.ctx, &t, 10U) == STM_ERR_OUT_OF_RANGE && !cs_calls);
    t.address = 0U;
    t.rx_data = rx;
    CHECK(host.ops->exec(host.ctx, &t, 10U) == STM_ERR_INVALID_ARG && !cs_calls);
    t.rx_data = NULL;
    t.size_bytes = 4097U;
    CHECK(host.ops->exec(host.ctx, &t, 10U) == STM_ERR_INVALID_ARG && !cs_calls);
    t.size_bytes = sizeof(tx);
    CHECK(host.ops->exec(host.ctx, &t, 0U) == STM_ERR_INVALID_ARG && !cs_calls);
    CHECK(host.ops->exec(host.ctx, &t, HAL_MAX_DELAY) == STM_ERR_INVALID_ARG && !cs_calls);
    uint32_t *masks[] = {&test_ipsr, &test_primask, &test_basepri, &test_faultmask};
    for (unsigned i = 0U; i < 4U; ++i)
    {
        *masks[i] = 1U;
        CHECK(host.ops->exec(host.ctx, &t, 10U) == STM_ERR_INVALID_CONTEXT && !cs_calls);
        *masks[i] = 0U;
    }
    for (unsigned stage = 1U; stage <= 2U; ++stage)
    {
        reset();
        flash_spi_bind(&c, &h, &gpio, 16U, 84000000U);
        fail_at = stage;
        CHECK(host.ops->exec(host.ctx, &t, 10U) == STM_ERR_IO && cs == GPIO_PIN_SET &&
              aborts == 1U && c.faulted);
        CHECK(c.last_hal_status == HAL_ERROR && c.last_abort_status == HAL_OK);
        unsigned before = cs_calls;
        CHECK(host.ops->exec(host.ctx, &t, 10U) == STM_ERR_INVALID_STATE && cs_calls == before);
    }
    reset();
    flash_spi_bind(&c, &h, &gpio, 16U, 84000000U);
    advance = 10U;
    CHECK(host.ops->exec(host.ctx, &t, 10U) == STM_ERR_TIMEOUT && calls == 1U && aborts == 1U &&
          cs == GPIO_PIN_SET);
    reset();
    flash_spi_bind(&c, &h, &gpio, 16U, 84000000U);
    fail_at = 1U;
    injected = HAL_TIMEOUT;
    abort_result = HAL_ERROR;
    CHECK(host.ops->exec(host.ctx, &t, 10U) == STM_ERR_TIMEOUT &&
          c.last_abort_status == HAL_ERROR && c.faulted);
    reset();
    flash_spi_bind(&c, &h, &gpio, 16U, 84000000U);
    t.opcode = 0x9FU;
    t.address_bytes = t.address_lines = 0U;
    t.tx_data = NULL;
    t.rx_data = rx;
    t.size_bytes = sizeof(rx);
    fail_at = 3U;
    CHECK(host.ops->exec(host.ctx, &t, 10U) == STM_ERR_IO && calls == 3U && aborts == 1U &&
          cs == GPIO_PIN_SET);
    reset();
    flash_spi_bind(&c, &h, &gpio, 16U, 84000000U);
    t.opcode = 0x06U;
    t.rx_data = NULL;
    t.size_bytes = t.data_lines = 0U;
    CHECK(host.ops->exec(host.ctx, &t, 10U) == STM_OK && calls == 1U && sent == 1U &&
          cs_calls == 2U && transmitted[0] == 0x06U);
    for (unsigned stage = 1U; stage <= 4U; ++stage)
    {
        reset();
        flash_spi_bind(&c, &h, &gpio, 16U, 84000000U);
        t.opcode = 0x9FU;
        t.rx_data = rx;
        t.size_bytes = sizeof(rx);
        t.data_lines = 1U;
        fail_at = stage;
        injected = HAL_TIMEOUT;
        CHECK(host.ops->exec(host.ctx, &t, 10U) == STM_ERR_TIMEOUT && calls == stage && c.faulted &&
              aborts == 1U && cs == GPIO_PIN_SET);
    }
    CHECK(!host.ops->buffer_ok(host.ctx, NULL, 1U));
    CHECK(!host.ops->buffer_ok(host.ctx, (void *)(uintptr_t)0x90000000U, 4U));
    CHECK(!host.ops->buffer_ok(host.ctx, (void *)(uintptr_t)(UINTPTR_MAX - 1U), 4U));
    puts("PASS SPI: binding/config, CS, 3/4-byte headers, FF RX, shared timeout/wrap, abort/fault "
         "latch, contexts");
    return 0;
}
