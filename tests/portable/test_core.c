/** @file test_core.c @brief 原生 C 测试；没有 HAL/CMSIS 头、宏、库或地址映射。 */
#include "stm_flash.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x)                                                                                             \
    do                                                                                                       \
    {                                                                                                        \
        if (!(x))                                                                                            \
        {                                                                                                    \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                                  \
            return 1;                                                                                        \
        }                                                                                                    \
    } while (0)
typedef struct
{
    uint8_t memory[8192], status[3];
    uint32_t id, tick, calls, writes, erases;
    int blocked, busy;
} model_t;
static stm_err_t validate(void *p, flash_host_info_t *info)
{
    (void)p;
    *info = (flash_host_info_t){
        .clock_hz = 1000000U, .max_transfer_bytes = 64U, .data_line_mask = (1U << 1U) | (1U << 4U)};
    return STM_OK;
}
static stm_err_t check_device(void *p, uint32_t size, uint32_t max_hz)
{
    (void)p;
    return size == 8192U && max_hz >= 1000000U ? STM_OK : STM_ERR_INVALID_CONFIG;
}
static const void *identity(void *p)
{
    return p;
}
static uint32_t tick(void *p)
{
    return ((model_t *)p)->tick;
}
static void delay(void *p, uint32_t ms)
{
    ((model_t *)p)->tick += ms;
}
static stm_err_t context(void *p)
{
    return ((model_t *)p)->blocked ? STM_ERR_INVALID_CONTEXT : STM_OK;
}
static stm_err_t exec(void *p, const flash_transfer_t *t, uint32_t timeout)
{
    model_t *m = p;
    ++m->calls;
    if (!timeout || t->size_bytes > 64U)
    {
        return STM_ERR_INVALID_ARG;
    }
    uint8_t *rx = t->rx_data;
    const uint8_t *tx = t->tx_data;
    switch (t->opcode)
    {
    case 0x9F:
        rx[0] = (uint8_t)(m->id >> 16);
        rx[1] = (uint8_t)(m->id >> 8);
        rx[2] = (uint8_t)m->id;
        return STM_OK;
    case 0x05:
        rx[0] = m->status[0] | (m->busy ? 1U : 0U);
        return STM_OK;
    case 0x35:
        rx[0] = m->status[1];
        return STM_OK;
    case 0x15:
        rx[0] = m->status[2];
        return STM_OK;
    case 0x06:
        m->status[0] |= 2U;
        return STM_OK;
    default:
        break;
    }
    if (t->address > sizeof(m->memory) || t->size_bytes > sizeof(m->memory) - t->address)
    {
        return STM_ERR_OUT_OF_RANGE;
    }
    if (t->opcode == 0x13 || t->opcode == 0x6C)
    {
        memcpy(rx, m->memory + t->address, t->size_bytes);
        return STM_OK;
    }
    if (!(m->status[0] & 2U))
    {
        return FLASH_ERR_PROTECTED;
    }
    if (t->opcode == 0x12)
    {
        if (t->size_bytes > 256U - t->address % 256U)
        {
            return STM_ERR_INVALID_ARG;
        }
        for (size_t i = 0; i < t->size_bytes; ++i)
        {
            m->memory[t->address + i] &= tx[i];
        }
        ++m->writes;
    }
    else if (t->opcode == 0x21)
    {
        if (t->address % 4096U || t->address > sizeof(m->memory) - 4096U)
        {
            return STM_ERR_INVALID_ARG;
        }
        memset(m->memory + t->address, 0xFF, 4096U);
        ++m->erases;
    }
    else
    {
        return STM_ERR_NOT_SUPPORTED;
    }
    m->status[0] &= (uint8_t)~2U;
    return STM_OK;
}
static const flash_host_ops_t host_ops = {.name = "native-test",
                                          .validate = validate,
                                          .check_device = check_device,
                                          .identity = identity,
                                          .exec = exec,
                                          .get_tick_ms = tick,
                                          .delay_ms = delay,
                                          .check_context = context};
static unsigned overridden_reads;
static stm_err_t custom_read(const flash_chip_io_t *io, uint32_t a, void *p, size_t n)
{
    ++overridden_reads;
    return flash_nor_read(io, a, p, n);
}
int main(void)
{
    model_t first = {.id = 0xC84019U}, second = {.id = 0xC84019U};
    memset(first.memory, 0xFF, sizeof(first.memory));
    memset(second.memory, 0xFF, sizeof(second.memory));
    flash_chip_ops_t custom_ops = flash_nor_ops;
    custom_ops.read = custom_read;
    flash_device_t custom = flash_device_gd25q256e;
    custom.name = "test-geometry";
    custom.size_bytes = 8192U;
    custom.ops = &custom_ops;
    flash_config_t config = {
        .device = &custom, .host = {.ops = &host_ops, .ctx = &first}, .read_mode = FLASH_READ_SINGLE};
    flash_handle_t a = NULL, b = NULL, duplicate = NULL;
    CHECK(flash_create(&config, &a) == STM_OK);
    CHECK(flash_create(&config, &duplicate) == STM_ERR_INVALID_STATE && !duplicate);
    config.host.ctx = &second;
    CHECK(flash_create(&config, &b) == STM_OK && b != a);
    uint8_t tx[300], rx[300];
    for (unsigned i = 0; i < sizeof(tx); ++i)
    {
        tx[i] = (uint8_t)i;
    }
    CHECK(flash_write(a, 250U, tx, sizeof(tx)) == STM_OK && first.writes == 6U && !second.writes);
    CHECK(flash_read(a, 250U, rx, sizeof(rx)) == STM_OK && !memcmp(tx, rx, sizeof(tx)) && overridden_reads);
    CHECK(flash_read(b, 250U, rx, sizeof(rx)) == STM_OK && rx[0] == 0xFFU);
    memset(tx, 0xFF, sizeof(tx));
    CHECK(flash_write(a, 250U, tx, sizeof(tx)) == FLASH_ERR_NEEDS_ERASE);
    CHECK(flash_erase(a, 0, 4096U) == STM_OK && first.erases == 1U);
    first.status[0] = 0x40U;
    CHECK(flash_erase(a, 0, 4096U) == FLASH_ERR_PROTECTED && first.erases == 1U);
    first.status[0] = 0U;
    CHECK(flash_read(a, 8191U, rx, 2U) == STM_ERR_OUT_OF_RANGE);
    first.blocked = 1;
    CHECK(flash_delete(&a) == STM_ERR_INVALID_CONTEXT && a);
    first.blocked = 0;
    CHECK(flash_delete(&a) == STM_OK && !a);
    CHECK(flash_delete(&b) == STM_OK);

    config.host.ctx = &first;
    custom.capabilities = FLASH_CAP_READ_SINGLE;
    CHECK(flash_create(&config, &a) == STM_OK);
    uint32_t unchanged_writes = first.writes, unchanged_erases = first.erases;
    CHECK(flash_write(a, 0U, tx, 1U) == STM_ERR_NOT_SUPPORTED);
    CHECK(flash_erase(a, 0U, 4096U) == STM_ERR_NOT_SUPPORTED);
    CHECK(first.writes == unchanged_writes && first.erases == unchanged_erases);
    CHECK(flash_delete(&a) == STM_OK);
    custom.capabilities = flash_device_gd25q256e.capabilities;
    config.device = &flash_device_w25q256jv_iq;
    uint32_t calls = first.calls;
    CHECK(flash_create(&config, &a) == STM_ERR_INVALID_CONFIG && !a && first.calls == calls + 1U);
    flash_device_t alias = custom;
    alias.name = "ambiguous-profile";
    const flash_device_t *list[] = {&custom, &alias};
    config.device = NULL;
    config.candidates = list;
    config.candidate_count = 2U;
    calls = first.calls;
    CHECK(flash_create(&config, &a) == STM_ERR_INVALID_CONFIG && !a && first.calls == calls + 1U);
    config.candidate_count = 1U;
    CHECK(flash_create(&config, &a) == STM_OK);
    flash_info_t info;
    CHECK(flash_get_info(a, &info) == STM_OK && info.device == &custom);
    first.tick = UINT32_MAX - 2U;
    first.busy = 1;
    CHECK(flash_read(a, 0, rx, 1U) == STM_ERR_TIMEOUT);
    CHECK(flash_get_info(a, &info) == STM_OK && !info.ready && info.last_error == STM_ERR_TIMEOUT);
    CHECK(flash_delete(&a) == STM_OK);
    first.busy = 0;
    first.id = 0x123456U;
    CHECK(flash_create(&config, &a) == STM_ERR_NOT_SUPPORTED && !a);
    first.id = 0xC84019U;
    flash_nor_profile_t bad = *custom.nor;
    bad.busy_index = 3U;
    custom.nor = &bad;
    config.device = &custom;
    CHECK(flash_create(&config, &a) == STM_ERR_INVALID_CONFIG && !a);
    puts("PASS: no-HAL core, explicit/AUTO/ambiguous IDs, custom ops, independent contexts, host transfer "
         "limit, timeout wrap, bounds and lifecycle");
    return 0;
}
