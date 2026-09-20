/**
 * @file    flash_bus_ospi.c
 * @brief   STM32H7 OSPI 总线后端；不包含器件指令表
 */
#include "flash_internal.h"

static stm_err_t validate(void *context, uint32_t *clock_hz)
{
    OSPI_HandleTypeDef *hal = context;
    uint32_t kernel = __HAL_RCC_GET_OSPI_SOURCE() == RCC_OSPICLKSOURCE_HCLK
                          ? HAL_RCC_GetHCLKFreq() : 0U;
    if ((hal->Instance != OCTOSPI1 && hal->Instance != OCTOSPI2) ||
        HAL_OSPI_GetState(hal) != HAL_OSPI_STATE_READY ||
        hal->Init.DualQuad != HAL_OSPI_DUALQUAD_DISABLE ||
        hal->Init.MemoryType != HAL_OSPI_MEMTYPE_MICRON ||
        hal->Init.ClockMode != HAL_OSPI_CLOCK_MODE_0 ||
        hal->Init.WrapSize != HAL_OSPI_WRAP_NOT_SUPPORTED ||
        hal->Init.ChipSelectBoundary != 0U ||
        hal->Init.FreeRunningClock != HAL_OSPI_FREERUNCLK_DISABLE ||
        hal->Init.ClockPrescaler == 0U || hal->Init.ClockPrescaler > 256U ||
        hal->Init.ChipSelectHighTime < 4U || kernel == 0U ||
        (uint64_t)kernel > (uint64_t)50000000U * hal->Init.ClockPrescaler) {
        return STM_ERR_INVALID_CONFIG;
    }
    *clock_hz = kernel / hal->Init.ClockPrescaler;
    return STM_OK;
}

static stm_err_t geometry(void *context, uint32_t size_bytes, uint32_t max_clock_hz)
{
    OSPI_HandleTypeDef *hal = context;
    if (hal->Init.DeviceSize >= 32U || (1ULL << hal->Init.DeviceSize) != size_bytes ||
        (uint64_t)HAL_RCC_GetHCLKFreq() > (uint64_t)max_clock_hz * hal->Init.ClockPrescaler) {
        return STM_ERR_INVALID_CONFIG;
    }
    return STM_OK;
}

static const void *identity(void *context) { return ((OSPI_HandleTypeDef *)context)->Instance; }

static HAL_StatusTypeDef command(void *context, const flash_command_t *in, uint32_t timeout)
{
    OSPI_RegularCmdTypeDef cmd = {0};
    cmd.OperationType = HAL_OSPI_OPTYPE_COMMON_CFG;
    cmd.FlashId = HAL_OSPI_FLASH_ID_1;
    cmd.Instruction = in->opcode;
    cmd.InstructionMode = HAL_OSPI_INSTRUCTION_1_LINE;
    cmd.InstructionSize = HAL_OSPI_INSTRUCTION_8_BITS;
    cmd.AddressMode = in->addressed ? HAL_OSPI_ADDRESS_1_LINE : HAL_OSPI_ADDRESS_NONE;
    cmd.AddressSize = in->address_bytes == 3U ? HAL_OSPI_ADDRESS_24_BITS : HAL_OSPI_ADDRESS_32_BITS;
    cmd.Address = in->address;
    cmd.AlternateBytesMode = HAL_OSPI_ALTERNATE_BYTES_NONE;
    cmd.DataMode = in->count == 0U ? HAL_OSPI_DATA_NONE :
                   in->data_lines == 4U ? HAL_OSPI_DATA_4_LINES : HAL_OSPI_DATA_1_LINE;
    cmd.NbData = in->count;
    cmd.DummyCycles = in->dummy_cycles;
    cmd.DQSMode = HAL_OSPI_DQS_DISABLE;
    cmd.SIOOMode = HAL_OSPI_SIOO_INST_EVERY_CMD;
    return HAL_OSPI_Command(context, &cmd, timeout);
}
static HAL_StatusTypeDef receive(void *context, uint8_t *data, uint32_t timeout)
{ return HAL_OSPI_Receive(context, data, timeout); }
static HAL_StatusTypeDef transmit(void *context, const uint8_t *data, uint32_t timeout)
{ return HAL_OSPI_Transmit(context, (uint8_t *)data, timeout); }

const flash_bus_ops_t flash_ospi_ops = { validate, geometry, identity, command, receive, transmit };
