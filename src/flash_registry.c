/** @file flash_registry.c @brief 只匹配已启用器件；未知/歧义拒绝初始化。 */
#include "flash_internal.h"
#ifndef STM_FLASH_ENABLE_GD25Q256E
#define STM_FLASH_ENABLE_GD25Q256E 1
#endif
#ifndef STM_FLASH_ENABLE_W25Q256JV_IQ
#define STM_FLASH_ENABLE_W25Q256JV_IQ 1
#endif
static const flash_device_t *const builtin[] = {
#if STM_FLASH_ENABLE_GD25Q256E
    &flash_device_gd25q256e,
#endif
#if STM_FLASH_ENABLE_W25Q256JV_IQ
    &flash_device_w25q256jv_iq,
#endif
    NULL};
static int matches(const flash_device_t *d, uint32_t id)
{
    return d != NULL && d->jedec_mask != 0U && d->jedec_mask <= 0xFFFFFFU && d->jedec_id <= 0xFFFFFFU &&
           (id & d->jedec_mask) == (d->jedec_id & d->jedec_mask);
}
stm_err_t flash_select_device(uint32_t id, const flash_config_t *c, const flash_device_t **out)
{
    *out = NULL;
    if (c->device != NULL)
    {
        if (!matches(c->device, id))
        {
            return STM_ERR_INVALID_CONFIG;
        }
        *out = c->device;
        return STM_OK;
    }
    const flash_device_t *const *list = c->candidates != NULL ? c->candidates : builtin;
    size_t count = c->candidates != NULL ? c->candidate_count : sizeof(builtin) / sizeof(builtin[0]) - 1U;
    for (size_t i = 0U; i < count; ++i)
    {
        if (!matches(list[i], id))
        {
            continue;
        }
        if (*out != NULL && *out != list[i])
        {
            *out = NULL;
            return STM_ERR_INVALID_CONFIG;
        }
        *out = list[i];
    }
    return *out != NULL ? STM_OK : STM_ERR_NOT_SUPPORTED;
}
