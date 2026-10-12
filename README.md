# stm_flash

## 🤖 让 Agent 帮助接入

> 将 stm32-hal-lib 中的 stm_flash 接入当前工程，为 `<MCU、Flash完整型号、SPI/QSPI/OSPI和引脚>` 制作只读 Demo。先读 README.md 与 AGENTS.md，复用组件，不猜接线、不覆盖已有代码；缺少信息先询问，未经确认不擦写。完成后说明版本、编译结果与未验证项。

**当前版本：4.1.0。新增 GD25Q128C/E 公共单线子集与普通 SPI 后端；保留 v4 接口。**

同步串行 NOR 库。核心仅依赖 C 标准库与 `stm_common`，没有 HAL/CMSIS/RTOS 依赖。
应用组合 **器件描述符 + host 操作表 + 实例上下文**。本次新增入口，不改变 v4 的 read/write/erase/verify 调用形式。

已发布基线为 **v4.0.1**（v4.0.0 的文档补丁）。原 H757 QSPI 实板结论保留，H723 OSPI 仍为软件模型回归。
普通 SPI 后端已通过 F407 HAL 编译及假 HAL 回归。GD25Q128 在 F407 实板以 10.5 MHz 完成 JEDEC 识别及授权末尾 4 KiB 扇区的擦除、跨页写入/读回、错误条件和恢复擦除验证；未做全片写擦验收。

## 支持的 Driver

下表列出当前源码内置的器件 Driver 和控制器后端；器件参数支持不等于每种器件/控制器组合都通过实板验证。

| 类型 | Driver / 型号 | 源码或接入入口 | 支持范围与限制 |
| --- | --- | --- | --- |
| NOR 器件 | GD25Q128C/E 公共子集 | [`flash_device_gd25q128`](src/devices/gd25q128.c) | 16 MiB；JEDEC `0xC84018`；三字节地址；256 B 页、4 KiB 扇区；仅 1-1-1；保守上限 50 MHz；F407 实板完成授权末尾 4 KiB 扇区写擦验收 |
| NOR 器件 | GD25Q256E | [`flash_device_gd25q256e`](src/devices/gd25q256e.c) | 32 MiB；JEDEC `0xC84019`；256 B 页、4 KiB 擦除块；单线 / 1-1-4 读取；保守上限 50 MHz |
| NOR 器件 | W25Q256JV-IQ | [`flash_device_w25q256jv_iq`](src/devices/w25q256jv_iq.c) | 32 MiB；JEDEC `0xEF4019`；256 B 页、4 KiB 擦除块；单线 / 1-1-4 读取；保守上限 50 MHz |
| 控制器后端 | STM32 HAL SPI | [`flash_spi_bind()`](adapters/stm32_hal/flash_spi.h)；target `stm_flash_spi` | `STM_FLASH_WITH_SPI=ON`；8 位双线主机、软件 CS、mode 0/3、MSB；阻塞 1-1-1，无 DMA；已用 F407 HAL 编译 |
| 控制器后端 | STM32 HAL QSPI | [`flash_qspi_bind()`](adapters/stm32_hal/flash_qspi.h)；target `stm_flash_qspi` | `STM_FLASH_WITH_QSPI=ON`；当前覆盖 H757 QSPI，阻塞间接传输 |
| 控制器后端 | STM32 HAL OSPI | [`flash_ospi_bind()`](adapters/stm32_hal/flash_ospi.h)；target `stm_flash_ospi` | `STM_FLASH_WITH_OSPI=ON`；当前覆盖 H723 OSPI，仅使用上述 NOR 的 SDR 协议，不支持八线 OPI/DTR |

内置器件可按 CMake 选项裁剪；当前没有并行 NOR 或 NAND 的内置后端。自定义器件/host 属于扩展接口，不是现成支持清单。四线读取要求 QE 已有效，组件不自动修改非易失寄存器。

## 结构

- `include/stm_flash.h`：应用 API、统一错误与诊断。
- `include/stm_flash_device.h`：只读器件描述、芯片操作扩展与通用 NOR 参数。
- `include/stm_flash_host.h`：同步事务、控制器与最小运行环境契约。
- `src/stm_flash.c`：实例生命周期、范围检查、分页、擦除前检查及读回校验。
- `src/flash_nor_common.c`：可复用 NOR 指令与状态操作。
- `src/devices/`：GD25Q128、GD25Q256E、W25Q256JV-IQ 分文件参数。
- `src/flash_registry.c`：受构建配置裁剪的识别候选表。
- `adapters/stm32_hal/`：可选 SPI/QSPI/OSPI 适配器，各自独立 target。

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
QSPI/OSPI 后端覆盖 H757 QSPI/H723 OSPI 配置，普通 SPI 后端通过 F407 HAL 编译，其他 HAL 系列需单独编译和板测，不承诺自动兼容。

`STM_FLASH_ENABLE_GD25Q128`、`STM_FLASH_ENABLE_GD25Q256E`、`STM_FLASH_ENABLE_W25Q256JV_IQ` 默认 ON，可关闭以裁剪对应描述符和 AUTO 候选。
没有内置型号时仍可显式传入应用提供的描述符。

`stm_common` 优先复用已有 target，其次同级目录；否则按固定 v1.0.0 提交下载。
可用 `STM_COMMON_FETCH=OFF` 禁止下载，或用 `STM_COMMON_GIT_REPOSITORY` 选择镜像。

### 普通 SPI（STM32F4）

```cmake
add_subdirectory(Lib/stm_flash)
set(STM_FLASH_WITH_SPI ON)
add_subdirectory(Lib/stm_flash/adapters/stm32_hal)
target_compile_definitions(stm_flash_spi PUBLIC STM_FLASH_HAL_HEADER="stm32f4xx_hal.h")
target_link_libraries(app PRIVATE stm_flash_spi)
```

有多个后端时，在一次 `add_subdirectory(adapters/stm32_hal)` 前设置所有所需开关。
仅 GD25Q128 可关闭 `STM_FLASH_ENABLE_GD25Q256E` 和 `STM_FLASH_ENABLE_W25Q256JV_IQ`；关闭全部内置型号时仍能显式使用自定义描述符。

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

## GD25Q128 普通 SPI 接入

```c
#include "stm_flash.h"
#include "flash_spi.h"

static flash_spi_context_t spi;
static flash_handle_t nor;

stm_err_t board_flash_init(SPI_HandleTypeDef *hal, GPIO_TypeDef *cs_port,
                          uint16_t cs_pin, uint32_t spi_kernel_hz)
{
    HAL_GPIO_WritePin(cs_port, cs_pin, GPIO_PIN_SET);
    const flash_config_t config = {
        .device = &flash_device_gd25q128,
        .host = flash_spi_bind(&spi, hal, cs_port, cs_pin, spi_kernel_hz),
        .read_mode = FLASH_READ_SINGLE,
    };
    return flash_create(&config, &nor);
}
```

完整只读参考见 [example/gd25q128_spi.c](example/gd25q128_spi.c)。该示例用能力裁剪拒绝擦写，且不猜测 SPI 实例或 CS 引脚。

- HAL、GPIO、时钟和 Flash 供电先由板级配置；CS 先拉高。SPI 必须为 master、2lines、8bit、MSB、软件 NSS、mode 0/3，关闭 TI/CRC，空闲且未锁定。
- `spi_kernel_hz` 是控制器实际内核时钟。例如本 F407 SPI1 使用 APB2 84 MHz 和分频 8，对应 SCK 10.5 MHz；不要将此频率照搬到其他时钟树。
- SPI 后端在 JEDEC 探测之前限制 SCK 不超过 50 MHz，随后检查器件上限。只实现 1-1-1、3/4 字节地址、0～32 个整字节 dummy cycle，单次数据不超过 4096 B。
- 每个事务由组件管理一次 CS 低/高，读阶段固定 MOSI=`FF`；指令、地址、dummy、数据共用同一个 timeout，支持 tick 回绕，不接受 0 或无限等待。
- 失败记录 `last_hal_status`、`last_abort_status`，拉高 CS 并锁存 `faulted`，不自动重试。应先确认控制器已安全停止，再删除实例、检查/重新初始化硬件并重新绑定；abort 失败不能直接视为可恢复。
- bind 不配置引脚、不复位器件；不会占用 HAL 全局回调。多个 CS 共用同一 SPI 控制器的并发/多实例管理不在本轮范围。

### 型号限制与诊断

`flash_device_gd25q128` 只覆盖核对过的 GD25Q128C/E 公共单线指令与状态规则，不代表所有 GD25Q128 后缀。
READ=`03`、PP=`02`、SE=`20`，三字节地址可覆盖完整 16 MiB，无需进入四字节模式。
BUSY/WEL、BP4:0、CMP、SUS1/SUS2 分别检查；C 的 SR3 WPS 有效时保守拒绝全部擦写。QE 只报告，不自动修改；不提供四线读、自动解锁或软件复位。
JEDEC `C84018` 无法区分 C/E，使用其他后缀前核对完整丝印与对应数据手册，不仅替换 ID。

描述符参数依据 GD25Q128E Rev1.4（April 2024，GigaDevice 数据手册）与 GD25Q128C Rev2.5 的公共规则核对；时钟限值取 50 MHz 保守子集，程序/扇区超时分别取 10/1000 ms。

create 返回 `STM_ERR_INVALID_CONFIG` 可能是主机配置无效，也可能是显式描述符 ID 不匹配；HAL_OK 只说明传输过程结束，不证明芯片存在。
遇到 `000000`/`FFFFFF`，检查真实 CS/SCK/MOSI/MISO 网络、供电及 HOLD#/RESET# 状态，必要时断电重上电并抓取波形；禁止放宽 ID mask、盲改引脚或擦除来“修复”。
未验证容量、全 FF 或重复读取一致均不构成可擦除的授权。

## 型号和扩展

`device != NULL`：先读取 JEDEC ID，只校验指定描述符；不匹配返回 `STM_ERR_INVALID_CONFIG`，不会改成另一个型号。
`device == NULL`：仅在已编译内置列表中识别，未知返回 `STM_ERR_NOT_SUPPORTED`，匹配多个不同描述符返回 `STM_ERR_INVALID_CONFIG`。
AUTO 可通过 `candidates/candidate_count` 使用调用方候选表。相同 JEDEC ID 无法证明封装或型号全部后缀，无法可靠区分时应显式配置。

新增型号可定义自己的静态 `flash_device_t`。纯参数差异复用 `flash_nor_ops` 与独立 `flash_nor_profile_t`；特殊行为定义 `flash_chip_ops_t`，复用公开的 `flash_nor_*` 函数并覆盖必要操作。无需修改核心或增加 MCU 系列分支。
通用 NOR 实现是复用代码，不是未知芯片兜底驱动。自定义 host 实现同步 `exec`、配置/容量检查、资源标识、时基/延时和上下文检查；可选 `buffer_ok` 加入平台缓冲区限制。
host 回调返回 `stm_err_t`，原生 HAL 状态仅保留在 HAL 上下文的 `last_hal_status`。

## 数据与故障语义

- GD25Q128 公共单线子集：JEDEC C84018，16 MiB、三字节地址。GD25Q256E：JEDEC C84019，32 MiB；W25Q256JV-IQ：JEDEC EF4019，32 MiB。后两者均使用独立四字节指令、256 B 页、4 KiB 扇区；当前保守串行时钟上限 50 MHz。
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
