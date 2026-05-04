#ifndef WIRETAP32_SIGNAL_TOOLS_H
#define WIRETAP32_SIGNAL_TOOLS_H

#include <Arduino.h>

struct SignalFrequencyResult {
    bool valid;
    uint32_t durationUs;
    uint32_t risingEdges;
    uint32_t fallingEdges;
    uint32_t highSegments;
    uint32_t lowSegments;
    uint32_t avgHighUs;
    uint32_t avgLowUs;
    float frequencyHz;
    float dutyPct;
};

struct SignalEdgeResult {
    bool valid;
    uint32_t durationUs;
    uint32_t risingEdges;
    uint32_t fallingEdges;
    bool startedHigh;
    bool endedHigh;
};

struct SignalAdcStats {
    bool valid;
    uint16_t samples;
    int minRaw;
    int maxRaw;
    float avgRaw;
    float minVoltage;
    float maxVoltage;
    float avgVoltage;
};

bool signalIsValidGpio(int pin);
bool signalIsSafeDigitalPin(int pin);
bool signalIsOutputCapablePin(int pin);
bool signalIsAdcCapablePin(int pin);
SignalFrequencyResult signalMeasureFrequency(int pin, uint32_t windowMs);
SignalEdgeResult signalCountEdges(int pin, uint32_t windowMs);
bool signalBuildScope(int pin, uint16_t samples, uint32_t intervalUs, String& wave, String& ruler);
SignalAdcStats signalMeasureAdc(int pin, uint16_t samples);
bool signalStartPwm(int pin, uint32_t freq, uint8_t dutyPct, String& error);

#endif
