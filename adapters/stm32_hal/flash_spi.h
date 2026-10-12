/**
 * @file flash_spi.h
 * @brief STM32 HAL 普通 SPI 阻塞式适配器。
 */
#ifndef FLASH_SPI_H
#define FLASH_SPI_H

#include "stm_flash_host.h"
#ifndef STM_FLASH_HAL_HEADER
#error "Define STM_FLASH_HAL_HEADER for the selected STM32 HAL family"
#endif
#include STM_FLASH_HAL_HEADER

#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct
    {
        SPI_HandleTypeDef *hal;              // 借用已初始化的 SPI 句柄。
        GPIO_TypeDef *cs_port;               // 借用片选 GPIO，低电平有效。
        uint16_t cs_pin;                     // 单个 GPIO_PIN_x 位。
        uint32_t kernel_clock_hz;            // 实际 SPI 内核时钟，不是输出 SCK。
        HAL_StatusTypeDef last_hal_status;   // 最近传输的 HAL 状态。
        HAL_StatusTypeDef last_abort_status; // 最近错误恢复的 HAL 状态。
        uint8_t faulted;                     // 通信故障后须检查硬件并重新绑定。
    } flash_spi_context_t;

    /**
     * @brief 纯绑定 SPI 与片选，不访问硬件或自动初始化 GPIO。
     * @param[in,out] ctx 独立上下文；设备生命周期内保持有效，不得重复绑定在用对象。
     * @param[in] hal 已初始化的主机、双线、8 位、MSB、软件 NSS、mode 0/3 SPI。
     * @param[in] cs_port 已配置为推挽输出的片选端口，由调用者预先拉高。
     * @param[in] cs_pin 单个有效引脚位，片选低有效。
     * @param[in] kernel_clock_hz 实际控制器内核频率，时钟改变后必须删除并重新绑定。
     * @return 借用上述资源的 host；参数与硬件配置在 flash_create 时校验。
     * @note 仅在线程/主循环中使用；调用方串行化整个 SPI 总线访问。绑定不改变 QE/保护位。
     */
    flash_host_t flash_spi_bind(flash_spi_context_t *ctx, SPI_HandleTypeDef *hal,
                                GPIO_TypeDef *cs_port, uint16_t cs_pin, uint32_t kernel_clock_hz);
    extern const flash_host_ops_t flash_spi_ops;
#ifdef __cplusplus
}
#endif
#endif
