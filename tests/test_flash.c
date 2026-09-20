/**
 * @file    test_flash.c
 * @brief   通过 NOR 模型与 HAL 桩验证 Flash 驱动
 */
#include "stm_flash.h"
#include "tests/test_allocator.h"

#define CHECK(x) do { if (!(x)) { return __LINE__; } } while (0)
#define MEMORY ((uint8_t *)0xD0000000UL)
static OSPI_RegularCmdTypeDef command;
static uint8_t registers[3], identity[3];
static uint32_t tick, calls, fail_at, programs, erases, busy, kernel;
static int protocol_error, ignore_write, ignore_erase, stuck_busy, reject_wel;
static HAL_StatusTypeDef injected;


// 用简短测试配置调用公开创建接口
static stm_err_t create_device(flash_handle_t *out, OSPI_HandleTypeDef *hal, flash_read_mode_t value)
{
    const flash_config_t config = {.hal = hal, .read_mode = value};
    return flash_create(&config, out);
}

// 查询公开诊断快照
static flash_info_t info(flash_handle_t handle)
{
    flash_info_t result = {0};
    (void)flash_get_info(handle, &result);
    return result;
}

// 提供独立测试运行时的内存填充
void *memset(void *dst, int value, size_t size)
{
    uint8_t *p = dst;
    while (size-- != 0U) { *p++ = (uint8_t)value; }
    return dst;
}

// 提供结构体复制所需的测试运行时
void *memcpy(void *dst, const void *src, size_t size)
{
    uint8_t *out = dst; const uint8_t *in = src;
    while (size-- != 0U) { *out++ = *in++; }
    return dst;
}

// 当前 H7 HAL 通用查询不支持 OSPI
uint32_t HAL_RCCEx_GetPeriphCLKFreq(uint64_t source)
{
    (void)source;
    return 0U;
}

// 返回模拟 HCLK
uint32_t HAL_RCC_GetHCLKFreq(void) { return kernel; }

// 返回模拟 HAL 状态
uint32_t HAL_OSPI_GetState(const OSPI_HandleTypeDef *hal)
{
    return hal->State;
}

// 读取模拟 tick
uint32_t HAL_GetTick(void) { return tick; }

// 推进模拟 tick
void HAL_Delay(uint32_t ms) { tick += ms; }

// 在指定调用处注入 HAL 错误
static HAL_StatusTypeDef next_call(void)
{
    return ++calls == fail_at ? injected : HAL_OK;
}

// 检查总线帧格式并模拟指令副作用
HAL_StatusTypeDef HAL_OSPI_Command(OSPI_HandleTypeDef *hal, OSPI_RegularCmdTypeDef *cmd, uint32_t timeout)
{
    (void)hal; (void)timeout;
    HAL_StatusTypeDef result = next_call();
    if (result != HAL_OK) { return result; }
    command = *cmd;
    int addressed = cmd->Instruction == 0x13U || cmd->Instruction == 0x6CU ||
                    cmd->Instruction == 0x12U || cmd->Instruction == 0x21U;
    if (cmd->InstructionMode != HAL_OSPI_INSTRUCTION_1_LINE ||
        cmd->InstructionDtrMode != HAL_OSPI_INSTRUCTION_DTR_DISABLE ||
        cmd->DataDtrMode != HAL_OSPI_DATA_DTR_DISABLE ||
        (addressed && (cmd->AddressSize != HAL_OSPI_ADDRESS_32_BITS ||
                       cmd->AddressMode != HAL_OSPI_ADDRESS_1_LINE)) ||
        (!addressed && cmd->AddressMode != HAL_OSPI_ADDRESS_NONE) ||
        (cmd->Instruction == 0x6CU && (cmd->DummyCycles != 8U || cmd->DataMode != HAL_OSPI_DATA_4_LINES)) ||
        (cmd->Instruction == 0x13U && (cmd->DummyCycles != 0U || cmd->DataMode != HAL_OSPI_DATA_1_LINE))) {
        protocol_error = 1; return HAL_ERROR;
    }
    if (cmd->Instruction == 0x06U && !reject_wel) { registers[0] |= 2U; }
    if (cmd->Instruction == 0x21U) {
        if (!(registers[0] & 2U) || cmd->Address % 4096U != 0U || cmd->Address >= FLASH_SIZE_BYTES) {
            protocol_error = 1; return HAL_ERROR;
        }
        if (!ignore_erase) { memset(MEMORY + cmd->Address, 0xFF, 4096U); }
        ++erases; registers[0] &= (uint8_t)~2U; busy = 3U;
    }
    return HAL_OK;
}

// 模拟状态、ID 和数组读取
HAL_StatusTypeDef HAL_OSPI_Receive(OSPI_HandleTypeDef *hal, uint8_t *data, uint32_t timeout)
{
    (void)hal; (void)timeout;
    HAL_StatusTypeDef result = next_call();
    if (result != HAL_OK) { return result; }
    switch (command.Instruction) {
    case 0x05U:
        data[0] = registers[0] | ((busy || stuck_busy) ? 1U : 0U);
        if (busy) { --busy; } break;
    case 0x35U: data[0] = registers[1]; break;
    case 0x15U: data[0] = registers[2]; break;
    case 0x9FU: for (unsigned i = 0; i < 3U; ++i) { data[i] = identity[i]; } break;
    case 0x13U: case 0x6CU:
        if (command.Address >= FLASH_SIZE_BYTES || command.NbData > FLASH_SIZE_BYTES - command.Address) {
            protocol_error = 1; return HAL_ERROR;
        }
        for (uint32_t i = 0; i < command.NbData; ++i) { data[i] = MEMORY[command.Address + i]; }
        break;
    default: protocol_error = 1; return HAL_ERROR;
    }
    return HAL_OK;
}

// 模拟 NOR 按位与编程与页边界限制
HAL_StatusTypeDef HAL_OSPI_Transmit(OSPI_HandleTypeDef *hal, uint8_t *data, uint32_t timeout)
{
    (void)hal; (void)timeout;
    HAL_StatusTypeDef result = next_call();
    if (result != HAL_OK) { return result; }
    if (command.Instruction != 0x12U || !(registers[0] & 2U) || command.NbData == 0U ||
        command.NbData > 256U - command.Address % 256U ||
        command.Address >= FLASH_SIZE_BYTES || command.NbData > FLASH_SIZE_BYTES - command.Address) {
        protocol_error = 1; return HAL_ERROR;
    }
    if (!ignore_write) {
        for (uint32_t i = 0; i < command.NbData; ++i) { MEMORY[command.Address + i] &= data[i]; }
    }
    ++programs; registers[0] &= (uint8_t)~2U; busy = 2U;
    return HAL_OK;
}

// 重置模拟器状态与默认外设配置
static OSPI_HandleTypeDef fixture(void)
{
    OSPI_HandleTypeDef h = {0};
    registers[0] = 0U; registers[1] = 2U; registers[2] = 0U;
    identity[0] = 0xEFU; identity[1] = 0x40U; identity[2] = 0x19U;
    tick = calls = fail_at = programs = erases = busy = 0U;
    protocol_error = ignore_write = ignore_erase = stuck_busy = reject_wel = 0;
    injected = HAL_ERROR; kernel = 275000000U;
    __HAL_RCC_OSPI_CONFIG(RCC_OSPICLKSOURCE_HCLK);
    h.Instance = OCTOSPI1; h.State = HAL_OSPI_STATE_READY;
    h.Init.DeviceSize = 25U; h.Init.ClockPrescaler = 8U;
    h.Init.ChipSelectHighTime = 4U; h.Init.MemoryType = HAL_OSPI_MEMTYPE_MICRON;
    h.Init.ClockMode = HAL_OSPI_CLOCK_MODE_0;
    h.Init.DualQuad = HAL_OSPI_DUALQUAD_DISABLE;
    h.Init.FreeRunningClock = HAL_OSPI_FREERUNCLK_DISABLE;
    h.Init.WrapSize = HAL_OSPI_WRAP_NOT_SUPPORTED;
    return h;
}

// 覆盖真实驱动的命令、边界、保护和失败路径
int test_entry(void)
{
    flash_handle_t d = NULL; OSPI_HandleTypeDef h = fixture();
    CHECK(create_device(&d, &h, FLASH_READ_QUAD) == STM_OK);
    CHECK(info(d).jedec_id == 0xEF4019U && info(d).clock_hz == 34375000U && programs == 0U && erases == 0U);
    CHECK(create_device(&d, &h, FLASH_READ_QUAD) == STM_ERR_INVALID_STATE);
    uint8_t tx[600], rx[600];
    for (unsigned i = 0; i < sizeof(tx); ++i) { tx[i] = (uint8_t)(i * 37U + 5U); }
    const uint32_t start = 0x00FFFFF0U;
    memset(MEMORY + start - 1U, 0xFF, sizeof(tx) + 2U);
    CHECK(flash_write(d, start, tx, sizeof(tx)) == STM_OK);
    CHECK(programs == 4U && MEMORY[start-1U] == 0xFF && MEMORY[start+sizeof(tx)] == 0xFF);
    CHECK(flash_read(d, start, rx, sizeof(rx)) == STM_OK);
    for (unsigned i = 0; i < sizeof(tx); ++i) { CHECK(rx[i] == tx[i]); }
    CHECK(flash_verify(d, start, tx, sizeof(tx)) == STM_OK);
    // 中断屏蔽状态不得进入阻塞 HAL 调用。
    __disable_irq();
    stm_err_t context = flash_read(d, 0U, rx, 1U);
    __enable_irq();
    CHECK(context == STM_ERR_INVALID_CONTEXT);
    __set_BASEPRI(0x80U);
    context = flash_read(d, 0U, rx, 1U);
    __set_BASEPRI(0U);
    CHECK(context == STM_ERR_INVALID_CONTEXT);
    __set_FAULTMASK(1U);
    context = flash_read(d, 0U, rx, 1U);
    __set_FAULTMASK(0U);
    CHECK(context == STM_ERR_INVALID_CONTEXT);
    CHECK(!protocol_error);
    uint32_t before = calls;
    CHECK(flash_read(d, FLASH_SIZE_BYTES, NULL, 0U) == STM_OK && calls == before);
    CHECK(flash_write(d, 0U, NULL, 0U) == STM_OK && calls == before);
    CHECK(flash_read(d, FLASH_SIZE_BYTES, rx, 1U) == STM_ERR_OUT_OF_RANGE);
    CHECK(flash_read(d, UINT32_MAX, rx, 1U) == STM_ERR_OUT_OF_RANGE);
    CHECK(flash_write(d, 0U, tx, SIZE_MAX) == STM_ERR_OUT_OF_RANGE);
    CHECK(flash_read(d, 0U, NULL, 1U) == STM_ERR_INVALID_ARG);
    CHECK(flash_read(d, 0U, (void*)0x90000000U, 1U) == STM_ERR_INVALID_ARG);
    CHECK(flash_read(d, 0U, (void*)(UINTPTR_MAX-1U), 4U) == STM_ERR_INVALID_ARG);
    CHECK(flash_erase(d, 1U, 4096U) == STM_ERR_INVALID_ARG);
    CHECK(flash_erase(d, 0U, 4097U) == STM_ERR_INVALID_ARG);
    CHECK(calls == before && info(d).ready);
    // 后段需要擦除时，不得提前改写前面的页。
    memset(MEMORY + 0x1000U, 0xFF, sizeof(tx)); MEMORY[0x1000U+599U] = 0U;
    CHECK(flash_write(d, 0x1000U, tx, sizeof(tx)) == FLASH_ERR_NEEDS_ERASE);
    CHECK(programs == 4U && MEMORY[0x1000U] == 0xFF && info(d).ready);
    memset(MEMORY + FLASH_SIZE_BYTES - 4096U, 0, 4096U);
    CHECK(flash_erase(d, FLASH_SIZE_BYTES - 4096U, 4096U) == STM_OK && erases == 1U);
    CHECK(flash_write(d, FLASH_SIZE_BYTES - 600U, tx, 600U) == STM_OK);
    CHECK(flash_read(d, FLASH_SIZE_BYTES - 600U, rx, 600U) == STM_OK);
    for (unsigned i = 0; i < 600U; ++i) { CHECK(rx[i] == tx[i]); }
    for (unsigned r = 0; r < 3U; ++r) {
        registers[r] |= r == 0U ? 4U : r == 1U ? 0x40U : 4U;
        uint32_t old = programs + erases;
        CHECK(flash_write(d, 0U, tx, 1U) == FLASH_ERR_PROTECTED);
        CHECK(flash_erase(d, 0U, 4096U) == FLASH_ERR_PROTECTED);
        CHECK(programs + erases == old && info(d).ready);
        registers[r] = r == 1U ? 2U : 0U;
    }
    registers[1] |= 0x80U;
    CHECK(flash_read(d, 0U, rx, 1U) == FLASH_ERR_SUSPENDED);
    CHECK(flash_write(d, 0U, tx, 1U) == FLASH_ERR_SUSPENDED);
    registers[1] = 2U;
    reject_wel = 1;
    CHECK(flash_erase(d, 0U, 4096U) == FLASH_ERR_PROTECTED);
    reject_wel = 0;
    memset(MEMORY, 0xFF, 256U); ignore_write = 1;
    CHECK(flash_write(d, 0U, tx, 1U) == STM_ERR_VERIFY && !info(d).ready);
    CHECK(flash_read(d, 0U, rx, 1U) == STM_ERR_INVALID_STATE);
    h = fixture(); flash_delete(&d);
    CHECK(create_device(&d, &h, FLASH_READ_SINGLE) == STM_OK);
    CHECK(flash_read(d, start, rx, 10U) == STM_OK && command.Instruction == 0x13U);
    ignore_erase = 1; MEMORY[0] = 0U;
    CHECK(flash_erase(d, 0U, 4096U) == STM_ERR_VERIFY && !info(d).ready);
    h = fixture(); flash_delete(&d);
    CHECK(create_device(&d, &h, FLASH_READ_QUAD) == STM_OK);
    tick = UINT32_MAX - 3U; stuck_busy = 1;
    CHECK(flash_read(d, 0U, rx, 1U) == STM_ERR_TIMEOUT && !info(d).ready);
    h = fixture(); flash_delete(&d);
    h.Init.DeviceSize = 24U;
    CHECK(create_device(&d, &h, FLASH_READ_QUAD) == STM_ERR_INVALID_CONFIG && calls == 0U);
    h.Init.DeviceSize = 25U; h.Init.ClockPrescaler = 1U;
    CHECK(create_device(&d, &h, FLASH_READ_QUAD) == STM_ERR_INVALID_CONFIG);
    h.Init.ClockPrescaler = 8U;
    __HAL_RCC_OSPI_CONFIG(RCC_OSPICLKSOURCE_PLL2);
    CHECK(create_device(&d, &h, FLASH_READ_QUAD) == STM_ERR_INVALID_CONFIG && calls == 0U);
    __HAL_RCC_OSPI_CONFIG(RCC_OSPICLKSOURCE_HCLK);
    identity[0] = 0U;
    CHECK(create_device(&d, &h, FLASH_READ_QUAD) == STM_ERR_NOT_SUPPORTED && !info(d).ready);
    identity[0] = 0xEFU; registers[1] = 0U;
    CHECK(create_device(&d, &h, FLASH_READ_QUAD) == STM_ERR_INVALID_CONFIG && !info(d).ready);
    registers[1] = 2U;
    CHECK(create_device(&d, &h, FLASH_READ_QUAD) == STM_OK);
    CHECK(flash_delete(&d) == STM_OK && !info(d).ready);
    CHECK(flash_delete(NULL) == STM_ERR_INVALID_ARG);
    // 初始化中的每个 HAL 调用均注入错误，并验证恢复。
    h = fixture(); flash_delete(&d);
    CHECK(create_device(&d, &h, FLASH_READ_QUAD) == STM_OK);
    uint32_t init_calls = calls;
    for (uint32_t at = 1U; at <= init_calls; ++at) {
        h = fixture(); flash_delete(&d); fail_at = at;
        CHECK(create_device(&d, &h, FLASH_READ_QUAD) == STM_ERR_IO && !info(d).ready);
        fail_at = 0U;
        CHECK(create_device(&d, &h, FLASH_READ_QUAD) == STM_OK);
    }
    // 每个擦写阶段的 HAL 故障都必须终止操作并停用实例。
    for (unsigned operation = 0U; operation < 2U; ++operation) {
        h = fixture(); flash_delete(&d);
        CHECK(create_device(&d, &h, FLASH_READ_QUAD) == STM_OK);
        memset(MEMORY, operation ? 0 : 0xFF, 4096U); calls = 0U;
        CHECK((operation ? flash_erase(d, 0U, 4096U) : flash_write(d, 250U, tx, 20U)) == STM_OK);
        uint32_t total = calls;
        for (uint32_t at = 1U; at <= total; ++at) {
            h = fixture(); flash_delete(&d);
            CHECK(create_device(&d, &h, FLASH_READ_QUAD) == STM_OK);
            memset(MEMORY, operation ? 0 : 0xFF, 4096U); calls = 0U; fail_at = at;
            injected = at % 3U == 0U ? HAL_BUSY : at % 3U == 1U ? HAL_ERROR : HAL_TIMEOUT;
            stm_err_t s = operation ? flash_erase(d, 0U, 4096U) : flash_write(d, 250U, tx, 20U);
            CHECK(s == (injected == HAL_TIMEOUT ? STM_ERR_TIMEOUT : STM_ERR_IO));
            CHECK(!info(d).ready && !protocol_error);
        }
    }
    CHECK(flash_delete(&d) == STM_OK);
    CHECK(live_allocations == 0U && invalid_frees == 0U);
    return 0;
}

// 中断中禁止初始化和访问
int test_interrupt_entry(void)
{
    OSPI_HandleTypeDef h = fixture(); flash_handle_t d = NULL;
    CHECK(create_device(&d, &h, FLASH_READ_QUAD) == STM_ERR_INVALID_CONTEXT && calls == 0U);
    uint8_t value;
    CHECK(flash_read(d, 0U, &value, 1U) == STM_ERR_INVALID_ARG && calls == 0U);
    CHECK(flash_delete(&d) == STM_OK);
    CHECK(live_allocations == 0U && invalid_frees == 0U);
    return 0;
}

// 验证对象所有权、内存不足、失败回收和配置快照
int test_handle_entry(void)
{
    OSPI_HandleTypeDef h = fixture();
    flash_handle_t d = NULL, second = NULL;
    flash_config_t config = {.hal = &h, .read_mode = FLASH_READ_QUAD};
    CHECK(flash_create(NULL, &d) == STM_ERR_INVALID_ARG && d == NULL);
    CHECK(flash_create(&config, NULL) == STM_ERR_INVALID_ARG);
    CHECK(flash_get_info(NULL, NULL) == STM_ERR_INVALID_ARG);
    allocation_failure = 1;
    CHECK(flash_create(&config, &d) == STM_ERR_NO_MEM);
    CHECK(d == NULL && live_allocations == 0U && calls == 0U);
    allocation_failure = 0;
    for (unsigned cycle = 0; cycle < 20U; ++cycle) {
        h = fixture();
        config.hal = &h;
        config.read_mode = FLASH_READ_QUAD;
        CHECK(flash_create(&config, &d) == STM_OK && d != NULL);
        CHECK(live_allocations == 1U);
        flash_handle_t saved = d;
        uint32_t before = calls;
        CHECK(flash_create(&config, &d) == STM_ERR_INVALID_STATE && d == saved);
        OSPI_HandleTypeDef alias = h;
        config.hal = &alias;
        CHECK(flash_create(&config, &second) == STM_ERR_INVALID_STATE && second == NULL);
        CHECK(calls == before && live_allocations == 1U);
        config.hal = NULL;
        config.read_mode = 0;
        CHECK(info(d).ready);
        CHECK(flash_get_info(d, NULL) == STM_ERR_INVALID_ARG);
        __disable_irq();
        stm_err_t err = flash_delete(&d);
        __enable_irq();
        CHECK(err == STM_ERR_INVALID_CONTEXT && d == saved && live_allocations == 1U);
        __set_BASEPRI(0x80U);
        err = flash_delete(&d);
        __set_BASEPRI(0U);
        CHECK(err == STM_ERR_INVALID_CONTEXT && d == saved);
        __set_FAULTMASK(1U);
        err = flash_delete(&d);
        __set_FAULTMASK(0U);
        CHECK(err == STM_ERR_INVALID_CONTEXT && d == saved);
        CHECK(flash_delete(&d) == STM_OK && d == NULL && calls == before);
        CHECK(flash_delete(&d) == STM_OK && live_allocations == 0U);
    }
    CHECK(allocation_calls > 20U && invalid_frees == 0U);
    return 0;
}

// 验证不同 OCTOSPI 实例独立占用以及非表头对象删除
int test_multiple_handles_entry(void)
{
    OSPI_HandleTypeDef h1 = fixture(), h2 = h1;
    h2.Instance = OCTOSPI2;
    flash_handle_t first = NULL, second = NULL, third = NULL;
    flash_config_t config = {.hal = &h1, .read_mode = FLASH_READ_SINGLE};
    CHECK(flash_create(&config, &first) == STM_OK);
    config.hal = &h2;
    CHECK(flash_create(&config, &second) == STM_OK && second != first);
    CHECK(live_allocations == 2U);
    CHECK(flash_delete(&first) == STM_OK && first == NULL);
    CHECK(info(second).ready && live_allocations == 1U);
    CHECK(flash_create(&config, &third) == STM_ERR_INVALID_STATE && third == NULL);
    CHECK(flash_delete(&second) == STM_OK && live_allocations == 0U);
    CHECK(flash_create(&config, &third) == STM_OK);
    CHECK(flash_delete(&third) == STM_OK && live_allocations == 0U && invalid_frees == 0U);
    return 0;
}
