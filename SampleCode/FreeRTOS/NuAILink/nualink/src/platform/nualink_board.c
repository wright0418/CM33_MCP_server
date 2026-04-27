#include "nualink_board.h"

#include <stdio.h>

#include "NuMicro.h"
#include "nualink_log.h"

static volatile bool s_led_on = false;
static volatile bool s_heartbeat_enabled = true;

static void prvSetLedPin(bool on)
{
    PC14 = on ? 0U : 1U;
    s_led_on = on;
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
    prvSetLedPin(on);
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

uint32_t NuAILink_BoardGetCoreClockHz(void)
{
    return SystemCoreClock;
}