/** @file stm_flash.h
 *  @brief 外部串行 NOR 阻塞式 API；器件与控制器由调用方组合。
 */
#ifndef STM_FLASH_H
#define STM_FLASH_H
#include "stm_flash_device.h"
#ifdef __cplusplus
extern "C"
{
#endif
#define STM_FLASH_VERSION "4.0.0"
#define FLASH_ERR_BASE ((stm_err_t)0x1000)
#define FLASH_ERR_PROTECTED (FLASH_ERR_BASE + 1)
#define FLASH_ERR_NEEDS_ERASE (FLASH_ERR_BASE + 2)
#define FLASH_ERR_SUSPENDED (FLASH_ERR_BASE + 3)
    typedef struct flash_context *flash_handle_t;
    typedef struct
    {
        const flash_device_t *device; /* 非空：只校验指定型号；NULL：自动识别。 */
        flash_host_t host;
        flash_read_mode_t read_mode;
        const flash_device_t *const *candidates; /* AUTO 可选候选表；NULL 使用编译内置表。 */
        size_t candidate_count;                  /* 自定义表长度；表仅创建时读取。 */
    } flash_config_t;
    typedef struct
    {
        const flash_device_t *device;
        const char *host_name;
        uint32_t size_bytes, page_size, erase_size, capabilities, jedec_id, clock_hz;
        stm_err_t last_error; /* 统一错误；原生 HAL 状态只保留在适配器上下文。 */
        flash_read_mode_t read_mode;
        uint8_t ready;
    } flash_info_t;
    /* 初始化不改 QE/保护位、不擦除；无默认线程安全。config 本身可为局部变量。 */
    /**
     * @brief 创建并初始化设备；失败不返回新对象，不执行破坏性自检。
     * @param[in] config 创建配置，仅在调用期间读取；host/器件描述符及上下文由调用者持有。
     * @param[out] out_handle 输出位置，调用前必须为 NULL；已有句柄时保持原值并拒绝创建。
     * @return STM_OK 或参数、配置、状态、内存、上下文及设备访问错误。
     */
    stm_err_t flash_create(const flash_config_t *config, flash_handle_t *out_handle);

    /**
     * @brief 释放组件对象并清空句柄，不关闭底层外设；调用前停止所有相关访问。
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
