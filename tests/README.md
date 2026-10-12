# stm_flash 验证

## 原生模型与假 HAL

```sh
cmake -S tests/portable -B build/portable -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/portable
ctest --test-dir build/portable --output-on-failure
```

核心不提供 HAL/CMSIS 路径或 MCU 宏；SPI adapter 的独立目标使用 `portable/fake_hal/spi_test_hal.h`，不向核心注入 MCU 依赖。需要宿主 C/C++ 编译器。

| 测试 | 范围 |
| --- | --- |
| `flash_portable` | 保留原核心独立上下文、自定义 ops、显式 ID 不匹配、AUTO 歧义、边界、分块、保护、超时回绕与生命周期；C/C++ 头文件 |
| `flash_gd25q128` | 实际 GD25Q128 描述符与 16 MiB 模型；AUTO、三字节命令、跨页/chunk、容量末端、扇区、BP/CMP/WPS、SUS/QE、错误锁存、只读能力拒绝写/擦且无 WREN |
| `flash_spi_test` | bind 无副作用、mode 0/3/非法配置、50 MHz 边界、CS、3/4 字节地址、dummy、RX MOSI=FF、无数据命令、整体超时/回绕、各传输阶段失败、abort 失败、故障锁存、非法上下文/缓冲；C++ SPI 头 |

默认开启全部内置描述符，以上测试依赖默认候选。另以组件根目录单独配置关闭部分/全部 `STM_FLASH_ENABLE_*` 验证裁剪构建，不能直接关闭描述符后跑依赖它的测试。
通过 `-DCMAKE_C_FLAGS=-O2 -DCMAKE_CXX_FLAGS=-O2` 或 `-Os` 分别验证优化级别；默认 Debug 为 O0（需检查所用工具链）。

## 真实 HAL 结构与 ARM 模型回归

```sh
python tests/run_tests.py --include /path/to/hal-config --include /path/to/hal/Inc --include /path/to/device/Include --include /path/to/cmsis/Core/Include --mcu STM32H757xx --bus qspi --build-dir build/h757-tests
```

运行器编译实际核心及选定适配器，使用 HAL 桩与 Unicorn Cortex-M7 执行 O0/O2/Os。H723 使用 `STM32H723xx` 与 `--bus ospi`。可用 `--compiler` 指定 ARM GCC 路径；Python 依赖见 requirements.txt。
保留原有容量、时序/协议、保护、超时、失败回收和数据故障回归，不删除测试换取通过。
普通 SPI 另在 F407 工程编译真实 HAL；假 HAL 不等于其他 STM32 系列均兼容。

## 实板安全与当前结果

- 使用明确的器件丝印、接线、时钟与目标固件；保留用户验收的回滚固件和源码哈希，烧录后独立读回 MCU Flash。
- 未授权外部 Flash 区域时只读，不执行 program/erase/WREN；全 FF 不代表可以擦除。
- ID 匹配后先重复读首尾/跨页/跨扇区，再分步两遍全片 CRC；这只验证读取一致性，不证明每个地址解码或写入可靠。
- 4.1.0 的原生、GD25Q128 模型、SPI 假 HAL 回归通过；SPI 后端已在 F407 工程使用真实 HAL 编译。
- **GD25Q128 实板有限范围验收通过：JEDEC `C84018`，SPI `10.5 MHz`。仅操作获授权的末尾 4 KiB 扇区 `0x00FFF000–0x00FFFFFF`，完成扇区擦除、跨页写入/读回、0→1 写入拒绝、末地址操作、越界及未对齐参数检查，最终再次擦除并确认全 `0xFF`（`stage=6`、`error=0`、`completed=1`、`final_all_ff=1`）。这不是全片写擦或长期可靠性验收。**
- 实板验证快照保存在外层工程 `build/gd25q128-test-20261012-new-board/write-test-snapshot.log`；板子恢复为只读验收固件，临时写测试入口未纳入组件。
- 既有 H757 QSPI 实板结论不扩展为 GD25Q128 或 H723 OSPI 实板结论。软件模型不验证电气、供电、板级信号完整性或物理磨损。
