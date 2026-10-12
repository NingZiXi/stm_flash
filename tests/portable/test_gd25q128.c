/**
 * @file test_gd25q128.c
 * @brief GD25Q128 三字节协议和完整容量的软件回归。
 */
#include "stm_flash.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do                                                                                             \
    {                                                                                              \
        if (!(x))                                                                                  \
        {                                                                                          \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
static uint8_t memory[16U * 1024U * 1024U];
static uint8_t status[3];
static uint32_t writes, erases, wrens, now, id = 0xC84018U;
static int busy;
static stm_err_t validate(void *p, flash_host_info_t *out)
{
    (void)p;
    *out =
        (flash_host_info_t){.clock_hz = 10500000U, .max_transfer_bytes = 64U, .data_line_mask = 2U};
    return STM_OK;
}
static stm_err_t geometry(void *p, uint32_t size, uint32_t hz)
{
    (void)p;
    return size == sizeof(memory) && hz == 50000000U ? STM_OK : STM_ERR_INVALID_CONFIG;
}
static const void *identity(void *p)
{
    return p;
}
static uint32_t tick(void *p)
{
    (void)p;
    return now;
}
static void delay(void *p, uint32_t n)
{
    (void)p;
    now += n;
}
static stm_err_t context(void *p)
{
    (void)p;
    return STM_OK;
}
static stm_err_t exec(void *p, const flash_transfer_t *t, uint32_t timeout)
{
    (void)p;
    if (!timeout || t->instruction_lines != 1U || t->size_bytes > 64U || t->dummy_cycles ||
        (t->size_bytes && t->data_lines != 1U))
    {
        return STM_ERR_INVALID_CONFIG;
    }
    uint8_t *rx = t->rx_data;
    switch (t->opcode)
    {
    case 0x9F:
        if (t->size_bytes != 3U || t->address_bytes)
        {
            return STM_ERR_INVALID_CONFIG;
        }
        rx[0] = (uint8_t)(id >> 16U);
        rx[1] = (uint8_t)(id >> 8U);
        rx[2] = (uint8_t)id;
        return STM_OK;
    case 0x05:
        rx[0] = status[0] | (busy ? 1U : 0U);
        return STM_OK;
    case 0x35:
        rx[0] = status[1];
        return STM_OK;
    case 0x15:
        rx[0] = status[2];
        return STM_OK;
    case 0x06:
        status[0] |= 2U;
        ++wrens;
        return STM_OK;
    default:
        break;
    }
    if (t->address_bytes != 3U || t->address_lines != 1U || t->address >= sizeof(memory) ||
        t->size_bytes > sizeof(memory) - t->address)
    {
        return STM_ERR_OUT_OF_RANGE;
    }
    if (t->opcode == 0x03)
    {
        memcpy(rx, memory + t->address, t->size_bytes);
        return STM_OK;
    }
    if (!(status[0] & 2U))
    {
        return STM_ERR_INVALID_STATE;
    }
    if (t->opcode == 0x02)
    {
        if (!t->tx_data || !t->size_bytes || t->size_bytes > 256U - t->address % 256U)
        {
            return STM_ERR_INVALID_CONFIG;
        }
        const uint8_t *tx = t->tx_data;
        for (size_t i = 0U; i < t->size_bytes; ++i)
        {
            memory[t->address + i] &= tx[i];
        }
        ++writes;
    }
    else if (t->opcode == 0x20)
    {
        if (t->size_bytes || t->address % 4096U || t->address > sizeof(memory) - 4096U)
        {
            return STM_ERR_INVALID_CONFIG;
        }
        memset(memory + t->address, 0xFF, 4096U);
        ++erases;
    }
    else
    {
        return STM_ERR_NOT_SUPPORTED;
    }
    status[0] &= (uint8_t)~2U;
    return STM_OK;
}
static const flash_host_ops_t ops = {
    .name = "gd25q128-model",
    .validate = validate,
    .check_device = geometry,
    .identity = identity,
    .exec = exec,
    .get_tick_ms = tick,
    .delay_ms = delay,
    .check_context = context,
};
int main(void)
{
    memset(memory, 0xFF, sizeof(memory));
    int token;
    flash_config_t c = {.host = {.ops = &ops, .ctx = &token}, .read_mode = FLASH_READ_SINGLE};
    flash_handle_t dev = NULL;
    CHECK(flash_create(&c, &dev) == STM_OK); // AUTO 内置入口。
    flash_info_t info;
    CHECK(flash_get_info(dev, &info) == STM_OK && info.device == &flash_device_gd25q128 &&
          info.jedec_id == 0xC84018U && info.size_bytes == sizeof(memory));
    CHECK(!wrens && !writes && !erases);
    uint8_t tx[300], rx[300];
    for (unsigned i = 0U; i < sizeof(tx); ++i)
    {
        tx[i] = (uint8_t)(i * 7U);
    }
    CHECK(flash_write(dev, 250U, tx, sizeof(tx)) == STM_OK && writes == 6U);
    CHECK(flash_read(dev, 250U, rx, sizeof(rx)) == STM_OK && !memcmp(tx, rx, sizeof(tx)));
    uint8_t zero = 0U;
    CHECK(flash_write(dev, 0xFFFFFFU, &zero, 1U) == STM_OK);
    CHECK(flash_read(dev, 0xFFFFFFU, rx, 1U) == STM_OK && !rx[0]);
    CHECK(flash_read(dev, 0xFFFFFFU, rx, 2U) == STM_ERR_OUT_OF_RANGE);
    CHECK(flash_erase(dev, 0xFFF000U, 4096U) == STM_OK && erases == 1U);
    CHECK(flash_read(dev, 0xFFFFFFU, rx, 1U) == STM_OK && rx[0] == 0xFFU);
    CHECK(flash_erase(dev, 1U, 4096U) == STM_ERR_INVALID_ARG);
    CHECK(flash_write(dev, 250U, rx, 1U) == FLASH_ERR_NEEDS_ERASE);
    const uint8_t masks[] = {4U, 8U, 16U, 32U, 64U, 0x40U, 4U};
    for (unsigned i = 0U; i < sizeof(masks); ++i)
    {
        unsigned index = i < 5U ? 0U : i - 4U;
        status[index] = masks[i];
        uint32_t before = wrens;
        CHECK(flash_write(dev, 0U, &zero, 1U) == FLASH_ERR_PROTECTED);
        CHECK(flash_erase(dev, 0U, 4096U) == FLASH_ERR_PROTECTED && wrens == before);
        CHECK(flash_read(dev, 0U, rx, 1U) == STM_OK);
        status[index] = 0U;
    }
    status[1] = 2U; // QE 仅报告，不妨碍单线，也不改状态寄存器。
    flash_status_t s;
    CHECK(flash_get_status(dev, &s) == STM_OK && (s.flags & FLASH_STATUS_QUAD_ENABLED));
    for (unsigned i = 0U; i < 2U; ++i)
    {
        status[1] = i ? 4U : 128U;
        uint32_t before = wrens;
        CHECK(flash_write(dev, 0U, &zero, 1U) == FLASH_ERR_SUSPENDED && wrens == before);
    }
    status[1] = 0U;
    now = UINT32_MAX - 2U;
    busy = 1;
    CHECK(flash_read(dev, 0U, rx, 1U) == STM_ERR_TIMEOUT);
    CHECK(flash_get_info(dev, &info) == STM_OK && !info.ready);
    CHECK(flash_delete(&dev) == STM_OK);
    busy = 0;
    c.device = &flash_device_gd25q128;
    c.read_mode = FLASH_READ_QUAD;
    CHECK(flash_create(&c, &dev) == STM_ERR_NOT_SUPPORTED && !dev);
    c.read_mode = FLASH_READ_SINGLE;
    id = 0xC84019U;
    CHECK(flash_create(&c, &dev) == STM_ERR_INVALID_CONFIG && !dev);
    id = 0xC84018U;
    flash_device_t readonly = flash_device_gd25q128;
    readonly.capabilities = FLASH_CAP_READ_SINGLE;
    c.device = &readonly;
    CHECK(flash_create(&c, &dev) == STM_OK);
    uint32_t previous_wrens = wrens, previous_writes = writes, previous_erases = erases;
    CHECK(flash_write(dev, 0U, &zero, 1U) == STM_ERR_NOT_SUPPORTED);
    CHECK(flash_erase(dev, 0U, 4096U) == STM_ERR_NOT_SUPPORTED);
    CHECK(wrens == previous_wrens && writes == previous_writes && erases == previous_erases);
    CHECK(flash_read(dev, 0U, rx, sizeof(rx)) == STM_OK);
    CHECK(flash_delete(&dev) == STM_OK && !dev);
    puts("PASS GD25Q128: AUTO, 3-byte protocol, full 16MiB endpoints, pages/chunks, BP/CMP/WPS, "
         "SUS/QE, timeout");
    return 0;
}
