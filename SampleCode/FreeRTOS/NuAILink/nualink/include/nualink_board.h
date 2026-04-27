#ifndef NUALINK_BOARD_H
#define NUALINK_BOARD_H

#include <stdbool.h>
#include <stdint.h>

void NuAILink_BoardInit(void);
void NuAILink_BoardSetLed(bool on);
void NuAILink_BoardEnableHeartbeat(bool enabled);
void NuAILink_BoardHeartbeatToggle(void);
bool NuAILink_BoardIsLedOn(void);
uint32_t NuAILink_BoardGetCoreClockHz(void);

#endif /* NUALINK_BOARD_H */