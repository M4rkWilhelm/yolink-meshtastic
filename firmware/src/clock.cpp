// Optional system clock override (build flag YL_CLOCK_HSI16).
//
// STM32duino's acsip_s76s board runs the core at 32 MHz from the 24 MHz crystal
// through the PLL, with the PLL at its 96 MHz limit so it can also feed USB,
// which this board doesn't have. This replaces it with the internal 16 MHz
// oscillator: no crystal or PLL to start on every wake-up, and lots of timing
// margin. Under evaluation for the crash investigation (see HARDWARE.md).
#ifdef YL_CLOCK_HSI16

#include <Arduino.h>

extern "C" void SystemClock_Config(void) {
    // Range 1 (1.8 V core) and 1 wait state: the most margin the chip offers.
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
    while (__HAL_PWR_GET_FLAG(PWR_FLAG_VOS)) {}

    RCC_OscInitTypeDef osc = {};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    osc.HSIState = RCC_HSI_ON;
    osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    osc.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) Error_Handler();

    RCC_ClkInitTypeDef clk = {};
    clk.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 |
                    RCC_CLOCKTYPE_PCLK2;
    clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    clk.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clk.APB1CLKDivider = RCC_HCLK_DIV1;
    clk.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_1) != HAL_OK) Error_Handler();

    // The PLL and crystal may still be running from before (e.g. after a
    // reset by the bootloader's clock setup); switch them off.
    osc = {};
    osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    osc.HSEState = RCC_HSE_OFF;
    osc.PLL.PLLState = RCC_PLL_OFF;
    HAL_RCC_OscConfig(&osc);
}

#endif  // YL_CLOCK_HSI16
