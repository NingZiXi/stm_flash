#include "stm_flash.h"
#include "stm_flash_host.h"
#include "stm_flash_device.h"
static_assert(sizeof(flash_handle_t) == sizeof(void *), "opaque handle");
