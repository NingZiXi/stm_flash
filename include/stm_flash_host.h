/** @file stm_flash_host.h
 *  @brief 同步串行 NOR 控制器契约；不依赖任何 MCU/HAL。
 */
#ifndef STM_FLASH_HOST_H
#define STM_FLASH_HOST_H
#include <stddef.h>
#include <stdint.h>
#include "stm_err.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct
    {
        uint8_t opcode, instruction_lines, address_lines, address_bytes;
        uint8_t data_lines, dummy_cycles;
        uint32_t address;
        const void *tx_data;
        void *rx_data;
        size_t size_bytes; /* 同步半双工；有数据时 tx/rx 必须恰好一个非空。 */
    } flash_transfer_t;
    typedef struct
    {
        uint32_t clock_hz, max_transfer_bytes;
        uint8_t data_line_mask; /* bit N 表示支持 N 根数据线。 */
    } flash_host_info_t;
    typedef struct
    {
        const char *name;
        stm_err_t (*validate)(void *ctx, flash_host_info_t *info);
        stm_err_t (*check_device)(void *ctx, uint32_t size_bytes, uint32_t max_clock_hz);
        const void *(*identity)(void *ctx); /* 稳定、非空的独占资源标识。 */
        stm_err_t (*exec)(void *ctx, const flash_transfer_t *transfer, uint32_t timeout_ms);
        uint32_t (*get_tick_ms)(void *ctx); /* 单调递增模 2^32。 */
        void (*delay_ms)(void *ctx, uint32_t ms);
        stm_err_t (*check_context)(void *ctx); /* 裸机/RTOS 自行落实阻塞调用条件。 */
        int (*buffer_ok)(void *ctx, const void *buffer, size_t size_bytes); /* 可空；非零允许。 */
    } flash_host_ops_t;
    typedef struct
    {
        const flash_host_ops_t *ops;
        void *ctx;
    } flash_host_t;
/* ops/ctx 及引用资源必须持续有效；使用期间不能重新绑定或修改配置。
 * bind 只组装对象，不初始化硬件。调用方串行化生命周期和读写，不提供锁。 */
#ifdef __cplusplus
}
#endif
#endif
