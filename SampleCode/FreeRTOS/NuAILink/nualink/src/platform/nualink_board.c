#include "nualink_board.h"

#include "NuMicro.h"

static volatile bool s_led_on = false;
static volatile bool s_heartbeat_enabled = true;

static void prvSetLedPin(bool on)
{
    PC14 = on ? 0U : 1U;
    s_led_on = on;
}

void NuAILink_BoardInit(void)
{
    volatile uint32_t delay_count;

    SYS_UnlockReg();

    CLK_EnableXtalRC(CLK_PWRCTL_HIRCEN_Msk | CLK_PWRCTL_HXTEN_Msk);
    CLK_WaitClockReady(CLK_STATUS_HIRCSTB_Msk | CLK_STATUS_HXTSTB_Msk);

    CLK->PCLKDIV = (CLK_PCLKDIV_APB0DIV_DIV2 | CLK_PCLKDIV_APB1DIV_DIV2);
    CLK_SetCoreClock(FREQ_180MHZ);

    CLK->AHBCLK0 |= CLK_AHBCLK0_GPACKEN_Msk | CLK_AHBCLK0_GPBCKEN_Msk | CLK_AHBCLK0_GPCCKEN_Msk | CLK_AHBCLK0_GPDCKEN_Msk |
                    CLK_AHBCLK0_GPECKEN_Msk | CLK_AHBCLK0_GPFCKEN_Msk | CLK_AHBCLK0_GPGCKEN_Msk | CLK_AHBCLK0_GPHCKEN_Msk;

    CLK_SetModuleClock(UART0_MODULE, CLK_CLKSEL1_UART0SEL_HIRC, CLK_CLKDIV0_UART0(1));
    CLK_SetModuleClock(TMR0_MODULE, CLK_CLKSEL1_TMR0SEL_HIRC, 0);

    CLK_EnableModuleClock(UART0_MODULE);
    CLK_EnableModuleClock(TMR0_MODULE);

    SYS->USBPHY &= ~SYS_USBPHY_HSUSBROLE_Msk;
    SYS->USBPHY = (SYS->USBPHY & ~(SYS_USBPHY_HSUSBROLE_Msk | SYS_USBPHY_HSUSBACT_Msk)) | SYS_USBPHY_HSUSBEN_Msk;
    for(delay_count = 0U; delay_count < 0x1000U; delay_count++)
    {
        __NOP();
    }
    SYS->USBPHY |= SYS_USBPHY_HSUSBACT_Msk;
    CLK_EnableModuleClock(HSUSBD_MODULE);

    SET_UART0_RXD_PB12();
    SET_UART0_TXD_PB13();

    GPIO_SetMode(PC, BIT14, GPIO_MODE_OUTPUT);
    prvSetLedPin(false);

    SYS_LockReg();

    UART_Open(UART0, 115200);
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
    if(s_heartbeat_enabled)
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