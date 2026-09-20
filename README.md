# stm_flash

基于 STM32H7 HAL OCTOSPI 的 **W25Q256JV-IQ（32 MiB）** 阻塞式驱动。提供不透明句柄、单线/四线读取、分页写入、4 KiB 擦除和校验，统一返回 `stm_err_t`。

当前识别 JEDEC ID **EF4019**，实测器件 W25Q256JVEIQ。用于外部 NOR Flash，不是 MCU 内部 Flash 驱动。不依赖 RTOS、日志或 RTT；不提供 DMA、内存映射和 XIP。

## 安装

需要 C11、CMake 3.22+、STM32H7 HAL/CMSIS 和 [stm_common](https://github.com/NingZiXi/stm_common)。使用 CMake 时可自动拉取公共依赖，只需在 STM32CubeMX 工程根目录克隆本组件：

```sh
git clone https://github.com/NingZiXi/stm_flash.git Lib/stm_flash
```

在 HAL 配置目标创建后加入：

```cmake
add_subdirectory(Lib/stm_flash)
target_link_libraries(your_firmware PRIVATE stm_flash)
```

将 `your_firmware` 替换为实际固件目标。公共依赖按以下顺序解析：已有 `stm_common` target → 同级 `stm_common/` → FetchContent 下载 **v1.0.0** 对应的固定提交 `ce3d186dde2d374a8e9c7b9068a7b88f97d57dc1`。Flash 与 SDRAM 共用一个 target，不重复下载。

自动下载默认使用 GitHub，需要 Git 和网络；源码位于构建目录的 `_deps/stm_common-src`。如需使用 Gitee，在添加任意驱动前设置：

```cmake
set(STM_COMMON_GIT_REPOSITORY "https://gitee.com/nzxhg/stm_common.git" CACHE STRING "")
```

离线构建可提前提供 target 或同级目录，并设置 `STM_COMMON_FETCH=OFF` 禁止自动拉取；依赖缺失会在配置阶段明确报错。也可在启用自动依赖解析时通过 `FETCHCONTENT_SOURCE_DIR_STM_COMMON` 指定已有源码的绝对路径。切换仓库地址时更新 CMake 缓存或使用新的构建目录。手动提供的依赖版本由工程负责，建议使用 v1.0.0。
默认继承已有 `stm32cubemx` target 的 HAL 头文件和芯片宏；其他构建方式设置 `STM_FLASH_LINK_CUBEMX=OFF`，自行提供 HAL/CMSIS 头文件、芯片宏及 HAL 实现。
Keil/IAR 工程可手动加入组件 `.c`，将组件目录和 `stm_common` 加入 include 路径。
`STM_FLASH_HAL_HEADER` 可指定其他 HAL 头文件，但不代表支持其他 STM32 系列。

组件对象仅在创建/删除时使用 `calloc/free`，读写不分配内存。须提供内部 RAM 堆，例如 STM32CubeMX 的 `sysmem.c` / `_sbrk`，不能把堆放在尚未初始化的外部存储中。

## 板级配置

先由板级完成 HAL、GPIO、时钟、OCTOSPI 和 OSPIM 初始化，保持 HAL tick 正常运行，并满足芯片上电等待。串行时钟限制为 **不超过 50 MHz**；已验证 34.375 MHz。

| 参数 | 配置 |
|---|---|
| 外设 / 内核时钟 | OCTOSPI1 或 OCTOSPI2 / HCLK |
| MemoryType | HAL_OSPI_MEMTYPE_MICRON |
| DeviceSize | **25**（32 MiB） |
| ClockPrescaler | 示例为 **8**，HCLK 275 MHz 时 34.375 MHz |
| ChipSelectHighTime | 至少 **4** |
| ClockMode | MODE0 |
| DualQuad / FreeRunningClock | 禁用 |
| WrapSize / ChipSelectBoundary | NOT_SUPPORTED / 0 |
| DelayBlockBypass / SampleShifting | 示例为 BYPASSED / NONE |

参考板使用 OCTOSPI1 / OSPIM Port 1：CS=PE11(AF11)、CLK=PF10(AF9)、IO0=PF8(AF10)、IO1=PF9(AF10)、IO2=PE2(AF9)、IO3=PF6(AF10)，电源 3.3 V。
其他板按实际引脚设置 CubeMX，不要直接照搬。IQ 型号 QE 出厂固定为 1；组件仍检查 QE，不写状态寄存器来自动解锁或改变模式。

## 使用示例

板级初始化完成后：

```c
#include "stm_flash.h"

flash_handle_t flash = NULL;
const flash_config_t config = {
    .hal = &hospi1,
    .read_mode = FLASH_READ_QUAD,
};
uint8_t data[16] = {0};
stm_err_t err = flash_create(&config, &flash);
if (err == STM_OK) {
    err = flash_read(flash, 0x01000000U, data, sizeof(data));
}
stm_err_t cleanup = flash_delete(&flash);
if (err == STM_OK) { err = cleanup; }
// 应用继续处理 err。
```

[example/main.c](example/main.c) 提供完整参考入口及 `board_init()`，默认只读，不加入库的编译目标。定义 `EXAMPLE_FLASH_WRITE_TEST=1` 会覆盖最后一个扇区，须先保留测试区域。示例完成后可通过调试器观察 `example_result`。

## 接口

| 函数 | 行为 |
|---|---|
| flash_create / flash_delete | 创建初始化 / 释放句柄 |
| flash_get_info | 查询 ID、时钟、读取模式、ready、最近 HAL 状态 |
| flash_read_status | 读取 SR1、SR2、SR3 |
| flash_read | 按字节读取，单线 13h 或四线 6Ch |
| flash_write | 预检整个范围、自动拆分 256 字节页并逐页校验 |
| flash_erase | 按 4 KiB 对齐擦除并检查全 FF |
| flash_verify | 与 RAM 数据比较，不匹配时停用实例 |

偏移和长度均为字节，命令 13h/6Ch/12h/21h 使用四字节地址，可跨 16 MiB。读写长度为零时可传 NULL 缓冲区；擦除偏移和长度必须 4 KiB 对齐。

写入不会自动擦除：整段预检发现 0→1 时返回 `FLASH_ERR_NEEDS_ERASE`，不提前写入前面的页。写保护返回 `FLASH_ERR_PROTECTED`，挂起操作返回 `FLASH_ERR_SUSPENDED`。不自动解锁、操作 OTP 或整片擦除。擦写中途失败可能已完成部分操作，不提供事务回滚或断电恢复。

## 生命周期与错误处理

- 输出句柄首次使用前设为 `NULL`。创建成功即可使用；失败释放内部对象，输出保持 NULL。已有句柄的输出变量会被拒绝并保留原值。
- 配置结构体只在创建时读取，HAL 句柄由应用持有，在设备整个生命周期内有效；使用期间不能重新配置其外设或时钟。
- `delete(&handle)` 成功后清空该变量，重复删除空句柄成功；其他句柄副本不会自动清空，删除后不可再使用。
- 删除只释放组件对象，不关闭 HAL 外设、不修改 MPU、不恢复存储数据；删除前先停止相关任务、DMA 和直接指针访问。
- 创建/删除须在普通线程上下文且中断开启时执行。应用须串行化同一组件的创建/删除，以及同一句柄的读写、查询和删除；组件不提供内部锁。
- 返回类型统一为 `stm_err_t`，`STM_OK=0`，用 `err != STM_OK` 判断错误。公共码定义见 [stm_common](https://github.com/NingZiXi/stm_common#错误码约定)。NULL 句柄返回 `STM_ERR_INVALID_ARG`，故障停用的有效句柄返回 `STM_ERR_INVALID_STATE`。
- `get_info` 返回只读快照；`ready` 表示软件允许访问，不代表硬件已自检通过。

同一个 OCTOSPI 外设只能有一个 Flash 对象。阻塞访问要求普通线程、中断开启和 HAL tick 正常；源缓冲区在整个调用期间必须稳定，不能使用 OCTOSPI 映射窗口作为缓冲区。

每次 HAL 传输超时 100 ms，页编程忙等待 10 ms，扇区擦除忙等待 1000 ms；没有整个多页/多扇区调用的统一时限。
HAL 错误、超时或校验失败会清零 ready，仍可通过 get_info 查询。Flash 可能在超时后继续擦写，须先确认原操作完成，再删除对象、由板级恢复 HAL、重新创建；不能盲目重发擦写。

## 验证

- 软件：真实驱动在 Cortex-M7 模拟环境执行，O0/O2/Os 均通过，覆盖边界、保护、HAL 故障、超时、分配失败和生命周期；见 [tests/README.md](tests/README.md)。
- 实板：STM32H723ZG + W25Q256JVEIQ，34.375 MHz；20 次创建/删除、单线/四线读取、四个扇区跨页/跨 16 MiB 擦写通过，原数据恢复并在 MCU 复位后比对一致。
- 未覆盖：全片耐久、温度范围、整板断电恢复、长期压力及 50 MHz 实测。

规格参考：[W25Q256JV 数据手册](https://atta.szlcsc.com/upload/public/pdf/source/20190529/C97522_47AE152D0BB11FB4C8509E398C0AD2EA.pdf)。

## 许可

组件原创代码采用 [MIT License](LICENSE)。`example/main.c` 中改编自 ST 的板级时钟初始化保留原版权及授权说明，见 [示例说明](example/README.md)；这些第三方部分不由本仓库重新授予 MIT 许可。HAL/CMSIS 不随组件分发，遵循各自许可。
