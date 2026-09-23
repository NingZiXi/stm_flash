/** @file stm_flash_device.h
 *  @brief 器件只读描述、可替换操作和通用 NOR 参数。
 */
#ifndef STM_FLASH_DEVICE_H
#define STM_FLASH_DEVICE_H
#include "stm_flash_host.h"
#ifdef __cplusplus
extern "C"
{
#endif
#define FLASH_CAP_READ_SINGLE (1UL << 0)
#define FLASH_CAP_READ_QUAD (1UL << 1)
#define FLASH_CAP_PROGRAM (1UL << 2)
#define FLASH_CAP_ERASE (1UL << 3)
#define FLASH_STATUS_BUSY (1UL << 0)
#define FLASH_STATUS_WRITE_ENABLED (1UL << 1)
#define FLASH_STATUS_PROTECTED (1UL << 2)
#define FLASH_STATUS_SUSPENDED (1UL << 3)
#define FLASH_STATUS_QUAD_ENABLED (1UL << 4)

    typedef struct
    {
        uint32_t valid_mask; // 本器件可查询的状态；未声明的位不能解释为 false
        uint32_t flags;      // FLASH_STATUS_*；PROTECTED 表示组件应拒绝擦写
    } flash_status_t;

    typedef enum
    {
        FLASH_READ_SINGLE = 1, // 1-1-1，指令与空周期由器件决定
        FLASH_READ_QUAD = 4    // 1-1-4，指令与空周期由器件决定
    } flash_read_mode_t;

    typedef struct flash_device flash_device_t;
    typedef struct
    {
        flash_host_t host;
        const flash_device_t *device;
        flash_read_mode_t read_mode;
    } flash_chip_io_t;
    typedef struct
    {
        stm_err_t (*init)(const flash_chip_io_t *io);
        stm_err_t (*get_status)(const flash_chip_io_t *io, flash_status_t *status);
        stm_err_t (*read)(const flash_chip_io_t *io, uint32_t offset, void *data, size_t size);
        stm_err_t (*program_page)(const flash_chip_io_t *io, uint32_t offset, const void *data, size_t size);
        stm_err_t (*erase_sector)(const flash_chip_io_t *io, uint32_t offset);
        stm_err_t (*wait_ready)(const flash_chip_io_t *io, uint32_t timeout_ms);
    } flash_chip_ops_t;
    typedef struct
    {
        uint8_t address_bytes, read_single, read_quad, quad_dummy, program, erase, write_enable;
        uint8_t status_commands[3], status_count;
        uint8_t busy_index, busy_mask, wel_index, wel_mask;
        uint8_t qe_index, qe_mask, suspend_index, suspend_mask;
        uint8_t protected_masks[3];
    } flash_nor_profile_t;
    struct flash_device
    {
        const char *name;
        uint32_t jedec_id, jedec_mask; /* 24 位；匹配不能证明封装/全部型号后缀。 */
        uint32_t size_bytes, page_size, erase_size, capabilities, max_clock_hz;
        uint32_t program_timeout_ms, erase_timeout_ms;
        const flash_chip_ops_t *ops;
        const flash_nor_profile_t *nor; /* 通用 NOR 实现使用；自定义 ops 可不用。 */
        const void *driver_data;        /* 可选、只读的自定义器件参数。 */
    };
    /* 自定义型号复用这些操作；有差异时定义自己的 const flash_chip_ops_t。
     * 操作由核心完成参数/范围检查后调用；超时、保护检查不能省略。 */
    stm_err_t flash_nor_init(const flash_chip_io_t *io);
    stm_err_t flash_nor_get_status(const flash_chip_io_t *io, flash_status_t *status);
    stm_err_t flash_nor_read(const flash_chip_io_t *io, uint32_t offset, void *data, size_t size);
    stm_err_t flash_nor_program_page(const flash_chip_io_t *io, uint32_t offset, const void *data,
                                     size_t size);
    stm_err_t flash_nor_erase_sector(const flash_chip_io_t *io, uint32_t offset);
    stm_err_t flash_nor_wait_ready(const flash_chip_io_t *io, uint32_t timeout_ms);
    extern const flash_chip_ops_t flash_nor_ops;
    extern const flash_device_t flash_device_gd25q256e;
    extern const flash_device_t flash_device_w25q256jv_iq;
/* 描述符和 ops 使用期间必须有效；推荐静态 const，每个实例共享。 */
#ifdef __cplusplus
}
#endif
#endif
