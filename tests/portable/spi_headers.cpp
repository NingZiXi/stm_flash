#include "flash_spi.h"
static_assert(sizeof(flash_spi_context_t) >= sizeof(void *), "C++ SPI adapter header");
