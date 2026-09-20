# 最小参考示例

`main.c` 是阅读和移植用的独立主程序，默认不加入宿主固件；没有配套头文件，也不依赖日志、RTT 或 SDRAM。

`board_init()` 包含 MPU、HAL、25 MHz HSE、550 MHz CPU/275 MHz HCLK、GPIO 和 OCTOSPI 初始化。复用参考 STM32H723 工程生成的 `gpio.c/h`、`octospi.c/h`；随后在任何 Flash 命令前将 OCTOSPI 改为 32 MiB、34.375 MHz、4 周期 CS 高电平，重新配置 OSPIM。正式移植建议直接在 CubeMX 设置这些参数。

默认：初始化 Flash、读起始 4 字节，完成后释放句柄，在空闲循环中保留 `example_result` 供调试器观察。定义 `EXAMPLE_FLASH_WRITE_TEST=1` 后会**擦除最后一个 4 KiB 扇区**，在其偏移 255 处写 4 字节，演示跨页编程并校验。只有明确保留这个扇区才能启用。

作为完整入口编译时，用此文件替换应用原 `main.c`，不要同时链接两个 `main`/`Error_Handler`。还需要原工程启动文件、HAL、CMSIS、SysTick 和 GPIO/OCTOSPI MSP。链接时需要 `stm_common` 头文件和可用的内部 RAM 堆（如参考 STM32H723 工程 sysmem.c）。

板级时钟代码改编自参考 STM32H723 工程 `Core/Src/main.c`。原版权声明：Copyright (c) 2026 STMicroelectronics. All rights reserved. This software is licensed under terms that can be found in the LICENSE file in the root directory of this software component. If no LICENSE file comes with this software, it is provided AS-IS.

上述 ST 代码采用其原始授权条款，不适用本仓库原创代码的 MIT 授权。HAL/CMSIS、启动文件和 CubeMX 生成的外设文件由使用者的工程提供。
