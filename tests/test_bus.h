/**
 * @file    test_bus.h
 * @brief   同一 NOR 协议模型使用两套真实 HAL 命令结构运行
 */
#ifndef TEST_BUS_H
#define TEST_BUS_H
#if defined(FLASH_TEST_QSPI)
#include "flash_qspi.h"
typedef flash_qspi_context_t test_context_t;
#define TEST_BIND flash_qspi_bind
#define TEST_RCC_SOURCE __HAL_RCC_GET_QSPI_SOURCE
typedef QSPI_HandleTypeDef test_hal_t;
typedef QSPI_CommandTypeDef test_command_t;
#define TEST_HAL_COMMAND HAL_QSPI_Command
#define TEST_HAL_RECEIVE HAL_QSPI_Receive
#define TEST_HAL_TRANSMIT HAL_QSPI_Transmit
#define TEST_HAL_GET_STATE HAL_QSPI_GetState
#define TEST_HAL_STATE_TYPE HAL_QSPI_StateTypeDef
#define TEST_INSTRUCTION_1_LINE QSPI_INSTRUCTION_1_LINE
#define TEST_ADDRESS_32_BITS QSPI_ADDRESS_32_BITS
#define TEST_ADDRESS_1_LINE QSPI_ADDRESS_1_LINE
#define TEST_ADDRESS_NONE QSPI_ADDRESS_NONE
#define TEST_DATA_4_LINES QSPI_DATA_4_LINES
#define TEST_DATA_1_LINE QSPI_DATA_1_LINE
#define TEST_RCC_CONFIG __HAL_RCC_QSPI_CONFIG
#define TEST_RCC_PLL2 RCC_QSPICLKSOURCE_PLL2
#define TEST_RCC_HCLK RCC_QSPICLKSOURCE_D1HCLK
#define TEST_SIZE_FIELD FlashSize
#define TEST_VALID_SIZE 24U
#define TEST_VALID_PRESCALER 7U
static int test_sdr_frame(const test_command_t *cmd)
{
    return cmd->DdrMode == QSPI_DDR_MODE_DISABLE && cmd->AlternateByteMode == QSPI_ALTERNATE_BYTES_NONE &&
           cmd->SIOOMode == QSPI_SIOO_INST_EVERY_CMD;
}
static void test_bus_fixture(test_hal_t *h)
{
    h->Instance = QUADSPI;
    h->State = HAL_QSPI_STATE_READY;
    h->Init.FlashSize = 24U;
    h->Init.ClockPrescaler = 7U;
    h->Init.FifoThreshold = 4U;
    h->Init.ChipSelectHighTime = QSPI_CS_HIGH_TIME_4_CYCLE;
    h->Init.ClockMode = QSPI_CLOCK_MODE_3;
    h->Init.FlashID = QSPI_FLASH_ID_1;
    h->Init.DualFlash = QSPI_DUALFLASH_DISABLE;
    h->Init.SampleShifting = QSPI_SAMPLE_SHIFTING_HALFCYCLE;
}
#else
#include "flash_ospi.h"
typedef flash_ospi_context_t test_context_t;
#define TEST_BIND flash_ospi_bind
#define TEST_RCC_SOURCE __HAL_RCC_GET_OSPI_SOURCE
typedef OSPI_HandleTypeDef test_hal_t;
typedef OSPI_RegularCmdTypeDef test_command_t;
#define TEST_HAL_COMMAND HAL_OSPI_Command
#define TEST_HAL_RECEIVE HAL_OSPI_Receive
#define TEST_HAL_TRANSMIT HAL_OSPI_Transmit
#define TEST_HAL_GET_STATE HAL_OSPI_GetState
#define TEST_HAL_STATE_TYPE uint32_t
#define TEST_INSTRUCTION_1_LINE HAL_OSPI_INSTRUCTION_1_LINE
#define TEST_ADDRESS_32_BITS HAL_OSPI_ADDRESS_32_BITS
#define TEST_ADDRESS_1_LINE HAL_OSPI_ADDRESS_1_LINE
#define TEST_ADDRESS_NONE HAL_OSPI_ADDRESS_NONE
#define TEST_DATA_4_LINES HAL_OSPI_DATA_4_LINES
#define TEST_DATA_1_LINE HAL_OSPI_DATA_1_LINE
#define TEST_RCC_CONFIG __HAL_RCC_OSPI_CONFIG
#define TEST_RCC_PLL2 RCC_OSPICLKSOURCE_PLL2
#define TEST_RCC_HCLK RCC_OSPICLKSOURCE_HCLK
#define TEST_SIZE_FIELD DeviceSize
#define TEST_VALID_SIZE 25U
#define TEST_VALID_PRESCALER 8U
static int test_sdr_frame(const test_command_t *cmd)
{
    return cmd->InstructionDtrMode == HAL_OSPI_INSTRUCTION_DTR_DISABLE &&
           cmd->DataDtrMode == HAL_OSPI_DATA_DTR_DISABLE;
}
static void test_bus_fixture(test_hal_t *h)
{
    h->Instance = OCTOSPI1;
    h->State = HAL_OSPI_STATE_READY;
    h->Init.DeviceSize = 25U;
    h->Init.ClockPrescaler = 8U;
    h->Init.ChipSelectHighTime = 4U;
    h->Init.MemoryType = HAL_OSPI_MEMTYPE_MICRON;
    h->Init.ClockMode = HAL_OSPI_CLOCK_MODE_0;
    h->Init.DualQuad = HAL_OSPI_DUALQUAD_DISABLE;
    h->Init.FreeRunningClock = HAL_OSPI_FREERUNCLK_DISABLE;
    h->Init.WrapSize = HAL_OSPI_WRAP_NOT_SUPPORTED;
}
#endif
#endif
