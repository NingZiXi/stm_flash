/**
 * @file gd25q128_spi.c
 * @brief GD25Q128 普通 SPI 只读接入示例；HAL 与引脚由调用者提供。
 */
#include "stm_flash.h"
#include "flash_spi.h"

static flash_spi_context_t host;
static flash_handle_t nor;
static flash_device_t readonly_device;

stm_err_t example_gd25q128_open(SPI_HandleTypeDef *hal, GPIO_TypeDef *cs_port, uint16_t cs_pin,
                                uint32_t spi_kernel_hz)
{
    if (nor)
    {
        return STM_ERR_INVALID_STATE;
    }
    if (!hal || !cs_port || !cs_pin || (cs_pin & (cs_pin - 1U)))
    {
        return STM_ERR_INVALID_ARG;
    }
    HAL_GPIO_WritePin(cs_port, cs_pin, GPIO_PIN_SET);
    readonly_device = flash_device_gd25q128;
    readonly_device.capabilities = FLASH_CAP_READ_SINGLE;
    const flash_config_t config = {
        .device = &readonly_device,
        .host = flash_spi_bind(&host, hal, cs_port, cs_pin, spi_kernel_hz),
        .read_mode = FLASH_READ_SINGLE,
    };
    return flash_create(&config, &nor);
}

stm_err_t example_gd25q128_read(uint32_t offset, void *buffer, size_t count)
{
    return flash_read(nor, offset, buffer, count);
}

stm_err_t example_gd25q128_close(void)
{
    // 删除只释放组件实例，不关闭借用的 HAL/GPIO。
    return flash_delete(&nor);
}
