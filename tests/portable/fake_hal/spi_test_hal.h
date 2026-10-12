#ifndef SPI_TEST_HAL_H
#define SPI_TEST_HAL_H
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
    typedef enum
    {
        HAL_OK,
        HAL_ERROR,
        HAL_BUSY,
        HAL_TIMEOUT
    } HAL_StatusTypeDef;
    typedef enum
    {
        HAL_UNLOCKED,
        HAL_LOCKED
    } HAL_LockTypeDef;
    typedef enum
    {
        HAL_SPI_STATE_RESET,
        HAL_SPI_STATE_READY,
        HAL_SPI_STATE_BUSY
    } HAL_SPI_StateTypeDef;
    typedef enum
    {
        GPIO_PIN_RESET,
        GPIO_PIN_SET
    } GPIO_PinState;
    typedef struct
    {
        uint32_t unused;
    } GPIO_TypeDef;
    typedef struct
    {
        uint32_t Mode, Direction, DataSize, CLKPolarity, CLKPhase, NSS, BaudRatePrescaler, FirstBit,
            TIMode, CRCCalculation;
    } SPI_InitTypeDef;
    typedef struct
    {
        void *Instance;
        SPI_InitTypeDef Init;
        HAL_LockTypeDef Lock;
    } SPI_HandleTypeDef;
#define SPI_MODE_MASTER 1U
#define SPI_DIRECTION_2LINES 2U
#define SPI_DATASIZE_8BIT 8U
#define SPI_POLARITY_LOW 0U
#define SPI_POLARITY_HIGH 1U
#define SPI_PHASE_1EDGE 0U
#define SPI_PHASE_2EDGE 1U
#define SPI_NSS_SOFT 1U
#define SPI_FIRSTBIT_MSB 0U
#define SPI_TIMODE_DISABLE 0U
#define SPI_CRCCALCULATION_DISABLE 0U
#define SPI_BAUDRATEPRESCALER_2 0U
#define SPI_BAUDRATEPRESCALER_4 8U
#define SPI_BAUDRATEPRESCALER_8 16U
#define SPI_BAUDRATEPRESCALER_16 24U
#define SPI_BAUDRATEPRESCALER_32 32U
#define SPI_BAUDRATEPRESCALER_64 40U
#define SPI_BAUDRATEPRESCALER_128 48U
#define SPI_BAUDRATEPRESCALER_256 56U
#define HAL_MAX_DELAY UINT32_MAX
    extern uint32_t test_ipsr, test_primask, test_basepri, test_faultmask;
#define __get_IPSR() test_ipsr
#define __get_PRIMASK() test_primask
#define __get_BASEPRI() test_basepri
#define __get_FAULTMASK() test_faultmask
    HAL_SPI_StateTypeDef HAL_SPI_GetState(SPI_HandleTypeDef *h);
    HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *h, uint8_t *p, uint16_t n,
                                       uint32_t timeout);
    HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *h, uint8_t *tx, uint8_t *rx,
                                              uint16_t n, uint32_t timeout);
    HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *h);
    void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
    uint32_t HAL_GetTick(void);
    void HAL_Delay(uint32_t n);
#ifdef __cplusplus
}
#endif
#endif
