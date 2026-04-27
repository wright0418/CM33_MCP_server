#include "nualink_transport.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "NuMicro.h"
#include "task.h"
#include "nualink_config.h"

#define NUALINK_USBD_VID                  0x0416U
#define NUALINK_USBD_PID                  0xB002U

#define SET_LINE_CODE                     0x20U
#define GET_LINE_CODE                     0x21U
#define SET_CONTROL_LINE_STATE            0x22U

#define CEP_MAX_PKT_SIZE                  64U
#define CEP_OTHER_MAX_PKT_SIZE            64U
#define EPA_MAX_PKT_SIZE                  512U
#define EPA_OTHER_MAX_PKT_SIZE            64U
#define EPB_MAX_PKT_SIZE                  512U
#define EPB_OTHER_MAX_PKT_SIZE            64U
#define EPC_MAX_PKT_SIZE                  64U
#define EPC_OTHER_MAX_PKT_SIZE            64U

#define CEP_BUF_BASE                      0x000U
#define CEP_BUF_LEN                       CEP_MAX_PKT_SIZE
#define EPA_BUF_BASE                      0x200U
#define EPA_BUF_LEN                       EPA_MAX_PKT_SIZE
#define EPB_BUF_BASE                      0x400U
#define EPB_BUF_LEN                       EPB_MAX_PKT_SIZE
#define EPC_BUF_BASE                      0x600U
#define EPC_BUF_LEN                       EPC_MAX_PKT_SIZE

#define BULK_IN_EP_NUM                    0x01U
#define BULK_OUT_EP_NUM                   0x02U
#define INT_IN_EP_NUM                     0x03U

typedef struct
{
    uint32_t u32DTERate;
    uint8_t u8CharFormat;
    uint8_t u8ParityType;
    uint8_t u8DataBits;
} nualink_line_coding_t;

extern S_HSUSBD_INFO_T gsHSInfo;

static nualink_line_coding_t s_line_coding = {115200U, 0U, 0U, 8U};
static uint16_t s_control_signal = 0U;

#if defined(__ICCARM__)
#pragma data_alignment=4
static uint8_t s_usb_rx_buffer[NUALINK_USB_RX_PACKET_SIZE];
#else
static uint8_t s_usb_rx_buffer[NUALINK_USB_RX_PACKET_SIZE] __attribute__((aligned(4)));
#endif

static volatile int8_t s_bulk_out_ready = 0;
static volatile uint32_t s_rx_size = 0U;
static volatile uint32_t s_tx_busy = 0U;
static volatile uint32_t s_rx_drop_count = 0U;
static uint32_t s_bulk_in_max_packet = EPA_MAX_PKT_SIZE;
static bool s_usb_started = false;

static void prvInitForHighSpeed(void)
{
    s_bulk_in_max_packet = EPA_MAX_PKT_SIZE;

    HSUSBD_SetEpBufAddr(EPA, EPA_BUF_BASE, EPA_BUF_LEN);
    HSUSBD_SET_MAX_PAYLOAD(EPA, EPA_MAX_PKT_SIZE);
    HSUSBD_ConfigEp(EPA, BULK_IN_EP_NUM, HSUSBD_EP_CFG_TYPE_BULK, HSUSBD_EP_CFG_DIR_IN);

    HSUSBD_SetEpBufAddr(EPB, EPB_BUF_BASE, EPB_BUF_LEN);
    HSUSBD_SET_MAX_PAYLOAD(EPB, EPB_MAX_PKT_SIZE);
    HSUSBD_ConfigEp(EPB, BULK_OUT_EP_NUM, HSUSBD_EP_CFG_TYPE_BULK, HSUSBD_EP_CFG_DIR_OUT);
    HSUSBD_ENABLE_EP_INT(EPB, HSUSBD_EPINTEN_RXPKIEN_Msk | HSUSBD_EPINTEN_SHORTRXIEN_Msk);

    HSUSBD_SetEpBufAddr(EPC, EPC_BUF_BASE, EPC_BUF_LEN);
    HSUSBD_SET_MAX_PAYLOAD(EPC, EPC_MAX_PKT_SIZE);
    HSUSBD_ConfigEp(EPC, INT_IN_EP_NUM, HSUSBD_EP_CFG_TYPE_INT, HSUSBD_EP_CFG_DIR_IN);
}

static void prvInitForFullSpeed(void)
{
    s_bulk_in_max_packet = EPA_OTHER_MAX_PKT_SIZE;

    HSUSBD_SetEpBufAddr(EPA, EPA_BUF_BASE, EPA_BUF_LEN);
    HSUSBD_SET_MAX_PAYLOAD(EPA, EPA_OTHER_MAX_PKT_SIZE);
    HSUSBD_ConfigEp(EPA, BULK_IN_EP_NUM, HSUSBD_EP_CFG_TYPE_BULK, HSUSBD_EP_CFG_DIR_IN);

    HSUSBD_SetEpBufAddr(EPB, EPB_BUF_BASE, EPB_BUF_LEN);
    HSUSBD_SET_MAX_PAYLOAD(EPB, EPB_OTHER_MAX_PKT_SIZE);
    HSUSBD_ConfigEp(EPB, BULK_OUT_EP_NUM, HSUSBD_EP_CFG_TYPE_BULK, HSUSBD_EP_CFG_DIR_OUT);
    HSUSBD_ENABLE_EP_INT(EPB, HSUSBD_EPINTEN_RXPKIEN_Msk | HSUSBD_EPINTEN_SHORTRXIEN_Msk);

    HSUSBD_SetEpBufAddr(EPC, EPC_BUF_BASE, EPC_BUF_LEN);
    HSUSBD_SET_MAX_PAYLOAD(EPC, EPC_OTHER_MAX_PKT_SIZE);
    HSUSBD_ConfigEp(EPC, INT_IN_EP_NUM, HSUSBD_EP_CFG_TYPE_INT, HSUSBD_EP_CFG_DIR_IN);
}

static void prvEndpointInit(void)
{
    HSUSBD_ENABLE_USB_INT(HSUSBD_GINTEN_USBIEN_Msk |
                          HSUSBD_GINTEN_CEPIEN_Msk |
                          HSUSBD_GINTEN_EPAIEN_Msk |
                          HSUSBD_GINTEN_EPBIEN_Msk |
                          HSUSBD_GINTEN_EPCIEN_Msk);
    HSUSBD_ENABLE_BUS_INT(HSUSBD_BUSINTEN_DMADONEIEN_Msk |
                          HSUSBD_BUSINTEN_RESUMEIEN_Msk |
                          HSUSBD_BUSINTEN_RSTIEN_Msk |
                          HSUSBD_BUSINTEN_VBUSDETIEN_Msk);
    HSUSBD_SET_ADDR(0U);

    HSUSBD_SetEpBufAddr(CEP, CEP_BUF_BASE, CEP_BUF_LEN);
    HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk | HSUSBD_CEPINTEN_STSDONEIEN_Msk);

    prvInitForHighSpeed();
}

static void prvClassRequest(void)
{
    if(gUsbCmd.bmRequestType & 0x80U)
    {
        switch(gUsbCmd.bRequest)
        {
            case GET_LINE_CODE:
                if((gUsbCmd.wIndex & 0xffU) == 0U)
                {
                    HSUSBD_PrepareCtrlIn((uint8_t *)&s_line_coding, 7U);
                }
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
                break;

            default:
                HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
                break;
        }
    }
    else
    {
        switch(gUsbCmd.bRequest)
        {
            case SET_CONTROL_LINE_STATE:
                if((gUsbCmd.wIndex & 0xffU) == 0U)
                {
                    s_control_signal = gUsbCmd.wValue;
                }
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
                HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_NAKCLR);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_STSDONEIEN_Msk);
                break;

            case SET_LINE_CODE:
                if((gUsbCmd.wIndex & 0xffU) == 0U)
                {
                    HSUSBD_CtrlOut((uint8_t *)&s_line_coding, 7U);
                }
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
                HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_NAKCLR);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_STSDONEIEN_Msk);
                break;

            default:
                HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_STALLEN_Msk);
                break;
        }
    }
}

void USBD20_IRQHandler(void)
{
    __IO uint32_t irq_status_l;
    __IO uint32_t irq_status;

    irq_status_l = HSUSBD->GINTSTS & HSUSBD->GINTEN;
    if(!irq_status_l)
    {
        return;
    }

    if(irq_status_l & HSUSBD_GINTSTS_USBIF_Msk)
    {
        irq_status = HSUSBD->BUSINTSTS & HSUSBD->BUSINTEN;

        if(irq_status & HSUSBD_BUSINTSTS_SOFIF_Msk)
        {
            HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_SOFIF_Msk);
        }

        if(irq_status & HSUSBD_BUSINTSTS_RSTIF_Msk)
        {
            HSUSBD_SwReset();
            HSUSBD_ResetDMA();
            HSUSBD->EP[EPA].EPRSPCTL = HSUSBD_EPRSPCTL_FLUSH_Msk;
            HSUSBD->EP[EPB].EPRSPCTL = HSUSBD_EPRSPCTL_FLUSH_Msk;
            s_tx_busy = 0U;
            s_bulk_out_ready = 0;

            if(HSUSBD->OPER & 0x04U)
            {
                prvInitForHighSpeed();
            }
            else
            {
                prvInitForFullSpeed();
            }

            HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk);
            HSUSBD_SET_ADDR(0U);
            HSUSBD_ENABLE_BUS_INT(HSUSBD_BUSINTEN_RSTIEN_Msk |
                                  HSUSBD_BUSINTEN_RESUMEIEN_Msk |
                                  HSUSBD_BUSINTEN_SUSPENDIEN_Msk |
                                  HSUSBD_BUSINTEN_VBUSDETIEN_Msk);
            HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_RSTIF_Msk);
            HSUSBD_CLR_CEP_INT_FLAG(0x1ffcU);
        }

        if(irq_status & HSUSBD_BUSINTSTS_RESUMEIF_Msk)
        {
            HSUSBD_ENABLE_BUS_INT(HSUSBD_BUSINTEN_RSTIEN_Msk |
                                  HSUSBD_BUSINTEN_SUSPENDIEN_Msk |
                                  HSUSBD_BUSINTEN_VBUSDETIEN_Msk);
            HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_RESUMEIF_Msk);
        }

        if(irq_status & HSUSBD_BUSINTSTS_SUSPENDIF_Msk)
        {
            HSUSBD_ENABLE_BUS_INT(HSUSBD_BUSINTEN_RSTIEN_Msk |
                                  HSUSBD_BUSINTEN_RESUMEIEN_Msk |
                                  HSUSBD_BUSINTEN_VBUSDETIEN_Msk);
            HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_SUSPENDIF_Msk);
        }

        if(irq_status & HSUSBD_BUSINTSTS_HISPDIF_Msk)
        {
            HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk);
            HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_HISPDIF_Msk);
        }

        if(irq_status & HSUSBD_BUSINTSTS_DMADONEIF_Msk)
        {
            g_hsusbd_DmaDone = 1U;
            HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_DMADONEIF_Msk);

            if(!(HSUSBD->DMACTL & HSUSBD_DMACTL_DMARD_Msk))
            {
                HSUSBD_ENABLE_EP_INT(EPB, HSUSBD_EPINTEN_RXPKIEN_Msk);
            }

            if(HSUSBD->DMACTL & HSUSBD_DMACTL_DMARD_Msk)
            {
                if(g_hsusbd_ShortPacket == 1U)
                {
                    HSUSBD->EP[EPA].EPRSPCTL = (HSUSBD->EP[EPA].EPRSPCTL & 0x10U) | HSUSBD_EP_RSPCTL_SHORTTXEN;
                    g_hsusbd_ShortPacket = 0U;
                }
            }
        }

        if(irq_status & HSUSBD_BUSINTSTS_PHYCLKVLDIF_Msk)
        {
            HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_PHYCLKVLDIF_Msk);
        }

        if(irq_status & HSUSBD_BUSINTSTS_VBUSDETIF_Msk)
        {
            if(HSUSBD_IS_ATTACHED())
            {
                HSUSBD_ENABLE_USB();
                HSUSBD_ENABLE_HS_HANDSHAKE();
            }
            else
            {
                HSUSBD_DISABLE_USB();
                HSUSBD_DISABLE_HS_HANDSHAKE();
                s_usb_started = false;
            }
            HSUSBD_CLR_BUS_INT_FLAG(HSUSBD_BUSINTSTS_VBUSDETIF_Msk);
        }
    }

    if(irq_status_l & HSUSBD_GINTSTS_CEPIF_Msk)
    {
        irq_status = HSUSBD->CEPINTSTS & HSUSBD->CEPINTEN;

        if(irq_status & HSUSBD_CEPINTSTS_SETUPTKIF_Msk)
        {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_SETUPTKIF_Msk);
            return;
        }

        if(irq_status & HSUSBD_CEPINTSTS_SETUPPKIF_Msk)
        {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_SETUPPKIF_Msk);
            HSUSBD_ProcessSetupPacket();
            return;
        }

        if(irq_status & HSUSBD_CEPINTSTS_OUTTKIF_Msk)
        {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_OUTTKIF_Msk);
            HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_STSDONEIEN_Msk);
            return;
        }

        if(irq_status & HSUSBD_CEPINTSTS_INTKIF_Msk)
        {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
            if(!(irq_status & HSUSBD_CEPINTSTS_STSDONEIF_Msk))
            {
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_TXPKIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_TXPKIEN_Msk);
                HSUSBD_CtrlIn();
            }
            else
            {
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_TXPKIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_TXPKIEN_Msk | HSUSBD_CEPINTEN_STSDONEIEN_Msk);
            }
            return;
        }

        if(irq_status & HSUSBD_CEPINTSTS_PINGIF_Msk)
        {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_PINGIF_Msk);
            return;
        }

        if(irq_status & HSUSBD_CEPINTSTS_TXPKIF_Msk)
        {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
            HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_NAKCLR);
            if(g_hsusbd_CtrlInSize)
            {
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_INTKIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_INTKIEN_Msk);
            }
            else
            {
                if(g_hsusbd_CtrlZero == 1U)
                {
                    HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_ZEROLEN);
                }
                HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
                HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk | HSUSBD_CEPINTEN_STSDONEIEN_Msk);
            }
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_TXPKIF_Msk);
            return;
        }

        if(irq_status & HSUSBD_CEPINTSTS_RXPKIF_Msk)
        {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_RXPKIF_Msk);
            HSUSBD_SET_CEP_STATE(HSUSBD_CEPCTL_NAKCLR);
            HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk | HSUSBD_CEPINTEN_STSDONEIEN_Msk);
            return;
        }

        if(irq_status & HSUSBD_CEPINTSTS_NAKIF_Msk)
        {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_NAKIF_Msk);
            return;
        }

        if(irq_status & HSUSBD_CEPINTSTS_STALLIF_Msk)
        {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STALLIF_Msk);
            return;
        }

        if(irq_status & HSUSBD_CEPINTSTS_ERRIF_Msk)
        {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_ERRIF_Msk);
            return;
        }

        if(irq_status & HSUSBD_CEPINTSTS_STSDONEIF_Msk)
        {
            HSUSBD_UpdateDeviceState();
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_STSDONEIF_Msk);
            HSUSBD_ENABLE_CEP_INT(HSUSBD_CEPINTEN_SETUPPKIEN_Msk);
            return;
        }

        if(irq_status & HSUSBD_CEPINTSTS_BUFFULLIF_Msk)
        {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_BUFFULLIF_Msk);
            return;
        }

        if(irq_status & HSUSBD_CEPINTSTS_BUFEMPTYIF_Msk)
        {
            HSUSBD_CLR_CEP_INT_FLAG(HSUSBD_CEPINTSTS_BUFEMPTYIF_Msk);
            return;
        }
    }

    if(irq_status_l & HSUSBD_GINTSTS_EPAIF_Msk)
    {
        irq_status = HSUSBD->EP[EPA].EPINTSTS & HSUSBD->EP[EPA].EPINTEN;
        s_tx_busy = 0U;
        HSUSBD_ENABLE_EP_INT(EPA, 0U);
        HSUSBD_CLR_EP_INT_FLAG(EPA, irq_status);
    }

    if(irq_status_l & HSUSBD_GINTSTS_EPBIF_Msk)
    {
        uint32_t index;

        irq_status = HSUSBD->EP[EPB].EPINTSTS & HSUSBD->EP[EPB].EPINTEN;
        s_rx_size = HSUSBD->EP[EPB].EPDATCNT & 0xffffU;

        if((s_bulk_out_ready == 0) && (s_rx_size <= sizeof(s_usb_rx_buffer)))
        {
            for(index = 0U; index < s_rx_size; index++)
            {
                s_usb_rx_buffer[index] = HSUSBD->EP[EPB].EPDAT_BYTE;
            }
            s_bulk_out_ready = 1;
            HSUSBD_ENABLE_EP_INT(EPB, 0U);
        }
        else
        {
            for(index = 0U; index < s_rx_size; index++)
            {
                (void)HSUSBD->EP[EPB].EPDAT_BYTE;
            }
            s_rx_size = 0U;
            s_rx_drop_count++;
            HSUSBD_ENABLE_EP_INT(EPB, HSUSBD_EPINTEN_RXPKIEN_Msk | HSUSBD_EPINTEN_SHORTRXIEN_Msk);
        }

        HSUSBD_CLR_EP_INT_FLAG(EPB, irq_status);
    }

    if(irq_status_l & HSUSBD_GINTSTS_EPCIF_Msk)
    {
        irq_status = HSUSBD->EP[EPC].EPINTSTS & HSUSBD->EP[EPC].EPINTEN;
        HSUSBD_CLR_EP_INT_FLAG(EPC, irq_status);
    }
}

void NuAILink_TransportInit(void)
{
    HSUSBD_Open(&gsHSInfo, prvClassRequest, NULL);
    prvEndpointInit();
    NVIC_SetPriority(USBD20_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
    NVIC_EnableIRQ(USBD20_IRQn);
}

void NuAILink_TransportPoll(void)
{
    if(HSUSBD_IS_ATTACHED())
    {
        if(!s_usb_started)
        {
            HSUSBD_Start();
            s_usb_started = true;
        }
    }
    else
    {
        s_usb_started = false;
    }
}

bool NuAILink_TransportIsAttached(void)
{
    return HSUSBD_IS_ATTACHED() ? true : false;
}

uint32_t NuAILink_TransportReadPacket(uint8_t *buffer, uint32_t max_length)
{
    uint32_t length = 0U;

    if((buffer == NULL) || (max_length == 0U))
    {
        return 0U;
    }

    if(s_bulk_out_ready)
    {
        NVIC_DisableIRQ(USBD20_IRQn);
        length = s_rx_size;
        if(length > max_length)
        {
            length = max_length;
            s_rx_drop_count++;
        }
        memcpy(buffer, s_usb_rx_buffer, length);
        s_rx_size = 0U;
        s_bulk_out_ready = 0;
        HSUSBD_ENABLE_EP_INT(EPB, HSUSBD_EPINTEN_RXPKIEN_Msk | HSUSBD_EPINTEN_SHORTRXIEN_Msk);
        NVIC_EnableIRQ(USBD20_IRQn);
    }

    return length;
}

int32_t NuAILink_TransportWrite(const uint8_t *data, uint32_t length, TickType_t timeout_ticks)
{
    uint32_t offset = 0U;
    TickType_t start_tick;

    if((data == NULL) || (length == 0U))
    {
        return 0;
    }

    if(!NuAILink_TransportIsAttached())
    {
        return -1;
    }

    start_tick = xTaskGetTickCount();

    while(offset < length)
    {
        uint32_t chunk_length;
        uint32_t index;

        while(s_tx_busy)
        {
            if((xTaskGetTickCount() - start_tick) > timeout_ticks)
            {
                return -2;
            }
            vTaskDelay(pdMS_TO_TICKS(1U));
        }

        chunk_length = length - offset;
        if(chunk_length > s_bulk_in_max_packet)
        {
            chunk_length = s_bulk_in_max_packet;
        }

        for(index = 0U; index < chunk_length; index++)
        {
            HSUSBD->EP[EPA].EPDAT_BYTE = data[offset + index];
        }

        s_tx_busy = 1U;
        /* Clear any stale IN-token flag before enabling the IRQ; otherwise a
         * pending INTKIF (left over from a host IN that was NAK'd while we
         * were preparing this chunk) would fire the IRQ immediately, clear
         * s_tx_busy prematurely, and let the next chunk overwrite the FIFO
         * before the current chunk has actually been transmitted. */
        HSUSBD_CLR_EP_INT_FLAG(EPA, HSUSBD_EPINTSTS_INTKIF_Msk);
        /* Only request "transmit short packet immediately" on the final
         * (possibly short) chunk — setting it on full-size packets makes the
         * controller commit transfers before we have finished filling the
         * FIFO when responses span multiple max-size packets. */
        if ((offset + chunk_length) >= length)
        {
            HSUSBD->EP[EPA].EPRSPCTL = HSUSBD_EP_RSPCTL_SHORTTXEN;
        }
        HSUSBD->EP[EPA].EPTXCNT = chunk_length;
        HSUSBD_ENABLE_EP_INT(EPA, HSUSBD_EPINTEN_INTKIEN_Msk);
        offset += chunk_length;
    }

    return (int32_t)length;
}

uint32_t NuAILink_TransportGetRxDropCount(void)
{
    return s_rx_drop_count;
}