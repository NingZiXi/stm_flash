/** @file flash_internal.h @brief 私有注册与对象状态。 */
#ifndef FLASH_INTERNAL_H
#define FLASH_INTERNAL_H
#include "stm_flash.h"
struct flash_context
{
    flash_chip_io_t io;
    flash_host_info_t host_info;
    const void *identity;
    uint32_t jedec_id;
    stm_err_t last_error;
    uint8_t ready;
    struct flash_context *next;
};
stm_err_t flash_select_device(uint32_t id, const flash_config_t *config, const flash_device_t **out);
#endif
