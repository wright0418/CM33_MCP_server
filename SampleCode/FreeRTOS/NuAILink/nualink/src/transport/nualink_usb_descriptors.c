#include "NuMicro.h"

#include "nualink_transport.h"

#define NUALINK_USBD_VID                  0x0416U
#define NUALINK_USBD_PID                  0xB002U

#define CEP_MAX_PKT_SIZE                  64U
#define CEP_OTHER_MAX_PKT_SIZE            64U
#define EPA_MAX_PKT_SIZE                  512U
#define EPA_OTHER_MAX_PKT_SIZE            64U
#define EPB_MAX_PKT_SIZE                  512U
#define EPB_OTHER_MAX_PKT_SIZE            64U
#define EPC_MAX_PKT_SIZE                  64U
#define EPC_OTHER_MAX_PKT_SIZE            64U

#define BULK_IN_EP_NUM                    0x01U
#define BULK_OUT_EP_NUM                   0x02U
#define INT_IN_EP_NUM                     0x03U

uint8_t gu8DeviceDescriptor[] =
{
    LEN_DEVICE,
    DESC_DEVICE,
    0x00, 0x02,
    0x02,
    0x00,
    0x00,
    CEP_MAX_PKT_SIZE,
    NUALINK_USBD_VID & 0x00FFU,
    (NUALINK_USBD_VID & 0xFF00U) >> 8,
    NUALINK_USBD_PID & 0x00FFU,
    (NUALINK_USBD_PID & 0xFF00U) >> 8,
    0x00, 0x03,
    0x01,
    0x02,
    0x00,
    0x01
};

uint8_t gu8QualifierDescriptor[] =
{
    LEN_QUALIFIER,
    DESC_QUALIFIER,
    0x00, 0x02,
    0x00,
    0x00,
    0x00,
    CEP_OTHER_MAX_PKT_SIZE,
    0x01,
    0x00
};

uint8_t gu8ConfigDescriptor[] =
{
    LEN_CONFIG, DESC_CONFIG, 0x43, 0x00, 0x02, 0x01, 0x00, 0xC0, 0x32,
    LEN_INTERFACE, DESC_INTERFACE, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,
    0x05, 0x24, 0x00, 0x10, 0x01,
    0x05, 0x24, 0x01, 0x00, 0x01,
    0x04, 0x24, 0x02, 0x00,
    0x05, 0x24, 0x06, 0x00, 0x01,
    LEN_ENDPOINT, DESC_ENDPOINT, (EP_INPUT | INT_IN_EP_NUM), EP_INT, EPC_MAX_PKT_SIZE & 0x00FFU, (EPC_MAX_PKT_SIZE & 0xFF00U) >> 8, 0x01,
    LEN_INTERFACE, DESC_INTERFACE, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
    LEN_ENDPOINT, DESC_ENDPOINT, (EP_INPUT | BULK_IN_EP_NUM), EP_BULK, EPA_MAX_PKT_SIZE & 0x00FFU, (EPA_MAX_PKT_SIZE & 0xFF00U) >> 8, 0x00,
    LEN_ENDPOINT, DESC_ENDPOINT, (EP_OUTPUT | BULK_OUT_EP_NUM), EP_BULK, EPB_MAX_PKT_SIZE & 0x00FFU, (EPB_MAX_PKT_SIZE & 0xFF00U) >> 8, 0x00
};

uint8_t gu8OtherConfigDescriptorHS[] =
{
    LEN_CONFIG, DESC_OTHERSPEED, 0x43, 0x00, 0x02, 0x01, 0x00, 0xC0, 0x32,
    LEN_INTERFACE, DESC_INTERFACE, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,
    0x05, 0x24, 0x00, 0x10, 0x01,
    0x05, 0x24, 0x01, 0x00, 0x01,
    0x04, 0x24, 0x02, 0x00,
    0x05, 0x24, 0x06, 0x00, 0x01,
    LEN_ENDPOINT, DESC_ENDPOINT, (EP_INPUT | INT_IN_EP_NUM), EP_INT, EPC_OTHER_MAX_PKT_SIZE & 0x00FFU, (EPC_OTHER_MAX_PKT_SIZE & 0xFF00U) >> 8, 0x01,
    LEN_INTERFACE, DESC_INTERFACE, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
    LEN_ENDPOINT, DESC_ENDPOINT, (EP_INPUT | BULK_IN_EP_NUM), EP_BULK, EPA_OTHER_MAX_PKT_SIZE & 0x00FFU, (EPA_OTHER_MAX_PKT_SIZE & 0xFF00U) >> 8, 0x00,
    LEN_ENDPOINT, DESC_ENDPOINT, (EP_OUTPUT | BULK_OUT_EP_NUM), EP_BULK, EPB_OTHER_MAX_PKT_SIZE & 0x00FFU, (EPB_OTHER_MAX_PKT_SIZE & 0xFF00U) >> 8, 0x00
};

uint8_t gu8ConfigDescriptorFS[] =
{
    LEN_CONFIG, DESC_CONFIG, 0x43, 0x00, 0x02, 0x01, 0x00, 0xC0, 0x32,
    LEN_INTERFACE, DESC_INTERFACE, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,
    0x05, 0x24, 0x00, 0x10, 0x01,
    0x05, 0x24, 0x01, 0x00, 0x01,
    0x04, 0x24, 0x02, 0x00,
    0x05, 0x24, 0x06, 0x00, 0x01,
    LEN_ENDPOINT, DESC_ENDPOINT, (EP_INPUT | INT_IN_EP_NUM), EP_INT, EPC_OTHER_MAX_PKT_SIZE & 0x00FFU, (EPC_OTHER_MAX_PKT_SIZE & 0xFF00U) >> 8, 0x01,
    LEN_INTERFACE, DESC_INTERFACE, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
    LEN_ENDPOINT, DESC_ENDPOINT, (EP_INPUT | BULK_IN_EP_NUM), EP_BULK, EPA_OTHER_MAX_PKT_SIZE & 0x00FFU, (EPA_OTHER_MAX_PKT_SIZE & 0xFF00U) >> 8, 0x00,
    LEN_ENDPOINT, DESC_ENDPOINT, (EP_OUTPUT | BULK_OUT_EP_NUM), EP_BULK, EPB_OTHER_MAX_PKT_SIZE & 0x00FFU, (EPB_OTHER_MAX_PKT_SIZE & 0xFF00U) >> 8, 0x00
};

uint8_t gu8OtherConfigDescriptorFS[] =
{
    LEN_CONFIG, DESC_OTHERSPEED, 0x43, 0x00, 0x02, 0x01, 0x00, 0xC0, 0x32,
    LEN_INTERFACE, DESC_INTERFACE, 0x00, 0x00, 0x01, 0x02, 0x02, 0x01, 0x00,
    0x05, 0x24, 0x00, 0x10, 0x01,
    0x05, 0x24, 0x01, 0x00, 0x01,
    0x04, 0x24, 0x02, 0x00,
    0x05, 0x24, 0x06, 0x00, 0x01,
    LEN_ENDPOINT, DESC_ENDPOINT, (EP_INPUT | INT_IN_EP_NUM), EP_INT, EPC_MAX_PKT_SIZE & 0x00FFU, (EPC_MAX_PKT_SIZE & 0xFF00U) >> 8, 0x01,
    LEN_INTERFACE, DESC_INTERFACE, 0x01, 0x00, 0x02, 0x0A, 0x00, 0x00, 0x00,
    LEN_ENDPOINT, DESC_ENDPOINT, (EP_INPUT | BULK_IN_EP_NUM), EP_BULK, EPA_MAX_PKT_SIZE & 0x00FFU, (EPA_MAX_PKT_SIZE & 0xFF00U) >> 8, 0x00,
    LEN_ENDPOINT, DESC_ENDPOINT, (EP_OUTPUT | BULK_OUT_EP_NUM), EP_BULK, EPB_MAX_PKT_SIZE & 0x00FFU, (EPB_MAX_PKT_SIZE & 0xFF00U) >> 8, 0x00
};

uint8_t gu8StringLang[4] =
{
    4,
    DESC_STRING,
    0x09, 0x04
};

uint8_t gu8VendorStringDesc[] =
{
    16,
    DESC_STRING,
    'N', 0, 'u', 0, 'v', 0, 'o', 0, 't', 0, 'o', 0, 'n', 0
};

uint8_t gu8ProductStringDesc[] =
{
    40,
    DESC_STRING,
    'N', 0, 'u', 0, 'A', 0, 'I', 0, 'L', 0, 'i', 0, 'n', 0, 'k', 0,
    ' ', 0, 'M', 0, 'C', 0, 'P', 0, ' ', 0, 'B', 0, 'r', 0, 'i', 0,
    'd', 0, 'g', 0, 'e', 0
};

uint8_t *gpu8UsbString[4] =
{
    gu8StringLang,
    gu8VendorStringDesc,
    gu8ProductStringDesc,
    NULL
};

uint8_t *gu8UsbHidReport[3] =
{
    NULL,
    NULL,
    NULL
};

uint32_t gu32UsbHidReportLen[3] =
{
    0U,
    0U,
    0U
};

uint32_t gu32ConfigHidDescIdx[3] =
{
    0U,
    0U,
    0U
};

S_HSUSBD_INFO_T gsHSInfo =
{
    gu8DeviceDescriptor,
    gu8ConfigDescriptor,
    gpu8UsbString,
    gu8QualifierDescriptor,
    gu8ConfigDescriptorFS,
    gu8OtherConfigDescriptorHS,
    gu8OtherConfigDescriptorFS,
    NULL,
    gu8UsbHidReport,
    gu32UsbHidReportLen,
    gu32ConfigHidDescIdx
};