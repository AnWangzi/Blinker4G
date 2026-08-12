#ifndef BLINKER4G_MODULE_DRIVER_H
#define BLINKER4G_MODULE_DRIVER_H

#include "Blinker4GModuleConfig.h"

#if BLINKER4G_MODULE == BLINKER4G_MODULE_WH_LTE_7S0
#include "Blinker4GWhLte7S0Driver.h"
using Blinker4GModuleDriver = Blinker4GWhLte7S0Driver;
#else
#error "Unsupported Blinker4G module profile"
#endif

#endif