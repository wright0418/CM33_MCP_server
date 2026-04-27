#include "nualink_board.h"

#include <stdio.h>

#include "NuMicro.h"
#include "FreeRTOS.h"
#include "task.h"
#include "nualink_log.h"
#include "nualink_tasks.h"

static volatile bool s_led_on = false;
static volatile bool s_heartbeat_enabled = true;
static volatile bool s_led_bpwm_active = false;
static volatile uint32_t s_led_bpwm_duty_percent = 0U;
static bool s_led_bpwm_clock_ready = false;
static bool s_eadc_clock_ready = false;
static bool s_eadc_pin_ready = false;
static bool s_eadc_opened = false;

#define NUALINK_LED_BPWM_MODULE BPWM2_MODULE
#define NUALINK_LED_BPWM BPWM2
#define NUALINK_LED_BPWM_CHANNEL 0U
#define NUALINK_LED_BPWM_CHANNEL_MASK BPWM_CH_0_MASK
#define NUALINK_LED_BPWM_FREQ_HZ 2000U

#define NUALINK_EADC_MODULE EADC0_MODULE
#define NUALINK_EADC_SAMPLE_MODULE 0U
#define NUALINK_EADC_VREF_MV 3300U
#define NUALINK_EADC_MAX_CODE 4095U

/* Phase 2.1: PB14 button state + debounce. */
#define NUALINK_BUTTON_DEBOUNCE_MS 30U
static volatile uint32_t s_button_last_event_tick = 0U;

static const char s_button_pressed_json[] =
    "{\"jsonrpc\":\"2.0\",\"method\":\"button.event\",\"params\":{\"pin\":\"PB14\",\"pressed\":true}}\n";
static const char s_button_released_json[] =
    "{\"jsonrpc\":\"2.0\",\"method\":\"button.event\",\"params\":{\"pin\":\"PB14\",\"pressed\":false}}\n";

static void prvSetLedPin(bool on)
{
    PC14 = on ? 0U : 1U;
    s_led_on = on;
}

static void prvSwitchPc14ToGpioMode(void)
{
    SYS_UnlockReg();
    SET_GPIO_PC14();
    SYS_LockReg();
    GPIO_SetMode(PC, BIT14, GPIO_MODE_OUTPUT);
}

static void prvSwitchPc14ToBpwmMode(void)
{
    SYS_UnlockReg();
    SET_BPWM2_CH0_PC14();
    SYS_LockReg();
}

static void prvEnsureLedBpwmClockReady(void)
{
    if (!s_led_bpwm_clock_ready)
    {
        CLK_SetModuleClock(NUALINK_LED_BPWM_MODULE, CLK_CLKSEL2_BPWM2SEL_PCLK0, 0U);
        CLK_EnableModuleClock(NUALINK_LED_BPWM_MODULE);
        s_led_bpwm_clock_ready = true;
    }
}

static void prvEnsureEadcClockReady(void)
{
    if (!s_eadc_clock_ready)
    {
        CLK_SetModuleClock(NUALINK_EADC_MODULE,
                           CLK_CLKSEL0_EADC0SEL_PLL_DIV2,
                           CLK_CLKDIV0_EADC0(12));
        CLK_EnableModuleClock(NUALINK_EADC_MODULE);
        s_eadc_clock_ready = true;
    }
}

static void prvEnsureEadcPinReady(void)
{
    if (!s_eadc_pin_ready)
    {
        SYS_UnlockReg();
        CLK->AHBCLK0 |= CLK_AHBCLK0_GPBCKEN_Msk;
        SET_EADC0_CH8_PB8();
        SET_EADC0_CH9_PB9();
        SYS_LockReg();

        /* Analog input pins should disable digital path to reduce leakage. */
        GPIO_DISABLE_DIGITAL_PATH(PB, BIT8 | BIT9);
        s_eadc_pin_ready = true;
    }
}

static bool prvEnsureEadcReady(void)
{
    int32_t status;

    prvEnsureEadcClockReady();
    prvEnsureEadcPinReady();

    if (!s_eadc_opened)
    {
        status = EADC_Open(EADC0, EADC_CTL_DIFFEN_SINGLE_END);
        if (status != 0)
        {
            return false;
        }
        EADC_CLR_INT_FLAG(EADC0, EADC_STATUS2_ADIF0_Msk);
        s_eadc_opened = true;
    }

    return true;
}

#if (NUALINK_ENABLE_BOOT_DIAGNOSTICS == 1)
/* Diagnostic helpers (only used by NuAILink_BoardInit). */
static void prvDiagDelay(volatile uint32_t loops)
{
    while (loops--)
    {
        __NOP();
    }
}

static void prvDiagBlink(uint32_t count)
{
    uint32_t i;
    for (i = 0U; i < count; i++)
    {
        PC14 = 0U;
        prvDiagDelay(150000U);
        PC14 = 1U;
        prvDiagDelay(150000U);
    }
    prvDiagDelay(800000U);
}

#define NUALINK_BOOT_BLINK(count) prvDiagBlink((count))
#else
#define NUALINK_BOOT_BLINK(count) ((void)(count))
#endif

/*
 * Bring up UART0 on PB12/PB13 using HIRC only, so printf is available
 * BEFORE we touch HXT, PLL, USBPHY etc.  After a chip reset HIRC is the
 * default HCLK source (12 MHz), so this works without any external clock.
 */
static void prvEarlyUartInit(void)
{
    /* HIRC is enabled by default after reset, but be explicit. */
    CLK->PWRCTL |= CLK_PWRCTL_HIRCEN_Msk;
    while ((CLK->STATUS & CLK_STATUS_HIRCSTB_Msk) == 0U)
    {
        /* HIRC must be ready before we can do anything; if not, just spin
         * here forever — the chip is in an unusable state. */
    }

    /* GPIO B clock for UART0 pins. */
    CLK->AHBCLK0 |= CLK_AHBCLK0_GPBCKEN_Msk | CLK_AHBCLK0_GPCCKEN_Msk;

    /* UART0 module clock = HIRC, divider = 1. */
    CLK->CLKSEL1 = (CLK->CLKSEL1 & ~CLK_CLKSEL1_UART0SEL_Msk) | CLK_CLKSEL1_UART0SEL_HIRC;
    CLK->CLKDIV0 = (CLK->CLKDIV0 & ~CLK_CLKDIV0_UART0DIV_Msk) | CLK_CLKDIV0_UART0(1);
    CLK_EnableModuleClock(UART0_MODULE);

    /* Pin mux for UART0 RXD/TXD. */
    SET_UART0_RXD_PB12();
    SET_UART0_TXD_PB13();

    /* Open UART0 at 115200-8n1 using HIRC (12 MHz) as input clock. */
    UART_Open(UART0, 115200);
}

void NuAILink_BoardInit(void)
{
    volatile uint32_t delay_count;
    uint32_t status;

    SYS_UnlockReg();

    /* Configure heartbeat LED first so we can flash it at every checkpoint. */
    CLK->AHBCLK0 |= CLK_AHBCLK0_GPCCKEN_Msk;
    SET_GPIO_PC14();
    GPIO_SetMode(PC, BIT14, GPIO_MODE_OUTPUT);
    prvSetLedPin(false);

    /* Bring up UART0 with HIRC only so printf works for all subsequent steps. */
    prvEarlyUartInit();
    NUALINK_BOOT_LOG("\r\n[BoardInit] step 1: UART0 up on HIRC (12 MHz)\r\n");
    NUALINK_BOOT_BLINK(1U);

    /* Step 2: enable HIRC + HXT and wait for both. */
    CLK_EnableXtalRC(CLK_PWRCTL_HIRCEN_Msk | CLK_PWRCTL_HXTEN_Msk);
    status = CLK_WaitClockReady(CLK_STATUS_HIRCSTB_Msk);
    NUALINK_BOOT_LOG("[BoardInit] step 2a: HIRCSTB ready=%lu\r\n", (unsigned long)status);
    status = CLK_WaitClockReady(CLK_STATUS_HXTSTB_Msk);
    NUALINK_BOOT_LOG("[BoardInit] step 2b: HXTSTB  ready=%lu (0=NO HXT crystal!)\r\n",
                     (unsigned long)status);
    NUALINK_BOOT_BLINK(2U);

    /* Step 3: PCLK div + core clock = 180 MHz. */
    CLK->PCLKDIV = (CLK_PCLKDIV_APB0DIV_DIV2 | CLK_PCLKDIV_APB1DIV_DIV2);
    NUALINK_BOOT_LOG("[BoardInit] step 3a: PCLKDIV set, calling CLK_SetCoreClock(180MHz)...\r\n");
    {
        uint32_t actual = CLK_SetCoreClock(FREQ_180MHZ);
        SystemCoreClockUpdate();
        NUALINK_BOOT_LOG("[BoardInit] step 3b: CoreClock returned=%lu Hz, SystemCoreClock=%lu Hz\r\n",
                         (unsigned long)actual, (unsigned long)SystemCoreClock);
    }
    /* UART baud generator depends on PCLK; reopen at the new clock. */
    UART_Close(UART0);
    UART_Open(UART0, 115200);
    NUALINK_BOOT_LOG("[BoardInit] step 3c: UART0 reopened at new HCLK\r\n");
    NUALINK_BOOT_BLINK(3U);

    /* Step 4: enable all GPIO and TMR0 clocks. */
    CLK->AHBCLK0 |= CLK_AHBCLK0_GPACKEN_Msk | CLK_AHBCLK0_GPBCKEN_Msk | CLK_AHBCLK0_GPCCKEN_Msk | CLK_AHBCLK0_GPDCKEN_Msk |
                    CLK_AHBCLK0_GPECKEN_Msk | CLK_AHBCLK0_GPFCKEN_Msk | CLK_AHBCLK0_GPGCKEN_Msk | CLK_AHBCLK0_GPHCKEN_Msk;
    CLK_SetModuleClock(TMR0_MODULE, CLK_CLKSEL1_TMR0SEL_HIRC, 0);
    CLK_EnableModuleClock(TMR0_MODULE);
    NUALINK_BOOT_LOG("[BoardInit] step 4: GPIO + TMR0 clocks enabled\r\n");
    NUALINK_BOOT_BLINK(4U);

    /* Step 5: USB PHY power-up sequence. */
    NUALINK_BOOT_LOG("[BoardInit] step 5a: USBPHY initial = 0x%08lX\r\n",
                     (unsigned long)SYS->USBPHY);
    SYS->USBPHY &= ~SYS_USBPHY_HSUSBROLE_Msk;
    SYS->USBPHY = (SYS->USBPHY & ~(SYS_USBPHY_HSUSBROLE_Msk | SYS_USBPHY_HSUSBACT_Msk)) | SYS_USBPHY_HSUSBEN_Msk;
    for (delay_count = 0U; delay_count < 0x1000U; delay_count++)
    {
        __NOP();
    }
    SYS->USBPHY |= SYS_USBPHY_HSUSBACT_Msk;
    CLK_EnableModuleClock(HSUSBD_MODULE);
    NUALINK_BOOT_LOG("[BoardInit] step 5b: USBPHY final   = 0x%08lX  (HSUSBEN+HSUSBACT)\r\n",
                     (unsigned long)SYS->USBPHY);
    NUALINK_BOOT_BLINK(5U);

    SYS_LockReg();
    NUALINK_BOOT_LOG("[BoardInit] DONE — clock=%lu Hz, exit BoardInit\r\n",
                     (unsigned long)SystemCoreClock);
}

void NuAILink_BoardSetLed(bool on)
{
    s_heartbeat_enabled = false;
    NuAILink_BoardLedUseGpio();
    prvSetLedPin(on);
    s_led_bpwm_duty_percent = on ? 100U : 0U;
}

bool NuAILink_BoardLedBpwmSet(uint32_t duty_percent)
{
    uint32_t bpwm_duty_percent;

    if (duty_percent > 100U)
    {
        return false;
    }

    s_heartbeat_enabled = false;
    prvEnsureLedBpwmClockReady();
    prvSwitchPc14ToBpwmMode();

    /* PC14 LED is active-low; convert user brightness to BPWM high-duty. */
    bpwm_duty_percent = 100U - duty_percent;

    (void)BPWM_ConfigOutputChannel(NUALINK_LED_BPWM,
                                   NUALINK_LED_BPWM_CHANNEL,
                                   NUALINK_LED_BPWM_FREQ_HZ,
                                   bpwm_duty_percent);
    BPWM_EnableOutput(NUALINK_LED_BPWM, NUALINK_LED_BPWM_CHANNEL_MASK);
    BPWM_Start(NUALINK_LED_BPWM, NUALINK_LED_BPWM_CHANNEL_MASK);

    s_led_bpwm_active = true;
    s_led_bpwm_duty_percent = duty_percent;
    s_led_on = (duty_percent > 0U);
    return true;
}

void NuAILink_BoardLedUseGpio(void)
{
    if (s_led_bpwm_active)
    {
        BPWM_Stop(NUALINK_LED_BPWM, NUALINK_LED_BPWM_CHANNEL_MASK);
        BPWM_DisableOutput(NUALINK_LED_BPWM, NUALINK_LED_BPWM_CHANNEL_MASK);
    }

    prvSwitchPc14ToGpioMode();
    s_led_bpwm_active = false;
}

void NuAILink_BoardEnableHeartbeat(bool enabled)
{
    s_heartbeat_enabled = enabled;
}

void NuAILink_BoardHeartbeatToggle(void)
{
    if (s_heartbeat_enabled)
    {
        prvSetLedPin(!s_led_on);
    }
}

bool NuAILink_BoardIsLedOn(void)
{
    return s_led_on;
}

bool NuAILink_BoardIsLedBpwmActive(void)
{
    return s_led_bpwm_active;
}

uint32_t NuAILink_BoardGetLedBpwmDutyPercent(void)
{
    return s_led_bpwm_duty_percent;
}

uint32_t NuAILink_BoardGetCoreClockHz(void)
{
    return SystemCoreClock;
}

bool NuAILink_BoardEadcRead(uint32_t channel, uint32_t *out_raw, uint32_t *out_millivolt)
{
    uint32_t timeout;
    uint32_t raw;

    if ((out_raw == NULL) || (out_millivolt == NULL))
    {
        return false;
    }
    if ((channel != 8U) && (channel != 9U))
    {
        return false;
    }
    if (!prvEnsureEadcReady())
    {
        return false;
    }

    EADC_ConfigSampleModule(EADC0,
                            NUALINK_EADC_SAMPLE_MODULE,
                            EADC_SOFTWARE_TRIGGER,
                            channel);
    EADC_CLR_INT_FLAG(EADC0, EADC_STATUS2_ADIF0_Msk);
    EADC_START_CONV(EADC0, BIT0);

    timeout = SystemCoreClock / 100U; /* ~10 ms timeout at current HCLK. */
    while (EADC_GET_DATA_VALID_FLAG(EADC0, BIT0) == 0U)
    {
        if (timeout == 0U)
        {
            return false;
        }
        timeout--;
    }

    raw = EADC_GET_CONV_DATA(EADC0, NUALINK_EADC_SAMPLE_MODULE) & 0xFFFU;
    *out_raw = raw;
    *out_millivolt = ((raw * NUALINK_EADC_VREF_MV) + (NUALINK_EADC_MAX_CODE / 2U)) / NUALINK_EADC_MAX_CODE;
    return true;
}

/*-----------------------------------------------------------*/
/* Phase 2.1: PB14 button + generic GPIO helpers.            */
/*-----------------------------------------------------------*/

static GPIO_T *prvGpioPortFromLetter(char port_letter)
{
    switch (port_letter)
    {
    case 'A':
    case 'a':
        return PA;
    case 'B':
    case 'b':
        return PB;
    case 'C':
    case 'c':
        return PC;
    case 'D':
    case 'd':
        return PD;
    case 'E':
    case 'e':
        return PE;
    case 'F':
    case 'f':
        return PF;
    case 'G':
    case 'g':
        return PG;
    case 'H':
    case 'h':
        return PH;
    default:
        return NULL;
    }
}

void NuAILink_BoardButtonInit(void)
{
    SYS_UnlockReg();

    /* GPIOB clock should already be on, but be defensive. */
    CLK->AHBCLK0 |= CLK_AHBCLK0_GPBCKEN_Msk;

    /* Force PB14 MFP back to GPIO function. */
    SYS->GPB_MFP3 &= ~SYS_GPB_MFP3_PB14MFP_Msk;

    /* Active-low button with internal pull-up. */
    GPIO_SetPullCtl(PB, BIT14, GPIO_PUSEL_PULL_UP);
    GPIO_SetMode(PB, BIT14, GPIO_MODE_INPUT);

    /* Detect press (falling) and release (rising). */
    GPIO_EnableInt(PB, 14, GPIO_INT_BOTH_EDGE);
    GPIO_CLR_INT_FLAG(PB, BIT14);

    SYS_LockReg();

    /* Priority must be numerically >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5)
     * to legally call FromISR FreeRTOS APIs.  Use 6 (low priority but still preempts
     * tasks). */
    NVIC_SetPriority(GPB_IRQn, 6);
    NVIC_EnableIRQ(GPB_IRQn);
}

bool NuAILink_BoardButtonIsPressed(void)
{
    /* Active-low: pin reads 0 when pressed. */
    return (PB14 == 0U);
}

bool NuAILink_BoardGpioRead(char port_letter, uint32_t pin, uint32_t *out_value)
{
    GPIO_T *port = prvGpioPortFromLetter(port_letter);

    if ((port == NULL) || (pin > 15U) || (out_value == NULL))
    {
        return false;
    }
    *out_value = (port->PIN >> pin) & 0x1U;
    return true;
}

bool NuAILink_BoardGpioWrite(char port_letter, uint32_t pin, uint32_t value)
{
    GPIO_T *port = prvGpioPortFromLetter(port_letter);

    if ((port == NULL) || (pin > 15U))
    {
        return false;
    }

    /* Forbid pins owned by NuAILink core (UART0 console + button input).
     * PC14 LED is intentionally writable through gpio.write so a host can
     * still poke it directly when not driven by BPWM. */
    if (port_letter == 'B' || port_letter == 'b')
    {
        if ((pin == 12U) || (pin == 13U) || (pin == 14U))
        {
            return false;
        }
    }

    /* If host drives PC14 as plain GPIO while LED BPWM mode is active,
     * transparently hand ownership back to GPIO first. */
    if (((port_letter == 'C') || (port_letter == 'c')) && (pin == 14U))
    {
        NuAILink_BoardLedUseGpio();
        s_led_bpwm_duty_percent = value ? 100U : 0U;
    }

    GPIO_SetMode(port, (1UL << pin), GPIO_MODE_OUTPUT);
    if (value)
    {
        port->DOUT |= (1UL << pin);
    }
    else
    {
        port->DOUT &= ~(1UL << pin);
    }
    return true;
}

void GPB_IRQHandler(void)
{
    if (GPIO_GET_INT_FLAG(PB, BIT14))
    {
        BaseType_t higher_woken = pdFALSE;
        TickType_t now;

        GPIO_CLR_INT_FLAG(PB, BIT14);

        now = xTaskGetTickCountFromISR();
        if ((now - s_button_last_event_tick) >= pdMS_TO_TICKS(NUALINK_BUTTON_DEBOUNCE_MS))
        {
            bool pressed = (PB14 == 0U);

            s_button_last_event_tick = now;
            (void)NuAILink_TasksPushNotificationFromISR(
                pressed ? s_button_pressed_json : s_button_released_json,
                &higher_woken);
        }

        portYIELD_FROM_ISR(higher_woken);
    }
}