#ifndef NUALINK_LOG_H
#define NUALINK_LOG_H

#include <stdio.h>

#include "nualink_config.h"

#if (NUALINK_ENABLE_DEBUG_LOG == 1)
#define NUALINK_LOG(...) ((void)printf(__VA_ARGS__))
#else
#define NUALINK_LOG(...) ((void)0)
#endif

#if (NUALINK_ENABLE_BOOT_DIAGNOSTICS == 1)
#define NUALINK_BOOT_LOG(...) ((void)printf(__VA_ARGS__))
#else
#define NUALINK_BOOT_LOG(...) ((void)0)
#endif

#define NUALINK_ERR(...) ((void)printf(__VA_ARGS__))

#endif /* NUALINK_LOG_H */
