/********************************** (C) COPYRIGHT *******************************
 * File Name          : Main.c
 * Author             : Steffen Beyer
 * Version            : 0.0.1
 * Date               : 2023/06/05
 * Description        : Timer capture mode interrupt demo
 *********************************************************************************/

#include <math.h>
#include "CH58x_common.h"

#define CAPTURE_SIZE 50

float targetFreq;

BOOL CapDMA;
volatile uint16_t CapPointer;
volatile BOOL CapTimeout;
volatile BOOL CapDMA_End;

// DMA buffer
__attribute__((aligned(4))) uint32_t CapBuf[CAPTURE_SIZE];

/*********************************************************************
 * @fn      CyclesPerUs
 *
 * @brief   Calculate number of clock cycles per ¦Ìs
 *
 * @return  number of cycles
 */
uint32_t CyclesPerUs(uint32_t us)
{
    return (float)FREQ_SYS / 1e6 * us;
}

/*********************************************************************
 * @fn      ClockInit
 *
 * @brief   Setup clock output
 *
 * @return  none
 */
void ClockInit(void)
{
    GPIOA_ModeCfg(GPIO_Pin_12, GPIO_ModeOut_PP_5mA);
    GPIOA_ModeCfg(GPIO_Pin_13, GPIO_ModeOut_PP_5mA);

    targetFreq = (float)FREQ_SYS / (uint8_t)(FREQ_SYS / 256000) / 256;
    PWMX_CLKCfg(FREQ_SYS / 256000); // f = ~256 kHz (error 0,16%)
    PWMX_CycleCfg(PWMX_Cycle_256); // f2 = 1 kHz, T = 1 ms
    PWMX_ACTOUT(CH_PWM4, 256 / 2, Low_Level, ENABLE);
    PWMX_ACTOUT(CH_PWM5, 256 / 2, Low_Level, ENABLE);
}

/*********************************************************************
 * @fn      DMA_Init
 *
 * @brief   Setup timer for DMA capture
 *
 * @return  none
 */
void DMA_Init(void)
{
    // PB10 instead of PA10
    GPIOPinRemap(ENABLE, RB_PIN_TMR1);
    GPIOB_ModeCfg(GPIO_Pin_10, GPIO_ModeIN_PU);

    TMR1_CapInit(Edge_To_Edge);
    TMR1_DMACfg(ENABLE, (uint16_t)(uint32_t)&CapBuf[0], (uint16_t)(uint32_t)&CapBuf[CAPTURE_SIZE], Mode_Single);
    TMR1_CAPTimeoutCfg(50 * (FREQ_SYS / 1000)); // 50 ms timeout
    TMR1_ITCfg(ENABLE, TMR1_2_IT_DMA_END | TMR0_3_IT_CYC_END); // Turn on DMA completion interrupt
}

/*********************************************************************
 * @fn      DMA_Run
 *
 * @brief   Start capture
 *
 * @return  none
 */
void DMA_Run(void)
{
    printf("\nrunning DMA capture...\n");
    CapDMA = TRUE;

    CapTimeout = FALSE;
    CapDMA_End = FALSE;
    // also resets the timer, so we get a valid first measurement
    TMR1_CapInit(Edge_To_Edge);
    TMR1_DMACfg(ENABLE, (uint16_t)(uint32_t)&CapBuf[0], (uint16_t)(uint32_t)&CapBuf[CAPTURE_SIZE], Mode_Single);
    PFIC_EnableIRQ(TMR1_IRQn);
}

/*********************************************************************
 * @fn      DMA_Stop
 *
 * @brief   Stop capture
 *
 * @return  none
 */
void DMA_Stop(void)
{
    PFIC_DisableIRQ(TMR1_IRQn);
    R8_TMR1_CTRL_DMA = 0;
}

/*********************************************************************
 * @fn      IRQ_Init
 *
 * @brief   Setup timer for IRQ capture
 *
 * @return  none
 */
void IRQ_Init(void)
{
    // PB11 instead of PA11
    GPIOPinRemap(ENABLE, RB_PIN_TMR2);
    GPIOB_ModeCfg(GPIO_Pin_11, GPIO_ModeIN_PU);

    TMR2_CapInit(Edge_To_Edge);
    TMR2_CAPTimeoutCfg(50 * (FREQ_SYS / 1000)); // 50 ms timeout
    TMR2_ITCfg(ENABLE, TMR0_3_IT_DATA_ACT | TMR0_3_IT_CYC_END); // Turn on data activity interrupt
}

/*********************************************************************
 * @fn      IRQ_Run
 *
 * @brief   Start capture
 *
 * @return  none
 */
void IRQ_Run(void)
{
    printf("\nrunning IRQ capture...\n");
    CapDMA = FALSE;

    CapPointer = 0;
    CapTimeout = FALSE;
    // also resets the timer, so we get a valid first measurement
    TMR2_CapInit(Edge_To_Edge);
    PFIC_EnableIRQ(TMR2_IRQn);
}

/*********************************************************************
 * @fn      IRQ_Stop
 *
 * @brief   Stop capture
 *
 * @return  none
 */
void IRQ_Stop(void)
{
    PFIC_DisableIRQ(TMR2_IRQn);
}

/*********************************************************************
 * @fn      Pause
 *
 * @brief   Short break for the user
 *
 * @return  none
 */
void Pause(void)
{
    printf("pausing for 5 seconds...\n");
    DelayMs(5000);
}

/*********************************************************************
 * @fn      DisplayResults
 *
 * @brief   Analyse the captured data and output
 *
 * @return  none
 */
void DisplayResults(void)
{
    uint32_t avgSum = 0;
    uint16_t avgCount = 0;
    uint16_t avgSkip = 0;

    // how many edges did we capture?
    uint16_t capSize = CapDMA ? (R16_TMR1_DMA_NOW - R16_TMR1_DMA_BEG) / sizeof(int32_t) : CapPointer;
    if (CapDMA && CapTimeout && capSize > 0) {
        // drop last value if DMA timeout
        capSize -= 1;
    }

    if (capSize > 0) {
        // display 5 usec values per line
        printf("captured edge intervals:\n");
        for (uint16_t i = 0; i < (capSize - 1) / 5 + 1; i++) {
            uint16_t start = i * 5;

            for (uint8_t j = 0; j < 5 && start + j < capSize; j++) {
                int32_t edge = CapBuf[start + j] & (1 << 25);
                int32_t cycles = CapBuf[start + j] & (1 << 25) - 1;
                float usec = cycles * 1e6 / FREQ_SYS;
                printf(edge ? "/" : "\\");
                printf("%4ld,%02d   ", (uint32_t)usec, (uint8_t)(usec * 100) % 100);

                // drop first sample for average calculatoin
                if (avgSkip > 0) {
                    avgSum += cycles;
                    avgCount += 1;
                } else {
                    avgSkip += 1;
                }
            }
            printf(" [usec]\n");
        }

        if (avgCount > 0) {
            float freq = 0.5 / ((float)avgSum / avgCount / FREQ_SYS);
            float error = fabsf(targetFreq - freq) / targetFreq * 100;
            printf("frequency (avg.): %ld,%02d Hz (error = %ld,%02d %%)\n",
                (uint32_t)freq, (uint8_t)(freq * 100) % 100,
                (uint32_t)error, (uint8_t)(error * 100) % 100);
        }
    } else {
        printf("no edges captured...\n");
    }

    if (CapTimeout) {
        printf("stopped due to timeout.\n");
    }
}

/*********************************************************************
 * @fn      DebugInit
 *
 * @brief   Setup UART as console
 *
 * @return  none
 */
void DebugInit(void)
{
    GPIOA_SetBits(GPIO_Pin_9);
    GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA);
    UART1_DefInit();
}

/*********************************************************************
 * @fn      main
 *
 * @brief   Entry point
 *
 * @return  none
 */
int main()
{
    SetSysClock(CLK_SOURCE_PLL_60MHz);
    // Setup UART1
    // PA8: RXD, PA9: TXD
    DebugInit();

    ClockInit();
    DMA_Init();
    IRQ_Init();

    printf("Start @ChipID=%02X\n", R8_CHIP_ID);
    printf("(PWM4) PA12: 1 kHz* clock output (push pull)\n");
    printf("(PWM5) PA13: 1 kHz* clock output (push pull)\n");
    printf("(TMR1) PB10: input DMA (pull up)\n");
    printf("(TMR2) PB11: input IRQ (pull up)\n");
    printf("* %ld,%02d Hz\n", (uint32_t)targetFreq, (uint8_t)(targetFreq * 100) % 100);

    while(1) {
        IRQ_Run();
        // wait for capture to finish (timeout 5s)
        for (uint16_t i = 0; i < 50 && CapPointer < CAPTURE_SIZE && !CapTimeout; i++) {
            DelayMs(100);
        }
        IRQ_Stop();
        DisplayResults();
        Pause();

        DMA_Run();
        // wait for capture to finish (timeout 5s)
        for (uint16_t i = 0; i < 50 && !CapDMA_End && !CapTimeout; i++) {
            DelayMs(100);
        }
        DMA_Stop();
        DisplayResults();
        Pause();
    }
}

/*********************************************************************
 * @fn      TMR1_IRQHandler
 *
 * @brief   TMR1 demo handler (DMA)
 *
 * @return  none
 */
__INTERRUPT
__HIGH_CODE
void TMR1_IRQHandler(void)
{
    if (TMR1_GetITFlag(TMR1_2_IT_DMA_END)) {
        TMR1_ClearITFlag(TMR1_2_IT_DMA_END);
        DMA_Stop();
        CapDMA_End = TRUE;
    } else if (TMR1_GetITFlag(TMR0_3_IT_CYC_END)) {
        TMR1_ClearITFlag(TMR0_3_IT_CYC_END);
        DMA_Stop();
        CapTimeout = TRUE;
    }
}

/*********************************************************************
 * @fn      TMR2_IRQHandler
 *
 * @brief   TMR2 demo handler (IRQ)
 *
 * @return  none
 */
__INTERRUPT
__HIGH_CODE
void TMR2_IRQHandler(void)
{
    if (TMR2_GetITFlag(TMR0_3_IT_DATA_ACT)) {
        TMR2_ClearITFlag(TMR0_3_IT_DATA_ACT);
        if (CapPointer < CAPTURE_SIZE) {
            CapBuf[CapPointer] = R32_TMR2_FIFO;
            CapPointer += 1;
        } else {
            IRQ_Stop();
        }
    } else if (TMR2_GetITFlag(TMR0_3_IT_CYC_END)) {
        TMR2_ClearITFlag(TMR0_3_IT_CYC_END);
        IRQ_Stop();
        CapTimeout = TRUE;
    }
}
