/**
 * @file    stm_flash.h
 * @brief   外部串行 NOR Flash 通用阻塞式存储接口
 */
#ifndef STM_FLASH_H
#define STM_FLASH_H

#include <stddef.h>
#include <stdint.h>
#include "stm_err.h"
#ifndef STM_FLASH_HAL_HEADER
#define STM_FLASH_HAL_HEADER "stm32h7xx_hal.h"
#endif
#include STM_FLASH_HAL_HEADER

#ifdef __cplusplus
extern "C" {
#endif

#define STM_FLASH_VERSION "3.0.0"

#define FLASH_ERR_BASE ((stm_err_t)0x1000)
#define FLASH_ERR_PROTECTED (FLASH_ERR_BASE + 1) // 擦写保护有效
#define FLASH_ERR_NEEDS_ERASE (FLASH_ERR_BASE + 2) // 写入包含 0 到 1 的变化
#define FLASH_ERR_SUSPENDED (FLASH_ERR_BASE + 3) // 芯片存在挂起操作

// 仅 OSPI 后端已实现；其他值预留，创建时返回 NOT_SUPPORTED。
typedef enum { FLASH_BUS_OSPI = 0, FLASH_BUS_SPI = 1, FLASH_BUS_QSPI = 2 } flash_bus_type_t;
typedef struct {
    flash_bus_type_t type;
    union {
        void *ospi; // OSPI_HandleTypeDef*；仅后端依赖具体 HAL 类型
        void *spi;  // 预留
        void *qspi; // 预留
    } handle;
} flash_bus_config_t;

typedef enum {
    FLASH_CHIP_AUTO = 0,
    FLASH_CHIP_W25Q256JV_IQ = 1
} flash_chip_t;

#define FLASH_CAP_READ_SINGLE (1UL << 0)
#define FLASH_CAP_READ_QUAD   (1UL << 1)
#define FLASH_CAP_PROGRAM    (1UL << 2)
#define FLASH_CAP_ERASE      (1UL << 3)
#define FLASH_STATUS_BUSY          (1UL << 0)
#define FLASH_STATUS_WRITE_ENABLED (1UL << 1)
#define FLASH_STATUS_PROTECTED     (1UL << 2)
#define FLASH_STATUS_SUSPENDED     (1UL << 3)
#define FLASH_STATUS_QUAD_ENABLED  (1UL << 4)

typedef struct {
    uint32_t valid_mask; // 本器件可查询的状态；未声明的位不能解释为 false
    uint32_t flags;      // FLASH_STATUS_*；PROTECTED 表示组件应拒绝擦写
} flash_status_t;

typedef enum {
    FLASH_READ_SINGLE = 1, // 1-1-1，指令与空周期由器件决定
    FLASH_READ_QUAD = 4    // 1-1-4，指令与空周期由器件决定
} flash_read_mode_t;

typedef struct flash_context *flash_handle_t;

typedef struct {
    flash_bus_config_t bus; // 已初始化且在对象生命周期内有效的总线
    flash_chip_t chip; // AUTO 仅匹配已适配的器件，不猜测未知芯片
    flash_read_mode_t read_mode; // 单线或四线读取
} flash_config_t;

// 可通过 get_info 查询的只读快照
typedef struct {
    flash_chip_t chip;             // 实际匹配的型号
    flash_bus_type_t bus_type;
    uint32_t size_bytes;           // 容量
    uint32_t page_size;            // 页编程边界
    uint32_t erase_size;           // 公共擦除接口的最小对齐单位
    uint32_t capabilities;         // FLASH_CAP_*
    uint32_t jedec_id;             // 24 位 JEDEC ID
    uint32_t clock_hz;             // 根据 HAL 配置计算的串行时钟，Hz
    HAL_StatusTypeDef last_hal_status;    // 最近一次 HAL 返回值
    flash_read_mode_t read_mode;    // 读取数据线模式
    uint8_t ready;                 // 非零表示接口可用，不代表硬件已测试
} flash_info_t;

/**
 * @brief 创建并初始化设备；失败不返回新对象，不执行破坏性自检。
 * @param[in] config 创建配置，仅在调用期间读取；HAL 句柄由板级持有。
 * @param[out] out_handle 输出位置，调用前必须为 NULL；已有句柄时保持原值并拒绝创建。
 * @return STM_OK 或参数、配置、状态、内存、上下文及设备访问错误。
 */
stm_err_t flash_create(const flash_config_t *config, flash_handle_t *out_handle);

/**
 * @brief 释放组件对象并清空句柄，不关闭 HAL 外设；调用前停止所有相关访问。
 * @param[in,out] handle 句柄地址，*handle 为 NULL 时也成功；其他别名不会自动清空。
 * @return STM_OK、STM_ERR_INVALID_ARG 或 STM_ERR_INVALID_CONTEXT。
 */
stm_err_t flash_delete(flash_handle_t *handle);

/**
 * @brief 查询实例信息，故障停用后仍可查询，不访问硬件。
 * @param handle 有效实例。
 * @param[out] info 输出快照。
 * @return STM_OK 或 STM_ERR_INVALID_ARG。
 */
stm_err_t flash_get_info(flash_handle_t handle, flash_info_t *info);

/**
 * @brief 查询通用设备状态，不等待 BUSY 清零；失败不修改输出
 * @param dev 已初始化的实例
 * @param status 输出支持掩码和状态位；各寄存器依次读取，不是原子快照
 * @return STM_OK 或访问错误
 */
stm_err_t flash_get_status(flash_handle_t dev, flash_status_t *status);

/**
 * @brief 从 Flash 读取数据
 * @param dev 已初始化的实例
 * @param offset_bytes 相对 Flash 起始位置的字节偏移
 * @param data 目标 RAM 缓冲区；零长度时可为空
 * @param size_bytes 字节数，允许为零
 * @return STM_OK 或范围、状态、通信错误
 */
stm_err_t flash_read(flash_handle_t dev, uint32_t offset_bytes, void *data, size_t size_bytes);

/**
 * @brief 分页写入并校验，不自动擦除；失败时此前的页可能已写入
 * @param dev 已初始化的实例，调用期间独占设备
 * @param offset_bytes 字节偏移，无需页对齐
 * @param data 源 RAM 缓冲区，整个调用期间必须保持不变
 * @param size_bytes 字节数；先检查整个范围是否需要擦除
 * @return STM_OK、FLASH_ERR_NEEDS_ERASE、FLASH_ERR_PROTECTED 或访问/校验错误
 */
stm_err_t flash_write(flash_handle_t dev, uint32_t offset_bytes, const void *data, size_t size_bytes);

/**
 * @brief 擦除并检查全 FF，失败时此前的扇区可能已擦除
 * @param dev 已初始化的实例
 * @param offset_bytes 按 info.erase_size 对齐的字节偏移
 * @param size_bytes info.erase_size 的整数倍，零长度不发送命令
 * @return STM_OK 或对齐、保护、通信、校验错误
 */
stm_err_t flash_erase(flash_handle_t dev, uint32_t offset_bytes, size_t size_bytes);

/**
 * @brief 比较 Flash 内容和 RAM 数据
 * @param dev 已初始化的实例
 * @param offset_bytes 字节偏移
 * @param data 预期数据
 * @param size_bytes 比较字节数
 * @return STM_OK 或 VERIFY/访问错误；不匹配会停用实例
 */
stm_err_t flash_verify(flash_handle_t dev, uint32_t offset_bytes, const void *data, size_t size_bytes);

#ifdef __cplusplus
}
#endif
#endif
