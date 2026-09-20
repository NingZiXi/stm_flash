/**
 * @file    stm_flash.h
 * @brief   STM32H7 OCTOSPI W25Q256JV-IQ 阻塞式存储接口
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

#define STM_FLASH_VERSION "2.0.0"

#define FLASH_ERR_BASE ((stm_err_t)0x1000)
#define FLASH_ERR_PROTECTED (FLASH_ERR_BASE + 1) // 擦写保护有效
#define FLASH_ERR_NEEDS_ERASE (FLASH_ERR_BASE + 2) // 写入包含 0 到 1 的变化
#define FLASH_ERR_SUSPENDED (FLASH_ERR_BASE + 3) // 芯片存在挂起操作

#define FLASH_SIZE_BYTES (32UL * 1024UL * 1024UL)
#define FLASH_PAGE_BYTES 256U
#define FLASH_SECTOR_BYTES 4096U
#define FLASH_JEDEC_ID 0xEF4019UL

typedef enum {
    FLASH_READ_SINGLE = 1, // 1-1-1，13h，无空周期
    FLASH_READ_QUAD = 4    // 1-1-4，6Ch，8 个空周期
} flash_read_mode_t;

typedef struct flash_context *flash_handle_t;

typedef struct {
    OSPI_HandleTypeDef *hal; // 已初始化并在句柄使用期间保持有效的 HAL 外设
    flash_read_mode_t read_mode; // 单线或四线读取
} flash_config_t;

// 可通过 get_info 查询的只读快照
typedef struct {
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
 * @brief 读取三个状态寄存器
 * @param dev 已初始化的实例
 * @param status 输出 SR1、SR2、SR3 的三个字节
 * @return STM_OK 或访问错误
 */
stm_err_t flash_read_status(flash_handle_t dev, uint8_t status[3]);

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
 * @param offset_bytes 4 KiB 对齐的字节偏移
 * @param size_bytes 4 KiB 的整数倍，零长度不发送命令
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
