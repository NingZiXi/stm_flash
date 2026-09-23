# stm_flash

**4.0.0：器件描述符、控制器接口与实例上下文重构版。已完成 H757 QSPI 实板验证；H723 OSPI 通过软件模型回归。**

同步串行 NOR 库。核心仅依赖 C 标准库与 `stm_common`，没有 HAL/CMSIS/RTOS 依赖。
应用组合 **器件描述符 + host 操作表 + 实例上下文**。本次为破坏性初始化 API 变更；现有 read/write/erase/verify 调用形式保留。

## 结构

- `include/stm_flash.h`：应用 API、统一错误与诊断。
- `include/stm_flash_device.h`：只读器件描述、芯片操作扩展与通用 NOR 参数。
- `include/stm_flash_host.h`：同步事务、控制器与最小运行环境契约。
- `src/stm_flash.c`：实例生命周期、范围检查、分页、擦除前检查及读回校验。
- `src/flash_nor_common.c`：可复用 NOR 指令与状态操作。
- `src/devices/`：GD25Q256E、W25Q256JV-IQ 分文件参数。
- `src/flash_registry.c`：受构建配置裁剪的识别候选表。
- `adapters/stm32_hal/`：可选 QSPI/OSPI 适配器，各自独立 target。

## CMake 接入

```cmake
add_subdirectory(Lib/stm_flash)
set(STM_FLASH_WITH_QSPI ON)
add_subdirectory(Lib/stm_flash/adapters/stm32_hal)
target_compile_definitions(stm_flash_qspi PUBLIC STM_FLASH_HAL_HEADER="stm32h7xx_hal.h")
target_link_libraries(app PRIVATE stm_flash_qspi)
```

OSPI 改用 `STM_FLASH_WITH_OSPI` 与 `stm_flash_ospi`。默认不编译任何 HAL 适配器。
适配器存在 `stm32cubemx` target 时继承其编译配置；其他工程显式提供 HAL 头路径、宏与链接依赖。
两个适配器只覆盖已验证的 H757 QSPI/H723 OSPI 配置，其他 HAL 系列需单独编译和板测，不承诺自动兼容。

`STM_FLASH_ENABLE_GD25Q256E`、`STM_FLASH_ENABLE_W25Q256JV_IQ` 默认 ON，可关闭以裁剪对应描述符和 AUTO 候选。
没有内置型号时仍可显式传入应用提供的描述符。

`stm_common` 优先复用已有 target，其次同级目录；否则按固定 v1.0.0 提交下载。
可用 `STM_COMMON_FETCH=OFF` 禁止下载，或用 `STM_COMMON_GIT_REPOSITORY` 选择镜像。

## BSP 初始化

```c
#include "stm_flash.h"
#include "flash_qspi.h"

static flash_qspi_context_t host;
static flash_handle_t nor;

stm_err_t board_flash_init(uint32_t qspi_kernel_hz)
{
    const flash_config_t config = {
        .device = &flash_device_gd25q256e,
        .host = flash_qspi_bind(&host, &hqspi, qspi_kernel_hz),
        .read_mode = FLASH_READ_SINGLE,
    };
    return flash_create(&config, &nor);
}
```

`hqspi` 须先由 CubeMX/BSP 初始化。第三个绑定参数是**实际外设内核时钟**，并非串行输出时钟；适配器结合 HAL 分频配置检查速率。
适配器不判断 MCU 时钟树。BSP 负责检查时钟源并提供正确频率，时钟改变后必须删除实例再重新绑定。

绑定函数只组合上下文和操作表，不访问硬件，也不修改 GPIO、时钟、QE、写保护或存储映射配置。
芯片描述符、芯片操作表、host 操作表、上下文及其 HAL 句柄须覆盖设备整个生命周期；`config` 本身在 create 返回后即可销毁。
成功创建后禁止修改描述符或重新绑定上下文。`delete` 只释放组件对象，不关闭硬件、不释放用户资源。
每个实例只在 create 分配内存，读写过程中不分配；不提供默认锁，生命周期及共享资源访问由调用者串行化，阻塞接口不用于 ISR。
同一适配器控制器资源不允许重复打开，即使使用不同的上下文或别名 HAL 句柄。

## 型号和扩展

`device != NULL`：先读取 JEDEC ID，只校验指定描述符；不匹配返回 `STM_ERR_INVALID_CONFIG`，不会改成另一个型号。
`device == NULL`：仅在已编译内置列表中识别，未知返回 `STM_ERR_NOT_SUPPORTED`，匹配多个不同描述符返回 `STM_ERR_INVALID_CONFIG`。
AUTO 可通过 `candidates/candidate_count` 使用调用方候选表。相同 JEDEC ID 无法证明封装或型号全部后缀，无法可靠区分时应显式配置。

新增型号可定义自己的静态 `flash_device_t`。纯参数差异复用 `flash_nor_ops` 与独立 `flash_nor_profile_t`；特殊行为定义 `flash_chip_ops_t`，复用公开的 `flash_nor_*` 函数并覆盖必要操作。无需修改核心或增加 MCU 系列分支。
通用 NOR 实现是复用代码，不是未知芯片兜底驱动。自定义 host 实现同步 `exec`、配置/容量检查、资源标识、时基/延时和上下文检查；可选 `buffer_ok` 加入平台缓冲区限制。
host 回调返回 `stm_err_t`，原生 HAL 状态仅保留在 HAL 上下文的 `last_hal_status`。

## 数据与故障语义

- GD25Q256E：JEDEC C84019，32 MiB；W25Q256JV-IQ：JEDEC EF4019，32 MiB。两者均使用独立四字节指令、256 B 页、4 KiB 扇区；当前保守串行时钟上限 50 MHz。
- 只支持同步 SDR 单线或 1-1-4 读取；使用 OSPI 控制器不意味着已经支持八线 OPI/DTR。四线模式要求 QE 已有效，库不自动修改非易失寄存器。
- GD 的 BP4、SUS1/SUS2 与 Winbond 状态保护规则各自描述，不能只改 JEDEC ID。
- 写入先检查全范围是否需要擦除，再按页和 host 传输上限分块，逐块读回校验；不自动擦除。擦除按扇区对齐，之后检查全 FF。
- 通信、超时或读回故障停用实例；失败前的页/扇区可能已经修改。未知/歧义识别不会尝试写入。
- 初始化不执行破坏性自检。应用必须自己备份需保留的数据，再显式执行擦写测试。
- 当前适配器只执行阻塞间接传输，不管理 DMA、XIP、Cache 或 RTOS 锁；其他可访问 RAM 缓冲区的属性由 BSP 保证。

## v3 → v4

1. `.bus.type/.bus.handle` 改为 `.host = flash_*_bind(...)`。
2. `.chip` 枚举改为 `.device` 描述符指针；AUTO 改为 NULL。
3. 显式链接所选 HAL 适配器 target；核心不再隐式引入 CubeMX。
4. `flash_info_t.chip/bus_type/last_hal_status` 改为 `device/host_name/last_error`，HAL 原始诊断从适配器上下文读取。
5. 手动源文件集成改用 `include/`、`src/` 和选定的 `adapters/stm32_hal/`；重新编译所有消费者，不与 v3 对象文件混用。

验证命令及范围见 [tests/README.md](tests/README.md)。完整板级参考见 [example/main.c](example/main.c)，时钟/引脚是原 H723 参考配置，移植时必须用本板 CubeMX 配置。
