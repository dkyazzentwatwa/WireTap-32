#ifndef WIRETAP32_PIN_SAFETY_H
#define WIRETAP32_PIN_SAFETY_H

#include <Arduino.h>

struct WireTapPinConfig {
    int dispSda;
    int dispScl;
    int i2cSda;
    int i2cScl;
    int spiMosi;
    int spiMiso;
    int spiSck;
    int spiCs;
    int uartTx;
    int uartRx;
    const char* modeName;
};

String buildPinSafetyReport(const WireTapPinConfig& cfg);

#endif
