#ifndef NUALINK_BOARD_H
#define NUALINK_BOARD_H

#include <stdbool.h>
#include <stdint.h>

void NuAILink_BoardInit(void);
void NuAILink_BoardSetLed(bool on);
bool NuAILink_BoardLedBpwmSet(uint32_t duty_percent);
void NuAILink_BoardLedUseGpio(void);
void NuAILink_BoardEnableHeartbeat(bool enabled);
void NuAILink_BoardHeartbeatToggle(void);
bool NuAILink_BoardIsLedOn(void);
bool NuAILink_BoardIsLedBpwmActive(void);
uint32_t NuAILink_BoardGetLedBpwmDutyPercent(void);
uint32_t NuAILink_BoardGetCoreClockHz(void);

/* Phase 2.1: PB14 button + generic GPIO read/write helpers. */
void NuAILink_BoardButtonInit(void);
bool NuAILink_BoardButtonIsPressed(void);
bool NuAILink_BoardGpioRead(char port_letter, uint32_t pin, uint32_t *out_value);
bool NuAILink_BoardGpioWrite(char port_letter, uint32_t pin, uint32_t value);

#endif /* NUALINK_BOARD_H */