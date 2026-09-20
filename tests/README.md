# 软件测试

编译真实 `stm_flash.c`，使用 HAL 桩和 Unicorn 执行 Cortex-M7 指令，不连接开发板。需要 ARM GCC、Python 3，以及提供 STM32H7 HAL/CMSIS 头文件的 CubeMX 工程。共享测试分配器来自同级 [stm_common](https://github.com/NingZiXi/stm_common)。

在当前组件仓库根目录运行（以下为 Windows PowerShell 示例）：

```powershell
python -m venv .venv
.venv/Scripts/python -m pip install -r tests/requirements.txt
.venv/Scripts/python tests/run_tests.py --project-root D:/path/to/stm32h7-project
```

Linux/macOS 使用 `.venv/bin/python`。运行器可自动向上查找 CubeMX 工程；独立克隆时建议显式传 `--project-root`。
非标准目录可重复传 `--include` 指定 HAL、HAL 配置和 CMSIS 目录；显式 include 且未指定 project-root 时不自动加宿主路径。
`--compiler` 指定编译器，`--mcu` 默认 STM32H723xx，`--build-dir` 指定产物目录，`--python-deps` 指定预安装 Python 依赖目录。切换 MCU 宏不表示新芯片已获硬件验证。

已验证 GNU Arm GCC 12.3.1、Unicorn 2.1.4、pyelftools 0.32；在 O0/O2/Os 下使用 Wall/Wextra/Werror，失败返回非零退出码和 C 测试行号。

## 覆盖范围

- v3 显式/自动型号选择、未知型号及总线拒绝、实例容量与能力、状态有效掩码和失败输出保留。

- 创建分配失败、失败资源回收、重复创建、配置快照、删除清空、20 次创建/删除循环及泄漏/重复释放检查。
- HAL 配置、调用上下文、地址和长度溢出、容量边界。
- JEDEC/QE、四字节命令、单线/四线读取、跨页/跨 16 MiB 编程、擦除对齐、保护/挂起、0→1 拒绝、超时回绕。
- 各 HAL 调用错误注入、编程/擦除失败的读回校验、两个 OCTOSPI 对象及非表头对象删除。

模型不模拟 GPIO、电源、真实外设时序、MPU/Cache 或器件内部电路。软件通过不能替代硬件验证；实板覆盖范围见 [README](../README.md#验证)。
