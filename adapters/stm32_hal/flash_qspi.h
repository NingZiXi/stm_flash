/** @file flash_qspi.h @brief STM32 HAL QSPI 可选适配器。 */
#ifndef FLASH_QSPI_H
#define FLASH_QSPI_H
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
        QSPI_HandleTypeDef *hal;
        uint32_t kernel_clock_hz; /* 实际控制器内核频率；时钟改变后须关闭并重新绑定。 */
        HAL_StatusTypeDef last_hal_status;
    } flash_qspi_context_t;
    /* 纯绑定，不修改硬件；ctx 必须独立且在设备整个生命周期内有效。
     * CubeMX/BSP 完成时钟/GPIO/控制器初始化；不会切换 QE、保护或映射模式。 */
    flash_host_t flash_qspi_bind(flash_qspi_context_t *ctx, QSPI_HandleTypeDef *hal,
                                 uint32_t kernel_clock_hz);
    extern const flash_host_ops_t flash_qspi_ops;
#ifdef __cplusplus
}
#endif
#endif
