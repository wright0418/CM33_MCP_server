#ifndef NUALINK_TRANSPORT_H
#define NUALINK_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "FreeRTOS.h"

void NuAILink_TransportInit(void);
void NuAILink_TransportPoll(void);
bool NuAILink_TransportIsAttached(void);
uint32_t NuAILink_TransportReadPacket(uint8_t *buffer, uint32_t max_length);
int32_t NuAILink_TransportWrite(const uint8_t *data, uint32_t length, TickType_t timeout_ticks);
uint32_t NuAILink_TransportGetRxDropCount(void);

#endif /* NUALINK_TRANSPORT_H */