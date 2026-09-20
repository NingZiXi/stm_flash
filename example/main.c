/**
 * @file    main.c
 * @brief   flash 组件板级初始化和最小使用示例
 */
#include "stm_flash.h"
#include "main.h"
#include "gpio.h"
#include "octospi.h"

#ifndef EXAMPLE_FLASH_WRITE_TEST
#define EXAMPLE_FLASH_WRITE_TEST 0
#endif

volatile stm_err_t example_result = STM_OK;
static void board_init(void);

// 初始化设备、访问数据并释放组件对象
int main(void)
{
    board_init();
    flash_handle_t device = NULL;
    const flash_config_t config = {
        .bus = {.type = FLASH_BUS_OSPI, .handle.ospi = &hospi1}, .chip = FLASH_CHIP_AUTO,
        .read_mode = FLASH_READ_QUAD,
    };
    example_result = flash_create(&config, &device);
    uint8_t data[4] = {0};
    if (example_result == STM_OK) {
        example_result = flash_read(device, 0U, data, sizeof(data));
    }
#if EXAMPLE_FLASH_WRITE_TEST
    flash_info_t info = {0};
    if (example_result == STM_OK) { example_result = flash_get_info(device, &info); }
    const uint32_t offset = info.size_bytes - info.erase_size;
    const uint8_t expected[] = {0x12U, 0x34U, 0xA5U, 0x5AU};
    // 启用擦写示例会覆盖最后一个扇区。
    if (example_result == STM_OK) {
        example_result = flash_erase(device, offset, info.erase_size);
    }
    if (example_result == STM_OK) {
        example_result = flash_write(device, offset + 255U, expected, sizeof(expected));
    }
    if (example_result == STM_OK) {
        example_result = flash_verify(device, offset + 255U, expected, sizeof(expected));
    }
#endif
    stm_err_t cleanup = flash_delete(&device);
    if (example_result == STM_OK) { example_result = cleanup; }
    for (;;) { __WFI(); }
}

// 初始化本板 HAL、时钟、GPIO 和 OCTOSPI
static void board_init(void)
{
    MPU_Region_InitTypeDef region = {0};

    // Region 0 禁止访问未配置的外部地址区。
    HAL_MPU_Disable();
    region.Enable = MPU_REGION_ENABLE;
    region.Number = MPU_REGION_NUMBER0;
    region.BaseAddress = 0x00000000UL;
    region.Size = MPU_REGION_SIZE_4GB;
    region.SubRegionDisable = 0x87U;
    region.TypeExtField = MPU_TEX_LEVEL0;
    region.AccessPermission = MPU_REGION_NO_ACCESS;
    region.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
    region.IsShareable = MPU_ACCESS_SHAREABLE;
    region.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
    region.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
    HAL_MPU_ConfigRegion(&region);
    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

    // 初始化期间保持中断开启，HAL tick 须正常运行。
    if (HAL_Init() != HAL_OK) { Error_Handler(); }

    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    if (HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY) != HAL_OK) { Error_Handler(); }

    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

    while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

    // HSE=25 MHz，PLL 输出 550 MHz。
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 2;
    RCC_OscInitStruct.PLL.PLLN = 44;
    RCC_OscInitStruct.PLL.PLLP = 1;
    RCC_OscInitStruct.PLL.PLLQ = 3;
    RCC_OscInitStruct.PLL.PLLR = 2;
    RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
    RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    RCC_OscInitStruct.PLL.PLLFRACN = 0;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
    {
      Error_Handler();
    }

    // CPU=550 MHz，HCLK=275 MHz，APB=137.5 MHz。
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                                |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
    RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
    {
      Error_Handler();
    }

    MX_GPIO_Init();
    MX_OCTOSPI1_Init();
    if (HAL_OSPI_DeInit(&hospi1) != HAL_OK) { Error_Handler(); }
    hospi1.Init.DeviceSize = 25U;
    hospi1.Init.ClockPrescaler = 8U;
    hospi1.Init.ChipSelectHighTime = 4U;
    if (HAL_OSPI_Init(&hospi1) != HAL_OK) { Error_Handler(); }
    OSPIM_CfgTypeDef manager = {0};
    manager.ClkPort = 1U;
    manager.NCSPort = 1U;
    manager.IOLowPort = HAL_OSPIM_IOPORT_1_LOW;
    if (HAL_OSPIM_Config(&hospi1, &manager, 100U) != HAL_OK) { Error_Handler(); }
    HAL_Delay(10U);
}

// 板级初始化失败时停机
void Error_Handler(void)
{
    __disable_irq();
    for (;;) { }
}
